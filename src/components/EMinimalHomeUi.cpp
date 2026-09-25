#include "EMinimalHomeUi.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "UITheme.h"
#include "icons/blocks.h"
#include "icons/book.h"
#include "icons/folder.h"
#if FREEINK_DEVICE_EMINIMAL
#include "icons/homeIcons48.h"
#endif
#include "icons/library.h"
#include "icons/settings2.h"
#include "icons/transfer.h"
#include "util/BookProgress.h"

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId SELECT = 1;

// The layout's hand-placed sizes were tuned on the ~220 PPI 4" boards; px()
// grows them with the UI (UITheme::chromeScale, 1.5x on e-Minimal).
int16_t px(const int v) { return static_cast<int16_t>(UITheme::scaledPx(v)); }

// Button-cursor mark, shared by the grid ring and the featured card's bar: a
// solid black band FRAME_WIDTH thick whose outer edge sits FRAME_GAP outside
// the cover (strokes draw inward), leaving white between cover and ring so it
// reads against dark covers. Dithered gray washed out on the 7.8" panel.
int16_t selectionFrameGap() { return px(10); }
int16_t selectionFrameWidth() { return px(4); }

// The page's one side margin: the status line (battery), heading, featured
// cover, outer grid columns and outer tab icons all sit on it, so the page
// spans the panel width. Must exceed selectionFrameGap(), which the ring and
// the featured bar spill outside a cover by.
int16_t sideMargin() { return px(24); }

// Grid slots keep the thumbs' 3:5 (w:h), uncropped: four columns fill the
// panel width at that shape, so no slot needs widening.
constexpr int MAX_SLOT_ASPECT_W = 3;
constexpr int MAX_SLOT_ASPECT_H = 5;

// drawIcon() plots 1:1, so a larger UI takes a second raster, not a stretch.
// Tab order: files, library, OPDS, transfer, settings.
struct HomeIcons {
  const uint8_t* tabs[5];
  const uint8_t* book;
  int16_t size;
};
const HomeIcons& homeIcons() {
  static constexpr HomeIcons SMALL = {{FolderIcon, LibraryIcon, BlocksIcon, TransferIcon, Settings2Icon}, BookIcon, 32};
#if FREEINK_DEVICE_EMINIMAL
  static constexpr HomeIcons LARGE = {
      {FolderIcon48, LibraryIcon48, BlocksIcon48, TransferIcon48, Settings2Icon48}, BookIcon48, 48};
  if (UITheme::scaledPx(SMALL.size) >= LARGE.size) return LARGE;
#endif
  return SMALL;
}
}  // namespace

EMinimalHomeUi::EMinimalHomeUi(GfxRenderer& renderer)
    : UiAppHost(renderer), coverCache(renderer), renderer(renderer) {}

void EMinimalHomeUi::begin(const std::vector<RecentBook>& recent, bool opds, bool continuing) {
  books = &recent;
  hasOpds = opds;
  hasContinueReading = continuing;
  if (!recent.empty()) coverCache.begin();
  resetUi();
  app.on(SELECT, &EMinimalHomeUi::onAction, this);
  app.setScreen(&EMinimalHomeUi::screenFn, this);
  refreshCoverPaths();
  progress = hasContinueReading && !books->empty() ? loadBookProgress(books->front().path) : -1;
  if (progress >= 0) snprintf(progressText, sizeof(progressText), "%d%%", progress);
}

void EMinimalHomeUi::refreshCoverPaths() {
  coverCache.invalidate();
  for (size_t i = 0; i < books->size() && i < coverPaths.size(); ++i) refreshCoverPath(i);
}

void EMinimalHomeUi::refreshCoverPath(size_t index) {
  if (index >= books->size() || index >= coverPaths.size()) return;
  coverCache.invalidate(index);
  coverPaths[index] = thumbHeights[index] > 0
                          ? UITheme::getCoverThumbPath((*books)[index].coverBmpPath, thumbHeights[index])
                          : std::string();
  if (index != 0) return;
  coverCache.readSize(coverPaths[0], featuredCoverWidth, featuredCoverHeight);
}

int EMinimalHomeUi::thumbHeightFor(size_t index) const {
  return index < thumbHeights.size() && thumbHeights[index] > 0 ? thumbHeights[index] : THUMB_HEIGHT;
}

bool EMinimalHomeUi::takeThumbHeightsChanged() { return std::exchange(thumbHeightsChanged, false); }

