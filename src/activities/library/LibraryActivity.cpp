#include "LibraryActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "activities/UiTabListActivity.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

// Grid cell geometry (px). The cover box is the generated thumbnail size.
constexpr int16_t CELL_PAD_X = 8;
constexpr int16_t CELL_PAD_TOP = 6;
constexpr int16_t CELL_PAD_BOTTOM = 4;
constexpr int16_t LABEL_GAP = 4;
constexpr int16_t CELL_GAP = 12;
constexpr int16_t ROW_GAP = 6;
constexpr int16_t MIN_CELL_WIDTH = library::THUMB_WIDTH + 2 * CELL_PAD_X + 24;
constexpr uint8_t CARD_RADIUS = 14;
constexpr int COVER_RADIUS = 6;
constexpr int PLACEHOLDER_ICON_SIZE = 32;
constexpr unsigned long MISSING_BOOK_NOTICE_MS = 1500;

// Where the screen was left, restored on the next visit (process lifetime).
struct SavedPosition {
  int tab = 0;
  int selection[library::CATEGORY_COUNT] = {};
  bool onTabBand = false;
};
SavedPosition savedPosition;

}  // namespace

void LibraryActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.setScreen(&LibraryActivity::screenTrampoline, this);
  LOG_DBG("LIB", "Enter: free heap %u, max alloc %u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));

  activeTab = savedPosition.tab;
  std::copy(std::begin(savedPosition.selection), std::end(savedPosition.selection), std::begin(selection));
  onTabBand = savedPosition.onTabBand;
  lastInputMs = millis();

  if (!index.load()) {
    // First visit (or a damaged index): loop() scans behind a popup before
    // the first grid, so no empty frame flashes up.
    pendingScan = PendingScan::Initial;
    return;
  }
  clampSelections();
  // Cached grid first; the reconciliation walk starts once input is idle.
  silentScanPending = library::isStale();
  requestUpdate();
}

void LibraryActivity::onExit() {
  // Runs under the render lock (ActivityManager::exitActivity).
  savedPosition.tab = activeTab;
  std::copy(std::begin(selection), std::end(selection), std::begin(savedPosition.selection));
  savedPosition.onTabBand = onTabBand;
  optionPopup.dismiss();
  scanner.reset();  // an unfinished walk removes its staging files; the index stays stale
  Activity::onExit();
}

// --- loop task ---------------------------------------------------------------

void LibraryActivity::loop() {
  if (optionPopup.isActive()) {
    optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
    lastInputMs = millis();
    return;
  }
  if (pendingScan != PendingScan::None) {
    const StrId message = pendingScan == PendingScan::Initial ? StrId::STR_SCANNING : StrId::STR_LIBRARY_UPDATING;
    pendingScan = PendingScan::None;
    runBlockingScan(message);
    return;
  }
  if (handleInput()) {
    lastInputMs = millis();
    coverPopupShown = false;  // the next render drops the popup; redraw it when work resumes
    return;
  }
  if (millis() - lastInputMs < WORK_IDLE_MS) return;
  if (silentScanPending) {
    silentScanPending = false;
    startSilentScan();
    return;
  }
  if (scanner) {
    stepSilentScan();
    return;
  }
  processNextCover();
}

