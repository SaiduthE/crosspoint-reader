#include "LibraryScanner.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

#include "LibraryIndex.h"

namespace library {

namespace {

constexpr char STACK_PATH[] = "/.crosspoint/library/stack.tmp";
constexpr char BLOB_PATH[] = "/.crosspoint/library/blob.tmp";
constexpr const char* RECORD_PATHS[CATEGORY_COUNT] = {"/.crosspoint/library/rec0.tmp", "/.crosspoint/library/rec1.tmp",
                                                      "/.crosspoint/library/rec2.tmp"};

// Reconciliation keys keep the top 20 hash bits and a 12-bit record index;
// candidates are confirmed against the full record, so a shared prefix only
// costs one extra record read.
constexpr uint32_t KEY_HASH_MASK = 0xFFFFF000u;
constexpr uint32_t KEY_INDEX_MASK = 0x00000FFFu;

bool formatForName(const std::string_view name, uint8_t& kind) {
  if (FsHelpers::hasEpubExtension(name)) {
    kind = static_cast<uint8_t>(BookFormat::Epub);
  } else if (FsHelpers::hasXtcExtension(name)) {
    kind = static_cast<uint8_t>(BookFormat::Xtc);
  } else if (FsHelpers::hasTxtExtension(name)) {
    kind = static_cast<uint8_t>(BookFormat::Txt);
  } else if (FsHelpers::hasMarkdownExtension(name)) {
    kind = static_cast<uint8_t>(BookFormat::Markdown);
  } else {
    return false;
  }
  return true;
}

bool hasNonAscii(const char* text, const size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (static_cast<uint8_t>(text[i]) >= 0x80) return true;
  }
  return false;
}

void removeIfPresent(const char* path) {
  if (Storage.exists(path)) Storage.remove(path);
}

}  // namespace

LibraryScanner::~LibraryScanner() {
  closeFiles();
  if (started && !finished) {
    removeIfPresent(STACK_PATH);
    removeIfPresent(BLOB_PATH);
    for (const char* path : RECORD_PATHS) removeIfPresent(path);
    removeIfPresent(INDEX_NEW_PATH);
  }
}

void LibraryScanner::closeFiles() {
  // HalFile methods assert on a never-opened handle, so only close open ones.
  if (dir) dir.close();
  dirOpen = false;
  if (stackFile) stackFile.close();
  if (blobFile) blobFile.close();
  for (auto& file : recordFiles) {
    if (file) file.close();
  }
  if (oldIndex) oldIndex.close();
}

