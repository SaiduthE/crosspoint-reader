#pragma once

// Text normalisation for the Library index: the fold used for search and sort
// keys, and the author key that merges spellings of one person.
//
// Nothing here reads a filename. Titles and authors come from the book's own
// metadata; a book whose metadata carries no title falls back to its filename
// stem, unparsed, and one with no author is grouped under Unknown.
//
// Pure code over UTF-8, no hardware and no allocation beyond the strings it
// returns or holds, so the whole unit is host-testable (test/library_text).
//
// Design notes that are easy to get wrong and were measured on a real card
// (docs/superpowers/specs/2026-08-05-addendum-a-findability.md, A2.1-A2.8):
//
//   * The fold DECOMPOSES. `utf8ComposeNfc()` goes the other way, so a fold
//     built on it passes on a card holding only decomposed text and then mangles
//     the first precomposed file to arrive from Windows or Calibre. Both forms
//     must produce the same output, and the host tests assert exactly that.
//   * Some letters have no canonical decomposition at all — U+00F8 (ø) is a
//     distinct letter, not o-with-stroke — so a decompose-only fold turns
//     "Søren" into "Sren". Those need an explicit map.
//   * The author KEY sorts its tokens, so for identity purposes name order
//     stops mattering and no First/Last guess is needed there. Display-side
//     helpers do use narrow rules — cleanPersonName inverts a single-comma
//     "Last, First", surnameKey takes the last word — because showing
//     "Austen, Jane" and "Jane Austen" as two people is worse than the guess.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace library {

// Longest author key written into an index record. Sized so a key fits the
// record's fixed field; measured to collide 0 times over a 69-book library.
inline constexpr size_t AUTHOR_KEY_MAX_BYTES = 12;

// Join an indexed folder with its basename using exactly one separator. The
// root folder is stored as "/"; treating it like a normal directory would
// reconstruct "//book.epub", which hashes differently and may not open.
std::string joinLibraryPath(std::string_view folder, std::string_view name);

// Casefold and strip diacritics for matching and sorting.
//
// Maps a handful of Latin letters that have no canonical decomposition,
// decomposes Latin accents, lowercases ASCII, preserves Unicode letters and
// numbers, and turns punctuation into a single space. Combining marks are
// dropped. Apostrophes survive as ASCII '\'' so names and elisions keep their
// shape.
//
// `stripArticle` additionally removes one leading article ("the ", "le ", "la ",
// ...) — correct for sort keys and search text, wrong for anything displayed.
std::string fold(std::string_view text, bool stripArticle = false);

// fold() into a caller-owned string, reusing its capacity, for loops that would
// otherwise allocate once per book. `text` must not view `out`.
void foldInto(std::string_view text, std::string& out, bool stripArticle = false);

// First letter of an already-folded sort key, or 0 when the key starts with a
// number/non-letter. The Library renders 0 as its shared '#' group.
uint32_t foldedGroupInitial(std::string_view folded);

// Tidy a person's name for DISPLAY, without reordering it.
//
// Drops bracketed spans ("George Sand [Sand, George]"), everything after a
// multi-author separator, and the trailing underscores and punctuation that
// exporters leave behind ("Lu Xun_", "Herbert G_ Wells"). An underscore
// between letters becomes a full stop, since that is what it replaced in a name
// a filesystem refused to hold.
//
// A single-comma "Austen, Jane" IS turned round into "Jane Austen" — with one
// book per author the spelling vote has no majority to settle it, so the comma
// is the one signal acted on here. Multi-comma names ("Smith, John, Jr.") and
// author lists are left exactly as written. Harmonising the several spellings
// of one person is done by picking the most common one that actually occurs,
// which needs the whole library and so belongs to the index build.
std::string cleanPersonName(std::string_view author);

// Order-insensitive identity for one person, at most AUTHOR_KEY_MAX_BYTES.
//
// Drops bracketed spans and everything after ';' (multi-author separator), folds,
// drops single-character tokens (initials), sorts the remaining tokens and joins
// them. "Lu, Xun", "Xun, Lu" and "Lu Xun [Xun, Lu]" all
// collapse to one key. Truncation uses the longest complete UTF-8 byte prefix,
// not a token boundary: the sort puts a short forename first, so a whole-token cut would reduce
// "Wollstonecraft, Mary" to "alex" and merge every Alex in the library; the byte
// cut keeps "mary wollsto", still a prefix of the full key.
std::string authorKey(std::string_view author);

// A Library search, folded and split once per query.
//
// Forgiving on purpose: case, accents, spaces, apostrophes and punctuation count
// for nothing on either side, and a word may sit anywhere inside a field. So
// "black jack" finds "BlackJack_EPUB" (blackjackepub) and "Give My Regards to
// Black Jack". Words split on whitespace only; punctuation typed inside a word
// joins it, so "x-men" is the one word "xmen". A leading article is dropped, as
// it is from the stored title folds.
//
// Every word must be found, each in any field of the book (title, author, file
// name) and in any order. A query with no words matches everything.
class SearchQuery {
 public:
  // One bit per word in markFound()'s mask; later words are ignored.
  static constexpr size_t MAX_WORDS = 32;

  explicit SearchQuery(std::string_view query);
  // The word views point into `key`, so a copy or move would leave them dangling.
  SearchQuery(const SearchQuery&) = delete;
  SearchQuery& operator=(const SearchQuery&) = delete;

  bool empty() const { return tokens.empty(); }
  const std::vector<std::string_view>& words() const { return tokens; }

  // Sets bit i of `found` for each word i that `folded` contains; true once
  // every word is set. `folded` is fold() output or a record's stored fold.
  // Start `found` at 0 for each book and pass it through each of its fields.
  bool markFound(std::string_view folded, uint32_t& found) const;
  bool matches(std::string_view folded) const {
    uint32_t found = 0;
    return markFound(folded, found);
  }

 private:
  std::string key;  // the words, compacted, space-separated
  std::vector<std::string_view> tokens;
};

// Ordering key for a shelf sorted by author: surname first, then the rest.
// "Herman Melville" becomes "melville herman", so the shelf reads C where a library
// would put it.
//
// Deliberately NOT the same key as authorKey(). That one sorts a name's words so
// that "Victor Hugo" and "Hugo Victor" hash alike and are recognised as one person;
// it is a GROUPING key and would be wrong to order by. This is derived from the
// DISPLAY name instead, which is safe because the spelling vote has already made
// every book by one author show the same name — so a group cannot split across
// two places on the shelf.
//
// The last word is taken as the surname. That is right for the western names on
// this card and wrong for some others, which is a limit worth stating rather than
// hiding: a single word name simply keys on itself.
std::string surnameKey(std::string_view displayAuthor);

}  // namespace library
