#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// The reader menu for XTC/XTCH books (manga, comics), opened with Confirm like
// the EPUB one. Pages are pictures, so there are no text settings; what can
// change is how pages refresh. Settings are written as they are picked; the
// reader redraws the page when the menu closes.
class XtcReaderMenuActivity final : public UiListActivity {
 public:
  enum class MenuAction { SELECT_CHAPTER, PAGE_CLEANUP, GO_HOME };

  explicit XtcReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                 int currentPage, int totalPages, bool hasChapters);

  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static constexpr size_t MAX_MENU_ITEMS = 8;
  freeink::ui::ListItem menuRowItems[MAX_MENU_ITEMS]{};
  std::vector<MenuItem> menuItems;

  int listCount() const override { return static_cast<int>(menuItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void drawChrome() override;
  void closeCancelled();

  OptionPopup optionPopup;
  std::string title;
  int currentPage = 0;
  int totalPages = 0;
  const std::vector<StrId> cleanupLabels = {StrId::STR_STATE_OFF, StrId::STR_CLEANUP_DOUBLE,
                                            StrId::STR_CLEANUP_WHITE_FLASH};
  // A value stored by the dropped GLR16/GLD16 options shows as Double refresh.
  static uint8_t cleanupIndex() {
    const uint8_t v = SETTINGS.pictureCleanup;
    return v < CrossPointSettings::PICTURE_CLEANUP_COUNT ? v : CrossPointSettings::PICTURE_CLEANUP_DOUBLE;
  }
};