bool LibraryScanner::begin() {
  started = true;
  Storage.mkdir(LIBRARY_DIR);

  // Both buffers live for the whole walk: the arena holds one folder's names
  // for sorting (and doubles as the finalisation copy buffer), the offsets
  // index it. Heap rather than stack: 17 KB is far past the task stack budget.
  arena = makeUniqueNoThrow<char[]>(ARENA_BYTES);
  arenaOffsets = makeUniqueNoThrow<uint16_t[]>(ARENA_MAX_ENTRIES);
  if (!arena || !arenaOffsets) {
    LOG_ERR("LIB", "OOM: scan arena (%u bytes)", static_cast<unsigned>(ARENA_BYTES));
    return false;
  }

  loadOldIndex();

  stackFile = Storage.open(STACK_PATH, O_RDWR | O_CREAT | O_TRUNC);
  if (!stackFile) {
    LOG_ERR("LIB", "Cannot create %s", STACK_PATH);
    return false;
  }
  if (!Storage.openFileForWrite("LIB", BLOB_PATH, blobFile)) {
    LOG_ERR("LIB", "Cannot create %s", BLOB_PATH);
    return false;
  }
  for (int c = 0; c < CATEGORY_COUNT; c++) {
    if (!Storage.openFileForWrite("LIB", RECORD_PATHS[c], recordFiles[c])) {
      LOG_ERR("LIB", "Cannot create %s", RECORD_PATHS[c]);
      return false;
    }
  }

  const char root[] = "/";
  if (!push(KIND_DIR, Category::Books, 0, 0, root, 1)) return false;
  LOG_DBG("LIB", "Scan started (old index: %u books), free heap %u, max alloc %u", oldValid ? oldHeader.total : 0,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  return true;
}

bool LibraryScanner::loadOldIndex() {
  oldValid = false;
  if (!Storage.exists(INDEX_PATH) || !Storage.openFileForRead("LIB", INDEX_PATH, oldIndex)) return false;
  uint8_t headerBytes[HEADER_SIZE];
  if (oldIndex.read(headerBytes, sizeof(headerBytes)) != static_cast<int>(sizeof(headerBytes))) return false;
  decodeHeader(headerBytes, oldHeader);
  if (!headerValid(oldHeader, static_cast<uint32_t>(oldIndex.fileSize()))) return false;
  oldValid = true;
  if (oldHeader.total == 0) return true;

  // 4 bytes per old book, freed with the scanner. Without it the walk still
  // works; it just drops enrichment (titles/thumb flags are rebuilt lazily).
  oldKeys = makeUniqueNoThrow<uint32_t[]>(oldHeader.total);
  if (!oldKeys) {
    LOG_ERR("LIB", "OOM: reconciliation keys (%u books)", oldHeader.total);
    return true;
  }
  constexpr uint16_t chunkRecords = ARENA_BYTES / RECORD_SIZE;
  if (!oldIndex.seek(oldHeader.recordStart)) {
    oldKeys.reset();
    return true;
  }
  for (uint16_t first = 0; first < oldHeader.total; first = static_cast<uint16_t>(first + chunkRecords)) {
    const uint16_t n = std::min<uint16_t>(chunkRecords, static_cast<uint16_t>(oldHeader.total - first));
    const int bytes = n * static_cast<int>(RECORD_SIZE);
    if (oldIndex.read(arena.get(), static_cast<size_t>(bytes)) != bytes) {
      LOG_ERR("LIB", "Old index read failed");
      oldKeys.reset();
      return true;
    }
    for (uint16_t i = 0; i < n; i++) {
      uint32_t key;
      memcpy(&key, arena.get() + static_cast<size_t>(i) * RECORD_SIZE + offsetof(IndexRecord, cacheKey), sizeof(key));
      oldKeys[first + i] = (key & KEY_HASH_MASK) | ((first + i) & KEY_INDEX_MASK);
    }
    vTaskDelay(1);
  }
  oldKeyCount = oldHeader.total;
  std::sort(oldKeys.get(), oldKeys.get() + oldKeyCount);
  return true;
}

bool LibraryScanner::push(const uint8_t kind, const Category category, const uint8_t depth, const uint32_t fileSize,
                          const char* path, const size_t pathLen) {
  if (pathLen == 0 || pathLen > MAX_PATH_LEN) return true;  // skipped, not an error
  const auto len16 = static_cast<uint16_t>(pathLen);
  const auto entryLen = static_cast<uint16_t>(STACK_ENTRY_HEADER + pathLen + 2);
  memcpy(entryBuf, &fileSize, sizeof(fileSize));
  entryBuf[4] = depth;
  entryBuf[5] = static_cast<uint8_t>(category);
  entryBuf[6] = kind;
  entryBuf[7] = 0;
  memcpy(entryBuf + 8, &len16, sizeof(len16));
  if (reinterpret_cast<const uint8_t*>(path) != entryBuf + STACK_ENTRY_HEADER) {
    memmove(entryBuf + STACK_ENTRY_HEADER, path, pathLen);
  }
  memcpy(entryBuf + STACK_ENTRY_HEADER + pathLen, &entryLen, sizeof(entryLen));
  if (!stackFile.seek(stackTop) || stackFile.write(entryBuf, entryLen) != entryLen) {
    LOG_ERR("LIB", "Scan stack write failed");
    return false;
  }
  stackTop += entryLen;
  pushed++;
  return true;
}

bool LibraryScanner::pop(StackEntry& entry) {
  uint16_t entryLen = 0;
  if (stackTop < STACK_ENTRY_HEADER + 2 || !stackFile.seek(stackTop - 2) ||
      stackFile.read(&entryLen, sizeof(entryLen)) != static_cast<int>(sizeof(entryLen))) {
    LOG_ERR("LIB", "Scan stack read failed");
    return false;
  }
  if (entryLen < STACK_ENTRY_HEADER + 2 || entryLen > STACK_ENTRY_MAX || entryLen > stackTop) {
    LOG_ERR("LIB", "Scan stack corrupt (%u)", entryLen);
    return false;
  }
  if (!stackFile.seek(stackTop - entryLen) || stackFile.read(entryBuf, entryLen) != entryLen) {
    LOG_ERR("LIB", "Scan stack read failed");
    return false;
  }
  memcpy(&entry.fileSize, entryBuf, sizeof(entry.fileSize));
  entry.depth = entryBuf[4];
  entry.category = entryBuf[5] < CATEGORY_COUNT ? static_cast<Category>(entryBuf[5]) : Category::Books;
  entry.kind = entryBuf[6];
  memcpy(&entry.pathLen, entryBuf + 8, sizeof(entry.pathLen));
  if (entry.pathLen != entryLen - STACK_ENTRY_HEADER - 2) {
    LOG_ERR("LIB", "Scan stack corrupt (path %u)", entry.pathLen);
    return false;
  }
  entryBuf[STACK_ENTRY_HEADER + entry.pathLen] = '\0';  // overwrites the consumed trailer
  entry.path = reinterpret_cast<const char*>(entryBuf + STACK_ENTRY_HEADER);
  stackTop -= entryLen;
  popped++;
  return true;
}

bool LibraryScanner::openDirectory(const StackEntry& entry) {
  memcpy(dirPath, entry.path, entry.pathLen);
  dirPath[entry.pathLen] = '\0';
  dirPathLen = entry.pathLen;
  dirDepth = entry.depth;
  dirCategory = entry.category;
  arenaUsed = 0;
  arenaEntries = 0;

  if (dir) dir.close();
  dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    LOG_ERR("LIB", "Cannot open folder %s", dirPath);
    if (dir) dir.close();
    return true;  // skip this folder, keep walking
  }
  dir.rewindDirectory();
  dirOpen = true;
  return true;
}

