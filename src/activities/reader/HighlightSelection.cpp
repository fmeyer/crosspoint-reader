#include "HighlightSelection.h"

#if CROSSPOINT_HIGHLIGHT_EXPERIMENT

#include <Arduino.h>
#include <BidiUtils.h>
#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>

namespace {

// Decode the UTF-8 codepoint that ends at byte index `end` (exclusive).
// Returns the codepoint's start index and writes the decoded value to outCp.
size_t prevCodepoint(const std::string& s, const size_t end, uint32_t* outCp) {
  size_t start = end;
  do {
    start--;
  } while (start > 0 && (static_cast<uint8_t>(s[start]) & 0xC0) == 0x80);
  const auto b0 = static_cast<uint8_t>(s[start]);
  const size_t len = end - start;
  uint32_t cp = b0;
  if ((b0 & 0xE0) == 0xC0 && len >= 2) {
    cp = ((b0 & 0x1FU) << 6) | (static_cast<uint8_t>(s[start + 1]) & 0x3FU);
  } else if ((b0 & 0xF0) == 0xE0 && len >= 3) {
    cp = ((b0 & 0x0FU) << 12) | ((static_cast<uint8_t>(s[start + 1]) & 0x3FU) << 6) |
         (static_cast<uint8_t>(s[start + 2]) & 0x3FU);
  } else if ((b0 & 0xF8) == 0xF0 && len >= 4) {
    cp = ((b0 & 0x07U) << 18) | ((static_cast<uint8_t>(s[start + 1]) & 0x3FU) << 12) |
         ((static_cast<uint8_t>(s[start + 2]) & 0x3FU) << 6) | (static_cast<uint8_t>(s[start + 3]) & 0x3FU);
  }
  *outCp = cp;
  return start;
}

bool isSentenceTerminator(const uint32_t cp) {
  return cp == '.' || cp == '!' || cp == '?' || cp == 0x2026 /* ellipsis */;
}

// Closing quotes/brackets that may trail the terminator ("...word."), skipped when
// scanning a word tail for the sentence-ending punctuation.
bool isClosingWrapper(const uint32_t cp) {
  return cp == '"' || cp == '\'' || cp == ')' || cp == ']' || cp == 0x201D /* right double quote */ ||
         cp == 0x2019 /* right single quote */ || cp == 0x00BB /* raquo */ || cp == 0x00AB /* laquo */;
}

// Tokenization is whitespace-only, so sentence punctuation stays glued to the word.
bool endsSentence(const char* word, const size_t len) {
  const std::string w(word, len);
  size_t end = w.size();
  while (end > 0) {
    uint32_t cp = 0;
    const size_t start = prevCodepoint(w, end, &cp);
    if (isClosingWrapper(cp)) {
      end = start;
      continue;
    }
    return isSentenceTerminator(cp);
  }
  return false;
}

// First-line indent marker inserted by the layout engine (U+2003 em-space).
bool startsWithEmSpace(const char* word, const size_t len) {
  return len >= 3 && static_cast<uint8_t>(word[0]) == 0xE2 && static_cast<uint8_t>(word[1]) == 0x80 &&
         static_cast<uint8_t>(word[2]) == 0x83;
}

}  // namespace

