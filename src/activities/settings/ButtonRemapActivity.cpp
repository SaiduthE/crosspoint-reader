#include "ButtonRemapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/FiveButtonInput.h"

namespace fui = freeink::ui;

namespace {
// UI steps correspond to logical roles in order: Back, Confirm, Left, Right
// (Back, Select, Up, Down on five-button boards).
constexpr uint8_t kRoleCount = 4;
// Marker used when a role has not been assigned yet.
constexpr uint8_t kUnassigned = 0xFF;
// Duration to show temporary error text when reassigning a button.
constexpr unsigned long kErrorDisplayMs = 1500;
// Five-button boards: holding Back this long cancels, holding Select resets.
constexpr unsigned long kEscapeHoldMs = 1000;
}  // namespace

void ButtonRemapActivity::onEnter() {
  Activity::onEnter();

  // Start with all roles unassigned to avoid duplicate blocking.
  currentStep = 0;
  tempMapping[0] = kUnassigned;
  tempMapping[1] = kUnassigned;
  tempMapping[2] = kUnassigned;
  tempMapping[3] = kUnassigned;
  errorMessage.clear();
  errorUntil = 0;
  armedButton = -1;
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    rowItems[i].label = getRoleName(i);
  }
  resetUi();
  app.setScreen(&ButtonRemapActivity::screenTrampoline, this);
  requestUpdate();
}

void ButtonRemapActivity::loop() {
  // Clear any temporary warning after its timeout.
  if (errorUntil > 0 && millis() > errorUntil) {
    errorMessage.clear();
    errorUntil = 0;
    requestUpdate();
    return;
  }

  if (five_button::active()) {
    loopFiveButton();
    return;
  }

  // Side buttons:
  // - Up: reset mapping to defaults and exit.
  // - Down: cancel without saving.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    // Persist default mapping immediately so the user can recover quickly.
    SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
    SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
    SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
    SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
    SETTINGS.saveToFile();
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    // Exit without changing settings.
    finish();
    return;
  }

  {
    // Make sure UI done rendering before accepting another assignment.
    // This avoids rapid double-presses that can advance the step without a visible redraw.
    RenderLock lock(*this);

    // Wait for a front button press to assign to the current role.
    const int pressedButton = mappedInput.getPressedFrontButton();
    if (pressedButton < 0) {
      return;
    }

    // Update temporary mapping and advance the remap step.
    // Only accept the press if this hardware button isn't already assigned elsewhere.
    if (!validateUnassigned(static_cast<uint8_t>(pressedButton))) {
      requestUpdate();
      return;
    }
    tempMapping[currentStep] = static_cast<uint8_t>(pressedButton);
    currentStep++;

    if (currentStep >= kRoleCount) {
      // All roles assigned; save to settings and exit.
      applyTempMapping();
      SETTINGS.saveToFile();
      finish();
      return;
    }

    requestUpdate();
  }
}

// Five-button boards have no spare keys: holds of the live Back/Select are the
// escapes, and keys assign on release so a hold never also assigns. After the
// fourth key the summary waits for the new Select before saving anything.
void ButtonRemapActivity::loopFiveButton() {
  // Holds fire while still down and swallow their own release.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Back, kEscapeHoldMs)) {
    // Exit without changing settings.
    finish();
    return;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, kEscapeHoldMs)) {
    SETTINGS.buttonMapBack = CrossPointSettings::FIVE_HW_BACK;
    SETTINGS.buttonMapConfirm = CrossPointSettings::FIVE_HW_CONFIRM;
    SETTINGS.buttonMapUp = CrossPointSettings::FIVE_HW_UP;
    SETTINGS.buttonMapDown = CrossPointSettings::FIVE_HW_DOWN;
    SETTINGS.saveToFile();
    finish();
    return;
  }

  // Same render gate as the side-button path.
  RenderLock lock(*this);

  // Only a key pressed on this screen counts, so a press carried in from
  // Settings can't assign on its release.
  const int pressedButton = mappedInput.getPressedFrontButton();
  if (pressedButton >= 0) {
    armedButton = static_cast<int8_t>(pressedButton);
  }
  const int releasedButton = mappedInput.getReleasedFrontButton();
  if (releasedButton < 0 || releasedButton != armedButton) {
    return;
  }
  armedButton = -1;
  const auto button = static_cast<uint8_t>(releasedButton);

  if (currentStep >= kRoleCount) {
    // Summary: only the newly chosen Select saves.
    if (button == tempMapping[1]) {
      applyTempMapping();
      SETTINGS.saveToFile();
      finish();
    }
    return;
  }

  if (!validateUnassigned(button)) {
    requestUpdate();
    return;
  }
  tempMapping[currentStep] = button;
  currentStep++;
  requestUpdate();
}