void EMinimalHomeUi::noteThumbHeight(size_t index, int slotWidth, int slotHeight) {
  if (index >= thumbHeights.size()) return;
  // Thumbs cover a (0.6*h, h) target box, so a height of max(h, w*5/3) makes
  // every cover overfill the slot; the cover renderer crops the overflow (full bleed).
  const int height = std::max({1, slotHeight, slotWidth * 5 / 3 + 2});
  if (thumbHeights[index] != height) {
    thumbHeights[index] = height;
    thumbHeightsChanged = true;
    refreshCoverPath(index);
  }
}

void EMinimalHomeUi::onAction(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<EMinimalHomeUi*>(user);
  self.pending = event.value;
  self.app.clearTapFlash();
}

int EMinimalHomeUi::selectedAction(const MappedInputManager& input) {
  pending = -1;
  const auto touch = routeTouch(input);
  return touch.snap.touchReleased ? pending : -1;
}

void EMinimalHomeUi::screenFn(UiScreen& screen, void* user) { static_cast<EMinimalHomeUi*>(user)->draw(screen); }

void EMinimalHomeUi::draw(UiScreen& screen) {
  coverCache.prepare();
  const auto& theme = screen.theme();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y), static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  // Every band spans the same content width, one side margin in from the
  // panel edges (sideMargin). The selection ring and the featured bar spill
  // outside a cover into that margin by design — they read as a frame around
  // the cover, not as the column edge.
  const int16_t side = sideMargin();
  screen.insetContent(fui::Insets{theme.spaceSm, side, theme.spaceSm, side});
  const bool landscape = renderer.getScreenWidth() > renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  // A 0 px battery strip (RoundedRaff/e-Minimal on this device, see
  // UITheme::getMetrics) centres the glyph on the header's top edge; give the
  // home its own status band and let drawHeaderBand centre the glyph in it.
  const auto header = screen.takeTop(metrics.batteryBarHeight > 0 ? metrics.batteryBarHeight : 2 * metrics.topPadding);
  const auto tabRect = screen.takeBottom(UITheme::scaledPx(metrics.coverGridTabBarHeight), theme.spaceMd);
  if (books->empty()) {
    drawTabs(screen, tabRect);
    drawEmpty(screen);
    drawHeaderBand(header, tabRect.x, tabRect.right());
    return;
  }
  auto headingText = theme.titleText;
  headingText.bold = true;
  const auto headingRect = screen.takeTop(screen.target().lineHeight(headingText.font), theme.spaceSm);
  // Bound the featured section while leaving room for its metadata.
  const int16_t featuredHeight = std::min<int>(
      screen.body().height, std::max<int>(std::min<int>(px(240), screen.body().height * 3 / 10),
                                          screen.target().lineHeight(theme.bodyText.font) * (landscape ? 1 : 2) +
                                              screen.target().lineHeight(theme.smallText.font) * 2 + px(32)));
  drawCurrent(screen, screen.takeTop(featuredHeight, theme.spaceMd));
  drawGrid(screen);
  screen.target().text(headingRect, hasContinueReading ? tr(STR_CONTINUE_READING) : tr(STR_START_READING), headingText);
  drawTabs(screen, tabRect);
  // The status line's outer edges (clock left, battery right) land on the
  // same margin as the covers and tab icons.
  drawHeaderBand(header, tabRect.x, tabRect.right());
}

void EMinimalHomeUi::drawHeaderBand(fui::Rect header, int coverLeft, int coverRight) {
  // Same alignment trick as the tabs: the clock's left edge and the battery's
  // right edge sit on the outer cover columns. drawHeader anchors both at
  // headerStatusInset() from the band edges, and the clock text is
  // left-anchored, so 1- vs 2-digit hours never move it.
  const int inset = GUI.headerStatusInset();
  const int headerX = std::max(0, coverLeft - inset);
  const int headerRight = std::min<int>(renderer.getScreenWidth(), coverRight + inset);
  if (UITheme::getInstance().getMetrics().batteryBarHeight == 0) {
    // The status line centres on the rect's top edge: start the rect mid-band.
    const int16_t half = header.height / 2;
    header.y += half;
    header.height -= half;
  }
  GUI.drawHeader(renderer, Rect{headerX, header.y, headerRight - headerX, header.height}, nullptr);
}

