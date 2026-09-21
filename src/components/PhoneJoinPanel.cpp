#include "PhoneJoinPanel.h"

#include <I18n.h>

#include <algorithm>
#include <string>

#include "components/UITheme.h"
#include "fontIds.h"
#include "network/PhonePortal.h"
#include "util/QrUtils.h"

namespace {
constexpr int QR_MAX_SIDE = 560;
constexpr int QR_MIN_SIDE = 120;

// Draws centred, wrapping only if it must; returns the height it took.
int drawWrapped(const GfxRenderer& renderer, const Rect& bounds, const int fontId, const char* text,
                const int maxLines, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (!text || *text == '\0') return 0;
  const int lineHeight = renderer.getLineHeight(fontId);
  int lines = 1;
  if (renderer.getTextWidth(fontId, text, style) > bounds.width) {
    lines = std::min(maxLines, static_cast<int>(renderer.wrappedText(fontId, text, bounds.width, maxLines, style).size()));
    if (lines < 1) lines = 1;
  }
  UITheme::drawCenteredWrappedText(renderer, bounds, fontId, text, maxLines, true, style,
                                   UITheme::TextVerticalAlignment::TOP);
  return lines * lineHeight;
}

// One numbered step: caption, QR, then up to two fallback lines. Returns the
// height it took.
int drawStep(const GfxRenderer& renderer, const Rect& column, const int qrSide, const char* caption,
             const std::string& payload, const std::string& line1, const std::string& line2) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int y = column.y;
  Rect text = column;
  text.y = y;
  y += drawWrapped(renderer, text, UI_10_FONT_ID, caption, 1, EpdFontFamily::BOLD);
  y += metrics.verticalSpacing;

  const Rect qr{column.x + (column.width - qrSide) / 2, y, qrSide, qrSide};
  QrUtils::drawQrCode(renderer, qr, payload);
  y += qrSide + metrics.verticalSpacing;

  text.y = y;
  y += drawWrapped(renderer, text, UI_10_FONT_ID, line1.c_str(), 2);
  if (!line2.empty()) {
    text.y = y;
    y += drawWrapped(renderer, text, SMALL_FONT_ID, line2.c_str(), 2);
  }
  return y - column.y;
}
}  // namespace

int PhoneJoinPanel::draw(const GfxRenderer& renderer, const Rect& bounds, const Content& content) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int gap = metrics.verticalSpacing;
  const int pad = metrics.contentSidePadding;
  int y = bounds.y;

  if (content.headline) {
    const Rect text{bounds.x + pad, y, bounds.width - pad * 2, bounds.y + bounds.height - y};
    y += drawWrapped(renderer, text, UI_12_FONT_ID, content.headline, 2, EpdFontFamily::BOLD);
    y += gap * 2;
  }

  const bool join = content.portal != nullptr && content.portal->isUp();
  const bool open = !content.pageUrl.empty();
  const int steps = (join ? 1 : 0) + (open ? 1 : 0);
  if (steps > 0) {
    // Below the QR row: two fallback lines, the gap and the status. Size the
    // codes so all of that still fits on the panel.
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int reservedBelow = lineHeight * 5 + gap * 5;
    const int columnWidth = (bounds.width - pad * 2) / steps;
    const int qrSide = std::max(
        QR_MIN_SIDE, std::min({columnWidth - pad, bounds.y + bounds.height - y - reservedBelow - lineHeight, QR_MAX_SIDE}));

    int stepHeight = 0;
    int column = 0;
    const auto columnRect = [&](const int index) {
      return Rect{bounds.x + pad + index * columnWidth, y, columnWidth, bounds.y + bounds.height - y};
    };
    if (join) {
      const PhonePortal& portal = *content.portal;
      std::string line2;
      if (!portal.passphrase().empty()) line2 = std::string(tr(STR_PHONE_JOIN_PASSWORD)) + " " + portal.passphrase();
      char caption[64];
      snprintf(caption, sizeof(caption), steps == 2 ? "1. %s" : "%s", tr(STR_PHONE_STEP_JOIN));
      stepHeight = std::max(stepHeight, drawStep(renderer, columnRect(column++), qrSide, caption, portal.joinQrPayload(),
                                                 portal.ssid(), line2));
    }
    if (open) {
      char caption[64];
      snprintf(caption, sizeof(caption), steps == 2 ? "2. %s" : "%s", tr(STR_PHONE_STEP_OPEN));
      stepHeight = std::max(stepHeight, drawStep(renderer, columnRect(column++), qrSide, caption, content.pageUrl,
                                                 content.pageUrl, content.pageAlt ? content.pageAlt : ""));
    }
    y += stepHeight;
  }

  if (content.status) {
    y += gap * 2;
    const Rect text{bounds.x + pad, y, bounds.width - pad * 2, bounds.y + bounds.height - y};
    y += drawWrapped(renderer, text, UI_10_FONT_ID, content.status, 2, EpdFontFamily::BOLD);
  }
  return y;
}