bool HighlightSelection::buildFromPage(const Page& page, const GfxRenderer& renderer, const int fontId,
                                       const int marginLeft, const int marginTop, const float lineCompression) {
  // buildTried is set on the way OUT, never here: it runs on the render task
  // while the main loop polls wasBuildAttempted(), and flipping it before the
  // model is complete lets the loop conclude "attempted but empty" mid-build
  // and free this object under the builder (use-after-free panic).
  rects.clear();
  textPool.clear();
  textOffset.clear();

  lineH = static_cast<int>(static_cast<float>(renderer.getLineHeight(fontId)) * lineCompression);
  if (lineH <= 0) {
    lineH = renderer.getLineHeight(fontId);
  }
  ascender = renderer.getFontAscenderSize(fontId);

  // Pre-count for exact reserves: growth reallocations fragment the small DRAM heap.
  size_t wordTotal = 0;
  size_t textBytes = 0;
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) {
      continue;
    }
    const auto& block = static_cast<const PageLine&>(*el).getBlock();
    if (!block || !block->valid()) {
      continue;
    }
    wordTotal += block->wordCount();
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      textBytes += block->wordTextLen(i) + 1;
    }
  }
  if (wordTotal == 0 || wordTotal > MAX_WORDS || textBytes > MAX_TEXT_BYTES) {
    LOG_DBG("HLS", "Page not selectable (words=%u, bytes=%u)", static_cast<uint32_t>(wordTotal),
            static_cast<uint32_t>(textBytes));
    buildTried = true;
    return false;
  }

  rects.reserve(wordTotal);
  textOffset.reserve(wordTotal + 1);
  textPool.reserve(textBytes);

  bool firstLine = true;
  int prevLineY = 0;
  bool prevEndsSentence = false;
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) {
      continue;
    }
    const auto& line = static_cast<const PageLine&>(*el);
    const auto& block = line.getBlock();
    if (!block || !block->valid() || block->wordCount() == 0) {
      continue;
    }

    const bool isRtl = block->getBlockStyle().isRtl;
    const int lineX = marginLeft + line.xPos;
    // Ruby annotations shift the word baseline down within the line; mirror
    // TextBlock::render so the rects track the visible glyphs.
    const int lineY = marginTop + line.yPos + block->getRubyShift(ascender);

    // Paragraph boundary heuristics: extra vertical gap (paragraph spacing) or the
    // em-space indent marker on the first word. First line of the page also counts.
    const bool paraStart = firstLine || (lineY - prevLineY > lineH + lineH / 4) ||
                           startsWithEmSpace(block->wordText(0), block->wordTextLen(0));

    for (uint16_t i = 0; i < block->wordCount(); i++) {
      const char* word = block->wordText(i);
      const auto style = block->wordStyle(i);
      const auto baseDir = static_cast<BidiUtils::BidiBaseDir>(BidiUtils::detectParagraphLevel(word, isRtl ? 1 : 0));
      const int w = renderer.getTextWidth(fontId, word, style, baseDir);

      uint8_t flags = 0;
      const bool wordStartsPara = rects.empty() || (i == 0 && paraStart);
      if (wordStartsPara) {
        flags |= FLAG_PARA_START | FLAG_SENTENCE_START;
      } else if (prevEndsSentence) {
        flags |= FLAG_SENTENCE_START;
      }

      textOffset.push_back(static_cast<uint16_t>(textPool.size()));
      textPool.append(word, block->wordTextLen(i));
      textPool += ' ';
      rects.push_back({static_cast<int16_t>(lineX + block->wordXpos(i)), static_cast<int16_t>(lineY),
                       static_cast<uint16_t>(std::max(w, 1)), flags});
      prevEndsSentence = endsSentence(word, block->wordTextLen(i));
    }

    prevLineY = lineY;
    firstLine = false;
  }
  textOffset.push_back(static_cast<uint16_t>(textPool.size()));

  anchor = selStart = selEnd = 0;
  paintedStart = paintedEnd = 0;
  hasPainted = false;
  pressLevel = 0;
  LOG_DBG("HLS", "Built %u word rects", static_cast<uint32_t>(rects.size()));
  buildTried = true;  // last: publishes the completed model to the main loop
  return !rects.empty();
}

bool HighlightSelection::sentenceHop(const bool forward) {
  if (rects.empty()) {
    return false;
  }
  const auto n = static_cast<uint16_t>(rects.size());
  uint16_t idx;
  if (forward) {
    idx = 0;  // wrap target: word 0 always carries SENTENCE_START
    for (uint16_t i = selEnd + 1; i < n; i++) {
      if ((rects[i].flags & FLAG_SENTENCE_START) != 0) {
        idx = i;
        break;
      }
    }
  } else {
    // Start of the current sentence; if the selection already sits on it, the
    // previous one. Wraps to the last sentence start on the page.
    const uint16_t current = scanBackFlag(selStart, FLAG_SENTENCE_START);
    if (current < selStart) {
      idx = current;
    } else if (selStart > 0) {
      idx = scanBackFlag(selStart - 1, FLAG_SENTENCE_START);
    } else {
      idx = scanBackFlag(n - 1, FLAG_SENTENCE_START);
    }
  }
  anchor = selStart = selEnd = idx;
  pressLevel = 0;
  return true;
}

bool HighlightSelection::onSinglePress(const bool forward) {
  if (rects.empty()) {
    return false;
  }
  const unsigned long now = millis();
  if (pressLevel > 0 && forward == lastPressForward && now - lastPressTime <= MULTI_PRESS_MS) {
    if (pressLevel < 3) {
      pressLevel++;
    }
  } else {
    pressLevel = 1;
  }
  lastPressForward = forward;
  lastPressTime = now;

  const uint16_t oldStart = selStart;
  const uint16_t oldEnd = selEnd;
  if (forward) {
    applyForward(pressLevel);
  } else {
    applyBack(pressLevel);
  }
  return selStart != oldStart || selEnd != oldEnd;
}

void HighlightSelection::applyForward(const uint8_t level) {
  const auto last = static_cast<uint16_t>(rects.size() - 1);
  switch (level) {
    case 1:
      if (selEnd < last) {
        selEnd++;
      }
      break;
    case 2:
      selStart = scanBackFlag(selStart, FLAG_SENTENCE_START);
      selEnd = scanEndFlag(selEnd, FLAG_SENTENCE_START);
      break;
    default:
      selStart = scanBackFlag(selStart, FLAG_PARA_START);
      selEnd = scanEndFlag(selEnd, FLAG_PARA_START);
      break;
  }
}

