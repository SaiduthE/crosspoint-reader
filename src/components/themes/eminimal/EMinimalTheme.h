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
};
