#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "LibraryText.h"

using library::authorKey;
using library::fold;
using library::SearchQuery;

namespace {

// The same text in both Unicode normal forms. Every fold test runs both, because
// a card holding only one form makes a one-sided test pass by accident — which
// is exactly how a fold built on utf8ComposeNfc() survives until the first file
// arrives from the other kind of machine.
struct NormalisationPair {
  const char* nfc;  // precomposed: e-acute is one codepoint
  const char* nfd;  // decomposed: e followed by a combining acute
  const char* expected;
};

constexpr NormalisationPair PAIRS[] = {
    {"pand\xC3\xA9mie", "pande\xCC\x81mie", "pandemie"},
    {"\xC3\x89"
     "clipse totale",
     "E\xCC\x81"
     "clipse totale",
     "eclipse totale"},
    {"M\xC3\xA9moires", "Me\xCC\x81moires", "memoires"},
    {"Ang\xC3\xA9lina", "Ange\xCC\x81lina", "angelina"},
    {"R\xC3\xA9"
     "camier",
     "Re\xCC\x81"
     "camier",
     "recamier"},
    {"Derri\xC3\xA8re les collines", "Derrie\xCC\x80re les collines", "derriere les collines"},
};

}  // namespace

TEST(LibraryFold, BothNormalisationsAgree) {
  for (const auto& p : PAIRS) {
    EXPECT_EQ(fold(p.nfc), p.expected) << "NFC input: " << p.nfc;
    EXPECT_EQ(fold(p.nfd), p.expected) << "NFD input: " << p.nfd;
    EXPECT_EQ(fold(p.nfc), fold(p.nfd)) << "forms disagree for " << p.expected;
  }
}

TEST(LibraryFold, LettersWithoutCanonicalDecomposition) {
  // These have no NFD form at all, so a decompose-only fold silently deletes
  // them. "Søren" losing its last letter is the case that motivated the map.
  EXPECT_EQ(fold("S\xC3\xB8ren"), "soren");
  EXPECT_EQ(fold("\xC3\x98rsted"), "orsted");
  EXPECT_EQ(fold("\xC3\x86"
                 "sop"),
            "aesop");
  EXPECT_EQ(fold("Stra\xC3\x9F"
                 "e"),
            "strasse");
  EXPECT_EQ(fold("\xC5\x81odz"), "lodz");
  EXPECT_EQ(fold("s\xC5\x93ur"), "soeur");
}

TEST(LibraryFold, ApostrophesSurviveInNamesAndElisions) {
  // U+2019 is what exporters actually emit; folding it to a space would split
  // "O'Malley" into two tokens and change both its sort place and its search.
  // ("Malley", not "Brien": C++ hex escapes are greedy, so \x99B would parse
  // as the single escape 0x99B.)
  EXPECT_EQ(fold("O\xE2\x80\x99Malley"), "o'malley");
  EXPECT_EQ(fold("O'Malley"), "o'malley");
  EXPECT_EQ(fold("L\xE2\x80\x99\xC3\x89n\xC3\xA9ide"), "l'eneide");
}

TEST(LibraryFold, TypographicDashesAndQuotesFoldLikeTheirAsciiForms) {
  // An em dash used to stay inside the fold word while an ASCII hyphen broke
  // it, so the same title written both ways sorted and searched differently.
  EXPECT_EQ(library::fold("a\u2014b"), library::fold("a-b"));
  EXPECT_EQ(library::fold("a\u2013b"), library::fold("a-b"));
  EXPECT_EQ(library::fold("\u201Cquoted\u201D"), library::fold("\"quoted\""));
}

TEST(LibraryFold, PunctuationSeparatesAndSpaceRunsCollapse) {
  EXPECT_EQ(fold("Le juge Untel.T2.Le po\xC3\xA8me"), "le juge untel t2 le poeme");
  EXPECT_EQ(fold("  spaced   out  "), "spaced out");
  EXPECT_EQ(fold("a---b"), "a b");
  EXPECT_EQ(fold("2085 _ Artificial"), "2085 artificial");
  EXPECT_EQ(fold(""), "");
  EXPECT_EQ(fold("!!!"), "");
}

TEST(LibraryFold, PreservesHebrewLettersAndDropsNiqqud) {
  EXPECT_EQ(fold("\u05E9\u05B8\u05C1\u05DC\u05D5\u05B9\u05DD"), "\u05E9\u05DC\u05D5\u05DD");
  EXPECT_TRUE(library::SearchQuery("\u05E9\u05DC").matches(fold("\u05E9\u05DC\u05D5\u05DD \u05E2\u05D5\u05DC\u05DD")));
  EXPECT_FALSE(authorKey("\u05E2\u05DE\u05D5\u05E1 \u05E2\u05D5\u05D6").empty());
}

