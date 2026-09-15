#pragma once

#include <cstddef>
#include <cstdint>

// Party games shared types.
//
// Everything in lib/PartyGames is free of Arduino and of the renderer: the rules
// and the JSON views are plain C++ so they build and run under test/party_games
// on the host. Pixels live in src/activities/games, the HTTP surface in
// src/network/GameServer.
namespace party {

inline constexpr uint8_t MAX_PLAYERS = 8;
inline constexpr uint8_t MAX_NAME_LEN = 12;  // excluding the terminator
inline constexpr uint8_t MAX_TEXT_LEN = 16;  // clue words and other short action payloads

enum class GameId : uint8_t { None = 0, Undercover, Codenames, Battleship, Ludo };
inline constexpr uint8_t GAME_COUNT = 4;

// Wire verbs. The HTTP layer parses ?v=<name> into one of these; the games only
// ever see the decoded form.
enum class Verb : uint8_t {
  None = 0,
  Ready,  // "I am done" / start the next phase
  Next,   // host advances past a reveal, or starts the next round
  Clue,   // Codenames spymaster: text = word, a = count
  Guess,  // Codenames operative: a = tile index
  Pass,   // Codenames team ends its turn
  Place,  // Battleship: a = ship, b = x, c = y, text = "h" or "v"
  Fire,   // Battleship: a = x, b = y
  Vote,   // Undercover: a = target seat
  Roll,   // Ludo
  Move,   // Ludo: a = token index
  Pick,   // host, in the lobby: a = game index
};

struct Action {
  Verb verb = Verb::None;
  int16_t a = -1;
  int16_t b = -1;
  int16_t c = -1;
  char text[MAX_TEXT_LEN + 1] = {};
};

struct Player {
  char name[MAX_NAME_LEN + 1] = {};
  uint32_t token = 0;       // 0 marks a free seat
  uint32_t lastSeenMs = 0;  // last poll, for the "connected" dot on the shared screen
  bool seated = false;
};

// Deterministic xorshift32. Seeded from esp_random() on the device and from a
// literal in the tests, which is what makes a dealt round reproducible.
class Rng {
 public:
  explicit Rng(uint32_t seed = 0x2545F491u) : state(seed ? seed : 0x2545F491u) {}
  uint32_t next();
  uint32_t below(uint32_t bound);  // 0 for bound == 0
  void shuffle(uint8_t* values, uint8_t count);
  void reseed(uint32_t seed) { state = seed ? seed : 0x2545F491u; }

 private:
  uint32_t state;
};

// Bounds-checked JSON writer over a caller-owned buffer. Truncates rather than
// growing: the views are served from a single fixed buffer owned by GameServer,
// so there is no per-request allocation on the device.
class JsonBuf {
 public:
  JsonBuf(char* out, size_t capacity) : buf(out), cap(capacity) {
    if (cap) buf[0] = '\0';
  }

  void raw(const char* text);
  void ch(char c);
  void str(const char* text);  // quoted and escaped
  void num(long value);
  void boolean(bool value);
  void key(const char* name);  // "name":
  void keyStr(const char* name, const char* text);
  void keyNum(const char* name, long value);
  void keyBool(const char* name, bool value);

  size_t size() const { return length; }
  bool overflowed() const { return overflow; }

 private:
  char* buf;
  size_t cap;
  size_t length = 0;
  bool overflow = false;
};

// Copies `in` into `out` keeping only printable ASCII, dropping the characters
// that would need escaping downstream. Always null-terminates. Returns the
// number of characters kept.
uint8_t sanitizeName(const char* in, char* out, uint8_t outCapacity);

}  // namespace party
