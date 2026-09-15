#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "Battleship.h"
#include "Codenames.h"
#include "GameSession.h"
#include "Ludo.h"
#include "PartyGames.h"
#include "Undercover.h"
#include "WordBank.h"

using namespace party;

namespace {

Action verb(const Verb v, const int16_t a = -1, const int16_t b = -1, const int16_t c = -1,
            const char* text = nullptr) {
  Action action;
  action.verb = v;
  action.a = a;
  action.b = b;
  action.c = c;
  if (text) snprintf(action.text, sizeof(action.text), "%s", text);
  return action;
}

// Seats `count` players and returns the session's tokens (token == seat + 1).
void seat(GameSession& session, const uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    char name[8];
    snprintf(name, sizeof(name), "P%u", i + 1);
    ASSERT_EQ(session.join(name, i + 1u, 0), static_cast<int8_t>(i));
  }
}

}  // namespace

// --- primitives --------------------------------------------------------------

TEST(Rng, IsDeterministicAndBounded) {
  Rng a(12345);
  Rng b(12345);
  for (int i = 0; i < 1000; i++) {
    const uint32_t value = a.next();
    EXPECT_EQ(value, b.next());
  }
  Rng c(7);
  for (int i = 0; i < 1000; i++) EXPECT_LT(c.below(6), 6u);
  EXPECT_EQ(c.below(0), 0u);
}

TEST(Rng, ShufflePreservesTheMultiset) {
  uint8_t values[8];
  for (uint8_t i = 0; i < 8; i++) values[i] = i;
  Rng rng(99);
  rng.shuffle(values, 8);
  std::set<uint8_t> seen(values, values + 8);
  EXPECT_EQ(seen.size(), 8u);
}

TEST(SanitizeName, DropsWhatWouldBreakTheViews) {
  char out[MAX_NAME_LEN + 1];
  EXPECT_EQ(sanitizeName("An\"na\\", out, sizeof(out)), 4u);
  EXPECT_STREQ(out, "Anna");
  EXPECT_EQ(sanitizeName("<script>", out, sizeof(out)), 6u);
  EXPECT_STREQ(out, "script");
  sanitizeName("averylongplayername", out, sizeof(out));
  EXPECT_EQ(strlen(out), MAX_NAME_LEN);
  EXPECT_EQ(sanitizeName("   ", out, sizeof(out)), 0u);
  EXPECT_EQ(sanitizeName(nullptr, out, sizeof(out)), 0u);
}

TEST(JsonBuf, EscapesAndFlagsTruncation) {
  char small[8];
  JsonBuf tiny(small, sizeof(small));
  tiny.str("aaaaaaaaaaaa");
  EXPECT_TRUE(tiny.overflowed());
  EXPECT_LT(strlen(small), sizeof(small));

  char big[64];
  JsonBuf json(big, sizeof(big));
  json.ch('{');
  json.keyStr("k", "a\"b");
  json.ch('}');
  EXPECT_FALSE(json.overflowed());
  EXPECT_STREQ(big, "{\"k\":\"a\\\"b\"}");
}

TEST(WordBank, HasDistinctCodeWordsThatFitATile) {
  const uint16_t count = words::codeWordCount();
  ASSERT_GE(count, 100);
  std::set<std::string> unique;
  for (uint16_t i = 0; i < count; i++) {
    const char* word = words::codeWord(i);
    EXPECT_LE(strlen(word), 9u) << word;
    EXPECT_GT(strlen(word), 0u);
    unique.insert(word);
  }
  EXPECT_EQ(unique.size(), count);
  ASSERT_GE(words::pairCount(), 20);
  for (uint16_t i = 0; i < words::pairCount(); i++) {
    EXPECT_STRNE(words::pairCivilian(i), words::pairUndercover(i));
  }
}

// --- lobby -------------------------------------------------------------------

TEST(GameSession, SeatsPlayersAndKeepsOneHost) {
  GameSession session(1);
  seat(session, 3);
  EXPECT_EQ(session.playerCount(), 3);
  EXPECT_TRUE(session.isHost(0));
  EXPECT_FALSE(session.isHost(1));

  // A reload carries the same token and must not burn a second seat.
  EXPECT_EQ(session.join("P1 again", 1u, 500), 0);
  EXPECT_EQ(session.playerCount(), 3);

  session.leave(0);
  EXPECT_EQ(session.playerCount(), 2);
  EXPECT_TRUE(session.isHost(1)) << "host moves to the lowest occupied seat";
}

TEST(GameSession, RejectsAnEmptyTableAndAFullOne) {
  GameSession session(2);
  EXPECT_EQ(session.join("nobody", 0, 0), -1) << "token 0 marks a free seat";
  for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
    EXPECT_GE(session.join("x", i + 1u, 0), 0);
  }
  EXPECT_EQ(session.join("late", 99u, 0), -1);
}

