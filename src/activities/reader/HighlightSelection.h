#pragma once

#if CROSSPOINT_HIGHLIGHT_EXPERIMENT

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class GfxRenderer;
class Page;

// Word-level selection model for the page currently on screen. Built once per page
// from the deserialized Page (word geometry + sentence/paragraph boundary flags),
// then operated on by index. Painting uses GfxRenderer::invertRegion, which is
// self-inverse: repainting a range un-highlights it, so selection changes cost one
// FAST_REFRESH with no SD reload or page re-render.
class HighlightSelection {
 public:
  // startAtCenter: place the initial cursor on the middle row's word nearest
  // mid-screen (anchor-picker mode) instead of the page's first word — any word
  // is then at most half a page of moves away.
  bool buildFromPage(const Page& page, const GfxRenderer& renderer, int fontId, int marginLeft, int marginTop,
                     float lineCompression, bool startAtCenter = false);
  bool isBuilt() const { return !rects.empty(); }
  // True only once buildFromPage has RETURNED (success or empty). The main loop
  // must not operate on — or free — this object before then: the build runs on
  // the render task.
  bool wasBuildAttempted() const { return buildTried; }

  // Collapse the selection to the first word of the next (forward) or previous
  // sentence; wraps around the page in both directions.
  bool sentenceHop(bool forward);
  // Single side-button press; consecutive presses of the same button within
  // MULTI_PRESS_MS escalate word -> sentence -> paragraph. Returns true when the
  // selection changed.
  bool onSinglePress(bool forward);

  // --- Anchor-picker phase (cursor = collapsed selection on the anchor) ---
  // Side-button press while picking the anchor: single press steps one word,
  // a rapid second press within MULTI_PRESS_MS hops a whole sentence instead.
  bool onCursorPress(bool forward);
  // Step the cursor by delta words, clamped to the page bounds.
  bool moveCursor(int delta);
  // Move the cursor one screen line down/up, landing on the word whose
  // x-center is closest to the current word's (dictionary-picker behavior).
  bool moveCursorLine(bool down);
  // Phase 2 -> 1 Back: drop any extension, keep the cursor on the anchor.
  void collapseToAnchor();

  // Full-render path: invert the current selection into a freshly rendered page.
  void paintCurrent(const GfxRenderer& renderer);
  // Incremental path: un-invert the previously painted range, invert the current one.
  void repaintDiff(const GfxRenderer& renderer);
  // Draw saved word ranges as 2px underlines at the text baseline (persistent
  // highlights). Plain black draws, so they are safe to repaint in every render
  // pass — including the grayscale anti-aliasing planes, where they must be
  // re-drawn to survive (same mechanism as the EPUB UNDERLINE style). Clamps
  // stale indices and merges overlapping ranges.
  void underlineRanges(const GfxRenderer& renderer, std::vector<std::pair<uint16_t, uint16_t>> ranges) const;

  std::string selectedText() const;
  uint16_t selectionStart() const { return selStart; }
  uint16_t selectionEnd() const { return selEnd; }
  // Screen-space center of the anchor word (dictionary lookup hand-off).
  bool anchorCenter(int& x, int& y) const;

  // Resolve a saved record's word range against this page's text. When
  // trustIndices is set and the text at [start,end] still begins with the
  // snippet, the stored range is kept; otherwise the snippet is searched in the
  // page text and start/end are remapped to the matching words (re-pagination
  // repair). Returns false when the snippet is not on this page.
  bool resolveSnippet(const std::string& snippet, uint16_t& start, uint16_t& end, bool trustIndices) const;

  static constexpr unsigned long MULTI_PRESS_MS = 450;

 private:
  struct WordRect {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint8_t flags;
    uint8_t row;  // line ordinal on the page, for vertical cursor moves (fills existing padding)
  };
  static constexpr uint8_t FLAG_SENTENCE_START = 0x01;
  static constexpr uint8_t FLAG_PARA_START = 0x02;
  // Word indices are uint16_t and text offsets must fit textOffset's uint16_t.
  static constexpr size_t MAX_WORDS = 2000;
  static constexpr size_t MAX_TEXT_BYTES = 60000;

  std::vector<WordRect> rects;
  std::string textPool;              // space-joined page text (snippet extraction)
  std::vector<uint16_t> textOffset;  // rects.size() + 1 entries (sentinel at end)
  int lineH = 0;
  int ascender = 0;       // baseline offset from line top, for underline placement
  uint16_t rowCount = 0;  // screen lines carrying words

  uint16_t anchor = 0;
  uint16_t selStart = 0;
  uint16_t selEnd = 0;
  uint16_t paintedStart = 0;
  uint16_t paintedEnd = 0;
  bool hasPainted = false;
  bool buildTried = false;

  uint8_t pressLevel = 0;
  bool lastPressForward = false;
  unsigned long lastPressTime = 0;

  void applyForward(uint8_t level);
  void applyBack(uint8_t level);
  uint16_t scanBackFlag(uint16_t from, uint8_t flag) const;
  uint16_t scanEndFlag(uint16_t from, uint8_t flag) const;
  void paintRange(const GfxRenderer& renderer, uint16_t a, uint16_t b) const;
  // Index of the word in `row` whose x-center is closest to centerX; -1 when
  // the row has no words.
  int closestInRow(uint8_t row, int centerX) const;
};

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
