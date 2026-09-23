#include "LibraryFormat.h"

#include <cstring>

namespace library {

namespace {

struct FolderKeyword {
  const char* word;  // lower-case ASCII
  Category category;
};

constexpr FolderKeyword FOLDER_KEYWORDS[] = {
    {"manga", Category::Manga},   {"mangas", Category::Manga},          {"manhwa", Category::Manga},
    {"manhua", Category::Manga},  {"comic", Category::Comics},          {"comics", Category::Comics},
    {"bd", Category::Comics},     {"graphic novels", Category::Comics}, {"book", Category::Books},
    {"books", Category::Books},   {"ebook", Category::Books},           {"ebooks", Category::Books},
    {"e-books", Category::Books}, {"novels", Category::Books},
};

constexpr const char* CATEGORY_FOLDER_NAMES[CATEGORY_COUNT] = {"Books", "Manga", "Comics"};
constexpr const char* FORMAT_LABELS[BOOK_FORMAT_COUNT] = {"EPUB", "XTC", "TXT", "MD"};

char asciiLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool matchesKeyword(const std::string_view name, const char* keyword) {
  const size_t len = strlen(keyword);
  if (name.size() < len) return false;
  for (size_t i = 0; i < len; i++) {
    if (asciiLower(name[i]) != keyword[i]) return false;
  }
  if (name.size() == len) return true;
  const char next = name[len];
  return next == ' ' || next == '-' || next == '_' || next == '(';
}

bool isContinuationByte(const char c) { return (static_cast<uint8_t>(c) & 0xC0) == 0x80; }

}  // namespace

void decodeHeader(const uint8_t* bytes, IndexHeader& out) { memcpy(&out, bytes, sizeof(IndexHeader)); }

bool headerValid(const IndexHeader& header, const uint32_t fileSize) {
  if (memcmp(header.magic, INDEX_MAGIC, sizeof(INDEX_MAGIC)) != 0) return false;
  if (header.version != INDEX_VERSION) return false;
  uint32_t sum = 0;
  for (const uint16_t count : header.count) sum += count;
  if (sum != header.total || header.total > MAX_BOOKS) return false;
  if (header.recordStart != HEADER_SIZE) return false;
  if (header.blobStart != header.recordStart + static_cast<uint32_t>(header.total) * RECORD_SIZE) return false;
  if (static_cast<uint64_t>(header.blobStart) + header.blobLen != header.selfSize) return false;
  return header.selfSize == fileSize;
}

uint16_t categoryStart(const IndexHeader& header, const Category category) {
  uint16_t start = 0;
  for (int c = 0; c < static_cast<int>(category); c++) start = static_cast<uint16_t>(start + header.count[c]);
  return start;
}

void decodeRecord(const uint8_t* bytes, IndexRecord& out) { memcpy(&out, bytes, sizeof(IndexRecord)); }

bool sanitizeRecord(IndexRecord& record, const IndexHeader& header) {
  record.title[TITLE_CAP - 1] = '\0';
  record.author[AUTHOR_CAP - 1] = '\0';
  if (record.titleLen >= TITLE_CAP || record.authorLen >= AUTHOR_CAP) return false;
  record.title[record.titleLen] = '\0';
  record.author[record.authorLen] = '\0';
  if (record.pathLen == 0 || record.pathLen > MAX_PATH_LEN) return false;
  if (static_cast<uint64_t>(record.pathOff) + record.pathLen > header.blobLen) return false;
  return record.format < BOOK_FORMAT_COUNT;
}

Category categoryForFolder(const std::string_view folderName, const Category inherited) {
  for (const auto& keyword : FOLDER_KEYWORDS) {
    if (matchesKeyword(folderName, keyword.word)) return keyword.category;
  }
  return inherited;
}

const char* categoryFolderName(const Category category) {
  const auto index = static_cast<size_t>(category);
  return index < CATEGORY_COUNT ? CATEGORY_FOLDER_NAMES[index] : CATEGORY_FOLDER_NAMES[0];
}

const char* formatLabel(const BookFormat format) {
  const auto index = static_cast<size_t>(format);
  return index < BOOK_FORMAT_COUNT ? FORMAT_LABELS[index] : "";
}

size_t copyTruncatedUtf8(const std::string_view src, char* out, const size_t cap) {
  if (!out || cap == 0) return 0;
  size_t len = src.size() < cap - 1 ? src.size() : cap - 1;
  // Never keep a sequence whose continuation bytes were cut off.
  while (len > 0 && len < src.size() && isContinuationByte(src[len])) len--;
  memcpy(out, src.data(), len);
  out[len] = '\0';
  return len;
}

size_t titleFromFilename(std::string_view fileName, char* out, const size_t cap) {
  if (!out || cap == 0) return 0;
  const size_t dot = fileName.rfind('.');
  std::string_view stem = (dot != std::string_view::npos && dot > 0) ? fileName.substr(0, dot) : fileName;
  while (!stem.empty() && (stem.front() == ' ' || stem.front() == '_')) stem.remove_prefix(1);
  while (!stem.empty() && (stem.back() == ' ' || stem.back() == '_')) stem.remove_suffix(1);
  if (stem.empty()) stem = fileName;
  const size_t len = copyTruncatedUtf8(stem, out, cap);
  for (size_t i = 0; i < len; i++) {
    if (out[i] == '_') out[i] = ' ';
  }
  return len;
}

uint32_t fnv1a(uint32_t hash, const void* data, const size_t len) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; i++) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

int gridStepHorizontal(const int index, const int count, const int direction) {
  if (count <= 0) return TAB_BAND;
  const int from = index < 0 ? 0 : (index >= count ? count - 1 : index);
  const int step = direction < 0 ? -1 : 1;
  return (from + step + count) % count;
}

int gridStepVertical(const int index, const int count, const int columns, const int direction) {
  if (count <= 0 || columns <= 0) return TAB_BAND;
  const int from = index < 0 ? 0 : (index >= count ? count - 1 : index);
  if (direction < 0) return from < columns ? TAB_BAND : from - columns;
  if (from + columns < count) return from + columns;
  return from / columns < (count - 1) / columns ? count - 1 : TAB_BAND;
}

int gridPageTop(const int index, const int pageItems) {
  if (index <= 0 || pageItems <= 0) return 0;
  return (index / pageItems) * pageItems;
}

CoverFit fitCover(const int bitmapWidth, const int bitmapHeight, const int boxWidth, const int boxHeight) {
  CoverFit fit{0, 0, 0.0f, 0.0f};
  // Crop an even pixel count so both sides lose the same amount; the +0.5
  // keeps floor(size * crop / 2) from rounding one pixel short.
  const auto axis = [](const int size, const int box, int& offset, float& crop) {
    if (size <= 0 || box <= 0) return;
    if (size > box) {
      int excess = size - box;
      excess += excess & 1;
      crop = (static_cast<float>(excess) + 0.5f) / static_cast<float>(size);
      offset = (box - (size - excess)) / 2;
    } else {
      offset = (box - size) / 2;
    }
  };
  axis(bitmapWidth, boxWidth, fit.offsetX, fit.cropX);
  axis(bitmapHeight, boxHeight, fit.offsetY, fit.cropY);
  return fit;
}

}  // namespace library
