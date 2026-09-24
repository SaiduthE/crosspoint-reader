#pragma once

#include <array>
#include <string>
#include <vector>

#include "HomeCoverCache.h"
#include "RecentBooksStore.h"
#include "UiAppHost.h"
#include "components/bars/tab-bar.h"
#include "components/media/book-card.h"
#include "components/media/cover-grid.h"

// e-Minimal's home page: upstream's Cover Grid home (CoverGridHomeUi, left as
// upstream ships it) redone for the 7.8" panel and a four-button board --
// 1.5x chrome and 48 px icons, full panel width, titles under the thumbs, a
// solid selection ring. Used only by the e-Minimal theme (HomeActivity).
class EMinimalHomeUi final : public UiAppHost {
 public:
  static constexpr int THUMB_HEIGHT = 400;
  static constexpr int GRID_COLUMNS = 4;
  static constexpr int GRID_ROWS = 2;
  static constexpr int MAX_BOOKS = 1 + GRID_COLUMNS * GRID_ROWS;
#if FREEINK_DEVICE_EMINIMAL  // the only device that runs this home (HomeActivity)
  static_assert(MAX_BOOKS <= HomeCoverCache::MAX_COVERS);
#endif
  explicit EMinimalHomeUi(GfxRenderer& renderer);
  void begin(const std::vector<RecentBook>& books, bool hasOpds, bool hasContinueReading);
  void refreshCoverPaths();
  void setSelection(int selection) { selected = selection; }
  int selectedAction(const MappedInputManager& input);
  // Exact generation height for a slot, recorded during draw. Thumbs must be
  // generated at the drawn size: rescaling a dithered 1-bit image aliases badly.
  int thumbHeightFor(size_t index) const;
  bool takeThumbHeightsChanged();

 private:
  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  void draw(UiScreen& screen);
  void drawHeaderBand(freeink::ui::Rect header, int coverLeft, int coverRight);
  void drawEmpty(UiScreen& screen);
  void drawCurrent(UiScreen& screen, freeink::ui::Rect rect);
  void drawGrid(UiScreen& screen);
  freeink::ui::Rect layoutGrid(UiScreen& screen, freeink::ui::Rect rect);
  void drawTabs(UiScreen& screen, freeink::ui::Rect rect);
  bool paintFramedCover(freeink::ui::DrawTarget& target, freeink::ui::Rect rect, size_t index);
  void refreshCoverPath(size_t index);
  void noteThumbHeight(size_t index, int slotWidth, int slotHeight);

  HomeCoverCache coverCache;
  GfxRenderer& renderer;
  const std::vector<RecentBook>* books = nullptr;
  std::array<std::string, MAX_BOOKS> coverPaths;
  int featuredCoverWidth = 0;
  int featuredCoverHeight = 0;
  std::array<int, MAX_BOOKS> thumbHeights{};
  bool thumbHeightsChanged = false;
  int selected = 0;
  int pending = -1;
  int progress = -1;
  bool hasOpds = false;
  bool hasContinueReading = false;
  char progressText[12]{};
  // Component styles and interaction tables stay off the render task's stack.
  freeink::ui::BookCardProps card;
  freeink::ui::CoverGridProps grid;
  freeink::ui::Rect gridBounds{};
  // Where bookCard painted the featured cover this frame (anchors its cursor bar).
  freeink::ui::Rect featuredCoverRect{};
  freeink::ui::TabBarProps tabs;
  std::array<freeink::ui::TabItem, 5> tabItems;
};