TEST(GameSession, VersionMovesWheneverTheScreenWouldChange) {
  GameSession session(3);
  const uint32_t start = session.version();
  seat(session, 3);
  EXPECT_GT(session.version(), start);

  const uint32_t beforePick = session.version();
  session.selectGame(GameId::Codenames);
  EXPECT_GT(session.version(), beforePick);
  session.selectGame(GameId::Codenames);
  EXPECT_EQ(session.version(), beforePick + 1) << "reselecting the same game is not a change";
}

TEST(GameSession, GatesStartOnTheMinimumSeatCount) {
  GameSession session(4);
  session.selectGame(GameId::Codenames);
  seat(session, 3);
  EXPECT_FALSE(session.canStart());
  EXPECT_FALSE(session.startGame());

  ASSERT_EQ(session.join("P4", 4u, 0), 3);
  EXPECT_TRUE(session.canStart());
  EXPECT_TRUE(session.startGame());
  EXPECT_FALSE(session.inLobby());
  EXPECT_EQ(session.game()->id(), GameId::Codenames);

  // A round in progress is closed to newcomers: roles are already dealt.
  EXPECT_EQ(session.join("P5", 5u, 0), -1);
}

TEST(GameSession, HostDrivesTheLobbyFromTheirPhone) {
  GameSession session(5);
  seat(session, 4);
  EXPECT_TRUE(session.applyAction(0, verb(Verb::Pick, gameIndex(GameId::Codenames))));
  EXPECT_EQ(session.selected(), GameId::Codenames);
  EXPECT_FALSE(session.applyAction(1, verb(Verb::Pick, gameIndex(GameId::Ludo)))) << "only the host picks";

  EXPECT_TRUE(session.applyAction(0, verb(Verb::Next)));
  EXPECT_FALSE(session.inLobby());
}

TEST(GameSession, CyclesThroughEveryGame) {
  GameSession session(6);
  std::set<int> seen;
  for (uint8_t i = 0; i < GAME_COUNT; i++) {
    seen.insert(static_cast<int>(session.selected()));
    session.cycleGame(1);
  }
  EXPECT_EQ(seen.size(), GAME_COUNT);
  const GameId before = session.selected();
  session.cycleGame(-1);
  session.cycleGame(1);
  EXPECT_EQ(session.selected(), before) << "cycling wraps in both directions";
}

TEST(GameSession, LobbyJsonNamesTheGamesAndThePlayers) {
  GameSession session(7);
  seat(session, 3);
  char buf[2048];
  const size_t written = session.writeStateJson(buf, sizeof(buf), 0, 0);
  ASSERT_GT(written, 0u);
  EXPECT_EQ(buf[written - 1], '}') << "a truncated document would not close";
  const std::string json(buf);
  EXPECT_NE(json.find("\"phase\":\"lobby\""), std::string::npos);
  EXPECT_NE(json.find("\"host\":true"), std::string::npos);
  EXPECT_NE(json.find("\"k\":\"battleship\""), std::string::npos);
  EXPECT_NE(json.find("\"n\":\"P2\""), std::string::npos);
}

TEST(GameSession, MarksAPhoneAwayWhenItStopsPolling) {
  GameSession session(8);
  ASSERT_EQ(session.join("P1", 1u, 1000), 0);
  EXPECT_FALSE(session.isAway(0, 1000));
  EXPECT_TRUE(session.isAway(0, 1000 + GameSession::AWAY_AFTER_MS + 1));
  session.touch(0, 40000);
  EXPECT_FALSE(session.isAway(0, 40000));
}

// --- undercover --------------------------------------------------------------

TEST(Undercover, DealsOneImpostorAndTwoInABigGroup) {
  for (uint8_t count = 3; count <= MAX_PLAYERS; count++) {
    Undercover game;
    Rng rng(count * 31u + 1u);
    game.start(count, rng);
    uint8_t impostors = 0;
    for (uint8_t i = 0; i < count; i++) {
      if (game.isUndercover(i)) impostors++;
    }
    EXPECT_EQ(impostors, count >= 6 ? 2 : 1) << "count " << static_cast<int>(count);
  }
}

TEST(Undercover, EveryoneReadyOpensTheVote) {
  Undercover game;
  Rng rng(11);
  game.start(4, rng);
  EXPECT_EQ(game.phase(), Undercover::Phase::Describe);
  for (uint8_t i = 0; i < 3; i++) {
    EXPECT_TRUE(game.apply(i, verb(Verb::Ready), false));
    EXPECT_EQ(game.phase(), Undercover::Phase::Describe);
  }
  EXPECT_FALSE(game.apply(0, verb(Verb::Ready), false)) << "ready twice is not a change";
  EXPECT_TRUE(game.apply(3, verb(Verb::Ready), false));
  EXPECT_EQ(game.phase(), Undercover::Phase::Vote);
}

