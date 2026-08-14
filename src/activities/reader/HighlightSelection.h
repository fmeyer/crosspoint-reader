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
  bool buildFromPage(const Page& page, const GfxRenderer& renderer, int fontId, int marginLeft, int marginTop,
                     float lineCompression);
  bool isBuilt() const { return !rects.empty(); }
  bool wasBuildAttempted() const { return buildTried; }

  // Collapse the selection to the first word of the next (forward) or previous
  // sentence; wraps around the page in both directions.
  bool sentenceHop(bool forward);
  // Single side-button press; consecutive presses of the same button within
  // MULTI_PRESS_MS escalate word -> sentence -> paragraph. Returns true when the
  // selection changed.
  bool onSinglePress(bool forward);

  // Full-render path: invert the current selection into a freshly rendered page.
  void paintCurrent(const GfxRenderer& renderer);
  // Incremental path: un-invert the previously painted range, invert the current one.
  void repaintDiff(const GfxRenderer& renderer);
  // Invert saved word ranges into a freshly rendered page (persistent highlights).
  // Clamps stale indices and merges overlaps so XOR painting never double-inverts.
  void paintRanges(const GfxRenderer& renderer, std::vector<std::pair<uint16_t, uint16_t>> ranges) const;

  std::string selectedText() const;
  uint16_t selectionStart() const { return selStart; }
  uint16_t selectionEnd() const { return selEnd; }

  static constexpr unsigned long MULTI_PRESS_MS = 450;

 private:
  struct WordRect {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint8_t flags;
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
};

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