bool LibraryScanner::enumerateSome() {
  for (int i = 0; i < ENTRIES_PER_YIELD; i++) {
    HalFile entry = dir.openNextFile();
    if (!entry) {
      dir.close();
      dirOpen = false;
      return flushArena();
    }
    if (++direntsSeen > MAX_DIRENTS) {
      LOG_ERR("LIB", "Entry cap (%u) reached, index truncated", static_cast<unsigned>(MAX_DIRENTS));
      flags |= HEADER_TRUNCATED;
      dir.close();
      dirOpen = false;
      // Drop this folder's unsorted names and everything still pending.
      arenaUsed = 0;
      arenaEntries = 0;
      stackTop = 0;
      return true;
    }
    const bool gotName = entry.getName(nameBuf, sizeof(nameBuf)) > 0;
    const bool isDirectory = entry.isDirectory();
    const auto fileSize = isDirectory ? 0u : static_cast<uint32_t>(entry.fileSize());
    if (!gotName || nameBuf[0] == '.' || strcmp(nameBuf, "System Volume Information") == 0 ||
        strcmp(nameBuf, "$RECYCLE.BIN") == 0) {
      continue;
    }
    const size_t nameLen = strlen(nameBuf);
    uint8_t kind = KIND_DIR;
    if (isDirectory) {
      if (dirDepth + 1 > MAX_SCAN_DEPTH) continue;
    } else if (!formatForName(std::string_view(nameBuf, nameLen), kind)) {
      continue;
    }
    if (!addToArena(kind, fileSize, nameBuf, nameLen)) return false;
  }
  vTaskDelay(1);  // feed the idle-task watchdog during long folders
  return true;
}

