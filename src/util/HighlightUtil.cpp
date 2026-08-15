#include "HighlightUtil.h"

#if CROSSPOINT_HIGHLIGHT_EXPERIMENT

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr uint8_t V2_MAGIC[4] = {'H', 'L', 'V', '2'};
constexpr size_t V1_HEADER_SIZE = 9;
constexpr size_t V2_HEADER_SIZE = 13;

struct RecordHeader {
  uint16_t spine = 0;
  uint16_t page = 0;
  uint32_t offset = HighlightRecord::NO_OFFSET;
  uint16_t wordStart = 0;
  uint16_t wordEnd = 0;
  uint8_t snippetLen = 0;
};

// Open a highlight file for reading and detect its version, leaving the file
// positioned at the first record. Returns false when the file doesn't exist or
// can't be opened.
bool openHighlightFile(const std::string& path, HalFile& file, bool& isV2) {
  if (!Storage.exists(path.c_str())) {
    return false;
  }
  if (!Storage.openFileForRead("HLU", path, file)) {
    return false;
  }
  uint8_t magic[4];
  if (file.read(magic, sizeof(magic)) == static_cast<int>(sizeof(magic)) &&
      memcmp(magic, V2_MAGIC, sizeof(magic)) == 0) {
    isV2 = true;
    return true;
  }
  // v1 file: no magic, records start at byte 0.
  isV2 = false;
  return file.seekSet(0);
}

bool readRecordHeader(HalFile& file, const bool isV2, RecordHeader& h) {
  uint8_t buf[V2_HEADER_SIZE];
  const size_t size = isV2 ? V2_HEADER_SIZE : V1_HEADER_SIZE;
  if (file.read(buf, size) != static_cast<int>(size)) {
    return false;
  }
  memcpy(&h.spine, &buf[0], sizeof(h.spine));
  memcpy(&h.page, &buf[2], sizeof(h.page));
  size_t at = 4;
  h.offset = HighlightRecord::NO_OFFSET;
  if (isV2) {
    memcpy(&h.offset, &buf[4], sizeof(h.offset));
    at = 8;
  }
  memcpy(&h.wordStart, &buf[at], sizeof(h.wordStart));
  memcpy(&h.wordEnd, &buf[at + 2], sizeof(h.wordEnd));
  h.snippetLen = buf[at + 4];
  return true;
}

bool writeRecordHeaderV2(HalFile& file, const RecordHeader& h) {
  uint8_t buf[V2_HEADER_SIZE];
  memcpy(&buf[0], &h.spine, sizeof(h.spine));
  memcpy(&buf[2], &h.page, sizeof(h.page));
  memcpy(&buf[4], &h.offset, sizeof(h.offset));
  memcpy(&buf[8], &h.wordStart, sizeof(h.wordStart));
  memcpy(&buf[10], &h.wordEnd, sizeof(h.wordEnd));
  buf[12] = h.snippetLen;
  return file.write(buf, sizeof(buf)) == sizeof(buf);
}

// Rewrite a v1 file as v2 (offsets absent) via a temp file. No-op on v2 files.
bool migrateToV2(const std::string& path) {
  {
    HalFile probe;
    bool isV2 = false;
    if (!openHighlightFile(path, probe, isV2)) {
      return false;
    }
    if (isV2) {
      return true;
    }
  }  // the probe must close before the rewrite reopens the path

  const std::string tmpPath = path + ".tmp";
  {
    HalFile in;
    bool isV2 = false;
    if (!openHighlightFile(path, in, isV2)) {
      return false;
    }
    HalFile out = Storage.open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!out) {
      LOG_ERR("HLU", "Failed to open %s", tmpPath.c_str());
      return false;
    }
    if (out.write(V2_MAGIC, sizeof(V2_MAGIC)) != sizeof(V2_MAGIC)) {
      LOG_ERR("HLU", "Failed to write v2 magic");
      return false;
    }
    RecordHeader h;
    uint8_t snippet[HighlightUtil::MAX_SNIPPET_BYTES];
    while (readRecordHeader(in, isV2, h)) {
      if (h.snippetLen > HighlightUtil::MAX_SNIPPET_BYTES) {
        break;  // corrupt record: drop it and everything after
      }
      if (h.snippetLen > 0 && in.read(snippet, h.snippetLen) != h.snippetLen) {
        break;
      }
      if (!writeRecordHeaderV2(out, h) || (h.snippetLen > 0 && out.write(snippet, h.snippetLen) != h.snippetLen)) {
        LOG_ERR("HLU", "Failed to write record during v2 migration");
        return false;
      }
    }
    // Both files must be closed before remove/rename below.
    in.close();
    out.close();
  }
  if (!Storage.remove(path.c_str())) {
    LOG_ERR("HLU", "Failed to remove %s", path.c_str());
    return false;
  }
  LOG_INF("HLU", "Migrated highlight file to v2: %s", path.c_str());
  return Storage.rename(tmpPath.c_str(), path.c_str());
}

}  // namespace

