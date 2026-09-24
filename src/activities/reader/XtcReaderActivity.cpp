#include "XtcReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "ProgressFile.h"
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "XtcReaderChapterSelectionActivity.h"
#include "XtcReaderMenuActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Four XTH pixels (a plane-1 nibble and a plane-2 nibble, index a << 4 | b,
// MSB = first pixel) -> two 4bpp bytes, first pixel in the high nibble, as a
// little-endian uint16 (byte 0 = pixels 0-1). Value (bit1 << 1) | bit2 is
// 0 white, 1 dark, 2 light, 3 black -> nibble 0xF, 0x5, 0xA, 0x0, the four
// levels the driver's gray4() gives the base/LSB/MSB planes. Not monotonic in
// v (dark is 1, light 2), so a table, not 15 - 5 * v -- that swapped the two
// greys and left manga shading dull and noisy.
struct XthGray4Table {
  uint16_t v[256];
  constexpr XthGray4Table() : v() {
    constexpr unsigned kNibble[4] = {0xF, 0x5, 0xA, 0x0};
    for (unsigned i = 0; i < 256; i++) {
      const unsigned a = i >> 4, b = i & 0x0F;
      unsigned nib[4] = {0, 0, 0, 0};
      for (unsigned k = 0; k < 4; k++) {
        const unsigned pv = (((a >> (3 - k)) & 1u) << 1) | ((b >> (3 - k)) & 1u);
        nib[k] = kNibble[pv];
      }
      const unsigned byte0 = (nib[0] << 4) | nib[1];
      const unsigned byte1 = (nib[2] << 4) | nib[3];
      v[i] = static_cast<uint16_t>(byte0 | (byte1 << 8));
    }
  }
};
constexpr XthGray4Table kXthGray4;
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "XthGray4Table packs output bytes little-endian");
// First pixel alone, the rest white: plane 2 only = dark, plane 1 only = light
// (the plane path's LSB = ~a & b, MSB = a ^ b), both = black.
static_assert(kXthGray4.v[0x08] == 0xFF5F, "XTH value 1 must be dark grey (0x5)");
static_assert(kXthGray4.v[0x80] == 0xFFAF, "XTH value 2 must be light grey (0xA)");
static_assert(kXthGray4.v[0x88] == 0xFF0F, "XTH value 3 must be black");
static_assert(kXthGray4.v[0x00] == 0xFFFF, "XTH value 0 must be white");

// A panel-sized XTH page expanded one physical row at a time into the rows
// GfxRenderer::displayGray4Rows() sends. Plane byte i is frame-buffer byte i
// (8 physical pixels, MSB first), so row y is plane bytes y * rowBytes ..
// + rowBytes - 1 and each byte's 8 pixels are 4bpp bytes 4i..4i+3 of `dst`
// (left pixel in the high nibble). The same pass leaves the B/W base in the
// frame buffer -- what the plane path leaves there for whatever draws over the
// page next -- and weighs the ink for the dark-page test. Runs on the reader's
// task, a row at a time, while the rows before it are on the wire: no
// blocking, no allocation.
struct XthGray4Rows {
  const uint8_t* plane1;
  const uint8_t* plane2;
  uint8_t* fb;
  size_t rowBytes;
  uint64_t ink;  // black 3, dark 2, light 1 a pixel, summed over the page
};

void fillXthGray4Row(void* ctx, const uint16_t row, uint8_t* dst) {
  auto& rows = *static_cast<XthGray4Rows*>(ctx);
  const size_t start = static_cast<size_t>(row) * rows.rowBytes;
  const uint8_t* const p1 = rows.plane1 + start;
  const uint8_t* const p2 = rows.plane2 + start;
  uint8_t* const fb = rows.fb + start;
  // The driver's send rows are word-aligned DMA buffers; one store a plane byte.
  uint32_t* const out = reinterpret_cast<uint32_t*>(dst);
  uint32_t ink = 0;  // at most 8 * 3 a byte, so a row's sum fits easily
  for (size_t i = 0, n = rows.rowBytes; i < n; i++) {
    const uint8_t a = p1[i], b = p2[i];
    fb[i] = static_cast<uint8_t>(~(a | b));
    // black 3, dark 2, light 1 == popcount(a) + 2 * popcount(b)
    ink += __builtin_popcount(a) + 2 * __builtin_popcount(b);
    out[i] = static_cast<uint32_t>(kXthGray4.v[(a & 0xF0) | (b >> 4)]) |
             (static_cast<uint32_t>(kXthGray4.v[((a & 0x0F) << 4) | (b & 0x0F)]) << 16);
  }
  rows.ink += ink;
}
}  // namespace

