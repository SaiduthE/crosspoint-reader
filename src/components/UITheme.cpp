#include "UITheme.h"

#include <BoardConfig.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/CoverGridHomeUi.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/eminimal/EMinimalTheme.h"
#include "components/themes/lyra/Lyra3CoversTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/roundedraff/RoundedRaffTheme.h"

UITheme UITheme::instance;

UITheme::UITheme() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::reload() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

float UITheme::chromeScale() {
#if FREEINK_DEVICE_EMINIMAL
  // Tied to the 1.5x font tier main.cpp registers for this device; the touch
  // boards carry a uiScale too but keep the stock fonts, so they stay as is.
  return BoardConfig::ACTIVE.uiScale;
#else
  return 1.0f;
#endif
}

int UITheme::scaledPx(const int px) { return static_cast<int>(std::lround(px * chromeScale())); }

bool UITheme::supportsCoverGrid() { return HalMemory::getPsramHeap().totalBytes > 0; }

bool UITheme::hasCoverGridHome() {
  return (SETTINGS.uiTheme == CrossPointSettings::COVER_GRID || SETTINGS.uiTheme == CrossPointSettings::EMINIMAL) &&
         supportsCoverGrid();
}

void UITheme::drawCoverGridHome(CoverGridHomeUi& home) { home.renderUi(); }

void UITheme::setTheme(CrossPointSettings::UI_THEME type) {
  if (type == CrossPointSettings::COVER_GRID && !supportsCoverGrid()) type = CrossPointSettings::LYRA;

  switch (type) {
    case CrossPointSettings::UI_THEME::CLASSIC:
      LOG_DBG("UI", "Using Classic theme");
      currentTheme = std::make_unique<BaseTheme>();
      currentMetrics = &BaseMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::COVER_GRID:
    case CrossPointSettings::UI_THEME::LYRA: {
      // The cover home owns its screen-lifetime UI state; other screens retain Lyra styling.
      auto theme = makeUniqueNoThrow<LyraTheme>();
      if (!theme) {
        LOG_ERR("UI", "OOM: Lyra theme");
        return;
      }
      currentTheme = std::move(theme);
      currentMetrics = &LyraMetrics::values;
      LOG_DBG("UI", "Using Lyra theme");
      break;
    }
    case CrossPointSettings::UI_THEME::ROUNDEDRAFF:
      LOG_DBG("UI", "Using RoundedRaff theme");
      currentTheme = std::make_unique<RoundedRaffTheme>();
      currentMetrics = &RoundedRaffMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::EMINIMAL:
      // RoundedRaff's metrics table, deliberately: see EMinimalTheme.h.
      LOG_DBG("UI", "Using e-Minimal theme");
      currentTheme = std::make_unique<EMinimalTheme>();
      currentMetrics = &RoundedRaffMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA_3_COVERS:
      LOG_DBG("UI", "Using Lyra 3 Covers theme");
      currentTheme = std::make_unique<Lyra3CoversTheme>();
      currentMetrics = &Lyra3CoversMetrics::values;
      break;
  }
  metricsValid = false;
}

