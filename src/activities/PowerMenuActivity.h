#pragma once

#include "activities/UiListActivity.h"

// The power button's long-press menu (eMinimal 7.8). ActivityManager::openPowerMenu()
// pushes it over whatever is on screen. Rows that leave the current screen
// (Sleep, Home) act from here; rows that act on the screen underneath
// (Refresh) return a MenuResult for openPowerMenu()'s handler to apply after
// the pop, before that screen repaints. Back closes it.
class PowerMenuActivity final : public UiListActivity {
 public:
  enum class Action { SLEEP, GO_HOME, REFRESH_SCREEN };
  static constexpr int MENU_ITEM_COUNT = 3;

  explicit PowerMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  int listCount() const override { return MENU_ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Static rows, built once in the constructor (see NetworkModeSelectionActivity).
  freeink::ui::ListItem rowItems_[MENU_ITEM_COUNT]{};
};