TEST(Undercover, OnlyTheHostMovesThePhaseAlong) {
  Undercover game;
  Rng rng(15);
  game.start(4, rng);
  EXPECT_FALSE(game.apply(2, verb(Verb::Next), false)) << "a guest cannot cut the describing short";
  EXPECT_EQ(game.phase(), Undercover::Phase::Describe);
  EXPECT_TRUE(game.apply(2, verb(Verb::Next), true)) << "the device holds the host's authority";
  EXPECT_EQ(game.phase(), Undercover::Phase::Vote);
}

TEST(Undercover, EliminatesTheMostVotedAndRefusesATie) {
  Undercover game;
  Rng rng(12);
  game.start(4, rng);
  game.apply(0, verb(Verb::Next), true);  // host skips the describing phase
  ASSERT_EQ(game.phase(), Undercover::Phase::Vote);

  EXPECT_FALSE(game.apply(0, verb(Verb::Vote, 0), false)) << "no voting for yourself";
  EXPECT_FALSE(game.apply(0, verb(Verb::Vote, 9), false)) << "seat out of range";

  // 2-2 split: nobody leaves.
  game.apply(0, verb(Verb::Vote, 1), false);
  game.apply(1, verb(Verb::Vote, 0), false);
  game.apply(2, verb(Verb::Vote, 3), false);
  game.apply(3, verb(Verb::Vote, 2), false);
  EXPECT_EQ(game.phase(), Undercover::Phase::Reveal);
  EXPECT_EQ(game.lastEliminated(), -1);
  for (uint8_t i = 0; i < 4; i++) EXPECT_TRUE(game.alive(i));

  ASSERT_TRUE(game.apply(0, verb(Verb::Next), true));
  EXPECT_EQ(game.phase(), Undercover::Phase::Describe);
  EXPECT_EQ(game.roundNumber(), 2);

  game.apply(0, verb(Verb::Next), true);
  game.apply(0, verb(Verb::Vote, 2), false);
  game.apply(1, verb(Verb::Vote, 2), false);
  game.apply(2, verb(Verb::Vote, 0), false);
  game.apply(3, verb(Verb::Vote, 2), false);
  EXPECT_FALSE(game.alive(2));
  EXPECT_EQ(game.lastEliminated(), 2);
}

TEST(Undercover, CiviliansWinWhenTheImpostorIsOut) {
  Undercover game;
  Rng rng(13);
  game.start(4, rng);
  int8_t impostor = -1;
  for (uint8_t i = 0; i < 4; i++) {
    if (game.isUndercover(i)) impostor = static_cast<int8_t>(i);
  }
  ASSERT_GE(impostor, 0);

  game.apply(0, verb(Verb::Next), true);
  for (uint8_t i = 0; i < 4; i++) {
    const int16_t target = i == impostor ? (impostor == 0 ? 1 : 0) : impostor;
    game.apply(i, verb(Verb::Vote, target), false);
  }
  EXPECT_TRUE(game.isOver());
  EXPECT_EQ(game.winner(), 1);
}

TEST(Undercover, ShowsEachPhoneOnlyItsOwnWord) {
  Undercover game;
  Rng rng(14);
  game.start(4, rng);
  std::string impostorWord;
  std::string civilianWord;
  for (uint8_t i = 0; i < 4; i++) {
    char buf[1024];
    JsonBuf json(buf, sizeof(buf));
    game.writeView(json, static_cast<int8_t>(i));
    const std::string view(buf);
    EXPECT_EQ(view.find("\"role\":0"), std::string::npos) << "roles stay hidden while everyone is alive";
    EXPECT_EQ(view.find("\"role\":1"), std::string::npos);
    const size_t at = view.find("\"word\":\"");
    ASSERT_NE(at, std::string::npos);
    const std::string word = view.substr(at + 8, view.find('"', at + 8) - at - 8);
    (game.isUndercover(i) ? impostorWord : civilianWord) = word;
  }
  EXPECT_FALSE(impostorWord.empty());
  EXPECT_FALSE(civilianWord.empty());
  EXPECT_NE(impostorWord, civilianWord);
}

// --- codenames ---------------------------------------------------------------

TEST(Codenames, DealsTheStandardKey) {
  Codenames game;
  Rng rng(21);
  game.start(4, rng);

  uint8_t counts[4] = {0, 0, 0, 0};
  std::set<std::string> unique;
  for (uint8_t i = 0; i < Codenames::TILES; i++) {
    counts[game.owner(i)]++;
    unique.insert(game.tileWord(i));
    EXPECT_FALSE(game.revealed(i));
  }
  EXPECT_EQ(unique.size(), Codenames::TILES) << "no repeated word on the grid";
  EXPECT_EQ(counts[Codenames::ASSASSIN], 1);
  EXPECT_EQ(counts[Codenames::NEUTRAL], 7);
  EXPECT_EQ(counts[game.turn()], 9) << "the team that starts gets the extra agent";
  EXPECT_EQ(counts[game.turn() == Codenames::RED ? Codenames::BLUE : Codenames::RED], 8);
}

