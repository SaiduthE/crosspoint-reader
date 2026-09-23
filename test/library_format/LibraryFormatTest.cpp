#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "LibraryFormat.h"

using namespace library;

namespace {

IndexHeader makeHeader(const uint16_t books, const uint16_t manga, const uint16_t comics, const uint32_t blobLen) {
  IndexHeader header{};
  memcpy(header.magic, INDEX_MAGIC, sizeof(INDEX_MAGIC));
  header.version = INDEX_VERSION;
  header.count[0] = books;
  header.count[1] = manga;
  header.count[2] = comics;
  header.total = static_cast<uint16_t>(books + manga + comics);
  header.recordStart = HEADER_SIZE;
  header.blobStart = HEADER_SIZE + header.total * RECORD_SIZE;
  header.blobLen = blobLen;
  header.selfSize = header.blobStart + blobLen;
  return header;
}

}  // namespace

TEST(LibraryCategory, KeywordsMatchWholeNameOrDelimitedPrefix) {
  EXPECT_EQ(categoryForFolder("Manga", Category::Books), Category::Manga);
  EXPECT_EQ(categoryForFolder("MANGAS", Category::Books), Category::Manga);
  EXPECT_EQ(categoryForFolder("manhwa (2024)", Category::Books), Category::Manga);
  EXPECT_EQ(categoryForFolder("Manhua_Collection", Category::Books), Category::Manga);
  EXPECT_EQ(categoryForFolder("Comics - DC", Category::Books), Category::Comics);
  EXPECT_EQ(categoryForFolder("comic", Category::Books), Category::Comics);
  EXPECT_EQ(categoryForFolder("BD", Category::Books), Category::Comics);
  EXPECT_EQ(categoryForFolder("Graphic Novels", Category::Books), Category::Comics);
  EXPECT_EQ(categoryForFolder("eBooks", Category::Manga), Category::Books);
  EXPECT_EQ(categoryForFolder("E-Books", Category::Comics), Category::Books);
  EXPECT_EQ(categoryForFolder("novels-classic", Category::Manga), Category::Books);
}

TEST(LibraryCategory, NonMatchingFoldersInherit) {
  EXPECT_EQ(categoryForFolder("Mangaka", Category::Books), Category::Books);
  EXPECT_EQ(categoryForFolder("My Manga", Category::Books), Category::Books);
  EXPECT_EQ(categoryForFolder("bdx", Category::Manga), Category::Manga);
  EXPECT_EQ(categoryForFolder("One Piece", Category::Manga), Category::Manga);
  EXPECT_EQ(categoryForFolder("", Category::Comics), Category::Comics);
  EXPECT_EQ(categoryForFolder("Bookshelf", Category::Comics), Category::Comics);
}

TEST(LibraryCategory, FolderNamesAndLabels) {
  EXPECT_STREQ(categoryFolderName(Category::Manga), "Manga");
  EXPECT_STREQ(categoryFolderName(Category::Comics), "Comics");
  EXPECT_STREQ(formatLabel(BookFormat::Epub), "EPUB");
  EXPECT_STREQ(formatLabel(BookFormat::Markdown), "MD");
}

TEST(LibraryTitle, StripsExtensionAndUnderscores) {
  char title[TITLE_CAP];
  EXPECT_EQ(titleFromFilename("The_Hobbit.epub", title, sizeof(title)), 10u);
  EXPECT_STREQ(title, "The Hobbit");
  titleFromFilename("vol.01.xtch", title, sizeof(title));
  EXPECT_STREQ(title, "vol.01");
  titleFromFilename("_notes_.md", title, sizeof(title));
  EXPECT_STREQ(title, "notes");
  titleFromFilename("README", title, sizeof(title));
  EXPECT_STREQ(title, "README");
}

TEST(LibraryTitle, TruncatesOnUtf8Boundary) {
  // 36 x "é" (2 bytes each) = 72 bytes; only 71 fit, so the last one is dropped whole.
  std::string name;
  for (int i = 0; i < 36; i++) name += "\xC3\xA9";
  name += ".epub";
  char title[TITLE_CAP];
  const size_t len = titleFromFilename(name, title, sizeof(title));
  EXPECT_EQ(len, 70u);
  EXPECT_EQ(strlen(title), 70u);
  EXPECT_EQ(static_cast<uint8_t>(title[69]), 0xA9);

  char small[4];
  EXPECT_EQ(copyTruncatedUtf8("\xE3\x81\x82\xE3\x81\x84", small, sizeof(small)), 3u);  // one 3-byte kana
  EXPECT_EQ(copyTruncatedUtf8("abcdef", small, sizeof(small)), 3u);
  EXPECT_STREQ(small, "abc");
}

TEST(LibraryHeader, ValidatesStructureAgainstFileSize) {
  const IndexHeader header = makeHeader(2, 1, 0, 40);
  EXPECT_TRUE(headerValid(header, header.selfSize));
  EXPECT_FALSE(headerValid(header, header.selfSize - 1));  // truncated write

  IndexHeader badMagic = header;
  badMagic.magic[0] = 'X';
  EXPECT_FALSE(headerValid(badMagic, header.selfSize));

  IndexHeader badCount = header;
  badCount.total = 4;
  EXPECT_FALSE(headerValid(badCount, header.selfSize));

  IndexHeader badVersion = header;
  badVersion.version = INDEX_VERSION + 1;
  EXPECT_FALSE(headerValid(badVersion, header.selfSize));

  uint8_t bytes[HEADER_SIZE];
  memcpy(bytes, &header, sizeof(bytes));
  IndexHeader decoded{};
  decodeHeader(bytes, decoded);
  EXPECT_TRUE(headerValid(decoded, header.selfSize));
  EXPECT_EQ(categoryStart(decoded, Category::Books), 0);
  EXPECT_EQ(categoryStart(decoded, Category::Manga), 2);
  EXPECT_EQ(categoryStart(decoded, Category::Comics), 3);
}

