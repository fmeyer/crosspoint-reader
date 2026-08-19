#pragma once

#if CROSSPOINT_HIGHLIGHT_EXPERIMENT

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Persists text highlights per book as an append-only binary file:
// /.crosspoint/highlights/<flattened-book-name>.hl
// v2 files start with the 4-byte magic "HLV2"; each record is then
// (little-endian): u16 spineIndex, u16 pageIndex, u32 pageVisibleOffset,
// u16 wordStart, u16 wordEnd, u8 snippetLen, snippetLen bytes of UTF-8 snippet.
// Files without the magic are v1 (no offset field); they parse transparently
// and are rewritten as v2 on the first save.
// The snippet is the durable ground truth: word indices are only valid for the
// layout settings active when the highlight was created. pageVisibleOffset (the
// visible-codepoint offset where the record's page started) anchors the record
// to content, so it survives re-pagination the way bookmarks do.
struct HighlightRecord {
  static constexpr uint32_t NO_OFFSET = 0xFFFFFFFFu;

  uint16_t spineIndex = 0;
  uint16_t pageIndex = 0;
  uint32_t pageVisibleOffset = NO_OFFSET;
  uint16_t wordStart = 0;
  uint16_t wordEnd = 0;
  std::string snippet;

  bool hasOffset() const { return pageVisibleOffset != NO_OFFSET; }
};

struct ChapterHighlightCount {
  uint16_t spineIndex;
  uint16_t count;
};

class HighlightUtil {
 public:
  static std::string getHighlightsDir();
  static std::string getHighlightPath(const std::string& bookPath);
  static bool saveHighlight(const std::string& bookPath, uint16_t spineIndex, uint16_t pageIndex,
                            uint32_t pageVisibleOffset, uint16_t wordStart, uint16_t wordEnd,
                            const std::string& snippet);
  // Header-only scan of one chapter: page/offset/word-range data in file order,
  // snippets left empty (no string allocations). Ordinals match
  // loadChapterHighlights, which reads the same records with snippets.
  static bool loadChapterRecordInfo(const std::string& bookPath, uint16_t spineIndex,
                                    std::vector<HighlightRecord>& outRecords);
  // Header-only scan: per-chapter record counts (sorted by spine index). Cheap —
  // no snippet text is read, so it is safe for any number of highlights.
  static bool loadChapterCounts(const std::string& bookPath, std::vector<ChapterHighlightCount>& outCounts);
  // Load one chapter's records (snippets included) for the management list.
  static bool loadChapterHighlights(const std::string& bookPath, uint16_t spineIndex,
                                    std::vector<HighlightRecord>& outRecords);
  // Number of records for one chapter (header scan, no snippets).
  static size_t countChapterHighlights(const std::string& bookPath, uint16_t spineIndex);
  // True when a record with the same spine, page, and word range already exists
  // (header-only scan). Guards against accidental double saves.
  static bool isDuplicate(const std::string& bookPath, uint16_t spineIndex, uint16_t pageIndex, uint16_t wordStart,
                          uint16_t wordEnd);
  // Remove the Nth record (file order) of one chapter via a streaming rewrite to a
  // temp file — constant RAM regardless of how many highlights the book holds.
  static bool deleteHighlight(const std::string& bookPath, uint16_t spineIndex, size_t chapterOrdinal);
  // Write every highlight to "/<book-stem> highlights.md" at the SD root:
  // "# title", "## chapter" per chapter, "> snippet" per record. Streams chapter
  // by chapter, so transient RAM stays bounded by one chapter's records.
  // titleFn(ctx, spine) supplies each chapter heading (plain function pointer +
  // context, no std::function).
  static bool exportMarkdown(const std::string& bookPath, const std::string& bookTitle,
                             std::string (*titleFn)(void* ctx, uint16_t spineIndex), void* ctx);

  static constexpr size_t MAX_SNIPPET_BYTES = 120;
  static constexpr size_t MAX_PAGE_HIGHLIGHTS = 32;
  // Cap on records per chapter, enforced at save time. Bounds the chapter list's
  // transient RAM: a record costs ~160 bytes worst case (8 B indices + string
  // object + ~128 B snippet heap block), so one chapter's list ≈ 10 KB while
  // open. Total book capacity scales with chapter count instead of a flat cap.
  static constexpr size_t MAX_CHAPTER_HIGHLIGHTS = 64;
};

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