bool LibraryActivity::handleInput() {
  using Button = MappedInputManager::Button;
  const int count = index.count(category());
  const bool onGrid = !onTabBand && count > 0;
  const int selected = selection[activeTab];

  // Fires while held; ActivityManager then swallows the matching release.
  if (onGrid && mappedInput.wasLongPressed(Button::Confirm, LONG_PRESS_MS)) {
    openBookMenu();
    return true;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    onGoHome();
    return true;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    if (onGrid) {
      openSelected();
    } else {
      selectTab((activeTab + 1) % library::CATEGORY_COUNT);
    }
    return true;
  }
  if (mappedInput.wasPressed(Button::Left) || mappedInput.wasPressed(Button::Right)) {
    const int direction = mappedInput.wasPressed(Button::Left) ? -1 : 1;
    if (onGrid) {
      moveSelection(library::gridStepHorizontal(selected, count, direction));
    } else {
      selectTab((activeTab + direction + library::CATEGORY_COUNT) % library::CATEGORY_COUNT);
    }
    return true;
  }
  if (mappedInput.wasPressed(Button::Up) || mappedInput.wasPressed(Button::Down)) {
    const int direction = mappedInput.wasPressed(Button::Up) ? -1 : 1;
    if (onGrid) {
      moveSelection(library::gridStepVertical(selected, count, gridColumns.load(), direction));
    } else if (count > 0) {
      // Down returns to the remembered cell; Up wraps round to the last one.
      moveSelection(direction > 0 ? selected : count - 1);
    }
    return true;
  }

  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch.event.action == ACTION_TAB) {
    app.clearTapFlash();
    if (touch.event.value >= 0 && touch.event.value < library::CATEGORY_COUNT) selectTab(touch.event.value);
    return true;
  }
  if (touch.event.action == ACTION_CELL) {
    app.clearTapFlash();
    if (touch.event.value >= 0 && touch.event.value < count) {
      moveSelection(touch.event.value);
      openSelected();
    }
    return true;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    pageBy(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
    return true;
  }
  // A held Confirm may still turn into a long-press: keep background work off.
  return mappedInput.isPressed(Button::Confirm);
}

void LibraryActivity::moveSelection(const int target) {
  {
    RenderLock lock(*this);
    if (target == library::TAB_BAND) {
      onTabBand = true;
    } else {
      onTabBand = false;
      selection[activeTab] = target;
    }
  }
  requestUpdate();
}

void LibraryActivity::selectTab(const int tab) {
  {
    RenderLock lock(*this);
    activeTab = tab;
    onTabBand = true;
  }
  requestUpdate();
}

void LibraryActivity::pageBy(const int direction) {
  const int count = index.count(category());
  if (count == 0) return;
  const int pageItems = std::max(1, gridPageItems.load());
  const int current = selection[activeTab];
  const int target = std::clamp(current + direction * pageItems, 0, count - 1);
  if (library::gridPageTop(target, pageItems) == library::gridPageTop(current, pageItems)) return;
  moveSelection(target);
}

void LibraryActivity::openSelected() {
  library::IndexRecord record;
  const bool found = index.readRecord(category(), static_cast<uint16_t>(selection[activeTab]), record) &&
                     index.readPath(record, pathBuf, sizeof(pathBuf)) && Storage.exists(pathBuf);
  if (!found) {
    LOG_ERR("LIB", "Selected book is gone, rescanning");
    {
      RenderLock lock(*this);
      GUI.drawPopup(renderer, tr(STR_LIBRARY_BOOK_MISSING));
    }
    vTaskDelay(pdMS_TO_TICKS(MISSING_BOOK_NOTICE_MS));
    library::markStale();
    pendingScan = PendingScan::Rescan;
    return;
  }
  onSelectBook(std::string(pathBuf));
}

void LibraryActivity::openBookMenu() {
  library::IndexRecord record;
  if (!index.readRecord(category(), static_cast<uint16_t>(selection[activeTab]), record)) return;
  const char* options[] = {tr(STR_OPEN), tr(STR_LIBRARY_SHOW_IN_FILES), tr(STR_LIBRARY_RESCAN)};
  optionPopup.show(record.title[0] != '\0' ? record.title : tr(STR_LIBRARY), options,
                   static_cast<int>(sizeof(options) / sizeof(options[0])), 0,
                   [this](const int choice) { onMenuChoice(choice); });
  requestUpdate();
}

void LibraryActivity::onMenuChoice(const int choice) {
  switch (choice) {
    case 0:
      openSelected();
      break;
    case 1: {
      library::IndexRecord record;
      if (index.readRecord(category(), static_cast<uint16_t>(selection[activeTab]), record) &&
          index.readPath(record, pathBuf, sizeof(pathBuf))) {
        activityManager.goToFileBrowser(std::string(pathBuf));
      }
      break;
    }
    case 2:
      pendingScan = PendingScan::Rescan;
      break;
    default:
      break;
  }
}

