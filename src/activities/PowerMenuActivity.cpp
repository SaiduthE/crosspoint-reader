#include "PowerMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId menuItems[PowerMenuActivity::MENU_ITEM_COUNT] = {
    StrId::STR_SLEEP,
    StrId::STR_GO_HOME_BUTTON,
    StrId::STR_FORCE_REFRESH,
};
}  // namespace

PowerMenuActivity::PowerMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("PowerMenu", renderer, mappedInput) {
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[i]);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_[i] = item;
  }
}

const char* PowerMenuActivity::headerTitle() const { return tr(STR_POWER_MENU); }

void PowerMenuActivity::activateIndex(const int index) {
  // Every row leaves this screen; a lingering flash would gray an unrelated
  // element on the next render.
  app.clearTapFlash();
  nav.selected = index;

  switch (static_cast<Action>(index)) {
    case Action::SLEEP:
      // Not finish(): the sleep screen replaces the whole stack, and popping
      // first would re-render the page underneath for nothing. The reader stays
      // on the stack, so the wake still resumes the book.
      activityManager.requestDeepSleep();
      return;
    case Action::GO_HOME:
      activityManager.goHome();
      return;
    case Action::REFRESH_SCREEN:
      // Applied by openPowerMenu()'s result handler on the screen underneath.
      setResult(MenuResult{static_cast<int>(Action::REFRESH_SCREEN)});
      finish();
      return;
  }
}

void PowerMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEM_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
