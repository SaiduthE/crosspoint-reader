#pragma once

#include <cstddef>
#include <string>

#include "activities/UiListActivity.h"
#include "activities/util/KeyboardEntryActivity.h"

// The one way screens ask for text. Five-button boards type either on the
// reader (KeyboardEntryActivity's linear walk) or on a phone
// (PhoneTextEntryActivity): this offers both, or goes straight to the method
// remembered in Settings > Controls > Text Entry. Every other board opens the
// device keyboard at once and never shows the choice.
//
// Same constructor and result contract as KeyboardEntryActivity:
// KeyboardResult{text}, or isCancelled.
class TextEntryActivity final : public UiListActivity {
 public:
  explicit TextEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                             std::string initialText = "", size_t maxLength = 0, InputType inputType = InputType::Text);

  void onEnter() override;

 private:
  static constexpr int ROW_DEVICE = 0;
  static constexpr int ROW_PHONE = 1;
  static constexpr int ROW_COUNT = 2;
  // Hold Select on a row: remember that method, then use it.
  static constexpr unsigned long REMEMBER_HOLD_MS = 1000;

  int listCount() const override { return ROW_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  void onBackButton() override { cancel(); }
  const char* headerTitle() const override { return title.c_str(); }

  void launch(bool phone);
  void onChildResult(const ActivityResult& result);
  void cancel();

  std::string title;
  std::string initialText;
  size_t maxLength;
  InputType inputType;
  // The chooser is on screen: a cancelled child comes back to it.
  bool choosing = false;

  freeink::ui::ListItem rowItems[ROW_COUNT]{};
};