std::string HighlightUtil::getHighlightsDir() { return "/.crosspoint/highlights/"; }

std::string HighlightUtil::getHighlightPath(const std::string& bookPath) {
  // Same flattening scheme as BookmarkUtil::getBookmarkPath: strip the leading
  // slash, turn path separators into underscores, drop the extension.
  std::string bookName = std::string(bookPath).erase(0, 1);
  std::replace(bookName.begin(), bookName.end(), '/', '_');
  std::replace(bookName.begin(), bookName.end(), '\\', '_');
  const size_t lastDot = bookName.find_last_of('.');
  if (lastDot != std::string::npos) {
    bookName.erase(lastDot);
  }
  bookName += ".hl";
  return getHighlightsDir() + bookName;
}

size_t HighlightUtil::countChapterHighlights(const std::string& bookPath, const uint16_t spineIndex) {
  HalFile file;
  bool isV2 = false;
  if (!openHighlightFile(getHighlightPath(bookPath), file, isV2)) {
    return 0;
  }
  size_t count = 0;
  RecordHeader h;
  while (readRecordHeader(file, isV2, h)) {
    if (h.spine == spineIndex) {
      count++;
    }
    if (!file.seekCur(h.snippetLen)) {
      break;
    }
  }
  return count;
}

bool HighlightUtil::isDuplicate(const std::string& bookPath, const uint16_t spineIndex, const uint16_t pageIndex,
                                const uint16_t wordStart, const uint16_t wordEnd) {
  HalFile file;
  bool isV2 = false;
  if (!openHighlightFile(getHighlightPath(bookPath), file, isV2)) {
    return false;
  }
  RecordHeader h;
  while (readRecordHeader(file, isV2, h)) {
    if (h.spine == spineIndex && h.page == pageIndex && h.wordStart == wordStart && h.wordEnd == wordEnd) {
      return true;
    }
    if (!file.seekCur(h.snippetLen)) {
      break;
    }
  }
  return false;
}

bool HighlightUtil::saveHighlight(const std::string& bookPath, const uint16_t spineIndex, const uint16_t pageIndex,
                                  const uint32_t pageVisibleOffset, const uint16_t wordStart, const uint16_t wordEnd,
                                  const std::string& snippet) {
  if (countChapterHighlights(bookPath, spineIndex) >= MAX_CHAPTER_HIGHLIGHTS) {
    LOG_ERR("HLU", "Chapter highlight limit reached (%u)", static_cast<uint32_t>(MAX_CHAPTER_HIGHLIGHTS));
    return false;
  }
  if (!Storage.ensureDirectoryExists("/.crosspoint/highlights")) {
    LOG_ERR("HLU", "Failed to create highlights dir");
    return false;
  }

  const std::string path = getHighlightPath(bookPath);
  // Pre-offset files are upgraded in place before the first v2 append.
  const bool exists = Storage.exists(path.c_str());
  if (exists && !migrateToV2(path)) {
    LOG_ERR("HLU", "Failed to migrate %s", path.c_str());
    return false;
  }
  HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("HLU", "Failed to open %s", path.c_str());
    return false;
  }
  if (!exists && file.write(V2_MAGIC, sizeof(V2_MAGIC)) != sizeof(V2_MAGIC)) {
    LOG_ERR("HLU", "Failed to write v2 magic");
    return false;
  }

  // UTF-8-safe truncation: never split a multi-byte sequence at the cap.
  size_t len = std::min(snippet.size(), MAX_SNIPPET_BYTES);
  while (len > 0 && len < snippet.size() && (static_cast<uint8_t>(snippet[len]) & 0xC0) == 0x80) {
    len--;
  }

  const RecordHeader h{spineIndex, pageIndex, pageVisibleOffset, wordStart, wordEnd, static_cast<uint8_t>(len)};
  if (!writeRecordHeaderV2(file, h) || file.write(snippet.data(), len) != len) {
    LOG_ERR("HLU", "Failed to write highlight record");
    return false;
  }
  LOG_DBG("HLU", "Saved highlight: spine=%u page=%u offset=%u words=[%u,%u] snippet=%u bytes", spineIndex, pageIndex,
          pageVisibleOffset, wordStart, wordEnd, static_cast<uint32_t>(len));
  return true;
}

