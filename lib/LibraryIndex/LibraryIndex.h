#pragma once

#include <cstddef>
#include <cstdint>

#include "LibraryFormat.h"

namespace library {

constexpr char LIBRARY_DIR[] = "/.crosspoint/library";
constexpr char INDEX_PATH[] = "/.crosspoint/library/index.bin";
// Written next to the live index and renamed over it once complete.
constexpr char INDEX_NEW_PATH[] = "/.crosspoint/library/index.new";
// Book caches (epub_<hash>/, xtc_<hash>/) live directly under this directory.
constexpr char BOOK_CACHE_DIR[] = "/.crosspoint";

// The SD content may have changed since the last reconciliation walk. True at
// every boot; set by the file transfer / download / delete / move paths.
void markStale();
bool isStale();
void clearStale();

// Read access to index.bin plus in-place record updates. Holds only the
// 64-byte header; records and paths are read on demand.
class LibraryIndex {
 public:
  // Reads and validates the header. Recovers a finished index.new left behind
  // by an interrupted rename.
  bool load();
  bool isValid() const { return valid; }
  const IndexHeader& header() const { return hdr; }
  uint16_t count(Category category) const;
  uint16_t total() const { return valid ? hdr.total : 0; }

  // Reads up to maxRecords records of `category` starting at `first` with one
  // seek + one read. Returns the number of records read (0 on error).
  int readRecords(Category category, uint16_t first, int maxRecords, IndexRecord* out) const;
  bool readRecord(Category category, uint16_t index, IndexRecord& out) const;
  // Copies the record's path into out (NUL-terminated).
  bool readPath(const IndexRecord& record, char* out, size_t cap) const;
  // Rewrites one record in place (title/author/flags enrichment).
  bool writeRecord(Category category, uint16_t index, const IndexRecord& record) const;

 private:
  uint32_t recordOffset(Category category, uint16_t index) const;

  IndexHeader hdr{};
  bool valid = false;
};

// Thumbnail path for an EPUB/XTC record: <BOOK_CACHE_DIR>/{epub,xtc}_<cacheKey>/thumb_<THUMB_HEIGHT>.bmp.
// Returns false for formats without a cover.
bool thumbPathFor(const IndexRecord& record, char* out, size_t cap);
// std::hash of a book path, as Epub/Xtc use for their cache directories.
uint32_t cacheKeyFor(const char* path, size_t len);

}  // namespace library