void LibraryActivity::runBlockingScan(const StrId message) {
  scanner.reset();  // a silent walk in flight is superseded
  silentScanPending = false;
  library::LibraryScanner::Result outcome = library::LibraryScanner::Result::Failed;
  {
    // The popup and its progress bar draw straight into the framebuffer.
    RenderLock lock(*this);
    const auto& metrics = UITheme::getInstance().getMetrics();
    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   tr(STR_LIBRARY));
    const Rect popup = GUI.drawPopup(renderer, I18N.get(message));

    // Heap-allocated for the walk only; see LibraryScanner for its peak.
    auto walk = makeUniqueNoThrow<library::LibraryScanner>();
    if (!walk) {
      LOG_ERR("LIB", "OOM: scanner");
    } else if (walk->begin()) {
      int shown = 0;
      do {
        outcome = walk->step(BLOCKING_SCAN_BUDGET_MS);
        const int percent = walk->progressPercent();
        if (outcome == library::LibraryScanner::Result::Running && percent >= shown + 10) {
          GUI.fillPopupProgress(renderer, popup, percent);
          shown = percent;
        }
      } while (outcome == library::LibraryScanner::Result::Running);
    }
    walk.reset();
    scanFailed = outcome == library::LibraryScanner::Result::Failed;
    if (!scanFailed) library::clearStale();
    reloadIndex();
  }
  LOG_DBG("LIB", "Scan done: %u books, free heap %u", index.total(), static_cast<unsigned>(ESP.getFreeHeap()));
  requestUpdate();
}

void LibraryActivity::startSilentScan() {
  // Heap-allocated for the walk only; see LibraryScanner for its peak.
  auto walk = makeUniqueNoThrow<library::LibraryScanner>();
  if (!walk) {
    LOG_ERR("LIB", "OOM: scanner");
    return;
  }
  if (!walk->begin()) {
    LOG_ERR("LIB", "Library walk could not start");
    return;
  }
  scanner = std::move(walk);
}

void LibraryActivity::stepSilentScan() {
  auto outcome = library::LibraryScanner::Result::Running;
  {
    // Serialises the index swap in finalisation against render-task reads.
    RenderLock lock(*this);
    outcome = scanner->step(SILENT_SCAN_BUDGET_MS);
    if (outcome == library::LibraryScanner::Result::Running) return;
    scanner.reset();
    if (outcome != library::LibraryScanner::Result::Failed) library::clearStale();
    if (outcome == library::LibraryScanner::Result::Changed) reloadIndex();
  }
  if (outcome == library::LibraryScanner::Result::Changed) requestUpdate();
}

void LibraryActivity::reloadIndex() {
  index.load();
  clampSelections();
  windowDirty = true;
  coverTab = -1;  // re-check the visible page
}

void LibraryActivity::clampSelections() {
  for (int c = 0; c < library::CATEGORY_COUNT; c++) {
    const int count = index.count(static_cast<library::Category>(c));
    selection[c] = count == 0 ? 0 : std::clamp(selection[c], 0, count - 1);
  }
  if (activeTab < 0 || activeTab >= library::CATEGORY_COUNT) activeTab = 0;
  if (index.count(category()) == 0) onTabBand = true;
}

bool LibraryActivity::needsCoverWork(const library::IndexRecord& record) {
  if (record.pathLen == 0) return false;
  constexpr uint8_t done = library::RECORD_META_DONE | library::RECORD_THUMB_DONE;
  if ((record.flags & done) != done) return true;
  // A cleared book cache takes the thumbnail with it.
  return (record.flags & library::RECORD_THUMB_OK) != 0 && library::thumbPathFor(record, pathBuf, sizeof(pathBuf)) &&
         !Storage.exists(pathBuf);
}