TEST(LibraryFold, GroupInitialUsesUnicodeLettersAndBucketsNumbers) {
  EXPECT_EQ(library::foldedGroupInitial(fold("Alpha", true)), static_cast<uint32_t>('a'));
  EXPECT_EQ(library::foldedGroupInitial(fold("\u05E9\u05DC\u05D5\u05DD", true)), 0x05E9u);
  EXPECT_EQ(library::foldedGroupInitial(fold("\u041A\u043D\u0438\u0433\u0430", true)), 0x041Au);
  EXPECT_EQ(library::foldedGroupInitial(fold("\u4E66", true)), 0x4E66u);
  EXPECT_EQ(library::foldedGroupInitial(fold("2085", true)), 0u);
  EXPECT_EQ(library::foldedGroupInitial(fold("\u0662\u0660\u0668\u0665", true)), 0u);
  EXPECT_EQ(library::foldedGroupInitial(fold("!!!", true)), 0u);
}

TEST(LibraryFold, ArticleStrippingOnlyWhenAsked) {
  EXPECT_EQ(fold("The Iliad"), "the iliad");
  EXPECT_EQ(fold("The Iliad", true), "iliad");
  EXPECT_EQ(fold("Les Mis\xC3\xA9rables", true), "miserables");
  EXPECT_EQ(fold("L\xE2\x80\x99\xC3\x89n\xC3\xA9ide", true), "eneide");
  // A title that IS an article-like word must not vanish.
  EXPECT_EQ(fold("The", true), "the");
}

TEST(LibraryAuthorKey, OrderAndPunctuationDoNotMatter) {
  const std::string expected = authorKey("Lu Xun");
  EXPECT_FALSE(expected.empty());
  for (const char* spelling : {"Lu, Xun", "Xun, Lu", "Lu Xun_", "Lu Xun [Xun, Lu]", "  lu   xun  "}) {
    EXPECT_EQ(authorKey(spelling), expected) << spelling;
  }
}

TEST(LibraryAuthorKey, InitialsAreIgnored) {
  EXPECT_EQ(authorKey("Herbert G Wells"), authorKey("Herbert Wells"));
  EXPECT_EQ(authorKey("Wells, Herbert G."), authorKey("Herbert Wells"));
}

TEST(LibraryAuthorKey, SecondaryAuthorsAndBracketsDropped) {
  EXPECT_EQ(authorKey("Emile Erckmann; Alexandre Chatrian"), authorKey("Emile Erckmann"));
  EXPECT_EQ(authorKey("George Sand [Sand, George]"), authorKey("George Sand"));
}

TEST(LibraryAuthorKey, FilesystemUnderscoreStandsInForAFullStop) {
  // The one input where the key's cleanup and cleanPersonName's differ before
  // folding: an underscore the filesystem took instead of a full stop. Both
  // reduce to the same initial, which fold() then drops as a one-letter token.
  EXPECT_EQ(authorKey("Herbert G_ Wells"), authorKey("Herbert Wells"));
  EXPECT_EQ(authorKey("Wells_ Herbert"), authorKey("Herbert Wells"));
}

TEST(LibraryAuthorKey, DistinctPeopleDoNotCollide) {
  EXPECT_NE(authorKey("Mary Wollstonecraft"), authorKey("Charlotte Bronte"));
  EXPECT_NE(authorKey("Victor Hugo"), authorKey("Jules Verne"));
}

TEST(LibraryAuthorKey, FitsTheRecordFieldWithoutCollapsingToAForename) {
  const std::string key = authorKey("Bartholomew Fitzgerald Wellington");
  ASSERT_FALSE(key.empty());
  EXPECT_LE(key.size(), library::AUTHOR_KEY_MAX_BYTES);
  EXPECT_NE(key.back(), ' ');

  // Sorting puts a short forename first, so cutting on a token boundary would
  // reduce this to "mary" and merge every Alex in the library. The byte cut must
  // keep enough of the surname to discriminate.
  const std::string mary = authorKey("Wollstonecraft, Mary");
  EXPECT_GT(mary.size(), 5u);
  EXPECT_NE(mary, "mary");
  EXPECT_NE(mary, authorKey("Mary Trevelyan"));

  // A truncated key stays a prefix of the untruncated one, so grouping is stable
  // however long the name is.
  EXPECT_EQ(authorKey("Wollstonecraft, Maryse").rfind("mary", 0), 0u);

  EXPECT_FALSE(authorKey("Nebuchadnezzarson").empty());
  EXPECT_TRUE(authorKey("").empty());
  EXPECT_TRUE(authorKey("Q. X. Z.").empty());  // initials only: no identity
}

// --- SearchQuery -------------------------------------------------------------
//
// Cases taken from the shape of the accented and
// apostrophised titles real cards hold — what a naive matcher gets wrong.