TEST(Codenames, GivesEachTeamOneSpymaster) {
  for (uint8_t count = 4; count <= MAX_PLAYERS; count++) {
    Codenames game;
    Rng rng(count * 7u);
    game.start(count, rng);
    uint8_t spies[3] = {0, 0, 0};
    for (uint8_t i = 0; i < count; i++) {
      if (game.isSpymaster(i)) spies[game.team(i)]++;
    }
    EXPECT_EQ(spies[Codenames::RED], 1) << "count " << static_cast<int>(count);
    EXPECT_EQ(spies[Codenames::BLUE], 1) << "count " << static_cast<int>(count);
  }
}

namespace {

// Finds a seat on `team` that is (or is not) the spymaster.
int8_t findSeat(const Codenames& game, const uint8_t count, const uint8_t team, const bool spymaster) {
  for (uint8_t i = 0; i < count; i++) {
    if (game.team(i) == team && game.isSpymaster(i) == spymaster) return static_cast<int8_t>(i);
  }
  return -1;
}

int8_t findTile(const Codenames& game, const uint8_t owner) {
  for (uint8_t i = 0; i < Codenames::TILES; i++) {
    if (game.owner(i) == owner && !game.revealed(i)) return static_cast<int8_t>(i);
  }
  return -1;
}

}  // namespace

TEST(Codenames, OnlyTheSpymasterOnTurnMayClue) {
  Codenames game;
  Rng rng(22);
  game.start(4, rng);
  const uint8_t on = game.turn();
  const uint8_t off = on == Codenames::RED ? Codenames::BLUE : Codenames::RED;

  EXPECT_FALSE(game.apply(findSeat(game, 4, off, true), verb(Verb::Clue, 2, -1, -1, "OCEAN"), false));
  EXPECT_FALSE(game.apply(findSeat(game, 4, on, false), verb(Verb::Clue, 2, -1, -1, "OCEAN"), false));
  EXPECT_FALSE(game.apply(findSeat(game, 4, on, true), verb(Verb::Clue, 2, -1, -1, nullptr), false))
      << "a clue needs a word";
  EXPECT_TRUE(game.apply(findSeat(game, 4, on, true), verb(Verb::Clue, 2, -1, -1, "OCEAN"), false));
  EXPECT_EQ(game.phase(), Codenames::Phase::Guess);
  EXPECT_STREQ(game.clue(), "OCEAN");
  EXPECT_EQ(game.guessesLeft(), 3) << "the clue count plus the traditional bonus guess";
  EXPECT_FALSE(game.apply(findSeat(game, 4, on, true), verb(Verb::Guess, 0), false)) << "spymasters never guess";
}

TEST(Codenames, AWrongColourEndsTheTurn) {
  Codenames game;
  Rng rng(23);
  game.start(4, rng);
  const uint8_t on = game.turn();
  const uint8_t off = on == Codenames::RED ? Codenames::BLUE : Codenames::RED;
  game.apply(findSeat(game, 4, on, true), verb(Verb::Clue, 2, -1, -1, "OCEAN"), false);

  const int8_t ownTile = findTile(game, on);
  ASSERT_TRUE(game.apply(findSeat(game, 4, on, false), verb(Verb::Guess, ownTile), false));
  EXPECT_TRUE(game.revealed(ownTile));
  EXPECT_EQ(game.turn(), on) << "a correct guess keeps the turn";

  const int8_t theirTile = findTile(game, off);
  ASSERT_TRUE(game.apply(findSeat(game, 4, on, false), verb(Verb::Guess, theirTile), false));
  EXPECT_EQ(game.turn(), off);
  EXPECT_EQ(game.phase(), Codenames::Phase::Clue);
  EXPECT_FALSE(game.apply(findSeat(game, 4, on, false), verb(Verb::Guess, findTile(game, on)), false))
      << "the turn really did pass";
}

TEST(Codenames, TheAssassinEndsItImmediately) {
  Codenames game;
  Rng rng(24);
  game.start(4, rng);
  const uint8_t on = game.turn();
  const uint8_t off = on == Codenames::RED ? Codenames::BLUE : Codenames::RED;
  game.apply(findSeat(game, 4, on, true), verb(Verb::Clue, 1, -1, -1, "DANGER"), false);
  ASSERT_TRUE(game.apply(findSeat(game, 4, on, false), verb(Verb::Guess, findTile(game, Codenames::ASSASSIN)), false));
  EXPECT_TRUE(game.isOver());
  EXPECT_EQ(game.winner(), off);
}

