#pragma once
#include <Epub.h>

#include <memory>
#include <string>

#include "activities/UiListActivity.h"

class EpubReaderChapterSelectionActivity final : public UiListActivity {
  std::shared_ptr<Epub> epub;
  int currentSpineIndex = 0;

  // Windowed row buffers: TOC entries are SD-backed (BookMetadataCache LUT
  // reads), so only the rows around the viewport are materialized. A
  // several-hundred-entry TOC (547 in a large collection) built up front cost
  // ~60KB of labels + ListItems — starving the CJK glyph arena into
  // SD-per-repaint — for rows that were never drawn. The window follows
  // nav.top via itemsWindowFirst (see fui::ListProps); refreshing it also
  // batch-prewarms the window's fallback glyphs, so each page of the list
  // pays one bounded SD pass and repaints stay RAM-only.
  //
  // The window must hold every row list() lays out from nav.top plus one
  // (the partial-row preview): nav.visibleRows + 1. On the 7.8" panel at
  // 1.5x, Lyra's 60 px rows fit ~28 in the band -- the old 24 read past the
  // array into the members below it, and the garbage ListItem's value /
  // sectionHeading pointers panicked getTextWidth (LoadProhibited on
  // entering the chapter list, 2026-09-20/21). 40 covers the tallest band
  // with the densest theme; buildScreen also hands list() the window count
  // so a short window can only show blank rows, never read past the array.
  static constexpr int TOC_WINDOW = 40;
  std::string windowLabels[TOC_WINDOW];
  freeink::ui::ListItem windowItems[TOC_WINDOW];
  int windowStart = -1;
  int windowCount = 0;
  void refreshTocWindow(int start);

  // Total TOC items count
  int listCount() const override { return epub ? epub->getTocItemsCount() : 0; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Back cancels with a result and Confirm activates on RELEASE here, and a
  // missing epub swallows everything past Back.
  bool handleButtons() override;
  // Header is drawn inside the safe area (not full-width like the base).
  void drawChrome() override;

 public:
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, int currentSpineIndex);
  void onEnter() override;
};