namespace {

// One book searched the way the Library does it: the stored title fold (article
// stripped), then the author, then the file name without its extension.
bool finds(const char* query, const char* title, const char* author = "", const char* fileStem = "") {
  const SearchQuery search(query);
  uint32_t found = 0;
  return search.markFound(fold(title, true), found) || search.markFound(fold(author), found) ||
         search.markFound(fold(fileStem), found);
}

std::vector<std::string> wordsOf(const char* query) {
  const SearchQuery search(query);
  return std::vector<std::string>(search.words().begin(), search.words().end());
}

}  // namespace

TEST(LibrarySearch, EmptyQueryMatchesEverything) {
  EXPECT_TRUE(SearchQuery("").empty());
  EXPECT_TRUE(SearchQuery("").matches(fold("Wuthering Heights")));
  EXPECT_TRUE(SearchQuery("!!!").matches(fold("Wuthering Heights")));
}

TEST(LibrarySearch, WholeWordMatches) { EXPECT_TRUE(SearchQuery("heights").matches(fold("Wuthering Heights"))); }

TEST(LibrarySearch, PrefixOfOneWordIsEnough) { EXPECT_TRUE(SearchQuery("hei").matches(fold("Wuthering Heights"))); }

TEST(LibrarySearch, EveryWordMayBeAbbreviated) {
  EXPECT_TRUE(SearchQuery("wut hei").matches(fold("Wuthering Heights")));
}

TEST(LibrarySearch, WordsNeedNotBeInOrder) {
  EXPECT_TRUE(SearchQuery("heights wuthering").matches(fold("Wuthering Heights")));
}

TEST(LibrarySearch, EveryWordMustHit) {
  EXPECT_FALSE(SearchQuery("wuthering blue").matches(fold("Wuthering Heights")));
}

// A substring, not only a word start: typing any part of a name is enough.
TEST(LibrarySearch, MidWordMatches) { EXPECT_TRUE(SearchQuery("eights").matches(fold("Wuthering Heights"))); }

TEST(LibrarySearch, AccentsAreIgnoredOnBothSides) {
  EXPECT_TRUE(SearchQuery("eneide").matches(fold("L'Énéide")));
  EXPECT_TRUE(SearchQuery("énéide").matches(fold("L'Eneide")));
  EXPECT_TRUE(SearchQuery("eluard").matches(fold("Éluard")));
}

TEST(LibrarySearch, ApostrophesAreIgnored) {
  EXPECT_TRUE(SearchQuery("cote").matches(fold("Le bureau d'à côté")));
  EXPECT_TRUE(SearchQuery("omalley").matches(fold("O'Malley")));
  EXPECT_TRUE(SearchQuery("o'malley").matches(fold("OMalley")));
}

TEST(LibrarySearch, CaseIsIgnored) { EXPECT_TRUE(SearchQuery("HEIGHTS").matches(fold("Wuthering Heights"))); }

TEST(LibrarySearch, LeadingArticleIsDroppedLikeTheStoredFold) {
  EXPECT_EQ(wordsOf("The Hobbit"), (std::vector<std::string>{"hobbit"}));
  EXPECT_TRUE(finds("the hobbit", "The Hobbit"));
  EXPECT_TRUE(finds("L\xE2\x80\x99\xC3\x89n\xC3\xA9ide", "L\xE2\x80\x99\xC3\x89n\xC3\xA9ide"));
}

// Whitespace splits words; punctuation typed inside one joins it.
TEST(LibrarySearch, WordsSplitOnWhitespaceOnly) {
  EXPECT_EQ(wordsOf("Black Jack"), (std::vector<std::string>{"black", "jack"}));
  EXPECT_EQ(wordsOf("  BlackJack_EPUB "), (std::vector<std::string>{"blackjackepub"}));
  EXPECT_EQ(wordsOf("x-men"), (std::vector<std::string>{"xmen"}));
  EXPECT_TRUE(SearchQuery("x-men").matches(fold("X-Men")));
}

// The report that motivated the forgiving rules.
TEST(LibrarySearch, BlackJackFindsEverySpelling) {
  constexpr const char* EPUB = "BlackJack_EPUB";
  constexpr const char* XTC = "BlachJack_XTC";
  constexpr const char* REGARDS = "Give My Regards to Black Jack";

  for (const char* query : {"Black Jack", "black jack", "BLACKJACK"}) {
    EXPECT_TRUE(finds(query, EPUB, "", EPUB)) << query;
    EXPECT_TRUE(finds(query, REGARDS, "", REGARDS)) << query;
    // "blachjackxtc" has "jack" but no "black": the misspelling is not forgiven.
    EXPECT_FALSE(finds(query, XTC, "", XTC)) << query;
  }

  EXPECT_FALSE(finds("regards black", EPUB, "", EPUB));
  EXPECT_FALSE(finds("regards black", XTC, "", XTC));
  EXPECT_TRUE(finds("regards black", REGARDS, "", REGARDS));

  EXPECT_FALSE(finds("xtc", EPUB, "", EPUB));
  EXPECT_TRUE(finds("xtc", XTC, "", XTC));
  EXPECT_FALSE(finds("xtc", REGARDS, "", REGARDS));

  EXPECT_TRUE(finds("jack", XTC, "", XTC));
}

