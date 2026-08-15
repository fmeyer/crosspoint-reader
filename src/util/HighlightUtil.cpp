#include "HighlightUtil.h"

#if CROSSPOINT_HIGHLIGHT_EXPERIMENT

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

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
  const std::string path = getHighlightPath(bookPath);
  if (!Storage.exists(path.c_str())) {
    return 0;
  }
  HalFile file;
  if (!Storage.openFileForRead("HLU", path, file)) {
    return 0;
  }
  size_t count = 0;
  uint8_t header[9];
  while (file.read(header, sizeof(header)) == static_cast<int>(sizeof(header))) {
    uint16_t spine;
    memcpy(&spine, &header[0], sizeof(spine));
    if (spine == spineIndex) {
      count++;
    }
    if (!file.seekCur(header[8])) {
      break;
    }
  }
  return count;
}

bool HighlightUtil::saveHighlight(const std::string& bookPath, const uint16_t spineIndex, const uint16_t pageIndex,
                                  const uint16_t wordStart, const uint16_t wordEnd, const std::string& snippet) {
  if (countChapterHighlights(bookPath, spineIndex) >= MAX_CHAPTER_HIGHLIGHTS) {
    LOG_ERR("HLU", "Chapter highlight limit reached (%u)", static_cast<uint32_t>(MAX_CHAPTER_HIGHLIGHTS));
    return false;
  }
  if (!Storage.ensureDirectoryExists("/.crosspoint/highlights")) {
    LOG_ERR("HLU", "Failed to create highlights dir");
    return false;
  }

  const std::string path = getHighlightPath(bookPath);
  HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("HLU", "Failed to open %s", path.c_str());
    return false;
  }

  // UTF-8-safe truncation: never split a multi-byte sequence at the cap.
  size_t len = std::min(snippet.size(), MAX_SNIPPET_BYTES);
  while (len > 0 && len < snippet.size() && (static_cast<uint8_t>(snippet[len]) & 0xC0) == 0x80) {
    len--;
  }

  uint8_t header[9];
  memcpy(&header[0], &spineIndex, sizeof(spineIndex));
  memcpy(&header[2], &pageIndex, sizeof(pageIndex));
  memcpy(&header[4], &wordStart, sizeof(wordStart));
  memcpy(&header[6], &wordEnd, sizeof(wordEnd));
  header[8] = static_cast<uint8_t>(len);

  if (file.write(header, sizeof(header)) != sizeof(header) || file.write(snippet.data(), len) != len) {
    LOG_ERR("HLU", "Failed to write highlight record");
    return false;
  }
  LOG_DBG("HLU", "Saved highlight: spine=%u page=%u words=[%u,%u] snippet=%u bytes", spineIndex, pageIndex, wordStart,
          wordEnd, static_cast<uint32_t>(len));
  return true;
}

bool HighlightUtil::loadHighlightsForPage(const std::string& bookPath, const uint16_t spineIndex,
                                          const uint16_t pageIndex,
                                          std::vector<std::pair<uint16_t, uint16_t>>& outRanges) {
  const std::string path = getHighlightPath(bookPath);
  if (!Storage.exists(path.c_str())) {
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("HLU", path, file)) {
    return false;
  }

  uint8_t header[9];
  while (file.read(header, sizeof(header)) == static_cast<int>(sizeof(header))) {
    uint16_t spine;
    uint16_t page;
    uint16_t wordStart;
    uint16_t wordEnd;
    memcpy(&spine, &header[0], sizeof(spine));
    memcpy(&page, &header[2], sizeof(page));
    memcpy(&wordStart, &header[4], sizeof(wordStart));
    memcpy(&wordEnd, &header[6], sizeof(wordEnd));
    if (spine == spineIndex && page == pageIndex) {
      if (outRanges.empty()) {
        outRanges.reserve(4);
      }
      outRanges.emplace_back(wordStart, wordEnd);
      if (outRanges.size() >= MAX_PAGE_HIGHLIGHTS) {
        break;
      }
    }
    if (!file.seekCur(header[8])) {
      break;
    }
  }
  return !outRanges.empty();
}