void EMinimalHomeUi::drawEmpty(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto body = screen.body();
  auto title = theme.titleText;
  title.bold = true;
  title.align = fui::TextAlign::Center;
  auto message = theme.bodyText;
  message.align = fui::TextAlign::Center;
  const auto& icons = homeIcons();
  const int16_t ICON_SIZE = icons.size;
  const int16_t titleHeight = screen.target().lineHeight(title.font);
  const int16_t messageHeight = screen.target().lineHeight(message.font);
  const int16_t contentHeight = ICON_SIZE + theme.spaceLg + titleHeight + theme.spaceSm + messageHeight;
  int16_t y = body.y + std::max(0, (body.height - contentHeight) / 2);
  renderer.drawIcon(icons.book, body.x + (body.width - ICON_SIZE) / 2, y, ICON_SIZE);
  y += ICON_SIZE + theme.spaceLg;
  screen.target().text(fui::Rect{body.x, y, body.width, titleHeight}, tr(STR_NO_OPEN_BOOK), title);
  y += titleHeight + theme.spaceSm;
  screen.target().text(fui::Rect{body.x, y, body.width, messageHeight}, tr(STR_START_READING), message);
}

void EMinimalHomeUi::drawCurrent(UiScreen& screen, fui::Rect rect) {
  const auto& theme = screen.theme();
  const auto& book = books->front();
  card.title = book.title.c_str();
  card.author = book.author.empty() ? nullptr : book.author.c_str();
  card.meta = nullptr;
  card.progressLabel = progress >= 0 ? progressText : nullptr;
  card.centerTextOnCover = true;
  card.progress = std::max(0, progress);
  card.progressMax = progress >= 0 ? 100 : 0;
  card.action = SELECT;
  // The featured card's selected state is a black cursor bar drawn after the
  // card (see below), not a bookCard indicator: every ring/outline treatment
  // tried here either overwhelmed the large cover or made the heading above
  // read as misaligned.
  card.state = fui::StateNormal;
  card.styles = theme.listRow;
  card.styles.selected.background = fui::Paint::dither(fui::Color::LightGray);
  // The grid thumbs' selection ring draws with this border (see
  // selectionFrameGap): solid black, matching the featured card's bar.
  card.styles.selected.border = fui::Paint::solid(fui::Color::Black);
  card.styles.selected.foreground = fui::Paint::solid(fui::Color::Black);
  card.styles.selected.radius = theme.listRowRadius;
  card.styles.active = card.styles.selected;
  card.titleText = theme.bodyText;
  card.titleText.maxLines = renderer.getScreenWidth() > renderer.getScreenHeight() ? 1 : 2;
  card.authorText = theme.smallText;
  card.progressText = theme.smallText;
  card.progressHeight = px(6);
  // No side padding: the featured cover's left edge and the text's right edge
  // sit on the page margin, like the grid's outer columns.
  const int16_t pad = px(6);
  card.padding = fui::Insets{pad, 0, pad, 0};
  card.gap = theme.spaceLg + theme.spaceSm;
  card.coverSize.height = std::max(1, std::min(rect.height - 2 * pad, (rect.width / 3) * 5 / 3));
  card.coverSize.width = std::max(1, card.coverSize.height * 3 / 5);
  noteThumbHeight(0, card.coverSize.width, card.coverSize.height);
  // Laid out from the featured slot, not the displayed cover below: a wide or
  // square featured image must not shrink the grid through its size cap.
  gridBounds = layoutGrid(screen, screen.body());
  // Generation bounds stay stable; the displayed cover follows the actual image.
  if (featuredCoverWidth > 0 && featuredCoverHeight > 0) {
    const float scale = std::min(1.0f, std::min(float(card.coverSize.width) / featuredCoverWidth,
                                                float(card.coverSize.height) / featuredCoverHeight));
    card.coverSize.width = std::max(1, static_cast<int>(featuredCoverWidth * scale));
    card.coverSize.height = std::max(1, static_cast<int>(featuredCoverHeight * scale));
  }
  featuredCoverRect = fui::Rect{};
  card.coverPainterUserData = this;
  card.coverPainter = [](fui::DrawTarget& target, fui::Rect cover, const fui::BookCardProps&, void* user) {
    auto& self = *static_cast<EMinimalHomeUi*>(user);
    self.featuredCoverRect = cover;
    return self.paintFramedCover(target, cover, 0);
  };
  fui::bookCard(screen.frame(), rect, card);

  if (selected == 0 && !BoardConfig::hasTouch() && !featuredCoverRect.empty()) {
    // Button boards only: a vertical bar left of the cover marks the featured
    // card as the button cursor without framing the cover. It is exactly the
    // left side of the grid's selection ring (same black, width, offset and
    // span), so the cursor reads the same in both bands. Touch boards tap
    // directly and need no cursor on the hero card.
    const int16_t gap = selectionFrameGap();
    const auto& cover = featuredCoverRect;
    screen.target().fill(fui::Rect{static_cast<int16_t>(cover.x - gap), static_cast<int16_t>(cover.y - gap),
                                   selectionFrameWidth(), static_cast<int16_t>(cover.height + 2 * gap)},
                         fui::Paint::solid(fui::Color::Black));
  }
}

