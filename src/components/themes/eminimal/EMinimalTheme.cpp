#include "EMinimalTheme.h"

void EMinimalTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                               const char* subtitle) const {
  // RoundedRaff skips untitled headers because its list home draws its own in
  // drawRecentBookCover. The cover grid home has no such header and draws the
  // battery through an untitled band, so draw every header.
  BaseTheme::drawHeader(renderer, rect, title, subtitle);
}
