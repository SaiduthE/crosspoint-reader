#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// On-disk format and pure helpers for the SD-card library index
// (/.crosspoint/library/index.bin). No Arduino or storage dependencies, so the
// logic here is host-testable; I/O lives in LibraryIndex / LibraryScanner.
namespace library {

enum class Category : uint8_t { Books = 0, Manga = 1, Comics = 2 };
constexpr int CATEGORY_COUNT = 3;

enum class BookFormat : uint8_t { Epub = 0, Xtc = 1, Txt = 2, Markdown = 3 };
constexpr uint8_t BOOK_FORMAT_COUNT = 4;

constexpr char INDEX_MAGIC[4] = {'C', 'P', 'L', 'B'};
constexpr uint8_t INDEX_VERSION = 1;

constexpr uint16_t MAX_BOOKS = 4000;
constexpr uint8_t MAX_SCAN_DEPTH = 6;
constexpr uint32_t MAX_DIRENTS = 20000;
// Longest full path (bytes, without NUL) the index stores; longer paths are skipped.
constexpr size_t MAX_PATH_LEN = 255;
constexpr int THUMB_HEIGHT = 144;
// Thumbnails are generated at 0.6 x height (Epub/Xtc::generateThumbBmp).
constexpr int THUMB_WIDTH = THUMB_HEIGHT * 6 / 10;

// Header flags.
constexpr uint16_t HEADER_TRUNCATED = 1u << 0;     // MAX_BOOKS or MAX_DIRENTS was hit
constexpr uint16_t HEADER_SORT_BATCHED = 1u << 1;  // a folder overflowed the sort arena

// Record flags.
constexpr uint8_t RECORD_META_DONE = 1u << 0;   // title/author taken from book metadata
constexpr uint8_t RECORD_THUMB_DONE = 1u << 1;  // thumbnail generation attempted
constexpr uint8_t RECORD_THUMB_OK = 1u << 2;    // thumb_<THUMB_HEIGHT>.bmp exists in the book's cache dir

constexpr size_t TITLE_CAP = 72;   // bytes incl. the NUL terminator
constexpr size_t AUTHOR_CAP = 36;  // bytes incl. the NUL terminator

struct IndexHeader {
  char magic[4];
  uint8_t version;
  uint8_t reserved0;
  uint16_t flags;
  uint16_t count[CATEGORY_COUNT];
  uint16_t total;
  uint32_t recordStart;
  uint32_t blobStart;
  uint32_t blobLen;
  uint32_t selfSize;  // whole file size; guards against a truncated write
  uint32_t contentHash;
  uint32_t lastScanMs;
  uint8_t reserved[24];
};
static_assert(sizeof(IndexHeader) == 64, "index header must stay 64 bytes");
static_assert(offsetof(IndexHeader, count) == 8 && offsetof(IndexHeader, recordStart) == 16 &&
                  offsetof(IndexHeader, contentHash) == 32 && offsetof(IndexHeader, lastScanMs) == 36,
              "index header layout is part of the file format");

// Fixed-size record; records are grouped [Books][Manga][Comics], so item i of
// category c sits at recordStart + (start(c) + i) * sizeof(IndexRecord).
struct IndexRecord {
  uint32_t pathOff;  // into the path blob
  uint16_t pathLen;
  uint8_t format;  // BookFormat
  uint8_t flags;   // RECORD_*
  uint32_t fileSize;
  // std::hash of the path: the <hash> in /.crosspoint/epub_<hash> and xtc_<hash>.
  // size_t is 32-bit on the ESP32 targets, so this is the full cache-dir hash.
  uint32_t cacheKey;
  uint8_t titleLen;
  uint8_t authorLen;
  char title[TITLE_CAP];    // NUL-terminated, titleLen < TITLE_CAP
  char author[AUTHOR_CAP];  // NUL-terminated, authorLen < AUTHOR_CAP
  uint8_t reserved[2];
};
static_assert(sizeof(IndexRecord) == 128, "index record must stay 128 bytes");
static_assert(offsetof(IndexRecord, fileSize) == 8 && offsetof(IndexRecord, cacheKey) == 12 &&
                  offsetof(IndexRecord, title) == 18 && offsetof(IndexRecord, author) == 90,
              "index record layout is part of the file format");
// Reconciliation packs a record index into 12 bits.
static_assert(MAX_BOOKS <= 4096, "record index must fit the reconciliation key");

constexpr uint32_t HEADER_SIZE = sizeof(IndexHeader);
constexpr uint32_t RECORD_SIZE = sizeof(IndexRecord);

// --- header / record validation (all loads go through memcpy) --------------
void decodeHeader(const uint8_t* bytes, IndexHeader& out);
// Structural check of a decoded header against the actual file size.
bool headerValid(const IndexHeader& header, uint32_t fileSize);
// First record index (absolute) of a category.
uint16_t categoryStart(const IndexHeader& header, Category category);
void decodeRecord(const uint8_t* bytes, IndexRecord& out);
// Bounds-checks a record against its header; also forces NUL termination.
bool sanitizeRecord(IndexRecord& record, const IndexHeader& header);

// --- scan helpers ------------------------------------------------------------
// Category a folder assigns to its subtree: its own keyword match, else the
// inherited one. Keywords are ASCII case-insensitive and match the whole name
// or a prefix followed by ' ', '-', '_' or '('.
Category categoryForFolder(std::string_view folderName, Category inherited);
// Canonical folder name for a category, used in the "put files here" hint.
const char* categoryFolderName(Category category);
// Short, untranslated format label drawn on placeholder covers ("EPUB").
const char* formatLabel(BookFormat format);

// Display title from a file name: extension stripped, '_' -> ' ', trimmed and
// truncated on a UTF-8 boundary to fit `cap` bytes incl. NUL. Returns length.
size_t titleFromFilename(std::string_view fileName, char* out, size_t cap);
// Copies src into out (cap bytes incl. NUL), truncating on a UTF-8 boundary.
size_t copyTruncatedUtf8(std::string_view src, char* out, size_t cap);

constexpr uint32_t FNV_OFFSET = 2166136261u;
uint32_t fnv1a(uint32_t hash, const void* data, size_t len);

// --- grid navigation ----------------------------------------------------------
constexpr int TAB_BAND = -1;
// Left/Right: one cell, wrapping around the list.
int gridStepHorizontal(int index, int count, int direction);
// Up/Down: one row. Up from the first row and Down from the last row return
// TAB_BAND; Down onto a short last row lands on the last item.
int gridStepVertical(int index, int count, int columns, int direction);
// First index of the page holding `index`.
int gridPageTop(int index, int pageItems);

// Placement of a thumbnail inside a fixed box: crop the overshooting axis
// (thumbnails are scale-to-fill), centre the undershooting one. crop* feed
// GfxRenderer::drawBitmap, which trims floor(size * crop / 2) px per side.
struct CoverFit {
  int offsetX;
  int offsetY;
  float cropX;
  float cropY;
};
CoverFit fitCover(int bitmapWidth, int bitmapHeight, int boxWidth, int boxHeight);

}  // namespace library