void LibraryActivity::processNextCover() {
  if (!index.isValid()) return;
  const int count = index.count(category());
  if (count == 0) return;
  const int pageItems = std::max(1, gridPageItems.load());
  const int top = library::gridPageTop(std::min(selection[activeTab], count - 1), pageItems);
  if (activeTab != coverTab || top != coverTop) {
    coverTab = activeTab;
    coverTop = top;
    coverCursor = 0;
    coverPageDone = false;
    coverPageChanged = false;
    coverPopupShown = false;
  }
  if (coverPageDone) return;

  const int end = std::min(count, top + pageItems);
  library::IndexRecord record;
  while (top + coverCursor < end) {
    const auto itemIndex = static_cast<uint16_t>(top + coverCursor);
    if (!index.readRecord(category(), itemIndex, record) || !needsCoverWork(record)) {
      coverCursor++;
      continue;
    }
    if (ESP.getMaxAllocHeap() < MIN_COVER_MAX_ALLOC) {
      LOG_DBG("LIB", "Covers deferred: max alloc %u", static_cast<unsigned>(ESP.getMaxAllocHeap()));
      break;  // retried when the page changes or the screen is reopened
    }

    // One slow item per pass so navigation stays responsive between books.
    RenderLock lock(*this);
    if (!coverPopupShown) {
      coverPopupRect = GUI.drawPopup(renderer, tr(STR_LIBRARY_LOADING_COVERS));
      coverPopupShown = true;
    }
    if (enrichRecord(record) && index.writeRecord(category(), itemIndex, record)) {
      windowDirty = true;
      coverPageChanged = true;
    }
    coverCursor++;
    GUI.fillPopupProgress(renderer, coverPopupRect, coverCursor * 100 / std::max(1, end - top));
    LOG_DBG("LIB", "Cover %u done: free heap %u, max alloc %u", itemIndex, static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    return;
  }

  coverPageDone = true;
  if (coverPageChanged || coverPopupShown) {
    coverPageChanged = false;
    coverPopupShown = false;
    requestUpdate();
  }
}

bool LibraryActivity::enrichRecord(library::IndexRecord& record) {
  if (!index.readPath(record, pathBuf, sizeof(pathBuf))) return false;
  if (!Storage.exists(pathBuf)) {
    LOG_ERR("LIB", "Indexed book missing: %s", pathBuf);
    library::markStale();
    if (!scanner) silentScanPending = true;
    return false;
  }

  const auto format = static_cast<library::BookFormat>(record.format);
  bool thumbOk = false;
  if (format == library::BookFormat::Epub) {
    // Heap: the Epub object and its metadata cache live only for this item.
    auto epub = makeUniqueNoThrow<Epub>(std::string(pathBuf), library::BOOK_CACHE_DIR);
    if (!epub) {
      LOG_ERR("LIB", "OOM: Epub");
      return false;  // deferred, flags untouched
    }
    // Builds book.bin for books never opened; CSS is left for the reader.
    if (epub->load(true, true)) {
      if (!epub->getTitle().empty()) {
        record.titleLen =
            static_cast<uint8_t>(library::copyTruncatedUtf8(epub->getTitle(), record.title, library::TITLE_CAP));
      }
      record.authorLen =
          static_cast<uint8_t>(library::copyTruncatedUtf8(epub->getAuthor(), record.author, library::AUTHOR_CAP));
      thumbOk = epub->generateThumbBmp(library::THUMB_HEIGHT);
    }
  } else if (format == library::BookFormat::Xtc) {
    auto xtc = makeUniqueNoThrow<Xtc>(std::string(pathBuf), library::BOOK_CACHE_DIR);
    if (!xtc) {
      LOG_ERR("LIB", "OOM: Xtc");
      return false;
    }
    if (xtc->load()) {
      // generateThumbBmp buffers the whole cover page in one allocation.
      const uint32_t pageBytes = ((static_cast<uint32_t>(xtc->getPageWidth()) + 7) / 8) * xtc->getPageHeight() *
                                 (xtc->getBitDepth() == 2 ? 2 : 1);
      if (ESP.getMaxAllocHeap() < pageBytes + 8 * 1024) {
        LOG_DBG("LIB", "XTC cover deferred: needs %u", static_cast<unsigned>(pageBytes));
        return false;
      }
      const std::string title = xtc->getTitle();
      if (!title.empty()) {
        record.titleLen = static_cast<uint8_t>(library::copyTruncatedUtf8(title, record.title, library::TITLE_CAP));
      }
      record.authorLen =
          static_cast<uint8_t>(library::copyTruncatedUtf8(xtc->getAuthor(), record.author, library::AUTHOR_CAP));
      thumbOk = xtc->generateThumbBmp(library::THUMB_HEIGHT);
    }
  }

  record.flags |= library::RECORD_META_DONE | library::RECORD_THUMB_DONE;
  if (thumbOk) {
    record.flags |= library::RECORD_THUMB_OK;
  } else {
    record.flags &= static_cast<uint8_t>(~library::RECORD_THUMB_OK);
  }
  return true;
}

// --- render task ---------------------------------------------------------------

void LibraryActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_LIBRARY));
  renderUi();
  drawFooter();
  renderer.displayBuffer();
}

