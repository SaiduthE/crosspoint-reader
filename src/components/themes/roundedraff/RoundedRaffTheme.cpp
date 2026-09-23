#include "RoundedRaffTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int kMenuRadius = 30;
constexpr int kBottomRadius = 15;
constexpr int kRowRadius = 20;
constexpr int kInteractiveInsetX = 20;
constexpr int kSelectableRowGap = 6;
constexpr int kTitleFontId = UI_12_FONT_ID;  // Requested main title size: 12px
constexpr int kGuideFontId = SMALL_FONT_ID;  // Closest available to requested 6px

// Continue Reading card: cover box on the left, title and author beside it.
constexpr int kCardRadius = kRowRadius;
constexpr int kCardPadding = 12;
constexpr int kCoverRadius = 12;
constexpr int kCoverHeight = RoundedRaffMetrics::values.homeCoverHeight;
constexpr int kCoverWidth = kCoverHeight * 6 / 10;  // Thumbnails are generated for a 0.6 aspect
constexpr int kCoverTextGap = 16;
constexpr int kAuthorGap = 8;
constexpr int kTitleMaxLines = 4;
constexpr int kSelectionStroke = 3;
constexpr float kMaxCropFraction = 0.9f;

void drawScrollBar(const GfxRenderer& renderer, Rect rect, int itemCount, int pageStartIndex, int pageItems) {
  if (itemCount <= 0 || pageItems <= 0 || itemCount <= pageItems) {
    return;
  }

  const int barW = RoundedRaffMetrics::values.scrollBarWidth;
  const int barX = rect.x + rect.width - RoundedRaffMetrics::values.scrollBarRightOffset - barW;
  const int barY = rect.y;
  const int barH = rect.height;

  const int thumbH = std::max(10, (barH * pageItems) / itemCount);
  const int maxStart = std::max(1, itemCount - pageItems);
  const int maxTravel = std::max(1, barH - thumbH);
  const int clampedStart = std::clamp(pageStartIndex, 0, maxStart);
  const int thumbY = barY + (clampedStart * maxTravel) / maxStart;

  renderer.fillRect(barX, thumbY, barW, thumbH);
}

// Centre-crops the bitmap to the box aspect; drawBitmap then shrinks it to fit. Thumbnails are
// generated scale-to-fill, so one axis usually overshoots the box.
bool drawCroppedBitmap(const GfxRenderer& renderer, const Bitmap& bitmap, const Rect& box) {
  const int bitmapW = bitmap.getWidth();
  const int bitmapH = bitmap.getHeight();
  if (bitmapW <= 0 || bitmapH <= 0) {
    return false;
  }

  // Whole source pixels trimmed per side, rounded up so the drawn image never exceeds the box.
  int trimX = 0;
  int trimY = 0;
  if (bitmapW * box.height > box.width * bitmapH) {
    const int keepW = std::max(1, bitmapH * box.width / box.height);
    trimX = (bitmapW - keepW + 1) / 2;
  } else {
    const int keepH = std::max(1, bitmapW * box.height / box.width);
    trimY = (bitmapH - keepH + 1) / 2;
  }
  // drawBitmap floors bitmapW * crop / 2; the extra half pixel lands that exactly on trimX/trimY.
  const float cropX = trimX > 0 ? std::min(kMaxCropFraction, (2.0f * trimX + 0.5f) / bitmapW) : 0.0f;
  const float cropY = trimY > 0 ? std::min(kMaxCropFraction, (2.0f * trimY + 0.5f) / bitmapH) : 0.0f;

  // drawBitmap never upscales, so centre a cover smaller than the box.
  const int offsetX = std::max(0, (box.width - (bitmapW - 2 * trimX)) / 2);
  const int offsetY = std::max(0, (box.height - (bitmapH - 2 * trimY)) / 2);
  return renderer.drawBitmap(bitmap, box.x + offsetX, box.y + offsetY, box.width, box.height, cropX, cropY);
}

// Draws the Continue Reading cover into its fixed box, or the placeholder when no usable thumbnail exists.
void drawCoverBox(const GfxRenderer& renderer, const Rect& box, const std::string& coverBmpPath) {
  renderer.fillRect(box.x, box.y, box.width, box.height, false);

  bool drawn = false;
  if (!coverBmpPath.empty()) {
    const std::string thumbPath = UITheme::getCoverThumbPath(coverBmpPath, RoundedRaffMetrics::values.homeCoverHeight);
    HalFile file;
    if (Storage.openFileForRead("HOME", thumbPath, file)) {
      Bitmap bitmap(file);
      // Books without a usable cover leave an empty thumbnail file, which fails to parse.
      drawn = bitmap.parseHeaders() == BmpReaderError::Ok && drawCroppedBitmap(renderer, bitmap, box);
    }
  }

  if (!drawn) {
    renderer.fillRect(box.x, box.y, box.width, box.height, false);
    renderer.fillRect(box.x, box.y + box.height / 3, box.width, box.height - box.height / 3, true);
    renderer.drawIcon(CoverIcon, box.x + 24, box.y + 24, 32);
  }

  renderer.maskRoundedRectOutsideCorners(box.x, box.y, box.width, box.height, kCoverRadius, Color::LightGray);
  renderer.drawRoundedRect(box.x, box.y, box.width, box.height, 1, kCoverRadius, true);
}

}  // namespace

void RoundedRaffTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const int sidePadding = RoundedRaffMetrics::values.contentSidePadding;
  const Rect card{rect.x + sidePadding, rect.y, rect.width - 2 * sidePadding, rect.height};

  if (recentBooks.empty()) {
    renderer.fillRoundedRect(card.x, card.y, card.width, card.height, kCardRadius, Color::LightGray);
    renderer.drawCenteredText(kTitleFontId, card.y + card.height / 2 - renderer.getLineHeight(kTitleFontId) / 2,
                              tr(STR_NO_OPEN_BOOK));
    return;
  }

  const RecentBook& book = recentBooks[0];
  const Rect cover{card.x + kCardPadding, card.y + kCardPadding, kCoverWidth, kCoverHeight};

  // Card and cover come from SD once, then from the stored snapshot. Text and selection are
  // drawn on top every render so they never mix with stale snapshot content.
  if (!coverRendered || !bufferRestored) {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    renderer.fillRoundedRect(card.x, card.y, card.width, card.height, kCardRadius, Color::LightGray);
    drawCoverBox(renderer, cover, book.coverBmpPath);
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
  }

  const int textX = cover.x + cover.width + kCoverTextGap;
  const int textWidth = card.x + card.width - kCardPadding - textX;
  const auto titleLines =
      renderer.wrappedText(kTitleFontId, book.title.c_str(), textWidth, kTitleMaxLines, EpdFontFamily::BOLD);
  const int titleLineHeight = renderer.getLineHeight(kTitleFontId);
  const bool hasAuthor = !book.author.empty();
  const int authorGap = titleLines.empty() ? 0 : kAuthorGap;
  const int blockHeight = static_cast<int>(titleLines.size()) * titleLineHeight +
                          (hasAuthor ? authorGap + renderer.getLineHeight(UI_10_FONT_ID) : 0);

  int textY = cover.y + (cover.height - blockHeight) / 2;
  for (const auto& line : titleLines) {
    renderer.drawText(kTitleFontId, textX, textY, line.c_str(), true, EpdFontFamily::BOLD);
    textY += titleLineHeight;
  }
  if (hasAuthor) {
    const std::string author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, textY + authorGap, author.c_str(), true);
  }

  if (selectorIndex == 0) {
    renderer.drawRoundedRect(card.x, card.y, card.width, card.height, kSelectionStroke, kCardRadius, true);
  }
}

int RoundedRaffTheme::getMenuRowHeight(const GfxRenderer& renderer) const {
  return renderer.getLineHeight(kTitleFontId) + 20;  // 10px top + 10px bottom
}

void RoundedRaffTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                      const std::function<std::string(int index)>& buttonLabel,
                                      const std::function<UIIcon(int index)>& rowIcon) const {
  (void)rowIcon;
  const int sidePadding = RoundedRaffMetrics::values.contentSidePadding;
  const int rowX = rect.x + sidePadding;
  const int rowHeight = getMenuRowHeight(renderer);  // shared with HomeActivity's touch grid
  const int rowGap = kSelectableRowGap;
  const int rowStep = rowHeight + rowGap;
  const int pageItems = std::max(1, rect.height / rowStep);
  const int safeSelectedIndex = std::max(0, selectedIndex);
  const int pageStartIndex = (safeSelectedIndex / pageItems) * pageItems;
  const int menuTop = rect.y;
  const int textLineHeight = renderer.getLineHeight(kTitleFontId);
  const int menuMaxWidth = std::max(0, rect.width - sidePadding * 2);

  for (int i = pageStartIndex; i < buttonCount && i < pageStartIndex + pageItems; ++i) {
    const std::string label = buttonLabel(i);
    const int rowY = menuTop + (i - pageStartIndex) * rowStep;
    constexpr int kRowPaddingX = 40;  // 20px L/R
    const int maxLabelWidth = std::max(0, menuMaxWidth - kRowPaddingX);
    const std::string truncatedLabel =
        renderer.truncatedText(kTitleFontId, label.c_str(), maxLabelWidth, EpdFontFamily::BOLD);
    const int rowWidth = std::min(
        menuMaxWidth, renderer.getTextWidth(kTitleFontId, truncatedLabel.c_str(), EpdFontFamily::BOLD) + kRowPaddingX);
    const bool isSelected = selectedIndex == i;
    renderer.fillRoundedRect(rowX, rowY, rowWidth, rowHeight, kMenuRadius, isSelected ? Color::Black : Color::White);
    const int textY = rowY + (rowHeight - textLineHeight) / 2;
    const int textX = rowX + kInteractiveInsetX;
    if (selectedIndex == i) {
      renderer.drawText(kTitleFontId, textX, textY, truncatedLabel.c_str(), false, EpdFontFamily::BOLD);
    } else {
      renderer.drawText(kTitleFontId, textX, textY, truncatedLabel.c_str(), true, EpdFontFamily::BOLD);
    }
  }

  drawScrollBar(renderer, rect, buttonCount, pageStartIndex, pageItems);
}

void RoundedRaffTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode,
                                     int contentStartX, int contentWidth) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineY = rect.y + rect.height + lineHeight + metrics.verticalSpacing;
  const int thickness = cursorMode ? 3 : 2;

  if (contentWidth > 0) {
    renderer.drawLine(rect.x + contentStartX, lineY, rect.x + contentStartX + contentWidth - 1, lineY, thickness, true);
    return;
  }

  constexpr int hPadding = 8;
  const int lineW = textWidth + hPadding * 2;
  const int lineStart = rect.x + (rect.width - lineW) / 2;
  renderer.drawLine(lineStart, lineY, lineStart + lineW - 1, lineY, thickness, true);
}

void RoundedRaffTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                       const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = 20;
  const int groupGap = 10;
  const int bottomMargin = 10;
  const int hintHeight = RoundedRaffMetrics::values.buttonHintsHeight - 10;  // 30px total guide height
  const int groupWidth = (pageWidth - sidePadding * 2 - groupGap) / 2;
  const int hintY = pageHeight - hintHeight - bottomMargin;
  const int textY = hintY + (hintHeight - renderer.getLineHeight(kGuideFontId)) / 2;

  if (renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute()) {
    renderer.fillRect(sidePadding, hintY, groupWidth, hintHeight, true);
    renderer.fillRect(sidePadding + groupWidth + groupGap, hintY, groupWidth, hintHeight, true);
    renderer.setOrientation(origOrientation);
    return;
  }

  const bool backDisabled = (btn1 == nullptr || btn1[0] == '\0');
  const int leftGroupX = sidePadding;
  const int rightGroupX = leftGroupX + groupWidth + groupGap;
  const std::string backLabel = backDisabled ? "" : std::string(btn1);
  // Callers should provide the button labels. If a label is not specified, it should render empty.
  const std::string selectText = (btn2 && btn2[0] != '\0') ? std::string(btn2) : "";
  const std::string upText = (btn3 && btn3[0] != '\0') ? std::string(btn3) : "";
  const std::string downText = (btn4 && btn4[0] != '\0') ? std::string(btn4) : "";

  // Ensure button hints always "win" visually even if other elements accidentally render into this area.
  renderer.fillRect(leftGroupX, hintY, groupWidth, hintHeight, false);
  renderer.fillRect(rightGroupX, hintY, groupWidth, hintHeight, false);

  renderer.drawRoundedRect(leftGroupX, hintY, groupWidth, hintHeight, 2, kBottomRadius, true);
  const int selectWidth = renderer.getTextWidth(kGuideFontId, selectText.c_str(), EpdFontFamily::REGULAR);
  const int downWidth = renderer.getTextWidth(kGuideFontId, downText.c_str(), EpdFontFamily::REGULAR);
  constexpr int innerEdgePadding = 16;

  const int backX = leftGroupX + innerEdgePadding;
  const int selectX = leftGroupX + groupWidth - innerEdgePadding - selectWidth;
  const int upX = rightGroupX + innerEdgePadding;
  const int downX = rightGroupX + groupWidth - innerEdgePadding - downWidth;

  if (!backDisabled) {
    renderer.drawText(kGuideFontId, backX, textY, backLabel.c_str(), true, EpdFontFamily::REGULAR);
  }
  renderer.drawText(kGuideFontId, selectX, textY, selectText.c_str(), true, EpdFontFamily::REGULAR);

  renderer.drawRoundedRect(rightGroupX, hintY, groupWidth, hintHeight, 2, kBottomRadius, true);

  renderer.drawText(kGuideFontId, upX, textY, upText.c_str(), true, EpdFontFamily::REGULAR);
  renderer.drawText(kGuideFontId, downX, textY, downText.c_str(), true, EpdFontFamily::REGULAR);

  renderer.setOrientation(origOrientation);
}