const char* ButtonRemapActivity::fiveButtonPrompt() const {
  switch (currentStep) {
    case 0:
      return tr(STR_REMAP_PRESS_FOR_BACK);
    case 1:
      return tr(STR_REMAP_PRESS_FOR_SELECT);
    case 2:
      return tr(STR_REMAP_PRESS_FOR_UP);
    case 3:
      return tr(STR_REMAP_PRESS_FOR_DOWN);
    default:
      return tr(STR_REMAP_PRESS_SELECT_TO_SAVE);
  }
}

void ButtonRemapActivity::render(RenderLock&&) {
  const bool fiveButton = five_button::active();
  // Only five-button boards wait on a summary step.
  const bool summary = fiveButton && currentStep >= kRoleCount;
  const auto labelForHardware = [&](uint8_t hardwareIndex) -> const char* {
    for (uint8_t i = 0; i < kRoleCount; i++) {
      if (tempMapping[i] == hardwareIndex) {
        // The summary marks the new Select key as the one that saves.
        return summary && i == 1 ? tr(STR_SAVE) : getRoleName(i);
      }
    }
    return "-";
  };

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_REMAP_FRONT_BUTTONS));
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    fiveButton ? fiveButtonPrompt() : tr(STR_REMAP_PROMPT));

  int topOffset = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  renderUi();

  // Temporary warning banner for duplicates.
  if (!errorMessage.empty()) {
    GUI.drawHelpText(renderer,
                     Rect{0, pageHeight - metrics.buttonHintsHeight - metrics.contentSidePadding - 15, pageWidth, 20},
                     errorMessage.c_str());
  }

  // Provide the reset/cancel escapes at the bottom of the screen (split across two lines):
  // side Up/Down, or held Select/Back on five-button boards.
  GUI.drawHelpText(renderer,
                   Rect{0, topOffset + 4 * metrics.listRowHeight + 4 * metrics.verticalSpacing, pageWidth, 20},
                   fiveButton ? tr(STR_REMAP_HOLD_RESET_HINT) : tr(STR_REMAP_RESET_HINT));
  GUI.drawHelpText(renderer,
                   Rect{0, topOffset + 4 * metrics.listRowHeight + 5 * metrics.verticalSpacing + 20, pageWidth, 20},
                   fiveButton ? tr(STR_REMAP_HOLD_CANCEL_HINT) : tr(STR_REMAP_CANCEL_HINT));

  // Live preview of logical labels under front buttons.
  // This mirrors the on-device front button order: Back, Confirm, Left, Right
  // (Back, Select, Up, Down on five-button boards).
  if (fiveButton) {
    GUI.drawButtonHints(renderer, labelForHardware(CrossPointSettings::FIVE_HW_BACK),
                        labelForHardware(CrossPointSettings::FIVE_HW_CONFIRM),
                        labelForHardware(CrossPointSettings::FIVE_HW_UP),
                        labelForHardware(CrossPointSettings::FIVE_HW_DOWN));
  } else {
    GUI.drawButtonHints(renderer, labelForHardware(CrossPointSettings::FRONT_HW_BACK),
                        labelForHardware(CrossPointSettings::FRONT_HW_CONFIRM),
                        labelForHardware(CrossPointSettings::FRONT_HW_LEFT),
                        labelForHardware(CrossPointSettings::FRONT_HW_RIGHT));
  }
  renderer.displayBuffer();
}

void ButtonRemapActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<ButtonRemapActivity*>(user)->buildScreen(screen);
}

void ButtonRemapActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int topOffset = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + topOffset),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width) + metrics.verticalSpacing),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.verticalSpacing),
                  static_cast<int16_t>(safe.x + metrics.verticalSpacing)});

  for (uint8_t i = 0; i < kRoleCount; ++i) {
    const uint8_t assignedButton = tempMapping[i];
    rowItems[i].value = assignedButton == kUnassigned ? tr(STR_UNASSIGNED) : getHardwareName(assignedButton);
  }

  fui::ListProps props;
  props.items = rowItems;
  props.count = kRoleCount;
  // No row is pending on the summary step.
  props.selectedIndex = currentStep < kRoleCount ? currentStep : -1;
  props.inputMask = fui::InputNone;
  props.scrollIndicator = false;
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  screen.list(props);
}

void ButtonRemapActivity::applyTempMapping() {
  // Commit temporary mapping into settings (logical role -> hardware).
  if (five_button::active()) {
    SETTINGS.buttonMapBack = tempMapping[0];
    SETTINGS.buttonMapConfirm = tempMapping[1];
    SETTINGS.buttonMapUp = tempMapping[2];
    SETTINGS.buttonMapDown = tempMapping[3];
    return;
  }
  SETTINGS.frontButtonBack = tempMapping[0];
  SETTINGS.frontButtonConfirm = tempMapping[1];
  SETTINGS.frontButtonLeft = tempMapping[2];
  SETTINGS.frontButtonRight = tempMapping[3];
}

bool ButtonRemapActivity::validateUnassigned(const uint8_t pressedButton) {
  // Block reusing a hardware button already assigned to another role.
  for (uint8_t i = 0; i < kRoleCount; i++) {
    if (tempMapping[i] == pressedButton && i != currentStep) {
      errorMessage = tr(STR_ALREADY_ASSIGNED);
      errorUntil = millis() + kErrorDisplayMs;
      return false;
    }
  }
  return true;
}

const char* ButtonRemapActivity::getRoleName(const uint8_t roleIndex) {
  const bool fiveButton = five_button::active();
  switch (roleIndex) {
    case 0:
      return tr(STR_BACK);
    case 1:
      return fiveButton ? tr(STR_SELECT) : tr(STR_CONFIRM);
    case 2:
      return fiveButton ? tr(STR_DIR_UP) : tr(STR_DIR_LEFT);
    case 3:
    default:
      return fiveButton ? tr(STR_DIR_DOWN) : tr(STR_DIR_RIGHT);
  }
}

const char* ButtonRemapActivity::getHardwareName(const uint8_t buttonIndex) const {
  switch (buttonIndex) {
    case CrossPointSettings::FRONT_HW_BACK:
      return tr(STR_HW_BACK_LABEL);
    case CrossPointSettings::FRONT_HW_CONFIRM:
      return five_button::active() ? tr(STR_HW_SELECT_LABEL) : tr(STR_HW_CONFIRM_LABEL);
    case CrossPointSettings::FRONT_HW_LEFT:
      return tr(STR_HW_LEFT_LABEL);
    case CrossPointSettings::FRONT_HW_RIGHT:
      return tr(STR_HW_RIGHT_LABEL);
    case CrossPointSettings::FIVE_HW_UP:
      return tr(STR_HW_UP_LABEL);
    case CrossPointSettings::FIVE_HW_DOWN:
      return tr(STR_HW_DOWN_LABEL);
    default:
      return "Unknown";
  }
}