void LibraryActivity::drawFooter() {
  const bool onGrid = !onTabBand && index.count(category()) > 0;
  const auto labels =
      mappedInput.mapLabels(tr(STR_HOME), onGrid ? tr(STR_OPEN) : tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void LibraryActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<LibraryActivity*>(user)->buildScreen(screen);
}

fui::CoverGridItem LibraryActivity::gridItem(const uint16_t cell, void* user) {
  const auto* self = static_cast<const LibraryActivity*>(user);
  const int slot = static_cast<int>(cell) - self->windowFirst;
  const char* title = slot >= 0 && slot < self->windowCount ? self->window[slot].title : "";
  return fui::coverGridItem(title, cell);
}

bool LibraryActivity::coverPainter(fui::DrawTarget&, const fui::Rect rect, const fui::CoverGridItem&,
                                   const uint16_t cell, void* user) {
  static_cast<LibraryActivity*>(user)->drawCover(rect, cell);
  return true;
}

void LibraryActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  const int count = index.count(category());
  const char* labels[library::CATEGORY_COUNT] = {tr(STR_LIBRARY_BOOKS), tr(STR_LIBRARY_MANGA), tr(STR_LIBRARY_COMICS)};
  buildTabBand(screen, mappedInput, labels, library::CATEGORY_COUNT, activeTab, onTabBand || count == 0, ACTION_TAB);

  const auto side = static_cast<int16_t>(metrics.contentSidePadding);
  screen.insetContent(fui::Insets{0, side, 0, side});
  const fui::Rect body = screen.body();
  if (count == 0) {
    buildEmptyState(screen, body);
    return;
  }

  // Page geometry from the live fonts and bands: as many columns/rows as fit
  // (portrait RoundedRaff: 3 x 3). Titles get two lines unless dropping to one
  // buys another row (taller headers, e.g. Lyra).
  const int16_t titleLine = screen.target().lineHeight(fui::GfxRendererTarget::FONT_SMALL);
  const int columns = std::clamp((body.width + CELL_GAP) / (MIN_CELL_WIDTH + CELL_GAP), 1, MAX_WINDOW);
  const auto rowsFor = [&](const int titleLines) {
    const int height = CELL_PAD_TOP + library::THUMB_HEIGHT + LABEL_GAP + titleLine * titleLines + CELL_PAD_BOTTOM;
    return std::clamp((body.height + ROW_GAP) / (height + ROW_GAP), 1, MAX_WINDOW / columns);
  };
  const int titleLines = rowsFor(1) > rowsFor(2) ? 1 : 2;
  const int rows = rowsFor(titleLines);
  const auto labelHeight = static_cast<int16_t>(titleLine * titleLines);
  const auto rowHeight =
      static_cast<int16_t>(CELL_PAD_TOP + library::THUMB_HEIGHT + LABEL_GAP + labelHeight + CELL_PAD_BOTTOM);
  const int pageItems = columns * rows;
  gridColumns.store(columns);
  gridPageItems.store(pageItems);

  const int selected = std::clamp(selection[activeTab], 0, count - 1);
  const int top = library::gridPageTop(selected, pageItems);
  ensureWindow(activeTab, top, std::min(pageItems, count - top));
  paintedSelection = onTabBand ? -1 : selected;

  // Props (styles included) are built in place: this runs on the render task
  // stack, which also carries the cover decode below it.
  fui::CoverGridProps props;
  const auto& theme = screen.theme();
  const auto radius = static_cast<uint8_t>(std::min<int>(metrics.listRowRadius, CARD_RADIUS));
  fui::StyleSet& cells = props.cellStyles;
  cells.explicitlySet = true;
  cells.normal.foreground = fui::Paint::solid(fui::Color::Black);
  cells.normal.radius = radius;
  if (metrics.coverCardFill) cells.normal.background = fui::Paint::dither(fui::Color::LightGray);
  cells.selected = cells.normal;
  props.selectionIndicator = fui::CoverGridSelectionIndicator::Cell;
  coverBackground = metrics.coverCardFill ? Color::LightGray : Color::White;
  coverSelectedBackground = coverBackground;
  if (theme.listSelectionStyle == fui::SelectionStyle::InvertFill) {
    cells.selected.background = fui::Paint::solid(fui::Color::Black);
    cells.selected.foreground = fui::Paint::solid(fui::Color::White);
    coverSelectedBackground = Color::Black;
  } else if (theme.listSelectionStyle == fui::SelectionStyle::LightPill && !metrics.coverCardFill) {
    cells.selected.background = fui::Paint::dither(fui::Color::LightGray);
    coverSelectedBackground = Color::LightGray;
  } else {
    props.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  }
  // Touch press/flash states keep the selected look.
  cells.focused = cells.selected;
  cells.active = cells.selected;
  cells.disabled = cells.normal;

  props.titleText = theme.smallText;
  props.titleText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.titleText.bold = true;
  props.titleText.maxLines = static_cast<uint8_t>(titleLines);
  props.titleText.align = fui::TextAlign::Center;
  props.itemProvider = &LibraryActivity::gridItem;
  props.itemProviderUserData = this;
  props.count = static_cast<uint16_t>(count);
  props.topIndex = static_cast<uint16_t>(top);
  props.selectedIndex = static_cast<int16_t>(paintedSelection);
  props.action = ACTION_CELL;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.columns = static_cast<uint8_t>(columns);
  props.coverSize = fui::Size{library::THUMB_WIDTH, library::THUMB_HEIGHT};
  props.rowHeight = rowHeight;
  props.gap = CELL_GAP;
  props.rowGap = ROW_GAP;
  props.cellInset = fui::Insets{CELL_PAD_TOP, CELL_PAD_X, CELL_PAD_BOTTOM, CELL_PAD_X};
  props.labelHeight = labelHeight;
  props.labelGap = LABEL_GAP;
  props.scrollIndicatorWidth = static_cast<int16_t>(metrics.listScrollWidth);
  props.coverPainter = &LibraryActivity::coverPainter;
  props.coverPainterUserData = this;
  // Exactly `rows` rows tall, so coverGrid's own row count equals pageItems.
  fui::Rect gridRect = body;
  gridRect.height = static_cast<int16_t>(rows * rowHeight + (rows - 1) * ROW_GAP);
  fui::coverGrid(screen.frame(), gridRect, props);
}

