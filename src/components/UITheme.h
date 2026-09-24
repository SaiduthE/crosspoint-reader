#pragma once

#include <Arduino.h>
#include <EpdFontFamily.h>

#include <functional>
#include <memory>

#include "CrossPointSettings.h"
#include "components/themes/BaseTheme.h"

class CoverGridHomeUi;

class UITheme {
  // Static instance
  static UITheme instance;

 public:
  enum class TextVerticalAlignment { TOP, CENTER, BOTTOM };

  UITheme();
  static UITheme& getInstance() { return instance; }

  const ThemeMetrics& getMetrics() const;
  const BaseTheme& getTheme() const { return currentTheme ? *currentTheme : fallbackTheme; }
  Rect getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints = false,
                         bool hasSideButtonHints = false);
  static void drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  // Wraps only overflowing text, then aligns the complete line block within bounds.
  static void drawCenteredWrappedText(const GfxRenderer& renderer, Rect bounds, int fontId, const char* text,
                                      int maxLines, bool black = true,
                                      EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                                      TextVerticalAlignment verticalAlignment = TextVerticalAlignment::CENTER);
  // The factor getMetrics() scales the pixel metrics by: the board's uiScale
  // where the UI fonts follow it (e-Minimal), 1 elsewhere. scaledPx() applies
  // it to a hand-placed size that is not in ThemeMetrics.
  static float chromeScale();
  static int scaledPx(int px);
  static bool supportsCoverGrid();
  static bool hasCoverGridHome();
  static void drawCoverGridHome(CoverGridHomeUi& home);
  void reload();
  void setTheme(CrossPointSettings::UI_THEME type);
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverHeight);
  static UIIcon getFileIcon(const std::string& filename);
  static int getStatusBarHeight();
  static int getProgressBarHeight();

  // Bench tuning without a reflash: patch one integer metric of the live
  // (scaled) set by name, e.g. from the serial console's CMD:METRIC. The value
  // is not persisted -- once a number is right it is baked into the theme
  // table or the uiScale rules. Returns false for an unknown name.
  // printMetrics() lists every tunable metric with its current value.
  bool setMetric(const char* name, int value);
  void printMetrics(Print& out) const;

 private:
  BaseTheme fallbackTheme;
  const ThemeMetrics* currentMetrics = &BaseMetrics::values;
  std::unique_ptr<BaseTheme> currentTheme;
  mutable ThemeMetrics adjustedMetrics;
  mutable bool metricsValid = false;
  mutable bool metricsForTouch = false;
};

// Helper macro to access current theme
#define GUI UITheme::getInstance().getTheme()