bool XtcReaderActivity::loadBook() {
  auto loadedXtc = makeUniqueNoThrow<Xtc>(bookPath, "/.crosspoint");
  if (!loadedXtc) {
    LOG_ERR("XTR", "Failed to allocate XTC object");
    return false;
  }
  if (!loadedXtc->load()) {
    LOG_ERR("XTR", "Failed to load XTC");
    return false;
  }
  xtc = std::move(loadedXtc);
  xtc->setupCacheDir();
  loadProgress();
  return true;
}

void XtcReaderActivity::openMenu() {
  const bool hasChapters = xtc->hasChapters() && !xtc->getChapters().empty();
  startActivityForResult(
      std::make_unique<XtcReaderMenuActivity>(renderer, mappedInput, xtc->getTitle(), static_cast<int>(currentPage) + 1,
                                              static_cast<int>(xtc->getPageCount()), hasChapters),
      [this](const ActivityResult& result) {
        const int action = result.isCancelled ? -1 : std::get<MenuResult>(result.data).action;
        if (action == static_cast<int>(XtcReaderMenuActivity::MenuAction::SELECT_CHAPTER)) {
          openChapterSelection();
          return;
        }
        if (action == static_cast<int>(XtcReaderMenuActivity::MenuAction::GO_HOME)) {
          onGoHome();
          return;
        }
        requestUpdate();  // redraw the page in any new cleanup mode
      });
}

void XtcReaderActivity::openChapterSelection() {
  if (xtc && xtc->hasChapters() && !xtc->getChapters().empty()) {
    startActivityForResult(std::make_unique<XtcReaderChapterSelectionActivity>(renderer, mappedInput, xtc, currentPage),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               currentPage = std::get<PageResult>(result.data).page;
                               requestUpdate();
                             }
                           });
  }
}

bool XtcReaderActivity::handleFormatInput() {
  if (!xtc) {
    return false;
  }

  // The reader menu on Confirm release or the touch menu gesture, as in EPUBs.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
    openMenu();
    return true;
  }
  return false;
}

void XtcReaderActivity::applyInitialOrientation() { renderer.setOrientation(GfxRenderer::Orientation::Portrait); }

void XtcReaderActivity::renderBook() {
  if (!xtc) {
    return;
  }

  renderPage();
  saveProgress();
}

XtcReaderActivity::StatusBarInfo XtcReaderActivity::getStatusBarInfo() const {
  const auto sb = SETTINGS.statusBarSpec();
  const int bookPageCount = static_cast<int>(xtc->getPageCount());
  const int bookPage = static_cast<int>(currentPage) + 1;
  std::string title = sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE ? xtc->getTitle() : "";

  if (!xtc->hasChapters()) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  const auto& chapters = xtc->getChapters();
  const auto chapterIt = std::find_if(chapters.begin(), chapters.end(), [this](const xtc::ChapterInfo& chapter) {
    return currentPage >= chapter.startPage && currentPage <= chapter.endPage;
  });

  if (chapterIt == chapters.end() || chapterIt->endPage < chapterIt->startPage) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = chapterIt->name.empty() ? tr(STR_UNNAMED) : chapterIt->name;
  }

  return StatusBarInfo{static_cast<int>(currentPage - chapterIt->startPage) + 1,
                       static_cast<int>(chapterIt->endPage - chapterIt->startPage) + 1, std::move(title)};
}

