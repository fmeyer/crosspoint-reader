#pragma once

#if CROSSPOINT_HIGHLIGHT_EXPERIMENT

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Persists text highlights per book as an append-only binary file:
// /.crosspoint/highlights/<flattened-book-name>.hl
// Record layout (little-endian): u16 spineIndex, u16 pageIndex, u16 wordStart,
// u16 wordEnd, u8 snippetLen, snippetLen bytes of UTF-8 snippet text.
// The snippet is the durable ground truth: word indices are only valid for the
// layout settings active when the highlight was created.
struct HighlightRecord {
  uint16_t spineIndex;
  uint16_t pageIndex;
  uint16_t wordStart;
  uint16_t wordEnd;
  std::string snippet;
};

struct ChapterHighlightCount {
  uint16_t spineIndex;
  uint16_t count;
};

class HighlightUtil {
 public:
  static std::string getHighlightsDir();
  static std::string getHighlightPath(const std::string& bookPath);
  static bool saveHighlight(const std::string& bookPath, uint16_t spineIndex, uint16_t pageIndex, uint16_t wordStart,
                            uint16_t wordEnd, const std::string& snippet);
  // Collect the saved [wordStart, wordEnd] ranges for one page. Returns false when
  // the book has no highlight file or the page has no records.
  static bool loadHighlightsForPage(const std::string& bookPath, uint16_t spineIndex, uint16_t pageIndex,
                                    std::vector<std::pair<uint16_t, uint16_t>>& outRanges);
  // Header-only scan: per-chapter record counts (sorted by spine index). Cheap —
  // no snippet text is read, so it is safe for any number of highlights.
  static bool loadChapterCounts(const std::string& bookPath, std::vector<ChapterHighlightCount>& outCounts);
  // Load one chapter's records (snippets included) for the management list.
  static bool loadChapterHighlights(const std::string& bookPath, uint16_t spineIndex,
                                    std::vector<HighlightRecord>& outRecords);
  // Number of records for one chapter (header scan, no snippets).
  static size_t countChapterHighlights(const std::string& bookPath, uint16_t spineIndex);
  // Remove the Nth record (file order) of one chapter via a streaming rewrite to a
  // temp file — constant RAM regardless of how many highlights the book holds.
  static bool deleteHighlight(const std::string& bookPath, uint16_t spineIndex, size_t chapterOrdinal);

  static constexpr size_t MAX_SNIPPET_BYTES = 120;
  static constexpr size_t MAX_PAGE_HIGHLIGHTS = 32;
  // Cap on records per chapter, enforced at save time. Bounds the chapter list's
  // transient RAM: a record costs ~160 bytes worst case (8 B indices + string
  // object + ~128 B snippet heap block), so one chapter's list ≈ 10 KB while
  // open. Total book capacity scales with chapter count instead of a flat cap.
  static constexpr size_t MAX_CHAPTER_HIGHLIGHTS = 64;
};

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
