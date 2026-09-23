#pragma once

#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>
#include <stdint.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "Epub/blocks/ImageBlock.h"

// Streaming cache writer for 2-bit pixels (4 levels). Packs 4 pixels per byte,
// MSB first.
//
// The .pxc file is written incrementally in small row bands rather than holding
// the whole decoded image in one heap buffer. A full-page image (e.g. 482x728)
// needs ~88KB packed, which will not fit alongside the ~20KB JPEG decoder on a
// fragmented 380KB heap (free heap is routinely ~55KB on an image page). When
// the cache cannot be written, every render pass re-decodes the JPEG from
// scratch; an anti-aliased image page renders ~14 times (BW + AA restore + two
// grayscale planes x ~6 strips), so a 2s decode becomes a ~30s freeze / watchdog
// reset. Streaming keeps the working set to a single MCU-row band, so caching
// succeeds and the image is decoded exactly once.
//
// Correctness relies on JPEGDEC delivering blocks in raster MCU order (outer
// loop over y, inner over x: see jpeg.inl DecodeJPEG). Consecutive MCU rows map
// to contiguous, non-overlapping destination row ranges, so once a block whose
// top row is Y arrives, every output row < Y is final and is flushed to disk.
struct PixelCache {
  uint8_t* buffer;   // band buffer: (bandRows + 1) rows; last row kept zeroed
  uint8_t* zeroRow;  // points at the spare zeroed row, for gap/clip fill
  int width;
  int height;
  int bytesPerRow;
  int originX;      // config.x - to convert screen coords to cache coords
  int originY;      // config.y
  int bandRows;     // rows held in the band buffer
  int bandStart;    // image-local row index of band buffer row 0
  int flushedRows;  // image-local rows already written to file
  HalFile file;
  std::string cachePathStr;
  bool ok;
  bool whole;    // band == the whole image, held in PSRAM
  bool hasFile;  // the .pxc is open and its header is written

  PixelCache()
      : buffer(nullptr),
        zeroRow(nullptr),
        width(0),
        height(0),
        bytesPerRow(0),
        originX(0),
        originY(0),
        bandRows(0),
        bandStart(0),
        flushedRows(0),
        ok(false),
        whole(false),
        hasFile(false) {}
  PixelCache(const PixelCache&) = delete;
  PixelCache& operator=(const PixelCache&) = delete;

  static constexpr int MIN_BAND_ROWS = 16;
  static constexpr size_t MAX_BAND_BYTES = 24 * 1024;  // band working-set ceiling
  // Whole-image mode ceilings. A 1404x1872 page image packs to ~640 KB; the
  // reserve leaves the IT8951's three PSRAM planes and the framebuffers alone.
  static constexpr size_t MAX_WHOLE_BYTES = 1024 * 1024;
  static constexpr size_t WHOLE_PSRAM_RESERVE = 1024 * 1024;
  static constexpr size_t WRITE_SLICE = 32 * 1024;