TEST(Codenames, FindingEveryAgentWins) {
  Codenames game;
  Rng rng(25);
  game.start(4, rng);
  const uint8_t on = game.turn();
  const int8_t spy = findSeat(game, 4, on, true);
  const int8_t field = findSeat(game, 4, on, false);

  for (int guard = 0; guard < 30 && !game.isOver(); guard++) {
    if (game.phase() == Codenames::Phase::Clue) {
      ASSERT_EQ(game.turn(), on) << "the other team never gets a turn in this run";
      ASSERT_TRUE(game.apply(spy, verb(Verb::Clue, 9, -1, -1, "ALL"), false));
    }
    ASSERT_TRUE(game.apply(field, verb(Verb::Guess, findTile(game, on)), false));
  }
  EXPECT_TRUE(game.isOver());
  EXPECT_EQ(game.winner(), on);
  EXPECT_EQ(game.remaining(on), 0);
}

TEST(Codenames, TheKeyIsOnlyOnTheSpymastersPhone) {
  Codenames game;
  Rng rng(26);
  game.start(4, rng);
  const int8_t spy = findSeat(game, 4, game.turn(), true);
  const int8_t field = findSeat(game, 4, game.turn(), false);

  char spyBuf[3072];
  JsonBuf spyJson(spyBuf, sizeof(spyBuf));
  game.writeView(spyJson, spy);
  EXPECT_FALSE(spyJson.overflowed());
  EXPECT_EQ(std::string(spyBuf).find("\"k\":-1"), std::string::npos) << "the spymaster sees every key";

  char fieldBuf[3072];
  JsonBuf fieldJson(fieldBuf, sizeof(fieldBuf));
  game.writeView(fieldJson, field);
  EXPECT_FALSE(fieldJson.overflowed());
  const std::string view(fieldBuf);
  size_t hidden = 0;
  for (size_t at = view.find("\"k\":-1"); at != std::string::npos; at = view.find("\"k\":-1", at + 1)) hidden++;
  EXPECT_EQ(hidden, Codenames::TILES) << "operatives see no key at all before a tile turns";
}

// --- battleship --------------------------------------------------------------

TEST(Battleship, RefusesOverlappingAndOverhangingShips) {
  Battleship game;
  Rng rng(31);
  game.start(2, rng);

  EXPECT_TRUE(game.apply(0, verb(Verb::Place, 0, 0, 0, "h"), false));
  EXPECT_FALSE(game.apply(0, verb(Verb::Place, 1, 0, 0, "h"), false)) << "overlaps the carrier";
  EXPECT_TRUE(game.apply(0, verb(Verb::Place, 1, 0, 1, "h"), false));
  EXPECT_FALSE(game.apply(0, verb(Verb::Place, 2, 8, 2, "h"), false)) << "hangs off the right edge";
  EXPECT_FALSE(game.apply(0, verb(Verb::Place, 2, 2, 8, "v"), false)) << "hangs off the bottom edge";
  EXPECT_TRUE(game.apply(0, verb(Verb::Place, 0, 4, 4, "v"), false)) << "a ship may be moved while placing";
}

TEST(Battleship, ReadyFillsTheFleetAndStartsTheDuel) {
  Battleship game;
  Rng rng(32);
  game.start(2, rng);
  EXPECT_TRUE(game.apply(0, verb(Verb::Ready), false));
  EXPECT_EQ(game.phase(), Battleship::Phase::Place) << "still waiting for the second captain";
  EXPECT_FALSE(game.apply(0, verb(Verb::Fire, 0, 0), false));
  EXPECT_TRUE(game.apply(1, verb(Verb::Ready), false));
  EXPECT_EQ(game.phase(), Battleship::Phase::Fire);
  EXPECT_EQ(game.shipsLeft(0), Battleship::SHIPS);
  EXPECT_EQ(game.shipsLeft(1), Battleship::SHIPS);
}

TEST(Battleship, OneShotPerTurnAndNoRepeats) {
  Battleship game;
  Rng rng(33);
  game.start(2, rng);
  game.apply(0, verb(Verb::Ready), false);
  game.apply(1, verb(Verb::Ready), false);

  EXPECT_FALSE(game.apply(1, verb(Verb::Fire, 0, 0), false)) << "seat 1 opens";
  ASSERT_TRUE(game.apply(0, verb(Verb::Fire, 3, 4), false));
  EXPECT_EQ(game.turn(), 1);
  EXPECT_NE(game.shotAt(1, 3, 4), Battleship::UNKNOWN);
  ASSERT_TRUE(game.apply(1, verb(Verb::Fire, 3, 4), false));
  EXPECT_FALSE(game.apply(0, verb(Verb::Fire, 3, 4), false)) << "that square is already spent";
}

