#pragma once
#include <I18n.h>
#include <LibraryIndex.h>
#include <LibraryScanner.h>

#include <atomic>
#include <memory>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/UiAppHost.h"

// Every book on the SD card as a cover grid, split into Books / Manga / Comics
// tabs by folder name (lib/LibraryIndex). The grid pages through
// /.crosspoint/library/index.bin one screenful at a time; covers and metadata
// titles are generated lazily for the visible page while the user is idle.
//
// Not a UiTabListActivity: its ring navigation and ListNav viewport are
// one-dimensional, while the grid needs row/column moves and page-aligned
// windows. It shares the tab band chrome (buildTabBand) and draws the cells
// with fui::coverGrid.
class LibraryActivity final : public Activity, private UiAppHost {
 public:
  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Library", renderer, mappedInput), UiAppHost(renderer) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Page window cap: the grid never shows more than this many cells.
  static constexpr int MAX_WINDOW = 12;
  static constexpr unsigned long LONG_PRESS_MS = 800;
  // Input quiet time before background work (scan steps, covers) resumes.
  static constexpr unsigned long WORK_IDLE_MS = 300;
  static constexpr uint32_t SILENT_SCAN_BUDGET_MS = 40;
  static constexpr uint32_t BLOCKING_SCAN_BUDGET_MS = 250;
  // Cover generation inflates the EPUB cover image; below this contiguous
  // block it is deferred, not marked failed.
  static constexpr uint32_t MIN_COVER_MAX_ALLOC = 48 * 1024;
  static constexpr freeink::ui::ActionId ACTION_TAB = 1;
  static constexpr freeink::ui::ActionId ACTION_CELL = 2;

  enum class PendingScan : uint8_t { None, Initial, Rescan };

  static void screenTrampoline(UiScreen& screen, void* user);
  static freeink::ui::CoverGridItem gridItem(uint16_t cell, void* user);
  static bool coverPainter(freeink::ui::DrawTarget& target, freeink::ui::Rect rect,
                           const freeink::ui::CoverGridItem& item, uint16_t cell, void* user);

  // --- render task ---
  void buildScreen(UiScreen& screen);
  void buildEmptyState(UiScreen& screen, freeink::ui::Rect body);
  void ensureWindow(int tab, int first, int wanted);
  void drawCover(freeink::ui::Rect rect, uint16_t cell);
  void drawPlaceholder(freeink::ui::Rect rect, const library::IndexRecord* record) const;
  void drawFooter();

  // --- loop task ---
  library::Category category() const { return static_cast<library::Category>(activeTab); }
  bool handleInput();
  void moveSelection(int target);
  void selectTab(int tab);
  void pageBy(int direction);
  void openSelected();
  void openBookMenu();
  void onMenuChoice(int choice);
  void runBlockingScan(StrId message);
  void startSilentScan();
  void stepSilentScan();
  void processNextCover();
  bool needsCoverWork(const library::IndexRecord& record);
  bool enrichRecord(library::IndexRecord& record);
  void reloadIndex();
  void clampSelections();

  // Navigation state: written by the loop task under RenderLock, read by render.
  library::LibraryIndex index;
  int activeTab = 0;
  int selection[library::CATEGORY_COUNT] = {};
  bool onTabBand = false;
  bool scanFailed = false;

  // Grid geometry measured by the last build, read by loop-task navigation.
  std::atomic<int> gridColumns{3};
  std::atomic<int> gridPageItems{9};

  // Render task: the records of the visible page and per-build paint state.
  library::IndexRecord window[MAX_WINDOW];
  int windowTab = -1;
  int windowFirst = 0;
  int windowCount = 0;
  bool windowDirty = true;
  int paintedSelection = -1;
  Color coverBackground = Color::White;
  Color coverSelectedBackground = Color::White;
  char thumbPathBuf[64] = {};

  // Loop task: background work.
  std::unique_ptr<library::LibraryScanner> scanner;  // silent reconciliation walk
  PendingScan pendingScan = PendingScan::None;
  bool silentScanPending = false;
  int coverTab = -1;
  int coverTop = -1;
  int coverCursor = 0;
  bool coverPageDone = true;
  bool coverPageChanged = false;
  bool coverPopupShown = false;
  Rect coverPopupRect;
  unsigned long lastInputMs = 0;
  char pathBuf[library::MAX_PATH_LEN + 1] = {};

  OptionPopup optionPopup;
};
