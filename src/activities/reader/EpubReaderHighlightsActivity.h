#pragma once

#if CROSSPOINT_HIGHLIGHT_EXPERIMENT

#include <Epub.h>

#include <memory>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"
#include "util/HighlightUtil.h"

// Two-level browser for a book's saved highlights. Level one lists chapters that
// have highlights (with counts, loaded via a cheap header-only scan); Confirm
// opens that chapter's highlights (at most MAX_CHAPTER_HIGHLIGHTS records in
// RAM). In the chapter view Confirm jumps to the highlight's page and holding
// Confirm deletes it (two-step confirmation, mirroring the bookmarks UI).
class EpubReaderHighlightsActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  ButtonNavigator buttonNavigator;
  std::vector<ChapterHighlightCount> chapters;
  std::vector<HighlightRecord> highlights;
  bool inChapter = false;  // false = chapter list, true = one chapter's highlights
  int chapterIndex = 0;
  int selectorIndex = 0;
  int confirmingDelete = 0;  // 0 = hide dialog, 1 = show dialog, 2 = allow confirmation to delete

 public:
  explicit EpubReaderHighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        const std::shared_ptr<Epub>& epub, const std::string& epubPath)
      : Activity("EpubReaderHighlights", renderer, mappedInput), epub(epub), epubPath(epubPath) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string chapterTitle(uint16_t spineIndex) const;
  void openChapter(int index);
  void backToChapters();
  int getGutterBottom(const GfxRenderer& renderer);
  int getListHeight(const GfxRenderer& renderer);
};

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
