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

bool HighlightUtil::saveHighlight(const std::string& bookPath, const uint16_t spineIndex, const uint16_t pageIndex,
                                  const uint16_t wordStart, const uint16_t wordEnd, const std::string& snippet) {
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

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
