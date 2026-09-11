#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include "activities/UiListActivity.h"

class MappedInputManager;

/**
 * Activity for selecting UI language
 */
class LanguageSelectActivity final : public UiListActivity {
 public:
  explicit LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override { return totalItems; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Only languages with strings compiled in are offered; see gen_i18n.py keep-list.
  constexpr static uint8_t totalItems = SELECTABLE_LANGUAGE_COUNT;

  // Row storage: totalItems is a compile-time constant, so a fixed-capacity
  // array avoids any heap allocation for the row list. Built once in
  // onEnter() — activateIndex() finishes the activity immediately on
  // selection, so buildScreen() never needs to see a different "Selected"
  // row within one visit.
  freeink::ui::ListItem rowItems[totalItems]{};
};