fui::Rect EMinimalHomeUi::layoutGrid(UiScreen& screen, fui::Rect rect) {
  const auto& theme = screen.theme();
  const int16_t frameGap = selectionFrameGap();
  grid.rowGap = static_cast<int16_t>(std::max<int>(theme.spaceSm, rect.width * 2 / 100));
  // Columns spread edge to edge (SpaceBetween in drawGrid): the outer covers
  // sit on the page margin with the heading, featured cover and tab icons,
  // and the gutters take whatever width the slots leave. grid.gap is only the
  // minimum gutter: the selection ring (frameGap outside the cover) plus as
  // much white again before the neighbouring cover.
  grid.gap = static_cast<int16_t>(2 * frameGap);
  // No side insets, so a cover's edge is its column edge (the ring spills
  // into the gutter or the page margin). The top inset keeps the ring inside
  // the cell; the title label closes the cell, so there is no bottom inset.
  grid.cellInset = fui::Insets{frameGap, 0, 0, 0};
  // One ellipsized title line under each thumb: the grid is height-bound, so
  // a second line would cost ~36 px of cover per row on the 7.8" panel. The
  // gap clears the selection ring below the cover.
  grid.titleText = theme.smallText;
  grid.titleText.maxLines = 1;
  grid.labelAlign = fui::TextAlign::Center;
  grid.labelFollowsCover = true;
  grid.labelGap = static_cast<int16_t>(frameGap + theme.spaceSm);
  grid.labelHeight =
      static_cast<int16_t>(screen.target().lineHeight(grid.titleText.font) * grid.titleText.maxLines);
  const int cellChromeHeight = frameGap + grid.labelGap + grid.labelHeight;
  // Covers take whatever height the featured card and tab bar leave (both
  // orientations are height-bound), so the grid runs down to the tab bar. The
  // featured-slot cap only stops the thumbs dwarfing the hero; at 3/2 it
  // clipped the 7.8" portrait thumbs ~12 px short of the tab bar.
  const int maxCoverWidth = std::max(1, (rect.width - (GRID_COLUMNS - 1) * grid.gap) / GRID_COLUMNS);
  const int maxCoverHeight =
      std::max(1, (rect.height - (GRID_ROWS - 1) * grid.rowGap) / GRID_ROWS - cellChromeHeight);
  grid.coverSize.height = std::max(1, std::min({maxCoverHeight, maxCoverWidth * 5 / 3, card.coverSize.height * 8 / 5}));
  // Never wider than MAX_SLOT_ASPECT; spare width goes to the gutters.
  grid.coverSize.width = std::max(
      1, std::min(maxCoverWidth, grid.coverSize.height * MAX_SLOT_ASPECT_W / MAX_SLOT_ASPECT_H));
  grid.rowHeight = static_cast<int16_t>(grid.coverSize.height + cellChromeHeight);
  // Full content width: SpaceBetween pins the outer columns to its edges.
  rect.height = GRID_ROWS * grid.rowHeight + (GRID_ROWS - 1) * grid.rowGap;
  return rect;
}

void EMinimalHomeUi::drawGrid(UiScreen& screen) {
  const auto rect = gridBounds;
  grid.count = books->size() > 1 ? books->size() - 1 : 0;
  grid.columns = GRID_COLUMNS;
  grid.columnLayout = fui::CoverGridColumnLayout::SpaceBetween;
  grid.action = SELECT;
  grid.inputMask = fui::InputTouch;
  grid.selectedIndex = selected > 0 && selected < static_cast<int>(books->size()) ? selected - 1 : -1;
  // A cover ring rather than the dithered Cell background, which was easy to
  // miss behind a dark cover. Solid black (card.styles.selected.border) with
  // white between it and the cover; see selectionFrameGap.
  grid.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  grid.selectedCoverFrameGap = selectionFrameGap();
  grid.selectedCoverFrameWidth = selectionFrameWidth();
  grid.cellStyles = card.styles;
  for (size_t i = 1; i < thumbHeights.size(); ++i) noteThumbHeight(i, grid.coverSize.width, grid.coverSize.height);
  grid.scrollIndicator = false;
  grid.itemProviderUserData = this;
  grid.itemProvider = [](uint16_t index, void* user) {
    const auto& self = *static_cast<EMinimalHomeUi*>(user);
    const auto& title = (*self.books)[index + 1].title;
    return fui::coverGridItem(title.empty() ? nullptr : title.c_str(), index + 1);
  };
  grid.coverPainterUserData = this;
  grid.coverPainter = [](fui::DrawTarget& target, fui::Rect cover, const fui::CoverGridItem&, uint16_t index,
                         void* user) {
    return static_cast<EMinimalHomeUi*>(user)->paintFramedCover(target, cover, index + 1);
  };
  fui::coverGrid(screen.frame(), rect, grid);
}

