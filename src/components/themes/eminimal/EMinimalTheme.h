#pragma once

#include "components/themes/roundedraff/RoundedRaffTheme.h"

// e-Minimal's own theme: RoundedRaff's look on every screen, with the cover
// grid home (UITheme::hasCoverGridHome). It runs on RoundedRaffMetrics::values
// -- RoundedRaffTheme.cpp reads that table directly, and the device's
// batteryBarHeight override in UITheme::getMetrics() keys on it.
class EMinimalTheme final : public RoundedRaffTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  // RoundedRaff's legend bar, drawn in HINT_FONT_ID: the legends keep the 1.5x
  // tier's 12 px while the rest of the UI uses the bigger fonts.
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
};

// Button-legend font, registered in main.cpp (not a generated id; fontIds.h).
constexpr int HINT_FONT_ID = 0x48494E54;  // "HINT"