namespace {
// The theme metrics are pixel sizes tuned on ~220 PPI 4" panels. The fui
// components derive their rows and headers from the UI font's line height, so
// they follow a board's larger font tier by themselves, but the hand-placed
// chrome (battery glyph, status bar lane, hint bar, home cover tile, popups,
// keyboard) does not -- on the e-Minimal 7.8" the 1.5x fonts left the battery
// squeezed and the reader footer against the glass edge. Scale those pixel
// fields by the profile's uiScale. Ratios, percentages, enums and booleans
// are left alone.
void scaleMetrics(ThemeMetrics& m, const float s) {
  if (s == 1.0f) return;
  const auto sc = [s](int& v) { v = static_cast<int>(std::lround(v * s)); };
  sc(m.batteryWidth);
  sc(m.batteryHeight);
  sc(m.topPadding);
  // batteryBarHeight stays: the glyph is centred in that strip, so growing it
  // only pushed the battery (and the header under it) down ~10 px.
  sc(m.headerHeight);
  sc(m.verticalSpacing);
  sc(m.previewPadding);
  sc(m.contentSidePadding);
  sc(m.listRowHeight);
  sc(m.listWithSubtitleRowHeight);
  sc(m.listRowGap);
  sc(m.listRowRadius);
  sc(m.listInset);
  sc(m.listSidePadding);
  sc(m.listScrollWidth);
  sc(m.headerSidePadding);
  sc(m.headerUnderlineSize);
  sc(m.menuRowHeight);
  sc(m.menuSpacing);
  sc(m.tabSpacing);
  sc(m.tabBarHeight);
  sc(m.scrollBarWidth);
  sc(m.scrollBarRightOffset);
  sc(m.homeTopPadding);
  sc(m.homeCoverHeight);
  sc(m.homeCoverTileHeight);
  sc(m.homeMenuTopOffset);
  sc(m.coverGridTabBarHeight);
  sc(m.buttonHintsHeight);
  sc(m.sideButtonHintsWidth);
  sc(m.progressBarHeight);
  sc(m.progressBarMarginTop);
  sc(m.statusBarHorizontalMargin);
  sc(m.statusBarVerticalMargin);
  sc(m.keyboardKeyHeight);
  sc(m.keyboardKeySpacing);
  sc(m.keyboardVerticalOffset);
  sc(m.popupMarginX);
  sc(m.popupMarginY);
  sc(m.popupFrameThickness);
  sc(m.popupCornerRadius);
  sc(m.popupTextBaselineOffsetY);
  sc(m.popupProgressBarHeight);
  sc(m.optionPopupItemSpacing);
  sc(m.optionPopupInnerPadding);
  sc(m.optionPopupSelectionVPadding);
  sc(m.optionPopupDialogSideMargin);
  sc(m.textFieldHorizontalPadding);
  sc(m.textFieldNormalThickness);
  sc(m.textFieldCursorThickness);
  sc(m.textFieldLineEndOffset);
  sc(m.controlRadius);
  sc(m.sheetRadius);
  sc(m.capsuleRadius);
}
}  // namespace

const ThemeMetrics& UITheme::getMetrics() const {
  // hasTouch() can flip once touch init completes after static construction, so the
  // cached copy is refreshed when the flag differs instead of copying the struct per call.
  const bool touch = gpio.hasTouch();
  if (!metricsValid || touch != metricsForTouch) {
    adjustedMetrics = *currentMetrics;
#if FREEINK_DEVICE_EMINIMAL
    scaleMetrics(adjustedMetrics, chromeScale());
    // RoundedRaff shares the battery line with the header title; with the 18 px
    // glyph the 24 px strip sat it visibly low. Judged on glass 2026-09-19 via
    // CMD:METRIC: 0 (glyph centred on the band's top edge) is right. Lyra's
    // detached 40 px strip is right as it is.
    if (currentMetrics == &RoundedRaffMetrics::values) adjustedMetrics.batteryBarHeight = 0;
#endif
    if (touch) {
      adjustedMetrics.buttonHintsHeight = 0;
    }
    metricsForTouch = touch;
    metricsValid = true;
  }
  return adjustedMetrics;
}