TEST(LibrarySearch, FileNameIsSearchedWhenTheTitleDiffers) {
  EXPECT_TRUE(finds("black jack", "Burakku Jakku ni Yoroshiku", "Shuho Sato", "Give My Regards to Black Jack"));
  EXPECT_FALSE(finds("black jack", "Burakku Jakku ni Yoroshiku", "Shuho Sato", "Volume 1"));
}

TEST(LibrarySearch, WordsMayComeFromDifferentFields) {
  EXPECT_TRUE(finds("emma austen", "Emma", "Jane Austen"));
  EXPECT_TRUE(finds("austen epub", "Emma", "Jane Austen", "Emma_EPUB"));
  EXPECT_FALSE(finds("emma bronte", "Emma", "Jane Austen", "Emma"));
}

// A 32-word query fills the found mask; later words are ignored.
TEST(LibrarySearch, WordCountIsCapped) {
  std::string many;
  for (int i = 0; i < 40; i++) many += "b ";
  const SearchQuery search(many);
  EXPECT_EQ(search.words().size(), SearchQuery::MAX_WORDS);
  EXPECT_TRUE(search.matches(fold("b")));
  EXPECT_FALSE(search.matches(fold("c")));
}

// The stored fold is capped at 96 bytes, so a title word beyond that cannot be
// found there. The file name is still searched, which usually covers it.
TEST(LibrarySearch, LongTitlesAreOnlySearchableWithinTheStoredFold) {
  const std::string longTitle(120, 'a');
  const std::string folded = fold(longTitle + " needle").substr(0, 96);
  EXPECT_FALSE(SearchQuery("needle").matches(folded));
}

// --- inverted author names ---------------------------------------------------

TEST(CleanPersonName, InvertedNameIsTurnedRound) {
  EXPECT_EQ(library::cleanPersonName("Austen, Jane"), "Jane Austen");
  EXPECT_EQ(library::cleanPersonName("Wollstonecraft, Mary"), "Mary Wollstonecraft");
}

TEST(CleanPersonName, PlainNameIsUntouched) { EXPECT_EQ(library::cleanPersonName("Emily Bronte"), "Emily Bronte"); }

// Two commas mean a suffix or a list, not an inversion — leave it alone rather
// than scramble it.
TEST(CleanPersonName, MultipleCommasAreLeftAlone) {
  // The trailing full stop is stripped by the existing noise rules.
  EXPECT_EQ(library::cleanPersonName("Smith, John, Jr."), "Smith, John, Jr");
}

TEST(CleanPersonName, DanglingCommaIsNotAnInversion) { EXPECT_EQ(library::cleanPersonName("Austen,"), "Austen"); }

// --- surnameKey --------------------------------------------------------------

TEST(SurnameKey, SurnameLeadsThenGivenNames) {
  EXPECT_EQ(library::surnameKey("Herman Melville"), "melville herman");
  EXPECT_EQ(library::surnameKey("Mary Wollstonecraft"), "wollstonecraft mary");
}

TEST(SurnameKey, SingleWordKeysOnItself) { EXPECT_EQ(library::surnameKey("Voltaire"), "voltaire"); }

TEST(SurnameKey, AccentsAreFolded) { EXPECT_EQ(library::surnameKey("Paul Éluard"), "eluard paul"); }

TEST(SurnameKey, ThreeWordNamesTakeTheLast) { EXPECT_EQ(library::surnameKey("Herbert G. Wells"), "wells herbert g"); }

TEST(SurnameKey, EmptyStaysEmpty) { EXPECT_EQ(library::surnameKey(""), ""); }

// The whole point of keying off the DISPLAY name: the spelling vote has already
// made every book by one author show one name, so a group cannot land in two
// places even though "Victor Hugo" and "Hugo Victor" both exist in the wild.
TEST(SurnameKey, HarmonisedDisplayNameKeepsAGroupTogether) {
  EXPECT_NE(library::surnameKey("Victor Hugo"), library::surnameKey("Hugo Victor"));
}

TEST(LibraryPath, RootDoesNotGainASecondSeparator) {
  EXPECT_EQ(library::joinLibraryPath("/", "book.epub"), "/book.epub");
  EXPECT_EQ(library::joinLibraryPath("", "book.epub"), "/book.epub");
}

TEST(LibraryPath, NestedFolderGetsOneSeparator) {
  EXPECT_EQ(library::joinLibraryPath("/Books", "book.epub"), "/Books/book.epub");
}
