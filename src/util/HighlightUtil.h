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
  // Load every record (snippets included) for the management list.
  static bool loadHighlights(const std::string& bookPath, std::vector<HighlightRecord>& outRecords);
  // Rewrite the whole file after a delete; removes it when records is empty.
  static bool saveAllHighlights(const std::string& bookPath, const std::vector<HighlightRecord>& records);

  static constexpr size_t MAX_SNIPPET_BYTES = 120;
  static constexpr size_t MAX_PAGE_HIGHLIGHTS = 32;
  static constexpr size_t MAX_BOOK_HIGHLIGHTS = 64;
};

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