void LibraryActivity::buildEmptyState(UiScreen& screen, const fui::Rect body) {
  const char* headline = nullptr;
  const char* hint = nullptr;
  char hintBuf[96];
  if (!index.isValid()) {
    if (!scanFailed) return;  // the first scan has not run yet
    headline = tr(STR_LIBRARY_SCAN_FAILED);
  } else if (index.total() == 0) {
    headline = tr(STR_LIBRARY_EMPTY);
    hint = tr(STR_LIBRARY_EMPTY_HINT);
  } else {
    headline = tr(STR_LIBRARY_CATEGORY_EMPTY);
    snprintf(hintBuf, sizeof(hintBuf), tr(STR_LIBRARY_FOLDER_HINT), library::categoryFolderName(category()));
    hint = hintBuf;
  }

  fui::TextStyle headlineStyle = screen.theme().bodyText;
  headlineStyle.font = fui::GfxRendererTarget::FONT_BODY;
  headlineStyle.bold = true;
  headlineStyle.maxLines = 2;
  headlineStyle.align = fui::TextAlign::Center;
  const int16_t bodyLine = screen.target().lineHeight(fui::GfxRendererTarget::FONT_BODY);
  const fui::Rect headlineRect{body.x, static_cast<int16_t>(body.y + body.height / 3), body.width,
                               static_cast<int16_t>(bodyLine * 2)};
  screen.target().text(headlineRect, headline, headlineStyle);

  if (hint) {
    fui::TextStyle hintStyle = screen.theme().smallText;
    hintStyle.font = fui::GfxRendererTarget::FONT_SMALL;
    hintStyle.maxLines = 3;
    hintStyle.align = fui::TextAlign::Center;
    const int16_t smallLine = screen.target().lineHeight(fui::GfxRendererTarget::FONT_SMALL);
    const fui::Rect hintRect{body.x, static_cast<int16_t>(headlineRect.bottom() + 8), body.width,
                             static_cast<int16_t>(smallLine * 3)};
    screen.target().text(hintRect, hint, hintStyle);
  }
}