bool HighlightUtil::loadChapterRecordInfo(const std::string& bookPath, const uint16_t spineIndex,
                                          std::vector<HighlightRecord>& outRecords) {
  HalFile file;
  bool isV2 = false;
  if (!openHighlightFile(getHighlightPath(bookPath), file, isV2)) {
    return false;
  }
  RecordHeader h;
  while (outRecords.size() < MAX_CHAPTER_HIGHLIGHTS && readRecordHeader(file, isV2, h)) {
    if (h.spine == spineIndex) {
      if (outRecords.empty()) {
        outRecords.reserve(8);
      }
      HighlightRecord rec;
      rec.spineIndex = h.spine;
      rec.pageIndex = h.page;
      rec.pageVisibleOffset = h.offset;
      rec.wordStart = h.wordStart;
      rec.wordEnd = h.wordEnd;
      outRecords.push_back(std::move(rec));
    }
    if (!file.seekCur(h.snippetLen)) {
      break;
    }
  }
  return !outRecords.empty();
}

bool HighlightUtil::loadChapterCounts(const std::string& bookPath, std::vector<ChapterHighlightCount>& outCounts) {
  HalFile file;
  bool isV2 = false;
  if (!openHighlightFile(getHighlightPath(bookPath), file, isV2)) {
    return false;
  }
  RecordHeader h;
  while (readRecordHeader(file, isV2, h)) {
    const uint16_t spine = h.spine;
    auto it = std::find_if(outCounts.begin(), outCounts.end(),
                           [spine](const ChapterHighlightCount& c) { return c.spineIndex == spine; });
    if (it != outCounts.end()) {
      it->count++;
    } else {
      if (outCounts.empty()) {
        outCounts.reserve(8);
      }
      outCounts.push_back({spine, 1});
    }
    if (!file.seekCur(h.snippetLen)) {
      break;
    }
  }
  std::sort(outCounts.begin(), outCounts.end(),
            [](const ChapterHighlightCount& a, const ChapterHighlightCount& b) { return a.spineIndex < b.spineIndex; });
  return !outCounts.empty();
}

bool HighlightUtil::loadChapterHighlights(const std::string& bookPath, const uint16_t spineIndex,
                                          std::vector<HighlightRecord>& outRecords) {
  HalFile file;
  bool isV2 = false;
  if (!openHighlightFile(getHighlightPath(bookPath), file, isV2)) {
    return false;
  }
  RecordHeader h;
  while (outRecords.size() < MAX_CHAPTER_HIGHLIGHTS && readRecordHeader(file, isV2, h)) {
    if (h.spine != spineIndex) {
      if (!file.seekCur(h.snippetLen)) {
        break;
      }
      continue;
    }
    HighlightRecord rec;
    rec.spineIndex = h.spine;
    rec.pageIndex = h.page;
    rec.pageVisibleOffset = h.offset;
    rec.wordStart = h.wordStart;
    rec.wordEnd = h.wordEnd;
    if (h.snippetLen > 0) {
      rec.snippet.resize(h.snippetLen);
      if (file.read(rec.snippet.data(), h.snippetLen) != h.snippetLen) {
        break;  // truncated record: keep what parsed cleanly
      }
    }
    outRecords.push_back(std::move(rec));
  }
  return !outRecords.empty();
}