bool LibraryScanner::addToArena(const uint8_t kind, const uint32_t fileSize, const char* name, const size_t nameLen) {
  const size_t needed = ARENA_ENTRY_HEADER + nameLen + 1;
  if (arenaUsed + needed > ARENA_BYTES || arenaEntries >= ARENA_MAX_ENTRIES) {
    // Folder too large to sort in one go: emit what we have as a sorted batch.
    flags |= HEADER_SORT_BATCHED;
    if (!flushArena()) return false;
  }
  char* slot = arena.get() + arenaUsed;
  slot[0] = static_cast<char>(kind);
  memcpy(slot + 1, &fileSize, sizeof(fileSize));
  memcpy(slot + ARENA_ENTRY_HEADER, name, nameLen);
  slot[ARENA_ENTRY_HEADER + nameLen] = '\0';
  arenaOffsets[arenaEntries++] = static_cast<uint16_t>(arenaUsed);
  arenaUsed += needed;
  return true;
}

bool LibraryScanner::flushArena() {
  const char* base = arena.get();
  std::sort(arenaOffsets.get(), arenaOffsets.get() + arenaEntries, [base](const uint16_t a, const uint16_t b) {
    return FsHelpers::naturalLess(base + a + ARENA_ENTRY_HEADER, base + b + ARENA_ENTRY_HEADER);
  });
  // Reverse push: the first name in order is popped first.
  for (int i = arenaEntries - 1; i >= 0; i--) {
    const char* slot = base + arenaOffsets[i];
    uint32_t fileSize;
    memcpy(&fileSize, slot + 1, sizeof(fileSize));
    const char* name = slot + ARENA_ENTRY_HEADER;
    if (!pushChild(static_cast<uint8_t>(slot[0]), fileSize, name, strlen(name))) return false;
    yieldEvery();
  }
  arenaUsed = 0;
  arenaEntries = 0;
  return true;
}

bool LibraryScanner::pushChild(const uint8_t kind, const uint32_t fileSize, const char* name, const size_t nameLen) {
  const bool atRoot = dirPathLen == 1 && dirPath[0] == '/';
  const size_t pathLen = dirPathLen + (atRoot ? 0 : 1) + nameLen;
  if (pathLen > MAX_PATH_LEN) {
    LOG_ERR("LIB", "Path too long, skipped: %s/%s", dirPath, name);
    return true;
  }
  // Compose the child path in place where push() expects it.
  char* path = reinterpret_cast<char*>(entryBuf + STACK_ENTRY_HEADER);
  memcpy(path, dirPath, dirPathLen);
  size_t pos = dirPathLen;
  if (!atRoot) path[pos++] = '/';
  memcpy(path + pos, name, nameLen);
  const Category category =
      kind == KIND_DIR ? categoryForFolder(std::string_view(name, nameLen), dirCategory) : dirCategory;
  return push(kind, category, static_cast<uint8_t>(dirDepth + 1), fileSize, path, pathLen);
}