void HighlightSelection::applyBack(const uint8_t level) {
  switch (level) {
    case 1:
      if (selEnd > anchor) {
        selEnd--;
      }
      break;
    case 2:
      // Trim toward the sentence containing the anchor; never grows the selection.
      selStart = std::max(selStart, scanBackFlag(anchor, FLAG_SENTENCE_START));
      selEnd = std::min(selEnd, scanEndFlag(anchor, FLAG_SENTENCE_START));
      break;
    default:
      selStart = selEnd = anchor;
      break;
  }
}

uint16_t HighlightSelection::scanBackFlag(const uint16_t from, const uint8_t flag) const {
  for (uint16_t i = from;; i--) {
    if ((rects[i].flags & flag) != 0 || i == 0) {
      return i;
    }
  }
}

uint16_t HighlightSelection::scanEndFlag(const uint16_t from, const uint8_t flag) const {
  const auto n = static_cast<uint16_t>(rects.size());
  for (uint16_t i = from + 1; i < n; i++) {
    if ((rects[i].flags & flag) != 0) {
      return i - 1;
    }
  }
  return n - 1;
}

void HighlightSelection::paintRange(const GfxRenderer& renderer, const uint16_t a, const uint16_t b) const {
  // One invert per screen line: min/max over the run keeps inter-word gaps inside
  // the highlight and stays correct for RTL lines where x is not monotonic.
  size_t i = a;
  while (i <= b) {
    const int16_t y = rects[i].y;
    int minX = rects[i].x;
    int maxX = rects[i].x + rects[i].w;
    size_t j = i + 1;
    while (j <= b && rects[j].y == y) {
      minX = std::min(minX, static_cast<int>(rects[j].x));
      maxX = std::max(maxX, rects[j].x + rects[j].w);
      j++;
    }
    renderer.invertRegion(minX - 1, y - 1, maxX - minX + 2, lineH + 2);
    i = j;
  }
}

void HighlightSelection::paintCurrent(const GfxRenderer& renderer) {
  if (rects.empty()) {
    return;
  }
  paintRange(renderer, selStart, selEnd);
  paintedStart = selStart;
  paintedEnd = selEnd;
  hasPainted = true;
}

void HighlightSelection::underlineRanges(const GfxRenderer& renderer,
                                         std::vector<std::pair<uint16_t, uint16_t>> ranges) const {
  if (rects.empty() || ranges.empty()) {
    return;
  }
  const auto last = static_cast<uint16_t>(rects.size() - 1);
  std::sort(ranges.begin(), ranges.end());
  uint16_t runStart = 0;
  uint16_t runEnd = 0;
  bool haveRun = false;
  const auto drawRun = [&](const uint16_t a, const uint16_t b) {
    // One underline per screen line: min/max over the run keeps inter-word gaps
    // covered and stays correct for RTL lines where x is not monotonic.
    size_t i = a;
    while (i <= b) {
      const int16_t y = rects[i].y;
      int minX = rects[i].x;
      int maxX = rects[i].x + rects[i].w;
      size_t j = i + 1;
      while (j <= b && rects[j].y == y) {
        minX = std::min(minX, static_cast<int>(rects[j].x));
        maxX = std::max(maxX, rects[j].x + rects[j].w);
        j++;
      }
      // Baseline + 2, matching TextBlock's UNDERLINE placement.
      const int underlineY = y + ascender + 2;
      renderer.drawLine(minX, underlineY, maxX, underlineY, 2, true);
      i = j;
    }
  };
  for (const auto& [start, rawEnd] : ranges) {
    if (start > last) {
      continue;  // stale indices from a layout that no longer matches
    }
    const uint16_t end = std::min(rawEnd, last);
    if (haveRun && start <= runEnd + 1) {
      runEnd = std::max(runEnd, end);
      continue;
    }
    if (haveRun) {
      drawRun(runStart, runEnd);
    }
    runStart = start;
    runEnd = end;
    haveRun = true;
  }
  if (haveRun) {
    drawRun(runStart, runEnd);
  }
}

void HighlightSelection::repaintDiff(const GfxRenderer& renderer) {
  if (rects.empty()) {
    return;
  }
  if (hasPainted) {
    if (paintedStart == selStart && paintedEnd == selEnd) {
      return;
    }
    paintRange(renderer, paintedStart, paintedEnd);  // XOR: un-highlights the old range
  }
  paintCurrent(renderer);
}

std::string HighlightSelection::selectedText() const {
  if (rects.empty()) {
    return {};
  }
  const size_t startOff = textOffset[selStart];
  const size_t endOff = textOffset[selEnd + 1] - 1;  // drop the trailing joiner space
  return textPool.substr(startOff, endOff - startOff);
}

#endif  // CROSSPOINT_HIGHLIGHT_EXPERIMENT