void XtcReaderActivity::renderStatusBarOverlay(GfxRenderer& renderer, const StatusBarOverlayPosition position) const {
  const auto sb = SETTINGS.statusBarSpec();
  const bool drawBottom = sb.xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_BOTTOM &&
                          position == StatusBarOverlayPosition::Bottom;
  const bool drawTop = sb.xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP &&
                       position == StatusBarOverlayPosition::Top;
  if (!drawBottom && !drawTop) {
    return;
  }

  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight <= 0) {
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);

  int clearY;
  int paddingBottom = 0;
  if (position == StatusBarOverlayPosition::Bottom) {
    clearY = renderer.getScreenHeight() - orientedMarginBottom - statusBarHeight - 4;
    if (clearY < 0) {
      clearY = 0;
    }
  } else {
    clearY = orientedMarginTop;
    paddingBottom = renderer.getScreenHeight() - statusBarHeight - orientedMarginBottom - orientedMarginTop - 4;
  }
  const int clearHeight = position == StatusBarOverlayPosition::Bottom
                              ? renderer.getScreenHeight() - orientedMarginBottom - clearY
                              : statusBarHeight + 4;
  if (clearHeight > 0) {
    renderer.fillRect(0, clearY, renderer.getScreenWidth(), clearHeight, false);
  }

  const int pageCount = static_cast<int>(xtc->getPageCount());
  const int displayPage = static_cast<int>(currentPage) + 1;
  const float progress = pageCount > 0 ? (static_cast<float>(displayPage) * 100.0f) / pageCount : 0.0f;
  const auto pageInfo = getStatusBarInfo();
  GUI.drawStatusBar(renderer, progress, pageInfo.currentPage, pageInfo.pageCount, pageInfo.title, paddingBottom);
}