bool LibraryScanner::emitBook(const StackEntry& entry) {
  if (total >= MAX_BOOKS) {
    flags |= HEADER_TRUNCATED;
    return true;
  }
  IndexRecord record{};
  record.pathOff = blobLen;
  record.pathLen = entry.pathLen;
  record.format = entry.kind;
  record.fileSize = entry.fileSize;
  record.cacheKey = cacheKeyFor(entry.path, entry.pathLen);

  const char* slash = strrchr(entry.path, '/');
  const char* name = slash ? slash + 1 : entry.path;
  const size_t nameLen = entry.pathLen - static_cast<size_t>(name - entry.path);
  if (hasNonAscii(name, nameLen)) {
    // macOS writes NFD names; the UI fonts only carry precomposed glyphs.
    const std::string composed = utf8ComposeNfc(std::string(name, nameLen));
    record.titleLen = static_cast<uint8_t>(titleFromFilename(composed, record.title, TITLE_CAP));
  } else {
    record.titleLen = static_cast<uint8_t>(titleFromFilename(std::string_view(name, nameLen), record.title, TITLE_CAP));
  }
  if (entry.kind == static_cast<uint8_t>(BookFormat::Txt) || entry.kind == static_cast<uint8_t>(BookFormat::Markdown)) {
    record.flags = RECORD_META_DONE | RECORD_THUMB_DONE;  // no metadata or cover to extract
  }
  reconcile(record, entry.path);

  const auto category = static_cast<size_t>(entry.category);
  if (recordFiles[category].write(&record, RECORD_SIZE) != RECORD_SIZE ||
      blobFile.write(entry.path, entry.pathLen) != entry.pathLen) {
    LOG_ERR("LIB", "Staging write failed");
    return false;
  }
  blobLen += entry.pathLen;
  counts[category]++;
  total++;
  contentHash = fnv1a(contentHash, entry.path, entry.pathLen);
  contentHash = fnv1a(contentHash, &entry.fileSize, sizeof(entry.fileSize));
  return true;
}

void LibraryScanner::reconcile(IndexRecord& record, const char* path) {
  if (!oldKeys || oldKeyCount == 0) return;
  const uint32_t prefix = record.cacheKey & KEY_HASH_MASK;
  const uint32_t* begin = oldKeys.get();
  const uint32_t* end = begin + oldKeyCount;
  for (const uint32_t* it = std::lower_bound(begin, end, prefix); it != end && (*it & KEY_HASH_MASK) == prefix; ++it) {
    const uint32_t index = *it & KEY_INDEX_MASK;
    if (!oldIndex.seek(oldHeader.recordStart + index * RECORD_SIZE) ||
        oldIndex.read(&oldRecord, RECORD_SIZE) != static_cast<int>(RECORD_SIZE) ||
        !sanitizeRecord(oldRecord, oldHeader)) {
      continue;
    }
    if (oldRecord.cacheKey != record.cacheKey || oldRecord.pathLen != record.pathLen ||
        oldRecord.fileSize != record.fileSize || oldRecord.format != record.format) {
      continue;
    }
    if (!oldIndex.seek(oldHeader.blobStart + oldRecord.pathOff) ||
        oldIndex.read(oldPathBuf, oldRecord.pathLen) != static_cast<int>(oldRecord.pathLen) ||
        memcmp(oldPathBuf, path, record.pathLen) != 0) {
      continue;
    }
    record.flags = oldRecord.flags;
    record.titleLen = oldRecord.titleLen;
    record.authorLen = oldRecord.authorLen;
    memcpy(record.title, oldRecord.title, TITLE_CAP);
    memcpy(record.author, oldRecord.author, AUTHOR_CAP);
    return;
  }
}

void LibraryScanner::yieldEvery() {
  // SD work never blocks long enough to let the idle task feed its watchdog.
  if (++opsSinceYield >= ENTRIES_PER_YIELD) {
    opsSinceYield = 0;
    vTaskDelay(1);
  }
}

void LibraryScanner::updateProgress() {
  const uint32_t pending = pushed - popped + (dirOpen ? 1 : 0);
  const uint32_t done = popped;
  const int estimate = done + pending > 0 ? static_cast<int>(done * 99 / (done + pending)) : 0;
  if (estimate > progress) progress = estimate;
}

