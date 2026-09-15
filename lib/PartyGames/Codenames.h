#pragma once

#include "Game.h"

namespace party {

// Codenames: a 5x5 word grid on the shared screen, the colour key on the two
// spymasters' phones. The device is the only board anyone needs.
class Codenames final : public Game {
 public:
  enum class Phase : uint8_t { Clue, Guess, Over };
  static constexpr uint8_t TILES = 25;
  static constexpr uint8_t GRID = 5;

  // Tile owners.
  static constexpr uint8_t NEUTRAL = 0;
  static constexpr uint8_t RED = 1;
  static constexpr uint8_t BLUE = 2;
  static constexpr uint8_t ASSASSIN = 3;

  GameId id() const override { return GameId::Codenames; }
  void start(uint8_t playerCount, Rng& rng) override;
  bool apply(uint8_t seat, const Action& action, bool isHost) override;
  bool isOver() const override { return phase_ == Phase::Over; }
  void statusLine(char* out, size_t cap) const override;
  void writeView(JsonBuf& out, int8_t seat) const override;

  // Shared-screen accessors.
  Phase phase() const { return phase_; }
  // Not word(): Arduino.h defines a function-like word() macro, and the
  // device-side board painter includes it.
  const char* tileWord(uint8_t tile) const;
  uint8_t owner(uint8_t tile) const { return tile < TILES ? key_[tile] : NEUTRAL; }
  bool revealed(uint8_t tile) const { return tile < TILES && revealed_[tile]; }
  uint8_t turn() const { return turn_; }
  uint8_t remaining(uint8_t team) const;
  const char* clue() const { return clue_; }
  uint8_t clueCount() const { return clueCount_; }
  uint8_t guessesLeft() const { return guessesLeft_; }
  uint8_t winner() const { return winner_; }
  uint8_t team(uint8_t seat) const { return seat < count_ ? team_[seat] : 0; }
  bool isSpymaster(uint8_t seat) const { return seat < count_ && spymaster_[seat]; }
  const char* message() const { return message_; }

 private:
  void endTurn();
  void reveal(uint8_t tile);

  uint8_t count_ = 0;
  uint16_t word_[TILES] = {};
  uint8_t key_[TILES] = {};
  bool revealed_[TILES] = {};
  uint8_t team_[MAX_PLAYERS] = {};
  bool spymaster_[MAX_PLAYERS] = {};
  uint8_t turn_ = RED;
  Phase phase_ = Phase::Clue;
  char clue_[MAX_TEXT_LEN + 1] = {};
  uint8_t clueCount_ = 0;
  uint8_t guessesLeft_ = 0;
  uint8_t winner_ = 0;
  char message_[64] = {};
};

}  // namespace party
