#include "TextEntryActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <memory>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "PhoneTextEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/FiveButtonInput.h"

namespace fui = freeink::ui;

TextEntryActivity::TextEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                     std::string initialText, const size_t maxLength, const InputType inputType)
    : UiListActivity("TextEntry", renderer, mappedInput),
      title(std::move(title)),
      initialText(std::move(initialText)),
      maxLength(maxLength),
      inputType(inputType) {
  // Static rows, built once (see NetworkModeSelectionActivity).
  rowItems[ROW_DEVICE].label = tr(STR_TYPE_ON_READER);
  rowItems[ROW_DEVICE].subtitle = tr(STR_TYPE_ON_READER_SUB);
  rowItems[ROW_DEVICE].icon = listIconFor(UIIcon::Text, 32);
  rowItems[ROW_DEVICE].actionValue = static_cast<int16_t>(ROW_DEVICE);
  rowItems[ROW_PHONE].label = tr(STR_TYPE_ON_PHONE);
  rowItems[ROW_PHONE].subtitle = tr(STR_TYPE_ON_PHONE_SUB);
  rowItems[ROW_PHONE].icon = listIconFor(UIIcon::Wifi, 32);
  rowItems[ROW_PHONE].actionValue = static_cast<int16_t>(ROW_PHONE);
}

void TextEntryActivity::onEnter() {
  UiListActivity::onEnter();
  // Only five-button boards choose; every other board can drive its keyboard.
  const int method =
      five_button::active() ? SETTINGS.textEntryMethod : static_cast<int>(CrossPointSettings::TEXT_ENTRY_DEVICE);
  choosing = method != CrossPointSettings::TEXT_ENTRY_DEVICE && method != CrossPointSettings::TEXT_ENTRY_PHONE;
  // The push is processed before this screen's first render, so a remembered
  // method never paints the chooser.
  if (!choosing) launch(method == CrossPointSettings::TEXT_ENTRY_PHONE);
}

void TextEntryActivity::launch(const bool phone) {
  app.clearTapFlash();
  std::unique_ptr<Activity> child;
  if (phone) {
    child = makeUniqueNoThrow<PhoneTextEntryActivity>(renderer, mappedInput, title, initialText, maxLength, inputType);
  } else {
    child = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, title, initialText, maxLength, inputType);
  }
  if (!child) {
    LOG_ERR("TXT", "OOM: text entry (%s)", phone ? "phone" : "keyboard");
    cancel();
    return;
  }
  startActivityForResult(std::move(child), [this](const ActivityResult& result) { onChildResult(result); });
}

void TextEntryActivity::onChildResult(const ActivityResult& result) {
  if (result.isCancelled && choosing) {
    // Back from either method returns to the choice, not to the caller.
    requestUpdate();
    return;
  }
  ActivityResult forwarded = result;
  setResult(std::move(forwarded));
  finish();
}

void TextEntryActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void TextEntryActivity::activateIndex(const int index) {
  nav.selected = index;
  launch(index == ROW_PHONE);
}

bool TextEntryActivity::handleButtons() {
  // Polled before the base's Confirm release: a fired hold swallows it.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, REMEMBER_HOLD_MS)) {
    const int selected = nav.selected;
    if (selected < 0 || selected >= ROW_COUNT) return true;
    const bool phone = selected == ROW_PHONE;
    SETTINGS.textEntryMethod = static_cast<uint8_t>(phone ? CrossPointSettings::TEXT_ENTRY_PHONE
                                                          : CrossPointSettings::TEXT_ENTRY_DEVICE);
    if (!SETTINGS.saveToFile()) LOG_ERR("TXT", "Could not save the text entry method");
    LOG_INF("TXT", "Text entry remembered: %s", phone ? "phone" : "reader");
    launch(phone);
    return true;
  }
  return UiListActivity::handleButtons();
}

void TextEntryActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  // How to make the choice stick, at the foot of the band.
  fui::TextStyle help = screen.theme().smallText;
  help.align = fui::TextAlign::Center;
  help.maxLines = 2;
  const int16_t pad = screen.theme().headerSidePadding;
  const fui::Rect helpBand = screen.takeBottom(static_cast<int16_t>(screen.target().lineHeight(help.font) * 2),
                                               static_cast<int16_t>(metrics.verticalSpacing));
  screen.target().text(helpBand.inset(fui::Insets{0, pad, 0, pad}), tr(STR_TEXT_ENTRY_REMEMBER), help);

  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(ROW_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