LibraryScanner::Result LibraryScanner::step(const uint32_t budgetMs) {
  if (!started || finished || failed) return Result::Failed;
  const unsigned long start = millis();
  do {
    if (dirOpen) {
      if (!enumerateSome()) return fail("folder read");
      continue;
    }
    if (stackTop == 0) return finalize();
    StackEntry entry{};
    if (!pop(entry)) return fail("stack");
    if (entry.kind == KIND_DIR) {
      if (!openDirectory(entry)) return fail("folder open");
    } else if (!emitBook(entry)) {
      return fail("record write");
    }
    yieldEvery();
  } while (millis() - start < budgetMs);
  updateProgress();
  return Result::Running;
}

LibraryScanner::Result LibraryScanner::fail(const char* reason) {
  LOG_ERR("LIB", "Scan failed: %s", reason);
  failed = true;
  closeFiles();
  return Result::Failed;
}

bool LibraryScanner::appendFile(HalFile& out, const char* path) {
  HalFile in;
  if (!Storage.openFileForRead("LIB", path, in)) return false;
  // COPY_CHUNK <= ARENA_BYTES: the arena is idle once the walk is done.
  while (true) {
    const int read = in.read(arena.get(), COPY_CHUNK);
    if (read < 0) return false;
    if (read == 0) return true;
    if (out.write(arena.get(), static_cast<size_t>(read)) != static_cast<size_t>(read)) return false;
    vTaskDelay(1);
  }
}

LibraryScanner::Result LibraryScanner::finalize() {
  if (dir) dir.close();
  dirOpen = false;
  if (stackFile) stackFile.close();
  removeIfPresent(STACK_PATH);
  if (blobFile) blobFile.close();
  for (auto& file : recordFiles) {
    if (file) file.close();
  }

  const bool unchanged = oldValid && oldHeader.contentHash == contentHash && oldHeader.total == total &&
                         oldHeader.flags == flags && memcmp(oldHeader.count, counts, sizeof(counts)) == 0;
  if (unchanged) {
    if (oldIndex) oldIndex.close();
    removeIfPresent(BLOB_PATH);
    for (const char* path : RECORD_PATHS) removeIfPresent(path);
    finished = true;
    progress = 100;
    LOG_DBG("LIB", "Scan: %u books, unchanged", total);
    return Result::Unchanged;
  }

  IndexHeader header{};
  memcpy(header.magic, INDEX_MAGIC, sizeof(INDEX_MAGIC));
  header.version = INDEX_VERSION;
  header.flags = flags;
  memcpy(header.count, counts, sizeof(counts));
  header.total = total;
  header.recordStart = HEADER_SIZE;
  header.blobStart = HEADER_SIZE + static_cast<uint32_t>(total) * RECORD_SIZE;
  header.blobLen = blobLen;
  header.selfSize = header.blobStart + blobLen;
  header.contentHash = contentHash;
  header.lastScanMs = millis();

  {
    HalFile out;
    if (!Storage.openFileForWrite("LIB", INDEX_NEW_PATH, out)) return fail("index.new create");
    bool ok = out.write(&header, HEADER_SIZE) == HEADER_SIZE;
    for (int c = 0; ok && c < CATEGORY_COUNT; c++) ok = appendFile(out, RECORD_PATHS[c]);
    ok = ok && appendFile(out, BLOB_PATH);
    const bool sizeOk = ok && out.fileSize() == header.selfSize;
    out.close();  // must close before the rename below
    if (!sizeOk) {
      removeIfPresent(INDEX_NEW_PATH);
      return fail("index.new write");
    }
  }

  if (oldIndex) oldIndex.close();
  removeIfPresent(INDEX_PATH);
  if (!Storage.rename(INDEX_NEW_PATH, INDEX_PATH)) return fail("index swap");
  removeIfPresent(BLOB_PATH);
  for (const char* path : RECORD_PATHS) removeIfPresent(path);
  finished = true;
  progress = 100;
  LOG_DBG("LIB", "Scan: %u books (%u/%u/%u), %u entries, flags 0x%x", total, counts[0], counts[1], counts[2],
          static_cast<unsigned>(direntsSeen), flags);
  return Result::Changed;
}

}  // namespace library