TEST(Battleship, SinkingEveryShipWins) {
  Battleship game;
  Rng rng(34);
  game.start(2, rng);
  game.apply(0, verb(Verb::Ready), false);
  game.apply(1, verb(Verb::Ready), false);

  // Seat 1 works the grid; seat 2 fires into a corner it has already used once,
  // which is rejected, so only seat 1 makes progress.
  uint8_t cell = 0;
  for (int guard = 0; guard < 400 && !game.isOver(); guard++) {
    if (game.turn() == 0) {
      const uint8_t x = cell % Battleship::SIZE;
      const uint8_t y = cell / Battleship::SIZE;
      cell++;
      game.apply(0, verb(Verb::Fire, x, y), false);
    } else {
      // Seat 2 keeps missing on purpose by shooting the same square twice; the
      // second attempt is refused, so force a fresh square each time.
      const uint8_t x = (cell * 7) % Battleship::SIZE;
      const uint8_t y = (cell * 3) % Battleship::SIZE;
      if (!game.apply(1, verb(Verb::Fire, x, y), false)) {
        for (uint8_t i = 0; i < Battleship::CELLS; i++) {
          if (game.shotAt(0, i % Battleship::SIZE, i / Battleship::SIZE) == Battleship::UNKNOWN) {
            ASSERT_TRUE(game.apply(1, verb(Verb::Fire, i % Battleship::SIZE, i / Battleship::SIZE), false));
            break;
          }
        }
      }
    }
  }
  EXPECT_TRUE(game.isOver());
  EXPECT_GE(game.winner(), 0);
  EXPECT_EQ(game.shipsLeft(1 - game.winner()), 0);
}

TEST(Battleship, APhoneNeverSeesTheOtherFleet) {
  Battleship game;
  Rng rng(35);
  game.start(2, rng);
  game.apply(0, verb(Verb::Ready), false);
  game.apply(1, verb(Verb::Ready), false);

  char buf[2048];
  JsonBuf json(buf, sizeof(buf));
  game.writeView(json, 0);
  EXPECT_FALSE(json.overflowed());
  const std::string view(buf);
  const size_t fleetAt = view.find("\"fleet\":\"");
  ASSERT_NE(fleetAt, std::string::npos);
  const std::string fleet = view.substr(fleetAt + 9, Battleship::CELLS);
  uint8_t occupied = 0;
  for (const char c : fleet) {
    if (c != '0') occupied++;
  }
  EXPECT_EQ(occupied, 5 + 4 + 3 + 3 + 2) << "own ships are there";

  const size_t outAt = view.find("\"outgoing\":\"");
  ASSERT_NE(outAt, std::string::npos);
  const std::string outgoing = view.substr(outAt + 12, Battleship::CELLS);
  EXPECT_EQ(outgoing, std::string(Battleship::CELLS, '0')) << "and the enemy grid is blank until fired on";
}

// --- ludo --------------------------------------------------------------------

TEST(Ludo, BoardGeometryIsAClosedRing) {
  std::set<std::pair<int, int>> unique;
  for (uint8_t i = 0; i < Ludo::TRACK; i++) {
    const Ludo::Cell cell = Ludo::trackCell(i);
    EXPECT_LT(cell.col, Ludo::GRID);
    EXPECT_LT(cell.row, Ludo::GRID);
    unique.insert({cell.col, cell.row});
    const Ludo::Cell next = Ludo::trackCell(static_cast<uint8_t>((i + 1) % Ludo::TRACK));
    const int dx = static_cast<int>(next.col) - static_cast<int>(cell.col);
    const int dy = static_cast<int>(next.row) - static_cast<int>(cell.row);
    EXPECT_LE(abs(dx), 1) << "step " << static_cast<int>(i);
    EXPECT_LE(abs(dy), 1) << "step " << static_cast<int>(i);
    EXPECT_FALSE(dx == 0 && dy == 0);
  }
  EXPECT_EQ(unique.size(), Ludo::TRACK);

  for (uint8_t player = 0; player < 4; player++) {
    EXPECT_EQ(Ludo::startIndex(player), player * 13);
    EXPECT_TRUE(Ludo::isSafeCell(Ludo::startIndex(player))) << "a start square shelters its owner";
    // The home column is entered from the ring cell before the player's start.
    const Ludo::Cell entry = Ludo::trackCell(static_cast<uint8_t>((Ludo::startIndex(player) + 51) % Ludo::TRACK));
    const Ludo::Cell first = Ludo::homeCell(player, 0);
    const int dx = abs(static_cast<int>(entry.col) - static_cast<int>(first.col));
    const int dy = abs(static_cast<int>(entry.row) - static_cast<int>(first.row));
    EXPECT_EQ(dx + dy, 1) << "player " << static_cast<int>(player) << " turns into its column";
    for (uint8_t step = 0; step < 5; step++) {
      const Ludo::Cell home = Ludo::homeCell(player, step);
      EXPECT_EQ(unique.count({home.col, home.row}), 0u) << "home cells are off the shared ring";
    }
  }
}