namespace {
struct MetricField {
  const char* name;
  int ThemeMetrics::*field;
};
// Every integer metric, by the name it has in ThemeMetrics. Ratios, percents,
// enums and booleans are deliberately absent.
constexpr MetricField kMetricFields[] = {
    {"batteryWidth", &ThemeMetrics::batteryWidth},
    {"batteryHeight", &ThemeMetrics::batteryHeight},
    {"topPadding", &ThemeMetrics::topPadding},
    {"batteryBarHeight", &ThemeMetrics::batteryBarHeight},
    {"headerHeight", &ThemeMetrics::headerHeight},
    {"verticalSpacing", &ThemeMetrics::verticalSpacing},
    {"previewPadding", &ThemeMetrics::previewPadding},
    {"contentSidePadding", &ThemeMetrics::contentSidePadding},
    {"listRowHeight", &ThemeMetrics::listRowHeight},
    {"listWithSubtitleRowHeight", &ThemeMetrics::listWithSubtitleRowHeight},
    {"listRowGap", &ThemeMetrics::listRowGap},
    {"listRowRadius", &ThemeMetrics::listRowRadius},
    {"listInset", &ThemeMetrics::listInset},
    {"listSidePadding", &ThemeMetrics::listSidePadding},
    {"listScrollWidth", &ThemeMetrics::listScrollWidth},
    {"headerSidePadding", &ThemeMetrics::headerSidePadding},
    {"headerUnderlineSize", &ThemeMetrics::headerUnderlineSize},
    {"menuRowHeight", &ThemeMetrics::menuRowHeight},
    {"menuSpacing", &ThemeMetrics::menuSpacing},
    {"tabSpacing", &ThemeMetrics::tabSpacing},
    {"tabBarHeight", &ThemeMetrics::tabBarHeight},
    {"scrollBarWidth", &ThemeMetrics::scrollBarWidth},
    {"scrollBarRightOffset", &ThemeMetrics::scrollBarRightOffset},
    {"homeTopPadding", &ThemeMetrics::homeTopPadding},
    {"homeCoverHeight", &ThemeMetrics::homeCoverHeight},
    {"homeCoverTileHeight", &ThemeMetrics::homeCoverTileHeight},
    {"homeMenuTopOffset", &ThemeMetrics::homeMenuTopOffset},
    {"coverGridTabBarHeight", &ThemeMetrics::coverGridTabBarHeight},
    {"buttonHintsHeight", &ThemeMetrics::buttonHintsHeight},
    {"sideButtonHintsWidth", &ThemeMetrics::sideButtonHintsWidth},
    {"progressBarHeight", &ThemeMetrics::progressBarHeight},
    {"progressBarMarginTop", &ThemeMetrics::progressBarMarginTop},
    {"statusBarHorizontalMargin", &ThemeMetrics::statusBarHorizontalMargin},
    {"statusBarVerticalMargin", &ThemeMetrics::statusBarVerticalMargin},
    {"keyboardKeyHeight", &ThemeMetrics::keyboardKeyHeight},
    {"keyboardKeySpacing", &ThemeMetrics::keyboardKeySpacing},
    {"keyboardVerticalOffset", &ThemeMetrics::keyboardVerticalOffset},
    {"popupMarginX", &ThemeMetrics::popupMarginX},
    {"popupMarginY", &ThemeMetrics::popupMarginY},
    {"popupFrameThickness", &ThemeMetrics::popupFrameThickness},
    {"popupCornerRadius", &ThemeMetrics::popupCornerRadius},
    {"popupTextBaselineOffsetY", &ThemeMetrics::popupTextBaselineOffsetY},
    {"popupProgressBarHeight", &ThemeMetrics::popupProgressBarHeight},
    {"optionPopupItemSpacing", &ThemeMetrics::optionPopupItemSpacing},
    {"optionPopupInnerPadding", &ThemeMetrics::optionPopupInnerPadding},
    {"optionPopupSelectionVPadding", &ThemeMetrics::optionPopupSelectionVPadding},
    {"optionPopupDialogSideMargin", &ThemeMetrics::optionPopupDialogSideMargin},
    {"textFieldHorizontalPadding", &ThemeMetrics::textFieldHorizontalPadding},
    {"textFieldNormalThickness", &ThemeMetrics::textFieldNormalThickness},
    {"textFieldCursorThickness", &ThemeMetrics::textFieldCursorThickness},
    {"textFieldLineEndOffset", &ThemeMetrics::textFieldLineEndOffset},
    {"controlRadius", &ThemeMetrics::controlRadius},
    {"sheetRadius", &ThemeMetrics::sheetRadius},
    {"capsuleRadius", &ThemeMetrics::capsuleRadius},
};
}  // namespace

