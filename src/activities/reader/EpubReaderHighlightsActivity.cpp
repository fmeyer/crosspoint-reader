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
  highlights.clear();
  HighlightUtil::loadHighlights(epubPath, highlights);
  LOG_DBG("EPH", "Loaded %d highlights for book: %s", static_cast<int>(highlights.size()), epubPath.c_str());
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
  // Delete confirmation mode
  if (confirmingDelete >= DELETE_MODE_DISPLAY) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (confirmingDelete == DELETE_MODE_DISPLAY) {
        confirmingDelete = DELETE_MODE_CONFIRM;  // first confirmation, update text
        requestUpdate();
        return;
      }
      highlights.erase(highlights.begin() + selectorIndex);
      if (!HighlightUtil::saveAllHighlights(epubPath, highlights)) {
        LOG_ERR("EPH", "Failed to save highlights after delete");
      }
      if (selectorIndex >= static_cast<int>(highlights.size()) && selectorIndex > 0) {
        selectorIndex--;
      }
      requestUpdate();
      confirmingDelete = DELETE_MODE_OFF;
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      requestUpdate();
      confirmingDelete = DELETE_MODE_OFF;
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {  // Open
    if (highlights.empty()) {
      return;
    }
    const auto& rec = highlights.at(selectorIndex);
    setResult(ProgressChangeResult{rec.spineIndex, rec.pageIndex});
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
    if (highlights.empty()) {
      return;
    }
    confirmingDelete = DELETE_MODE_DISPLAY;
    requestUpdate();
  }

  buttonNavigator.onNextRelease([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, highlights.size());
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, highlights.size());
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, highlights.size(),
                                                   GUI.getListPageItems(getListHeight(renderer), true));
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, highlights.size(),
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
  const int numHighlights = highlights.size();

  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_HIGHLIGHTS), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_HIGHLIGHTS), true, EpdFontFamily::BOLD);

  const auto getTitle = [this](int index) {
    return highlights.at(confirmingDelete >= DELETE_MODE_DISPLAY ? selectorIndex : index).snippet;
  };
  const auto getSubtitle = [this](int index) {
    const auto& rec = highlights.at(confirmingDelete >= DELETE_MODE_DISPLAY ? selectorIndex : index);
    const auto tocIndex = epub->getTocIndexForSpineIndex(rec.spineIndex);
    const auto tocTitle = (tocIndex >= 0) ? (epub->getTocItem(tocIndex)).title : tr(STR_UNNAMED);
    return tocTitle + " - " + std::to_string(rec.pageIndex + 1);
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
                       tr(STR_HOLD_CONFIRM_TO_DELETE));
    }
  } else {
    GUI.drawHelpText(renderer, Rect{contentX, LINE_HEIGHT * 2, contentWidth, LINE_HEIGHT},
                     tr(STR_HIGHLIGHT_INSTRUCTIONS));
  }

  const auto backLabel = confirmingDelete >= DELETE_MODE_DISPLAY ? tr(STR_CANCEL) : tr(STR_BACK);
  const auto confirmLabel =
      numHighlights > 0 ? (confirmingDelete >= DELETE_MODE_DISPLAY ? tr(STR_DELETE) : tr(STR_OPEN)) : "";
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