TEST(Ludo, ASixIsTheOnlyWayOutOfTheYard) {
  Ludo game;
  Rng rng(41);
  game.start(4, rng);
  uint8_t moves[Ludo::TOKENS];
  for (uint8_t die = 1; die <= 5; die++) {
    EXPECT_EQ(game.legalMoves(0, die, moves), 0) << "die " << static_cast<int>(die);
  }
  EXPECT_EQ(game.legalMoves(0, 6, moves), Ludo::TOKENS);
}

TEST(Ludo, NoLegalMovePassesTheTurn) {
  Ludo game;
  Rng rng(42);
  game.start(4, rng);
  for (int guard = 0; guard < 200; guard++) {
    const uint8_t before = game.turn();
    ASSERT_TRUE(game.apply(before, verb(Verb::Roll), false));
    if (game.die() == 6) {
      EXPECT_EQ(game.phase(), Ludo::Phase::Move);
      return;
    }
    EXPECT_EQ(game.phase(), Ludo::Phase::Roll);
    EXPECT_EQ(game.turn(), (before + 1) % 4) << "a useless roll hands the dice on";
  }
  FAIL() << "no six in 200 rolls";
}

TEST(Ludo, ThreeSixesForfeitTheTurn) {
  // The dice come from the game's own generator, seeded from the caller's. Mirror
  // it to find a seed whose first three rolls are all sixes.
  for (uint32_t seed = 1; seed < 20000; seed++) {
    Rng seeder(seed);
    Rng mirror(seeder.next());
    if (1 + mirror.below(6) != 6) continue;
    if (1 + mirror.below(6) != 6) continue;
    if (1 + mirror.below(6) != 6) continue;

    Ludo game;
    Rng rng(seed);
    game.start(4, rng);
    ASSERT_TRUE(game.apply(0, verb(Verb::Roll), false));
    ASSERT_EQ(game.die(), 6);
    ASSERT_TRUE(game.apply(0, verb(Verb::Move, 0), false));
    EXPECT_EQ(game.turn(), 0) << "a six is played again";
    ASSERT_TRUE(game.apply(0, verb(Verb::Roll), false));
    ASSERT_TRUE(game.apply(0, verb(Verb::Move, 1), false));
    ASSERT_EQ(game.turn(), 0);
    ASSERT_TRUE(game.apply(0, verb(Verb::Roll), false));
    EXPECT_EQ(game.turn(), 1) << "the third six is forfeited";
    EXPECT_EQ(game.phase(), Ludo::Phase::Roll);
    return;
  }
  FAIL() << "no seed produced three sixes";
}

TEST(Ludo, AFullGameKeepsItsInvariantsAndEnds) {
  Ludo game;
  Rng rng(44);
  game.start(4, rng);

  int captures = 0;
  int homed = 0;
  for (int guard = 0; guard < 20000 && !game.isOver(); guard++) {
    const uint8_t player = game.turn();
    ASSERT_TRUE(game.apply(player, verb(Verb::Roll), false));
    if (game.phase() != Ludo::Phase::Move) continue;

    uint8_t moves[Ludo::TOKENS];
    const uint8_t options = game.legalMoves(player, game.die(), moves);
    ASSERT_GT(options, 0);
    int yardBefore = 0;
    int homeBefore = 0;
    for (uint8_t p = 0; p < 4; p++) {
      for (uint8_t t = 0; t < Ludo::TOKENS; t++) {
        if (p != player && game.position(p, t) == Ludo::IN_YARD) yardBefore++;
      }
      if (p == player) homeBefore = game.tokensHome(p);
    }
    ASSERT_TRUE(game.apply(player, verb(Verb::Move, moves[0]), false));

    int yardAfter = 0;
    for (uint8_t p = 0; p < 4; p++) {
      for (uint8_t t = 0; t < Ludo::TOKENS; t++) {
        const int8_t at = game.position(p, t);
        ASSERT_GE(at, Ludo::IN_YARD);
        ASSERT_LE(at, static_cast<int8_t>(Ludo::HOME));
        if (p != player && at == Ludo::IN_YARD) yardAfter++;
      }
    }
    if (yardAfter > yardBefore) captures++;
    if (game.tokensHome(player) > homeBefore) homed++;

    // No two players ever share an unsafe ring cell once a move resolves.
    for (uint8_t a = 0; a < 4; a++) {
      for (uint8_t ta = 0; ta < Ludo::TOKENS; ta++) {
        const int8_t pa = game.position(a, ta);
        if (pa < 0 || pa > static_cast<int8_t>(Ludo::LAST_TRACK)) continue;
        const uint8_t cellA = Ludo::absoluteCell(a, pa);
        if (Ludo::isSafeCell(cellA)) continue;
        for (uint8_t b = static_cast<uint8_t>(a + 1); b < 4; b++) {
          for (uint8_t tb = 0; tb < Ludo::TOKENS; tb++) {
            const int8_t pb = game.position(b, tb);
            if (pb < 0 || pb > static_cast<int8_t>(Ludo::LAST_TRACK)) continue;
            EXPECT_NE(Ludo::absoluteCell(b, pb), cellA) << "an unsafe square held two colours";
          }
        }
      }
    }
  }

  EXPECT_TRUE(game.isOver());
  ASSERT_GE(game.winner(), 0);
  EXPECT_EQ(game.tokensHome(static_cast<uint8_t>(game.winner())), Ludo::TOKENS);
  EXPECT_GT(captures, 0) << "a 20000 move game with no capture means the rule is dead";
  EXPECT_GT(homed, 0);
}

