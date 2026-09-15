#pragma once

#include "Game.h"

namespace party {

// Battleship for the two seated players; everyone else watches the shared
// screen, which shows both grids with the ships still hidden.
class Battleship final : public Game {
 public:
  enum class Phase : uint8_t { Place, Fire, Over };
  static constexpr uint8_t SIZE = 10;
  static constexpr uint8_t CELLS = SIZE * SIZE;
  static constexpr uint8_t SHIPS = 5;
  static constexpr uint8_t PLAYERS = 2;

  // Cell states on a board, as the opponent sees them.
  static constexpr uint8_t UNKNOWN = 0;
  static constexpr uint8_t MISS = 1;
  static constexpr uint8_t HIT = 2;

  static uint8_t shipLength(uint8_t ship);

  GameId id() const override { return GameId::Battleship; }
  void start(uint8_t playerCount, Rng& rng) override;
  bool apply(uint8_t seat, const Action& action, bool isHost) override;
  bool isOver() const override { return phase_ == Phase::Over; }
  void statusLine(char* out, size_t cap) const override;
  void writeView(JsonBuf& out, int8_t seat) const override;

  // Shared-screen accessors.
  Phase phase() const { return phase_; }
  uint8_t turn() const { return turn_; }
  int8_t winner() const { return winner_; }
  uint8_t shotAt(uint8_t player, uint8_t x, uint8_t y) const;
  bool sunkAt(uint8_t player, uint8_t x, uint8_t y) const;
  uint8_t shipsLeft(uint8_t player) const;
  bool ready(uint8_t player) const { return player < PLAYERS && ready_[player]; }
  const char* message() const { return message_; }

 private:
  bool canPlace(uint8_t player, uint8_t ship, uint8_t x, uint8_t y, bool horizontal) const;
  void clearShip(uint8_t player, uint8_t ship);
  bool placeShip(uint8_t player, uint8_t ship, uint8_t x, uint8_t y, bool horizontal);
  void autoPlace(uint8_t player);
  bool isSunk(uint8_t player, uint8_t ship) const;
  bool allSunk(uint8_t player) const;

  uint8_t board_[PLAYERS][CELLS] = {};  // 0 empty, otherwise ship index + 1
  uint8_t shots_[PLAYERS][CELLS] = {};  // UNKNOWN / MISS / HIT, indexed by the owner of the board
  bool ready_[PLAYERS] = {};
  uint8_t turn_ = 0;
  int8_t winner_ = -1;
  Phase phase_ = Phase::Place;
  char message_[64] = {};
  Rng rng_;
};

}  // namespace party