bool HighlightUtil::exportMarkdown(const std::string& bookPath, const std::string& bookTitle,
                                   std::string (*titleFn)(void* ctx, uint16_t spineIndex), void* ctx) {
  std::vector<ChapterHighlightCount> counts;
  if (!loadChapterCounts(bookPath, counts)) {
    return false;  // nothing saved for this book
  }

  // "/<book-stem> highlights.md" at the SD root, book stem flattened the same
  // way as the .hl file name.
  std::string stem = std::string(bookPath).erase(0, 1);
  std::replace(stem.begin(), stem.end(), '/', '_');
  std::replace(stem.begin(), stem.end(), '\\', '_');
  const size_t lastDot = stem.find_last_of('.');
  if (lastDot != std::string::npos) {
    stem.erase(lastDot);
  }
  const std::string outPath = "/" + stem + " highlights.md";

  HalFile out = Storage.open(outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!out) {
    LOG_ERR("HLU", "Failed to open %s", outPath.c_str());
    return false;
  }
  const auto writeStr = [&out](const std::string& s) { return out.write(s.data(), s.size()) == s.size(); };

  if (!writeStr("# " + (bookTitle.empty() ? stem : bookTitle) + "\n")) {
    LOG_ERR("HLU", "Failed to write %s", outPath.c_str());
    return false;
  }
  for (const auto& chapter : counts) {
    std::vector<HighlightRecord> records;
    if (!loadChapterHighlights(bookPath, chapter.spineIndex, records)) {
      continue;
    }
    std::string block = "\n## " + titleFn(ctx, chapter.spineIndex) + "\n";
    for (const auto& rec : records) {
      block += "\n> " + rec.snippet + "\n";
    }
    if (!writeStr(block)) {
      LOG_ERR("HLU", "Failed to write %s", outPath.c_str());
      return false;
    }
  }
  LOG_INF("HLU", "Exported highlights to %s", outPath.c_str());
  return true;
}

bool HighlightUtil::deleteHighlight(const std::string& bookPath, const uint16_t spineIndex,
                                    const size_t chapterOrdinal) {
  const std::string path = getHighlightPath(bookPath);
  const std::string tmpPath = path + ".tmp";

  size_t kept = 0;
  {
    HalFile in;
    bool isV2 = false;
    if (!openHighlightFile(path, in, isV2)) {
      return false;
    }
    HalFile out = Storage.open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!out) {
      LOG_ERR("HLU", "Failed to open %s", tmpPath.c_str());
      return false;
    }
    // The rewrite always produces a v2 file, upgrading v1 input as a side effect.
    if (out.write(V2_MAGIC, sizeof(V2_MAGIC)) != sizeof(V2_MAGIC)) {
      LOG_ERR("HLU", "Failed to write v2 magic");
      return false;
    }

    RecordHeader h;
    uint8_t snippet[MAX_SNIPPET_BYTES];
    size_t ordinal = 0;
    while (readRecordHeader(in, isV2, h)) {
      if (h.snippetLen > MAX_SNIPPET_BYTES) {
        break;  // corrupt record: drop it and everything after
      }
      if (h.snippetLen > 0 && in.read(snippet, h.snippetLen) != h.snippetLen) {
        break;
      }
      bool skip = false;
      if (h.spine == spineIndex) {
        skip = ordinal == chapterOrdinal;
        ordinal++;
      }
      if (skip) {
        continue;
      }
      if (!writeRecordHeaderV2(out, h) || (h.snippetLen > 0 && out.write(snippet, h.snippetLen) != h.snippetLen)) {
        LOG_ERR("HLU", "Failed to write highlight record during rewrite");
        return false;
      }
      kept++;
    }
    // Both files must be closed before remove/rename below.
    in.close();
    out.close();
  }

  if (!Storage.remove(path.c_str())) {
    LOG_ERR("HLU", "Failed to remove %s", path.c_str());
    return false;
  }
  if (kept == 0) {
    return Storage.remove(tmpPath.c_str());
  }
  return Storage.rename(tmpPath.c_str(), path.c_str());
}

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