void EMinimalHomeUi::drawTabs(UiScreen& screen, fui::Rect rect) {
  int count = 0;
  for (int i = 0; i < 5; ++i) {
    if (i == 2 && !hasOpds) continue;
    auto& tab = tabItems[count];
    tab.value = books->size() + count;
    tab.selected = selected == tab.value;
    tab.label = nullptr;
    ++count;
  }
  tabs.tabs = tabItems.data();
  tabs.count = count;
  tabs.layout = fui::TabBarLayout::SpaceBetween;
  tabs.action = SELECT;
  tabs.inputMask = fui::InputTouch;
  tabs.iconSize = homeIcons().size;
  tabs.iconPainterUserData = this;
  tabs.iconPainter = [](fui::DrawTarget&, fui::Rect iconRect, const fui::TabItem& tab, uint8_t, void* user) {
    const auto& self = *static_cast<EMinimalHomeUi*>(user);
    const int index = tab.value - static_cast<int>(self.books->size());
    const int icon = !self.hasOpds && index >= 2 ? index + 1 : index;
    self.renderer.drawIcon(homeIcons().tabs[icon], iconRect.x, iconRect.y, iconRect.width, !tab.selected);
    return true;
  };
  // Selected tab is a black pill with a white icon, matching the Settings tab
  // pills and RoundedRaff's selected list rows (UiTabListActivity.cpp).
  tabs.tabStyles.explicitlySet = true;
  tabs.tabStyles.normal.background = fui::Paint::solid(fui::Color::White);
  tabs.tabStyles.selected.background = fui::Paint::solid(fui::Color::Black);
  tabs.tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
  tabs.tabStyles.selected.radius = screen.theme().listRowRadius;
  tabs.tabStyles.focused = tabs.tabStyles.selected;
  tabs.tabStyles.active = tabs.tabStyles.selected;
  tabs.selectedUnderline = 0;
  // Pill insets: taller than the icon (top/bottom) so the fill reads as a
  // pill, not a square; left/right stay at the component default (4) because
  // distributedSlotWidth and the overhang maths below derive from them.
  tabs.tabInset = fui::Insets{px(12), 4, px(12), 4};
  tabs.contentInset = fui::Insets{0, 0, 0, 0};
  // SpaceBetween pins the outer SLOTS to the bar's edges and centres each
  // icon in its slot. Fix the slot width (the widest-natural-tab width the
  // component would pick: a square pill twice the icon, plus the pill insets)
  // and widen the bar by the slot's side overhang, so the outer ICONS land
  // on the page margin like the covers above them.
  const int16_t iconSize = tabs.iconSize;
  tabs.distributedSlotWidth = static_cast<int16_t>(2 * iconSize + tabs.tabInset.left + tabs.tabInset.right);
  const int16_t overhang = static_cast<int16_t>((tabs.distributedSlotWidth - iconSize) / 2);
  rect.x = static_cast<int16_t>(rect.x - overhang);
  rect.width = static_cast<int16_t>(rect.width + 2 * overhang);
  fui::tabBar(screen.frame(), rect, tabs);
}

bool EMinimalHomeUi::paintFramedCover(fui::DrawTarget& target, fui::Rect rect, size_t index) {
  const int16_t SHADOW_OFFSET = px(2);
  const auto ink = fui::Paint::solid(fui::Color::Black);
  target.fill(fui::Rect{rect.right(), static_cast<int16_t>(rect.y + SHADOW_OFFSET), SHADOW_OFFSET, rect.height}, ink);
  target.fill(fui::Rect{static_cast<int16_t>(rect.x + SHADOW_OFFSET), rect.bottom(), rect.width, SHADOW_OFFSET}, ink);
  const bool drawn = index < coverPaths.size() && coverCache.paint(rect, index, coverPaths[index]);
  target.stroke(rect, ink, px(1), 0);
  return drawn;
}