void XtcReaderActivity::renderPage() {
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  size_t pageBufferSize;
  if (bitDepth == 2) {
    pageBufferSize = static_cast<size_t>(pageWidth) * ((static_cast<size_t>(pageHeight) + 7) / 8) * 2;
  } else {
    pageBufferSize = ((pageWidth + 7) / 8) * pageHeight;
  }

  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(pageBufferSize));
  if (!pageBuffer) {
    LOG_ERR("XTR", "Failed to allocate page buffer (%lu bytes)", pageBufferSize);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_MEMORY_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  const unsigned long tLoad = millis();
  size_t bytesRead = xtc->loadPage(currentPage, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    LOG_ERR("XTR", "Failed to load page %lu: bufferSize=%lu bitDepth=%u error=%s", currentPage, pageBufferSize,
            bitDepth, xtc::errorToString(xtc->getLastError()));
    free(pageBuffer);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // After a dark page even a GC16 leaves the old art faintly behind on this
  // panel (judged 2026-09-22). "Manga Page Cleanup" (reader menu) picks the
  // extra pass: repeat the new page's GC16 from frame memory (default,
  // ~0.55 s), flash white first (the wake scrub's trick, ~0.73 s), or none.
  const uint8_t cleanup = SETTINGS.pictureCleanup;
  const bool afterDark = prevPageDark;
  // 1-bit pages are dithered into fine dot patterns that ghost through one
  // GC16 even between light pages (judged 2026-09-22); XTCH's true grays do
  // not. So in Double mode a 1-bit page always gets the second pass, an XTCH
  // page only after a dark one.
  const bool doubleMode = cleanup != CrossPointSettings::PICTURE_CLEANUP_OFF &&
                          cleanup != CrossPointSettings::PICTURE_CLEANUP_WHITE_FLASH;
  const bool flashed = afterDark && cleanup == CrossPointSettings::PICTURE_CLEANUP_WHITE_FLASH;
  const bool repeat = doubleMode && (afterDark || bitDepth == 1);
  if (flashed) {
    renderer.clearScreen();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  renderer.clearScreen();

  const uint16_t maxSrcY = pageHeight;

  if (bitDepth == 2) {
    const unsigned long t0 = millis();
    const size_t planeSize = static_cast<size_t>(pageWidth) * ((static_cast<size_t>(pageHeight) + 7) / 8);
    const uint8_t* plane1 = pageBuffer;
    const uint8_t* plane2 = pageBuffer + planeSize;
    const size_t colBytes = (pageHeight + 7) / 8;

    auto getPixelValue = [&](uint16_t x, uint16_t y) -> uint8_t {
      const size_t colIndex = pageWidth - 1 - x;
      const size_t byteInCol = y / 8;
      const size_t bitInByte = 7 - (y % 8);
      const size_t byteOffset = colIndex * colBytes + byteInCol;
      const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
      const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
      return (bit1 << 1) | bit2;
    };

    // In portrait, logical (x, y) lands at frame-buffer row panelHeight-1-x,
    // byte y/8, MSB first -- exactly an XTH plane's layout (columns stored
    // right to left, 8 vertical pixels a byte). A page the size of the panel
    // therefore maps onto the frame buffer byte for byte, and each pass below
    // is one bitwise loop instead of 2.6 M drawPixel calls that walk the
    // column-major planes against the cache (~13 s a page on 1404x1872).
    // Frame-buffer bit 1 = white. Pixel values: 0 white, 1 dark, 2 light, 3 black.
    uint8_t* const fb = renderer.getFrameBuffer();
    const bool direct = fb && renderer.getOrientation() == GfxRenderer::Orientation::Portrait &&
                        renderer.getWriteTarget() == fb && pageHeight % 8 == 0 &&
                        pageHeight == renderer.getDisplayWidth() && pageWidth == renderer.getDisplayHeight() &&
                        colBytes == renderer.getDisplayWidthBytes();
    enum class Pass { Base, Lsb, Msb };
    auto fillPass = [&](const Pass pass) {
      if (direct) {
        for (size_t i = 0; i < planeSize; i++) {
          const uint8_t a = plane1[i], b = plane2[i];
          fb[i] = pass == Pass::Base ? static_cast<uint8_t>(~(a | b))
                  : pass == Pass::Lsb ? static_cast<uint8_t>(~a & b)
                                      : static_cast<uint8_t>(a ^ b);
        }
        return;
      }
      renderer.clearScreen(pass == Pass::Base ? 0xFF : 0x00);
      for (uint16_t y = 0; y < pageHeight; y++) {
        for (uint16_t x = 0; x < pageWidth; x++) {
          const uint8_t pv = getPixelValue(x, y);
          if (pass == Pass::Base ? pv >= 1 : pass == Pass::Lsb ? pv == 1 : (pv == 1 || pv == 2)) {
            renderer.drawPixel(x, y, pass == Pass::Base);
          }
        }
      }
    };

    // 16-level panels (IT8951): expand the page straight into the driver's
    // send rows (fillXthGray4Row, above) as the page's single grey refresh,
    // instead of staging the base, copying the LSB/MSB planes and having the
    // driver fold all three back into 4bpp. No 4bpp frame is built first: a
    // 1.3 MB PSRAM write the driver then read back row by row (~215 ms a
    // page); now most of the expansion overlaps the wire time. The size check
    // keeps the row fill's frame-buffer writes and plane reads in step with
    // the rows the driver asks for.
    if (direct && renderer.supportsGray4() && planeSize == renderer.getBufferSize()) {
      XthGray4Rows rows{plane1, plane2, fb, renderer.getDisplayWidthBytes(), 0};
      // HALF, as the staged base asked of displayGrayBuffer(): GC16 every page.
      // The planes are only read until this returns; pageBuffer is freed after.
      if (renderer.displayGray4Rows(fillXthGray4Row, &rows, HalDisplay::HALF_REFRESH)) {
        // Only the next page's cleanup reads this, so it can wait for the sum.
        prevPageDark = rows.ink > static_cast<uint64_t>(planeSize) * 8 * 3 * DARK_PAGE_PERCENT / 100;
        pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
        if (repeat) renderer.repeatLastRefresh();
        const unsigned long tGray = millis();

        // The frame buffer already holds the base; nothing is staged, so this
        // only settles the display's bookkeeping, as on the plane path.
        renderer.cleanupGrayscaleWithFrameBuffer();

        free(pageBuffer);

        LOG_INF("XTR", "Page %lu/%lu (2-bit, gray4%s%s): load %lu ms, gray4 %lu, cleanup %lu", currentPage + 1,
                xtc->getPageCount(),
                flashed  ? ", white flash first"
                : repeat ? ", repeated"
                         : "",
                prevPageDark ? ", dark" : "", t0 - tLoad, tGray - t0, millis() - tGray);
        return;
      }
      LOG_ERR("XTR", "displayGray4Rows refused the page; using the plane path");
    }

    fillPass(Pass::Base);
    const unsigned long tBase = millis();

    // Ink weight for the next page's white flash: black 3, dark 2, light 1.
    if (direct) {
      uint64_t ink = 0;
      for (size_t i = 0; i < planeSize; i++) {
        const uint8_t a = plane1[i], b = plane2[i];
        ink += 3 * __builtin_popcount(a & b) + 2 * __builtin_popcount(~a & b & 0xFF) +
               __builtin_popcount(a & ~b & 0xFF);
      }
      prevPageDark = ink > static_cast<uint64_t>(planeSize) * 8 * 3 * DARK_PAGE_PERCENT / 100;
    } else {
      prevPageDark = false;
    }

    // Every picture page gets the ghost-clearing refresh. On a combined-base
    // panel (this one) the base is only staged and the gray pass is the
    // page's one refresh: HALF turns that from DU4 (differential, ghosts on
    // art) into GC16, ~0.17 s more. Others scrub, then refine.
    if (renderer.grayscaleCapabilities().base == HalDisplay::GrayscaleBase::Combined) {
      renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      renderer.preconditionGrayscale();
    }
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();

    const unsigned long tBaseShown = millis();

    fillPass(Pass::Lsb);
    renderer.copyGrayscaleLsbBuffers();
    fillPass(Pass::Msb);
    renderer.copyGrayscaleMsbBuffers();
    const unsigned long tPlanes = millis();

    renderer.displayGrayBuffer();
    if (repeat) renderer.repeatLastRefresh();
    const unsigned long tGray = millis();

    fillPass(Pass::Base);
    renderer.cleanupGrayscaleWithFrameBuffer();

    free(pageBuffer);

    LOG_INF("XTR", "Page %lu/%lu (2-bit, %s%s%s): load %lu ms, fill %lu, base %lu, planes %lu, gray %lu, cleanup %lu",
            currentPage + 1, xtc->getPageCount(), direct ? "direct" : "per-pixel",
            flashed  ? ", white flash first"
            : repeat ? ", repeated"
                     : "",
            prevPageDark ? ", dark" : "",
            t0 - tLoad, tBase - t0, tBaseShown - tBase, tPlanes - tBaseShown, tGray - tPlanes, millis() - tGray);
    return;
  } else {
    const size_t srcRowBytes = (pageWidth + 7) / 8;

    // Bit 1 = white; everything else is ink.
    uint64_t white = 0;
    for (size_t i = 0; i < pageBufferSize; i++) white += __builtin_popcount(pageBuffer[i]);
    const uint64_t total = static_cast<uint64_t>(srcRowBytes) * 8 * pageHeight;
    prevPageDark = (total - white) * 100 > total * DARK_PAGE_PERCENT;

    // XTG is row-major (8 horizontal pixels a byte, MSB = left); the portrait
    // frame buffer holds column x at row panelHeight-1-x, 8 vertical pixels a
    // byte (MSB = top). Both use bit 1 = white. A panel-sized page is copied
    // by transposing 8x8 blocks straight into place: ~41 K blocks instead of
    // 2.6 M drawPixel calls (0.5-2 s a page).
    const unsigned long tFill = millis();
    uint8_t* const fb = renderer.getFrameBuffer();
    const size_t fbRowBytes = renderer.getDisplayWidthBytes();
    const bool direct = fb && renderer.getOrientation() == GfxRenderer::Orientation::Portrait &&
                        renderer.getWriteTarget() == fb && pageHeight % 8 == 0 &&
                        pageHeight == renderer.getDisplayWidth() && pageWidth == renderer.getDisplayHeight() &&
                        fbRowBytes * 8 == pageHeight;
    if (direct) {
      for (uint16_t y0 = 0; y0 < pageHeight; y0 += 8) {
        const uint8_t* src = pageBuffer + static_cast<size_t>(y0) * srcRowBytes;
        const size_t fbByte = y0 / 8;
        for (size_t bx = 0; bx < srcRowBytes; bx++) {
          uint8_t s[8];
          for (int k = 0; k < 8; k++) s[k] = src[k * srcRowBytes + bx];
          for (int j = 0; j < 8; j++) {
            const size_t x = bx * 8 + j;
            if (x >= pageWidth) break;  // row padding in the last byte
            uint8_t t = 0;
            for (int k = 0; k < 8; k++) t |= static_cast<uint8_t>(((s[k] >> (7 - j)) & 1) << (7 - k));
            fb[(pageWidth - 1 - x) * fbRowBytes + fbByte] = t;
          }
        }
      }
    } else {
      for (uint16_t srcY = 0; srcY < maxSrcY; srcY++) {
        const size_t srcRowStart = srcY * srcRowBytes;

        for (uint16_t srcX = 0; srcX < pageWidth; srcX++) {
          const size_t srcByte = srcRowStart + srcX / 8;
          const size_t srcBit = 7 - (srcX % 8);
          const bool isBlack = !((pageBuffer[srcByte] >> srcBit) & 1);

          if (isBlack) {
            renderer.drawPixel(srcX, srcY, true);
          }
        }
      }
    }
    LOG_DBG("XTR", "Page %lu/%lu 1-bit %s: load %lu ms, fill %lu ms", currentPage + 1, xtc->getPageCount(),
            direct ? "direct" : "per-pixel", tFill - tLoad, millis() - tFill);
  }

  free(pageBuffer);

  if (SETTINGS.statusBarSpec().xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP) {
    renderStatusBarOverlay(renderer, StatusBarOverlayPosition::Top);
  } else {
    renderStatusBarOverlay(renderer, StatusBarOverlayPosition::Bottom);
  }

  // XTC pages are pictures (manga, comics). The differential DU refresh
  // leaves the last page's art behind on the next, so every page gets the
  // GC16 clear (HALF on this panel, ~0.23 s more than DU).
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  if (repeat) renderer.repeatLastRefresh();
  pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();

  LOG_INF("XTR", "Page %lu/%lu (1-bit%s%s)", currentPage + 1, xtc->getPageCount(),
          flashed  ? ", white flash first"
          : repeat ? ", repeated"
                   : "",
          prevPageDark ? ", dark" : "");
}

bool XtcReaderActivity::pageTurn(bool isForward) {
  if (!xtc) return false;
  if (isForward) {
    if (currentPage < xtc->getPageCount()) {
      currentPage++;
      return true;
    }
  } else {
    if (currentPage > 0) {
      currentPage--;
      return true;
    }
  }
  return false;
}

bool XtcReaderActivity::skipPages(int amount) {
  if (!xtc) return false;
  int newPage = static_cast<int>(currentPage) + amount;
  if (newPage < 0) newPage = 0;
  if (newPage > static_cast<int>(xtc->getPageCount())) newPage = static_cast<int>(xtc->getPageCount());
  if (newPage != static_cast<int>(currentPage)) {
    currentPage = static_cast<uint32_t>(newPage);
    return true;
  }
  return false;
}

bool XtcReaderActivity::isAtEndOfBook() const { return xtc && currentPage >= xtc->getPageCount(); }

void XtcReaderActivity::onReturnFromEndOfBook() {
  if (xtc && xtc->getPageCount() > 0) {
    currentPage = xtc->getPageCount() - 1;
  } else {
    currentPage = 0;
  }
}

void XtcReaderActivity::saveProgress() const {
  if (!xtc) return;
  uint8_t data[4];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = (currentPage >> 16) & 0xFF;
  data[3] = (currentPage >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(xtc->getCachePath(), data, sizeof(data))) {
    LOG_ERR("XTC", "Failed to save progress: page %lu", currentPage);
  }
}

void XtcReaderActivity::loadProgress() {
  if (!xtc) return;
  HalFile f;
  if (Storage.openFileForRead("XTC", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      if (currentPage >= xtc->getPageCount() && xtc->getPageCount() > 0) {
        currentPage = xtc->getPageCount() - 1;
      }
      LOG_DBG("XTC", "Loaded progress: page %lu/%lu", currentPage + 1, xtc->getPageCount());
    }
  }
}

ScreenshotInfo XtcReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Xtc;
  if (xtc) {
    const std::string t = xtc->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
    const uint32_t pageCount = xtc->getPageCount();
    info.totalPages = pageCount;
    uint32_t clampedPage = (pageCount > 0 && currentPage >= pageCount) ? pageCount - 1 : currentPage;
    info.progressPercent = pageCount > 0 ? xtc->calculateProgress(clampedPage) : 0;
    info.currentPage = static_cast<int>(clampedPage) + 1;
  } else {
    info.currentPage = currentPage + 1;
  }
  return info;
}
