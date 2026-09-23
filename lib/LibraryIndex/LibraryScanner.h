#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "LibraryFormat.h"

namespace library {

// Resumable SD-card walk that rebuilds index.bin.
//
// Depth-first in natural name order (so each category ends up in path order)
// with ONE open directory handle: a folder's entries are read into a fixed
// arena, sorted, and pushed in reverse onto an SD-backed stack, so RAM does
// not grow with the library. Records stream to per-category staging files;
// finalisation concatenates header + records + path blob into index.new and
// swaps it in, unless the content hash shows nothing changed.
//
// Enrichment (metadata titles, thumbnail flags) is carried over from the old
// index for books whose path and size are unchanged.
//
// Peak heap: 16 KB name arena + 1 KB sort offsets + 4 B per book of the old
// index (16 KB at MAX_BOOKS) + this object (~1.5 KB of path/record scratch)
// + the open file handles, i.e. ~35 KB at 4000 books, all freed with the
// scanner.
class LibraryScanner {
 public:
  enum class Result : uint8_t { Running, Changed, Unchanged, Failed };

  LibraryScanner() = default;
  ~LibraryScanner();
  LibraryScanner(const LibraryScanner&) = delete;
  LibraryScanner& operator=(const LibraryScanner&) = delete;

  // Allocates the work buffers, opens the staging files and reads the old
  // index for reconciliation. False on OOM or SD errors.
  bool begin();
  // Walks for about budgetMs (yielding to the scheduler every few entries).
  Result step(uint32_t budgetMs);
  // 0..100, monotonic; an estimate while the walk is running.
  int progressPercent() const { return progress; }
  uint16_t booksFound() const { return total; }

 private:
  static constexpr size_t ARENA_BYTES = 16 * 1024;
  static constexpr uint16_t ARENA_MAX_ENTRIES = 512;
  static constexpr size_t ARENA_ENTRY_HEADER = 5;  // kind (u8) + file size (u32)
  static constexpr uint8_t KIND_DIR = 0xFF;        // otherwise a BookFormat
  // Stack entry: u32 size, u8 depth, u8 category, u8 kind, u8 pad, u16 pathLen,
  // path bytes, then a u16 total-length trailer so entries pop from the end.
  static constexpr size_t STACK_ENTRY_HEADER = 10;
  static constexpr size_t STACK_ENTRY_MAX = STACK_ENTRY_HEADER + MAX_PATH_LEN + 2;
  static constexpr size_t COPY_CHUNK = 4096;
  static constexpr int ENTRIES_PER_YIELD = 16;

  struct StackEntry {
    uint32_t fileSize;
    uint8_t depth;
    Category category;
    uint8_t kind;
    uint16_t pathLen;
    const char* path;  // points into entryBuf
  };

  bool loadOldIndex();
  bool push(uint8_t kind, Category category, uint8_t depth, uint32_t fileSize, const char* path, size_t pathLen);
  bool pop(StackEntry& entry);
  bool openDirectory(const StackEntry& entry);
  bool enumerateSome();
  bool addToArena(uint8_t kind, uint32_t fileSize, const char* name, size_t nameLen);
  bool flushArena();
  bool pushChild(uint8_t kind, uint32_t fileSize, const char* name, size_t nameLen);
  bool emitBook(const StackEntry& entry);
  void reconcile(IndexRecord& record, const char* path);
  Result finalize();
  bool appendFile(HalFile& out, const char* path);
  Result fail(const char* reason);
  void closeFiles();
  void updateProgress();
  void yieldEvery();

  std::unique_ptr<char[]> arena;
  std::unique_ptr<uint16_t[]> arenaOffsets;
  // Old index keys, (cacheKey & KEY_HASH_MASK) | record index, sorted.
  std::unique_ptr<uint32_t[]> oldKeys;
  uint16_t oldKeyCount = 0;
  IndexHeader oldHeader{};
  bool oldValid = false;

  HalFile oldIndex;
  HalFile stackFile;
  HalFile blobFile;
  HalFile recordFiles[CATEGORY_COUNT];
  HalFile dir;
  bool dirOpen = false;

  char dirPath[MAX_PATH_LEN + 1] = {};
  size_t dirPathLen = 0;
  uint8_t dirDepth = 0;
  Category dirCategory = Category::Books;
  size_t arenaUsed = 0;
  uint16_t arenaEntries = 0;

  // Scratch kept off the stack: the loop task runs Epub parsing elsewhere.
  char nameBuf[MAX_PATH_LEN + 1] = {};
  char oldPathBuf[MAX_PATH_LEN + 1] = {};
  uint8_t entryBuf[STACK_ENTRY_MAX] = {};
  IndexRecord oldRecord{};

  uint32_t stackTop = 0;
  uint32_t direntsSeen = 0;
  uint32_t pushed = 0;
  uint32_t popped = 0;
  uint16_t counts[CATEGORY_COUNT] = {};
  uint16_t total = 0;
  uint16_t flags = 0;
  uint32_t blobLen = 0;
  uint32_t contentHash = FNV_OFFSET;
  int progress = 0;
  int opsSinceYield = 0;
  bool started = false;
  bool finished = false;
  bool failed = false;
};

}  // namespace library
