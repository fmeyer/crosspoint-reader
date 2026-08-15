#include "EpubReaderHighlightsActivity.h"

#if CROSSPOINT_HIGHLIGHT_EXPERIMENT

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ENTER_DELETE_MODE_MS = 700;
constexpr int DELETE_MODE_OFF = 0;
constexpr int DELETE_MODE_DISPLAY = 1;
constexpr int DELETE_MODE_CONFIRM = 2;

constexpr int LINE_HEIGHT = 60;
}  // namespace

void EpubReaderHighlightsActivity::onEnter() {
  Activity::onEnter();
  chapters.clear();
  HighlightUtil::loadChapterCounts(epubPath, chapters);
  LOG_DBG("EPH", "Loaded highlight counts for %d chapters: %s", static_cast<int>(chapters.size()), epubPath.c_str());
  requestUpdate();
}

std::string EpubReaderHighlightsActivity::chapterTitle(const uint16_t spineIndex) const {
  const auto tocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  return (tocIndex >= 0) ? (epub->getTocItem(tocIndex)).title : tr(STR_UNNAMED);
}

void EpubReaderHighlightsActivity::openChapter(const int index) {
  highlights.clear();
  HighlightUtil::loadChapterHighlights(epubPath, chapters.at(index).spineIndex, highlights);
  chapterIndex = index;
  inChapter = true;
  selectorIndex = 0;
  requestUpdate();
}

void EpubReaderHighlightsActivity::backToChapters() {
  highlights.clear();
  highlights.shrink_to_fit();
  chapters.clear();
  HighlightUtil::loadChapterCounts(epubPath, chapters);
  inChapter = false;
  if (chapterIndex >= static_cast<int>(chapters.size())) {
    chapterIndex = chapters.empty() ? 0 : static_cast<int>(chapters.size()) - 1;
  }
  selectorIndex = chapterIndex;
  requestUpdate();
}

int EpubReaderHighlightsActivity::getGutterBottom(const GfxRenderer& renderer) {
  const auto orientation = renderer.getOrientation();
  const bool isPortrait = orientation == GfxRenderer::Orientation::Portrait;
  return isPortrait ? 75 : 40;
}

int EpubReaderHighlightsActivity::getListHeight(const GfxRenderer& renderer) {
  return renderer.getScreenHeight() - getGutterBottom(renderer) - LINE_HEIGHT;
}

void EpubReaderHighlightsActivity::loop() {
  // Delete confirmation mode (chapter view only)
  if (confirmingDelete >= DELETE_MODE_DISPLAY) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (confirmingDelete == DELETE_MODE_DISPLAY) {
        confirmingDelete = DELETE_MODE_CONFIRM;  // first confirmation, update text
        requestUpdate();
        return;
      }
      if (!HighlightUtil::deleteHighlight(epubPath, chapters.at(chapterIndex).spineIndex, selectorIndex)) {
        LOG_ERR("EPH", "Failed to delete highlight");
      }
      confirmingDelete = DELETE_MODE_OFF;
      // Reload the chapter; fall back to the chapter list when it emptied.
      const uint16_t spine = chapters.at(chapterIndex).spineIndex;
      highlights.clear();
      if (!HighlightUtil::loadChapterHighlights(epubPath, spine, highlights)) {
        backToChapters();
        return;
      }
      if (selectorIndex >= static_cast<int>(highlights.size()) && selectorIndex > 0) {
        selectorIndex--;
      }
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      requestUpdate();
      confirmingDelete = DELETE_MODE_OFF;
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {  // Open
    if (!inChapter) {
      if (!chapters.empty()) {
        openChapter(selectorIndex);
      }
      return;
    }
    if (highlights.empty()) {
      return;
    }
    const auto& rec = highlights.at(selectorIndex);
    ProgressChangeResult result{rec.spineIndex, rec.pageIndex};
    if (rec.hasOffset()) {
      // Content anchor: the reader's offset-jump path lands on the right page
      // even when the layout has changed since the highlight was saved.
      result.hasVisibleTextOffset = true;
      result.visibleTextOffset = rec.pageVisibleOffset;
    }
    setResult(std::move(result));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (inChapter) {
      backToChapters();
      return;
    }
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (inChapter && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
    if (highlights.empty()) {
      return;
    }
    confirmingDelete = DELETE_MODE_DISPLAY;
    requestUpdate();
  }

  const int itemCount = inChapter ? highlights.size() : chapters.size();

  buttonNavigator.onNextRelease([this, itemCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, itemCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, itemCount] {
    selectorIndex =
        ButtonNavigator::nextPageIndex(selectorIndex, itemCount, GUI.getListPageItems(getListHeight(renderer), true));
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, itemCount] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, itemCount,
                                                       GUI.getListPageItems(getListHeight(renderer), true));
    requestUpdate();
  });
}

void EpubReaderHighlightsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 40 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int hintGutterBottom = getGutterBottom(renderer);
  const int contentY = hintGutterHeight;
  const int listY = contentY + LINE_HEIGHT;
  const int listHeight = getListHeight(renderer);

  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_HIGHLIGHTS), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_HIGHLIGHTS), true, EpdFontFamily::BOLD);

  if (!inChapter) {
    // Level one: chapters that have highlights.
    const auto getTitle = [this](int index) { return chapterTitle(chapters.at(index).spineIndex); };
    const auto getValue = [this](int index) { return std::to_string(chapters.at(index).count); };

    if (!chapters.empty()) {
      GUI.drawList(renderer, Rect{contentX, listY, contentWidth, listHeight}, chapters.size(), selectorIndex, getTitle,
                   nullptr, nullptr, getValue);
    } else {
      GUI.drawHelpText(renderer, Rect{contentX, LINE_HEIGHT * 2, contentWidth, LINE_HEIGHT},
                       tr(STR_HIGHLIGHT_INSTRUCTIONS));
    }

    const auto labels =
        mappedInput.mapLabels(tr(STR_BACK), chapters.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // Level two: one chapter's highlights.
  const int numHighlights = highlights.size();
  const auto getTitle = [this](int index) {
    return highlights.at(confirmingDelete >= DELETE_MODE_DISPLAY ? selectorIndex : index).snippet;
  };
  const auto getSubtitle = [this](int index) {
    const auto& rec = highlights.at(confirmingDelete >= DELETE_MODE_DISPLAY ? selectorIndex : index);
    return chapterTitle(rec.spineIndex) + " - " + std::to_string(rec.pageIndex + 1);
  };

  if (numHighlights > 0) {
    if (confirmingDelete >= DELETE_MODE_DISPLAY) {
      GUI.drawHelpText(renderer, Rect{0, pageHeight / 2 - LINE_HEIGHT * 2, contentWidth, LINE_HEIGHT},
                       tr(STR_CONFIRM_DELETE_HIGHLIGHT));

      // render list with just the selected item for the user to confirm to delete
      GUI.drawList(renderer, Rect{contentX, pageHeight / 2, contentWidth, LINE_HEIGHT}, 1, 0, getTitle, getSubtitle);
    } else {
      GUI.drawList(renderer, Rect{contentX, listY, contentWidth, listHeight}, numHighlights, selectorIndex, getTitle,
                   getSubtitle);

      GUI.drawHelpText(renderer, Rect{contentX, pageHeight - hintGutterBottom, contentWidth, LINE_HEIGHT},
                       tr(STR_HOLD_OPEN_TO_DELETE));
    }
  }

  const auto backLabel = confirmingDelete >= DELETE_MODE_DISPLAY ? tr(STR_CANCEL) : tr(STR_BACK);
  const auto confirmLabel =
      numHighlights > 0 ? (confirmingDelete >= DELETE_MODE_DISPLAY ? tr(STR_DELETE) : tr(STR_OPEN)) : "";
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