TEST(Ludo, OnlyTheSeatOnTurnCanAct) {
  Ludo game;
  Rng rng(45);
  game.start(3, rng);
  EXPECT_FALSE(game.apply(1, verb(Verb::Roll), false));
  EXPECT_FALSE(game.apply(2, verb(Verb::Move, 0), false));
  EXPECT_TRUE(game.apply(0, verb(Verb::Roll), false));
}

// --- the served document -----------------------------------------------------

TEST(GameSession, EveryGameViewFitsTheServerBuffer) {
  // GameServer serves from one 3 KB buffer; Codenames with a full table is the
  // largest document, so it decides the size.
  constexpr size_t SERVER_BUFFER = 3072;
  for (uint8_t index = 0; index < GAME_COUNT; index++) {
    GameSession session(100u + index);
    seat(session, MAX_PLAYERS);
    session.selectGame(gameAt(index));
    ASSERT_TRUE(session.startGame()) << gameMeta(gameAt(index)).key;

    for (int8_t viewer = -1; viewer < static_cast<int8_t>(MAX_PLAYERS); viewer++) {
      char buf[SERVER_BUFFER];
      const size_t written = session.writeStateJson(buf, sizeof(buf), viewer, 1000);
      EXPECT_LT(written, SERVER_BUFFER - 64) << gameMeta(gameAt(index)).key << " leaves no headroom";
      EXPECT_EQ(buf[written - 1], '}') << gameMeta(gameAt(index)).key << " document was truncated";
    }
  }
}

TEST(GameSession, HostCanDealAgainOrReturnToTheLobby) {
  GameSession session(55);
  seat(session, 4);
  session.selectGame(GameId::Codenames);
  ASSERT_TRUE(session.startGame());

  // Finish a round the short way: walk the guessing team into the assassin.
  const auto finishRound = [&session] {
    auto* game = static_cast<Codenames*>(session.game());
    const uint8_t on = game->turn();
    int8_t spy = -1;
    int8_t field = -1;
    for (uint8_t i = 0; i < 4; i++) {
      if (game->team(i) != on) continue;
      (game->isSpymaster(i) ? spy : field) = static_cast<int8_t>(i);
    }
    ASSERT_TRUE(session.applyAction(spy, verb(Verb::Clue, 1, -1, -1, "X")));
    for (uint8_t i = 0; i < Codenames::TILES; i++) {
      if (game->owner(i) != Codenames::ASSASSIN) continue;
      ASSERT_TRUE(session.applyAction(field, verb(Verb::Guess, i)));
      break;
    }
    ASSERT_TRUE(session.game()->isOver());
  };

  finishRound();
  const uint8_t host = static_cast<uint8_t>(session.hostSeat());
  const uint8_t nonHost = host == 0 ? 1 : 0;
  EXPECT_FALSE(session.applyAction(nonHost, verb(Verb::Next))) << "only the host restarts";
  EXPECT_TRUE(session.applyAction(host, verb(Verb::Next)));
  EXPECT_FALSE(session.inLobby());
  EXPECT_FALSE(session.game()->isOver()) << "a fresh deal of the same game";

  EXPECT_FALSE(session.applyAction(host, verb(Verb::Pick, gameIndex(GameId::Ludo))))
      << "a live round is not abandoned by a tap on a phone";

  finishRound();
  EXPECT_TRUE(session.applyAction(host, verb(Verb::Ready)));
  EXPECT_TRUE(session.inLobby()) << "the host can walk a finished round back to the lobby";
  EXPECT_TRUE(session.applyAction(host, verb(Verb::Pick, gameIndex(GameId::Ludo))));
  EXPECT_EQ(session.selected(), GameId::Ludo);
}
