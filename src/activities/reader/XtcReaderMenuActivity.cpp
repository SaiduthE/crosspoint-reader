#include "XtcReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

XtcReaderMenuActivity::XtcReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const std::string& title, const int currentPage, const int totalPages,
                                             const bool hasChapters)
    : UiListActivity("XtcReaderMenu", renderer, mappedInput),
      title(title),
      currentPage(currentPage),
      totalPages(totalPages) {
  if (hasChapters) menuItems.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  menuItems.push_back({MenuAction::PAGE_CLEANUP, StrId::STR_PICTURE_CLEANUP});
  menuItems.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  for (size_t i = 0; i < menuItems.size() && i < MAX_MENU_ITEMS; i++) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[i].labelId);
    item.actionValue = static_cast<int16_t>(i);
    menuRowItems[i] = item;
  }
}

void XtcReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, 0, 0};
  setResult(std::move(result));
  finish();
}

bool XtcReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void XtcReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  app.clearTapFlash();
  nav.selected = index;

  const auto action = menuItems[index].action;
  if (action == MenuAction::PAGE_CLEANUP) {
    optionPopup.show(StrId::STR_PICTURE_CLEANUP, cleanupLabels.data(), static_cast<int>(cleanupLabels.size()),
                     cleanupIndex(), [this](int idx) {
                       SETTINGS.pictureCleanup = static_cast<uint8_t>(idx);
                       SETTINGS.saveToFile();
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  setResult(MenuResult{static_cast<int>(action), 0, 0});
  finish();
}

bool XtcReaderMenuActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool XtcReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }
  return false;
}

void XtcReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  const int percent = totalPages > 0 ? (currentPage * 100) / totalPages : 0;
  const std::string progressLine = std::to_string(currentPage) + " / " + std::to_string(totalPages) + "  ·  " +
                                   std::string(tr(STR_BOOK_PREFIX)) + std::to_string(percent) + "%";
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight));
  const int16_t pad = screen.theme().headerSidePadding;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), progressLine.c_str(), screen.theme().smallText);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (size_t i = 0; i < menuItems.size(); i++) {
    if (menuItems[i].action == MenuAction::PAGE_CLEANUP) {
      menuRowItems[i].value = I18N.get(cleanupLabels[cleanupIndex()]);
    }
  }

  fui::ListProps props;
  props.items = menuRowItems;
  props.count = static_cast<uint16_t>(menuItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void XtcReaderMenuActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());
}

void XtcReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  renderer.displayBuffer();
}
