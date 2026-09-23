#include "LibraryIndex.h"

#include <HalStorage.h>
#include <Logging.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <string_view>

namespace library {

#ifdef ARDUINO
static_assert(sizeof(size_t) == sizeof(uint32_t),
              "cacheKey must hold the full std::hash width used for the epub_/xtc_ cache dirs");
#endif

namespace {
// Set from the loop task (activity hooks) and read by the Library screen.
std::atomic<bool> staleFlag{true};
}  // namespace

void markStale() { staleFlag.store(true); }
bool isStale() { return staleFlag.load(); }
void clearStale() { staleFlag.store(false); }

uint32_t cacheKeyFor(const char* path, const size_t len) {
  // std::hash<std::string_view> is specified to equal std::hash<std::string>
  // for the same characters, so no std::string copy is needed.
  return static_cast<uint32_t>(std::hash<std::string_view>{}(std::string_view(path, len)));
}

bool thumbPathFor(const IndexRecord& record, char* out, const size_t cap) {
  const char* prefix = nullptr;
  if (record.format == static_cast<uint8_t>(BookFormat::Epub)) {
    prefix = "epub";
  } else if (record.format == static_cast<uint8_t>(BookFormat::Xtc)) {
    prefix = "xtc";
  } else {
    return false;
  }
  const int written = snprintf(out, cap, "%s/%s_%lu/thumb_%d.bmp", BOOK_CACHE_DIR, prefix,
                               static_cast<unsigned long>(record.cacheKey), THUMB_HEIGHT);
  return written > 0 && static_cast<size_t>(written) < cap;
}

bool LibraryIndex::load() {
  valid = false;
  if (!Storage.exists(INDEX_PATH)) {
    if (!Storage.exists(INDEX_NEW_PATH)) return false;
    // A rescan finished writing index.new but was interrupted before the swap;
    // the header validation below rejects a partial file.
    Storage.rename(INDEX_NEW_PATH, INDEX_PATH);
  }

  HalFile file;
  if (!Storage.openFileForRead("LIB", INDEX_PATH, file)) {
    LOG_ERR("LIB", "Cannot open %s", INDEX_PATH);
    return false;
  }
  uint8_t bytes[HEADER_SIZE];
  if (file.read(bytes, sizeof(bytes)) != static_cast<int>(sizeof(bytes))) {
    LOG_ERR("LIB", "Index header truncated");
    return false;
  }
  decodeHeader(bytes, hdr);
  if (!headerValid(hdr, static_cast<uint32_t>(file.fileSize()))) {
    LOG_ERR("LIB", "Index header invalid");
    return false;
  }
  valid = true;
  LOG_DBG("LIB", "Index: %u books (%u/%u/%u), flags 0x%x", hdr.total, hdr.count[0], hdr.count[1], hdr.count[2],
          hdr.flags);
  return true;
}

uint16_t LibraryIndex::count(const Category category) const {
  const auto index = static_cast<size_t>(category);
  return valid && index < CATEGORY_COUNT ? hdr.count[index] : 0;
}

uint32_t LibraryIndex::recordOffset(const Category category, const uint16_t index) const {
  return hdr.recordStart + (static_cast<uint32_t>(categoryStart(hdr, category)) + index) * RECORD_SIZE;
}

int LibraryIndex::readRecords(const Category category, const uint16_t first, const int maxRecords,
                              IndexRecord* out) const {
  const uint16_t available = count(category);
  if (!out || maxRecords <= 0 || first >= available) return 0;
  const int wanted = maxRecords < available - first ? maxRecords : available - first;

  HalFile file;
  if (!Storage.openFileForRead("LIB", INDEX_PATH, file)) {
    LOG_ERR("LIB", "Cannot open %s", INDEX_PATH);
    return 0;
  }
  if (!file.seek(recordOffset(category, first))) {
    LOG_ERR("LIB", "Seek to record %u failed", first);
    return 0;
  }
  // `out` is an IndexRecord array, so the bytes land in naturally aligned fields.
  const int bytes = wanted * static_cast<int>(RECORD_SIZE);
  if (file.read(out, static_cast<size_t>(bytes)) != bytes) {
    LOG_ERR("LIB", "Short read of %d records", wanted);
    return 0;
  }
  for (int i = 0; i < wanted; i++) {
    if (!sanitizeRecord(out[i], hdr)) {
      LOG_ERR("LIB", "Corrupt record %u", static_cast<unsigned>(first + i));
      out[i] = IndexRecord{};
      markStale();
    }
  }
  return wanted;
}

bool LibraryIndex::readRecord(const Category category, const uint16_t index, IndexRecord& out) const {
  return readRecords(category, index, 1, &out) == 1 && out.pathLen > 0;
}

bool LibraryIndex::readPath(const IndexRecord& record, char* out, const size_t cap) const {
  if (!valid || !out || record.pathLen == 0 || record.pathLen >= cap) return false;
  HalFile file;
  if (!Storage.openFileForRead("LIB", INDEX_PATH, file)) {
    LOG_ERR("LIB", "Cannot open %s", INDEX_PATH);
    return false;
  }
  if (!file.seek(hdr.blobStart + record.pathOff) ||
      file.read(out, record.pathLen) != static_cast<int>(record.pathLen)) {
    LOG_ERR("LIB", "Path read failed");
    return false;
  }
  out[record.pathLen] = '\0';
  return true;
}

bool LibraryIndex::writeRecord(const Category category, const uint16_t index, const IndexRecord& record) const {
  if (!valid || index >= count(category)) return false;
  HalFile file = Storage.open(INDEX_PATH, O_RDWR);
  if (!file) {
    LOG_ERR("LIB", "Cannot open %s for update", INDEX_PATH);
    return false;
  }
  if (!file.seek(recordOffset(category, index)) || file.write(&record, RECORD_SIZE) != RECORD_SIZE) {
    LOG_ERR("LIB", "Record %u update failed", index);
    return false;
  }
  return true;
}

}  // namespace library
