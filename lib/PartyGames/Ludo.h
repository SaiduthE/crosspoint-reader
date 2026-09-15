#pragma once

#include "Game.h"

namespace party {

// Ludo for two to four seats. The phones are the dice cups and the token
// pickers; the 15x15 board only exists on the shared screen.
class Ludo final : public Game {
 public:
  enum class Phase : uint8_t { Roll, Move, Over };

  static constexpr uint8_t TOKENS = 4;
  static constexpr uint8_t TRACK = 52;       // shared ring cells
  static constexpr uint8_t GRID = 15;        // board is 15 x 15 cells
  static constexpr int8_t IN_YARD = -1;      // token has not entered yet
  static constexpr uint8_t LAST_TRACK = 51;  // final shared-ring step
  static constexpr uint8_t HOME = 57;        // 52..56 are the home column, 57 is home

  struct Cell {
    uint8_t col;
    uint8_t row;
  };

  // Board geometry, shared with the renderer and asserted by the host tests.
  static Cell trackCell(uint8_t index);                 // 0..51
  static Cell homeCell(uint8_t player, uint8_t step);   // step 0..4
  static Cell yardCell(uint8_t player, uint8_t token);  // 0..3
  static uint8_t startIndex(uint8_t player) { return static_cast<uint8_t>(player * 13); }
  static uint8_t absoluteCell(uint8_t player, int8_t position);  // position 0..51 -> ring index
  static bool isSafeCell(uint8_t trackIndex);

  GameId id() const override { return GameId::Ludo; }
  void start(uint8_t playerCount, Rng& rng) override;
  bool apply(uint8_t seat, const Action& action, bool isHost) override;
  bool isOver() const override { return phase_ == Phase::Over; }
  void statusLine(char* out, size_t cap) const override;
  void writeView(JsonBuf& out, int8_t seat) const override;

  // Shared-screen accessors.
  Phase phase() const { return phase_; }
  uint8_t players() const { return count_; }
  int8_t position(uint8_t player, uint8_t token) const;
  uint8_t turn() const { return turn_; }
  uint8_t die() const { return die_; }
  int8_t winner() const { return winner_; }
  uint8_t tokensHome(uint8_t player) const;
  const char* message() const { return message_; }
  // Fills `out` (at least TOKENS entries) with the movable tokens for the
  // current roll and returns how many there are.
  uint8_t legalMoves(uint8_t player, uint8_t die, uint8_t* out) const;

 private:
  bool canMove(uint8_t player, uint8_t token, uint8_t die) const;
  bool captureAt(uint8_t mover, uint8_t trackIndex);
  void advanceTurn();
  void finishRoll();

  uint8_t count_ = 0;
  int8_t pos_[4][TOKENS] = {};
  uint8_t turn_ = 0;
  uint8_t die_ = 0;
  uint8_t sixStreak_ = 0;
  Phase phase_ = Phase::Roll;
  int8_t winner_ = -1;
  char message_[64] = {};
  Rng rng_;
};

}  // namespace party