bool HighlightUtil::loadChapterCounts(const std::string& bookPath, std::vector<ChapterHighlightCount>& outCounts) {
  const std::string path = getHighlightPath(bookPath);
  if (!Storage.exists(path.c_str())) {
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("HLU", path, file)) {
    return false;
  }

  uint8_t header[9];
  while (file.read(header, sizeof(header)) == static_cast<int>(sizeof(header))) {
    uint16_t spine;
    memcpy(&spine, &header[0], sizeof(spine));
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
    if (!file.seekCur(header[8])) {
      break;
    }
  }
  std::sort(outCounts.begin(), outCounts.end(),
            [](const ChapterHighlightCount& a, const ChapterHighlightCount& b) { return a.spineIndex < b.spineIndex; });
  return !outCounts.empty();
}

bool HighlightUtil::loadChapterHighlights(const std::string& bookPath, const uint16_t spineIndex,
                                          std::vector<HighlightRecord>& outRecords) {
  const std::string path = getHighlightPath(bookPath);
  if (!Storage.exists(path.c_str())) {
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("HLU", path, file)) {
    return false;
  }

  uint8_t header[9];
  while (outRecords.size() < MAX_CHAPTER_HIGHLIGHTS &&
         file.read(header, sizeof(header)) == static_cast<int>(sizeof(header))) {
    HighlightRecord rec;
    memcpy(&rec.spineIndex, &header[0], sizeof(rec.spineIndex));
    memcpy(&rec.pageIndex, &header[2], sizeof(rec.pageIndex));
    memcpy(&rec.wordStart, &header[4], sizeof(rec.wordStart));
    memcpy(&rec.wordEnd, &header[6], sizeof(rec.wordEnd));
    const uint8_t snippetLen = header[8];
    if (rec.spineIndex != spineIndex) {
      if (!file.seekCur(snippetLen)) {
        break;
      }
      continue;
    }
    if (snippetLen > 0) {
      rec.snippet.resize(snippetLen);
      if (file.read(rec.snippet.data(), snippetLen) != snippetLen) {
        break;  // truncated record: keep what parsed cleanly
      }
    }
    outRecords.push_back(std::move(rec));
  }
  return !outRecords.empty();
}

bool HighlightUtil::deleteHighlight(const std::string& bookPath, const uint16_t spineIndex,
                                    const size_t chapterOrdinal) {
  const std::string path = getHighlightPath(bookPath);
  const std::string tmpPath = path + ".tmp";

  size_t kept = 0;
  {
    HalFile in;
    if (!Storage.openFileForRead("HLU", path, in)) {
      return false;
    }
    HalFile out = Storage.open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!out) {
      LOG_ERR("HLU", "Failed to open %s", tmpPath.c_str());
      return false;
    }

    uint8_t header[9];
    uint8_t snippet[MAX_SNIPPET_BYTES];
    size_t ordinal = 0;
    while (in.read(header, sizeof(header)) == static_cast<int>(sizeof(header))) {
      const uint8_t snippetLen = header[8];
      if (snippetLen > MAX_SNIPPET_BYTES) {
        break;  // corrupt record: drop it and everything after
      }
      if (snippetLen > 0 && in.read(snippet, snippetLen) != snippetLen) {
        break;
      }
      uint16_t spine;
      memcpy(&spine, &header[0], sizeof(spine));
      bool skip = false;
      if (spine == spineIndex) {
        skip = ordinal == chapterOrdinal;
        ordinal++;
      }
      if (skip) {
        continue;
      }
      if (out.write(header, sizeof(header)) != sizeof(header) ||
          (snippetLen > 0 && out.write(snippet, snippetLen) != snippetLen)) {
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