bool UITheme::setMetric(const char* name, const int value) {
  getMetrics();  // make sure the live copy exists before patching it
  for (const auto& f : kMetricFields) {
    if (strcmp(f.name, name) == 0) {
      adjustedMetrics.*(f.field) = value;
      return true;
    }
  }
  return false;
}

void UITheme::printMetrics(Print& out) const {
  const ThemeMetrics& m = getMetrics();
  for (const auto& f : kMetricFields) out.printf("%s=%d\n", f.name, m.*(f.field));
}

// Screen area excluding the button hints
Rect UITheme::getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints, bool hasSideButtonHints) {
  auto orientation = renderer.getOrientation();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  Rect safeArea = Rect{0, 0, screenWidth, screenHeight};
  const ThemeMetrics metrics = getMetrics();
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      if (hasFrontButtonHints) {
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      if (hasFrontButtonHints) {
        safeArea.x += metrics.buttonHintsHeight;
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      if (hasFrontButtonHints) {
        safeArea.y += metrics.buttonHintsHeight;
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      if (hasFrontButtonHints) {
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
  }
  return safeArea;
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverHeight) {
  size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, std::to_string(coverHeight));
  }
  return coverBmpPath;
}

UIIcon UITheme::getFileIcon(const std::string& filename) {
  if (filename.back() == '/') {
    return Folder;
  }
  if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
    return Book;
  }
  if (FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename)) {
    return Text;
  }
  if (FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename)) {
    return Image;
  }
  return File;
}

int UITheme::getStatusBarHeight() {
  const ThemeMetrics metrics = UITheme::getInstance().getMetrics();
  const auto sb = SETTINGS.statusBarSpec();

  // Layout reservation is hardware-agnostic: pass clockAvailable=true so the
  // reserved height does not depend on whether an RTC is present.
  return (sb.textLaneVisible(true) ? (metrics.statusBarVerticalMargin) : 0) +
         (sb.showsProgressBar() ? (sb.progressBarHeightPx + metrics.progressBarMarginTop) : 0);
}

int UITheme::getProgressBarHeight() {
  const ThemeMetrics metrics = UITheme::getInstance().getMetrics();
  const auto sb = SETTINGS.statusBarSpec();
  return sb.showsProgressBar() ? (sb.progressBarHeightPx + metrics.progressBarMarginTop) : 0;
}

// Centered text implementation that takes the safe area into account
void UITheme::drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black, EpdFontFamily::Style style) {
  const int x = screen.x + (screen.width - renderer.getTextWidth(fontId, text, style)) / 2;
  renderer.drawText(fontId, x, y, text, black, style);
}

void UITheme::drawCenteredWrappedText(const GfxRenderer& renderer, Rect bounds, int fontId, const char* text,
                                      int maxLines, bool black, EpdFontFamily::Style style,
                                      TextVerticalAlignment verticalAlignment) {
  if (!text || *text == '\0' || bounds.width <= 0 || bounds.height <= 0 || maxLines <= 0) return;

  const int lineHeight = renderer.getLineHeight(fontId);
  if (lineHeight <= 0) return;

  const int lineLimit = std::min(maxLines, bounds.height / lineHeight);
  if (lineLimit <= 0) return;

  const auto alignedTop = [&](const int textHeight) {
    switch (verticalAlignment) {
      case TextVerticalAlignment::CENTER:
        return bounds.y + (bounds.height - textHeight) / 2;
      case TextVerticalAlignment::BOTTOM:
        return bounds.y + bounds.height - textHeight;
      case TextVerticalAlignment::TOP:
      default:
        return bounds.y;
    }
  };

  if (renderer.getTextWidth(fontId, text, style) <= bounds.width) {
    drawCenteredText(renderer, bounds, fontId, alignedTop(lineHeight), text, black, style);
    return;
  }

  const auto lines = renderer.wrappedText(fontId, text, bounds.width, lineLimit, style);
  int y = alignedTop(static_cast<int>(lines.size()) * lineHeight);
  for (const auto& line : lines) {
    drawCenteredText(renderer, bounds, fontId, y, line.c_str(), black, style);
    y += lineHeight;
  }
}