void LibraryActivity::ensureWindow(const int tab, const int first, const int wanted) {
  if (!windowDirty && tab == windowTab && first == windowFirst && windowCount >= wanted) return;
  windowCount = index.readRecords(static_cast<library::Category>(tab), static_cast<uint16_t>(first),
                                  std::min(wanted, MAX_WINDOW), window);
  windowTab = tab;
  windowFirst = first;
  windowDirty = false;

  // One SD pass for any CJK titles on the page (see FileBrowserActivity).
  struct PrewarmCtx {
    const library::IndexRecord* records;
    int count;
  } ctx{window, windowCount};
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* c, const uint32_t i) -> const char* {
        const auto* p = static_cast<const PrewarmCtx*>(c);
        return static_cast<int>(i) < p->count ? p->records[i].title : nullptr;
      },
      &ctx, static_cast<uint32_t>(windowCount), EpdFontFamily::BOLD);
}

void LibraryActivity::drawCover(const fui::Rect rect, const uint16_t cell) {
  const int slot = static_cast<int>(cell) - windowFirst;
  const library::IndexRecord* record = slot >= 0 && slot < windowCount ? &window[slot] : nullptr;
  const Color background = static_cast<int>(cell) == paintedSelection ? coverSelectedBackground : coverBackground;

  // 1-bit thumbnails only ink their black pixels: lay down paper first.
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  bool drawn = false;
  if (record && (record->flags & library::RECORD_THUMB_OK) != 0 &&
      library::thumbPathFor(*record, thumbPathBuf, sizeof(thumbPathBuf))) {
    HalFile file;
    if (Storage.openFileForRead("LIB", thumbPathBuf, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        // Thumbnails are scale-to-fill: crop the overshooting axis to the box.
        const auto fit = library::fitCover(bitmap.getWidth(), bitmap.getHeight(), rect.width, rect.height);
        drawn = renderer.drawBitmap(bitmap, rect.x + fit.offsetX, rect.y + fit.offsetY, rect.width, rect.height,
                                    fit.cropX, fit.cropY);
      }
    }
  }
  if (!drawn) drawPlaceholder(rect, record);
  renderer.maskRoundedRectOutsideCorners(rect.x, rect.y, rect.width, rect.height, COVER_RADIUS, background);
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, COVER_RADIUS, true);
}

void LibraryActivity::drawPlaceholder(const fui::Rect rect, const library::IndexRecord* record) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawIcon(CoverIcon, rect.x + (rect.width - PLACEHOLDER_ICON_SIZE) / 2,
                    rect.y + rect.height / 3 - PLACEHOLDER_ICON_SIZE / 2, PLACEHOLDER_ICON_SIZE);
  if (!record || record->pathLen == 0) return;
  const char* label = library::formatLabel(static_cast<library::BookFormat>(record->format));
  const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
  renderer.drawText(SMALL_FONT_ID, rect.x + (rect.width - labelWidth) / 2,
                    rect.y + rect.height * 2 / 3 - renderer.getLineHeight(SMALL_FONT_ID) / 2, label, true);
}