  // Open the cache file, write the header, and allocate a band buffer big enough
  // to hold the tallest single decode block (maxBlockDstRows output rows).
  bool begin(const std::string& cachePath, int w, int h, int ox, int oy, int maxBlockDstRows) {
    width = w;
    height = h;
    originX = ox;
    originY = oy;
    bytesPerRow = (w + 3) / 4;  // 2 bits per pixel, 4 pixels per byte
    bandStart = 0;
    flushedRows = 0;
    ok = false;
    whole = false;
    hasFile = false;

    // Whole-image mode. The streaming band above exists because the full
    // payload will not fit RAM -- true of the internal heap, false of PSRAM,
    // where a 1165x1820 page image is ~520 KB against ~6.6 MB free. When it
    // fits, the band IS the image and three things follow: nothing is ever
    // evicted, so advanceTo() has no work; the file is written as one
    // sequential burst instead of `height` separate ~300-byte row writes, which
    // is far kinder to a marginal SD bus; and the payload is handed to
    // ImageBlock's render slot at the end, so the remaining passes of this page
    // render neither re-read nor -- the point -- re-decode it, even when the
    // card refuses the write outright.
    const size_t wholeBytes = (size_t)(h + 1) * bytesPerRow;
    if (h > 0 && bytesPerRow > 0 && wholeBytes <= MAX_WHOLE_BYTES &&
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > wholeBytes + WHOLE_PSRAM_RESERVE) {
      buffer = (uint8_t*)heap_caps_malloc(wholeBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (buffer) {
        memset(buffer, 0, wholeBytes);
        bandRows = h;
        zeroRow = buffer + (size_t)h * bytesPerRow;
        whole = true;
      }
    }

    if (!whole) {
    int wantRows = maxBlockDstRows + 2;
    if (wantRows < MIN_BAND_ROWS) wantRows = MIN_BAND_ROWS;
    if (wantRows > h) wantRows = h;

    size_t maxRowsByMem = MAX_BAND_BYTES / (size_t)bytesPerRow;
    if (maxRowsByMem < 1) maxRowsByMem = 1;
    if ((size_t)wantRows > maxRowsByMem) wantRows = (int)maxRowsByMem;

    // A single decode block must fit inside the band, otherwise streaming would
    // drop rows. This only fails for pathological upscales that could not be
    // cached at all; fall back to the no-cache path.
    if (wantRows < maxBlockDstRows) {
      LOG_ERR("IMG", "Cache band too small (%d < %d rows) for %dx%d", wantRows, maxBlockDstRows, w, h);
      return false;
    }
    bandRows = wantRows;

    const size_t bufSize = (size_t)(bandRows + 1) * bytesPerRow;  // +1 spare zero row
    buffer = (uint8_t*)malloc(bufSize);
    if (!buffer) {
      LOG_ERR("IMG", "OOM cache band: %u bytes", (unsigned)bufSize);
      return false;
    }
    memset(buffer, 0, bufSize);
    zeroRow = buffer + (size_t)bandRows * bytesPerRow;
    }

    cachePathStr = cachePath;
    if (Storage.openFileForWrite("IMG", cachePath, file)) {
      const uint16_t w16 = (uint16_t)w;
      const uint16_t h16 = (uint16_t)h;
      if (file.write(&w16, 2) == 2 && file.write(&h16, 2) == 2) {
        hasFile = true;
      } else {
        LOG_ERR("IMG", "Failed to write cache header: %s", cachePath.c_str());
        abort();
      }
    } else {
      LOG_ERR("IMG", "Failed to open cache file for writing: %s", cachePath.c_str());
    }

    // Streaming has nowhere to put its rows without the file. Whole-image mode
    // does: it can still fill the render slot from PSRAM and spare every later
    // pass the decode, so a card that will not take the write costs the
    // on-disk cache and nothing else.
    if (!hasFile && !whole) {
      free(buffer);
      buffer = nullptr;
      return false;
    }

    LOG_DBG("IMG", "Cache stream started: %s (%dx%d, %s %d rows%s)", cachePath.c_str(), w, h,
            whole ? "whole" : "band", bandRows, hasFile ? "" : ", PSRAM only");
    ok = true;
    return true;
  }

  // Flush every output row below newTopRow (they are final in raster order) and
  // reposition the band to start at newTopRow. Returns false if a write failed,
  // in which case the caller must stop caching for the rest of the decode.
  bool advanceTo(int newTopRow) {
    if (!ok) return false;
    if (whole) return true;  // the band is the image: no row is ever evicted
    if (newTopRow <= bandStart) return true;
    if (newTopRow > height) newTopRow = height;

    for (int r = bandStart; r < newTopRow; ++r) {
      const int idx = r - bandStart;
      const uint8_t* rowPtr = (idx < bandRows) ? (buffer + (size_t)idx * bytesPerRow) : zeroRow;
      if (file.write(rowPtr, (size_t)bytesPerRow) != (size_t)bytesPerRow) {
        LOG_ERR("IMG", "Cache write error at row %d", r);
        ok = false;
        return false;
      }
    }
    flushedRows = newTopRow;
    bandStart = newTopRow;
    memset(buffer, 0, (size_t)bandRows * bytesPerRow);  // fresh band (gaps stay black)
    return true;
  }

  // Flush the final band and zero-fill any rows never covered (image clipped by
  // the screen), then close the file.
  bool finalize() {
    if (!ok) {
      abort();
      return false;
    }
    if (whole) return finalizeWhole();
    for (int r = flushedRows; r < height; ++r) {
      const int idx = r - bandStart;
      const uint8_t* rowPtr = (idx >= 0 && idx < bandRows) ? (buffer + (size_t)idx * bytesPerRow) : zeroRow;
      if (file.write(rowPtr, (size_t)bytesPerRow) != (size_t)bytesPerRow) {
        LOG_ERR("IMG", "Cache write error at row %d", r);
        abort();
        return false;
      }
    }
    file.close();
    LOG_DBG("IMG", "Cache written: %s (%dx%d, %d bytes)", cachePathStr.c_str(), width, height,
            4 + bytesPerRow * height);
    ok = false;  // file handed off; nothing left to clean up
    return true;
  }

  // Whole-image mode: write the payload in one sequential burst, then hand it
  // to the render slot. The handoff happens whether or not the write landed --
  // losing the on-card cache must not cost this page render its decode.
  bool finalizeWhole() {
    const size_t payload = (size_t)bytesPerRow * height;
    bool written = hasFile;
    for (size_t done = 0; done < payload && written;) {
      const size_t want = (payload - done < WRITE_SLICE) ? (payload - done) : WRITE_SLICE;
      if (file.write(buffer + done, want) != want) {
        LOG_ERR("IMG", "Cache write error at byte %u of %u", (unsigned)done, (unsigned)payload);
        written = false;
        break;
      }
      done += want;
    }
    if (written) {
      file.close();
      LOG_DBG("IMG", "Cache written: %s (%dx%d, %d bytes)", cachePathStr.c_str(), width, height,
              4 + bytesPerRow * height);
    } else {
      abort();  // drop the partial file; the payload below stands in for it
    }
    if (ImageBlock::adoptRenderCache(cachePathStr, buffer, (uint16_t)width, (uint16_t)height)) {
      buffer = nullptr;  // ownership passed to the slot; the destructor must not free it
      zeroRow = nullptr;
      LOG_DBG("IMG", "Payload kept in PSRAM (%u KB) - no later pass re-decodes",
              (unsigned)(payload / 1024));
    }
    ok = false;
    return written;
  }

  // Drop a partial/failed cache so a later decode re-creates it cleanly.
  void abort() {
    if (file.isOpen()) file.close();
    if (!cachePathStr.empty()) {
      Storage.remove(cachePathStr.c_str());
    }
    ok = false;
  }

  ~PixelCache() {
    if (file.isOpen()) {
      // The file is still open, so neither finalize() nor abort() ran, or a
      // mid-stream write failed (advanceTo() cleared ok but left the file open).
      // Drop the partial cache so we leave no corrupt file behind.
      abort();
    }
    if (buffer) {
      free(buffer);
      buffer = nullptr;
    }
  }
};