TEST(LibraryRecord, SanitizeBoundsChecksAndTerminates) {
  const IndexHeader header = makeHeader(1, 0, 0, 20);
  IndexRecord record{};
  record.pathOff = 0;
  record.pathLen = 20;
  record.format = static_cast<uint8_t>(BookFormat::Xtc);
  record.titleLen = 3;
  memset(record.title, 'x', sizeof(record.title));
  EXPECT_TRUE(sanitizeRecord(record, header));
  EXPECT_STREQ(record.title, "xxx");
  EXPECT_STREQ(record.author, "");

  IndexRecord outOfBlob = record;
  outOfBlob.pathOff = 1;
  EXPECT_FALSE(sanitizeRecord(outOfBlob, header));
  IndexRecord badTitle = record;
  badTitle.titleLen = TITLE_CAP;
  EXPECT_FALSE(sanitizeRecord(badTitle, header));
  IndexRecord badFormat = record;
  badFormat.format = BOOK_FORMAT_COUNT;
  EXPECT_FALSE(sanitizeRecord(badFormat, header));

  // Unaligned source buffer: decode must go through memcpy.
  uint8_t raw[RECORD_SIZE + 1];
  memcpy(raw + 1, &record, RECORD_SIZE);
  IndexRecord decoded{};
  decodeRecord(raw + 1, decoded);
  EXPECT_EQ(decoded.pathLen, 20);
  EXPECT_STREQ(decoded.title, "xxx");
}

TEST(LibraryHash, FnvIsOrderSensitive) {
  const uint32_t ab = fnv1a(fnv1a(FNV_OFFSET, "a", 1), "b", 1);
  const uint32_t ba = fnv1a(fnv1a(FNV_OFFSET, "b", 1), "a", 1);
  EXPECT_NE(ab, ba);
  EXPECT_EQ(fnv1a(FNV_OFFSET, "", 0), FNV_OFFSET);
  EXPECT_EQ(fnv1a(FNV_OFFSET, "a", 1), 0xE40C292Cu);  // FNV-1a 32 reference value
}

TEST(LibraryGrid, HorizontalWraps) {
  EXPECT_EQ(gridStepHorizontal(0, 5, 1), 1);
  EXPECT_EQ(gridStepHorizontal(4, 5, 1), 0);
  EXPECT_EQ(gridStepHorizontal(0, 5, -1), 4);
  EXPECT_EQ(gridStepHorizontal(0, 0, 1), TAB_BAND);
}

TEST(LibraryGrid, VerticalReachesTabBandAndShortLastRow) {
  // 3 columns, 8 items: rows [0 1 2] [3 4 5] [6 7]
  EXPECT_EQ(gridStepVertical(1, 8, 3, -1), TAB_BAND);
  EXPECT_EQ(gridStepVertical(4, 8, 3, -1), 1);
  EXPECT_EQ(gridStepVertical(1, 8, 3, 1), 4);
  EXPECT_EQ(gridStepVertical(5, 8, 3, 1), 7);  // short last row: land on the last item
  EXPECT_EQ(gridStepVertical(7, 8, 3, 1), TAB_BAND);
  EXPECT_EQ(gridStepVertical(6, 8, 3, 1), TAB_BAND);
  EXPECT_EQ(gridStepVertical(0, 0, 3, 1), TAB_BAND);
}

TEST(LibraryGrid, PageTop) {
  EXPECT_EQ(gridPageTop(0, 9), 0);
  EXPECT_EQ(gridPageTop(8, 9), 0);
  EXPECT_EQ(gridPageTop(9, 9), 9);
  EXPECT_EQ(gridPageTop(20, 6), 18);
}

TEST(LibraryCover, CropsOvershootAndCentresUndershoot) {
  // Wide thumbnail: 100x144 into 86x144 -> crop 14 px, 7 per side.
  CoverFit fit = fitCover(100, 144, 86, 144);
  EXPECT_EQ(static_cast<int>(100 * fit.cropX / 2.0f), 7);
  EXPECT_EQ(fit.offsetX, 0);
  EXPECT_EQ(fit.cropY, 0.0f);

  // Odd excess rounds up to an even crop and leaves a centred 1 px gap.
  fit = fitCover(91, 144, 86, 144);
  const int perSide = static_cast<int>(91 * fit.cropX / 2.0f);
  EXPECT_EQ(perSide, 3);
  EXPECT_LE(91 - 2 * perSide, 86);
  EXPECT_EQ(fit.offsetX, 0);

  // Tall thumbnail: 86x160 -> crop height.
  fit = fitCover(86, 160, 86, 144);
  EXPECT_EQ(static_cast<int>(160 * fit.cropY / 2.0f), 8);
  EXPECT_EQ(fit.cropX, 0.0f);

  // Small bitmap: centred, no crop.
  fit = fitCover(60, 100, 86, 144);
  EXPECT_EQ(fit.offsetX, 13);
  EXPECT_EQ(fit.offsetY, 22);
  EXPECT_EQ(fit.cropX, 0.0f);
  EXPECT_EQ(fit.cropY, 0.0f);
}
