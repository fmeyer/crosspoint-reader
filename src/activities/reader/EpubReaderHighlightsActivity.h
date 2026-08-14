#pragma once

#if CROSSPOINT_HIGHLIGHT_EXPERIMENT

#include <Epub.h>

#include <memory>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"
#include "util/HighlightUtil.h"

// List of a book's saved highlights: Confirm jumps to the highlight's page,
// holding Confirm deletes it (two-step confirmation, mirroring the bookmarks UI).
class EpubReaderHighlightsActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  std::vector<HighlightRecord> highlights;
  int confirmingDelete = 0;  // 0 = hide dialog, 1 = show dialog, 2 = allow confirmation to delete

 public:
  explicit EpubReaderHighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        const std::shared_ptr<Epub>& epub, const std::string& epubPath)
      : Activity("EpubReaderHighlights", renderer, mappedInput), epub(epub), epubPath(epubPath) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int getGutterBottom(const GfxRenderer& renderer);
  int getListHeight(const GfxRenderer& renderer);
};

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
