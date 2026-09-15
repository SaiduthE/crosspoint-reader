#pragma once

#include "Battleship.h"
#include "Codenames.h"
#include "Game.h"
#include "Ludo.h"
#include "Undercover.h"

namespace party {

// The lobby and the round in progress: seats, join tokens, which game is
// selected, and the JSON document each phone polls.
//
// The active game is constructed in place in `storage_` rather than on the heap.
// A party session churns through rounds, and a repeated new/delete of a ~500
// byte object is exactly the fragmentation the reader cannot afford once WiFi
// and the HTTP server have taken their share of DRAM.
class GameSession {
 public:
  // A phone that has not polled within this window shows as away on the shared
  // screen. The seat is kept: someone whose screen locked is still playing.
  static constexpr uint32_t AWAY_AFTER_MS = 15000;

  // Sized to the largest game; Battleship's two 10x10 grids dominate.
  static constexpr size_t GAME_STORAGE_BYTES = 640;

  explicit GameSession(uint32_t seed = 0x9E3779B9u) : rng_(seed) {}
  ~GameSession() { destroyGame(); }

  GameSession(const GameSession&) = delete;
  GameSession& operator=(const GameSession&) = delete;

  // Lobby -------------------------------------------------------------------
  // Returns the seat index, or -1 when the table is full. A token already in
  // use returns its existing seat, so a phone reload does not consume a seat.
  int8_t join(const char* name, uint32_t token, uint32_t nowMs);
  int8_t seatForToken(uint32_t token) const;
  void touch(uint8_t seat, uint32_t nowMs);
  bool leave(uint8_t seat);
  uint8_t playerCount() const { return count_; }
  const Player& player(uint8_t seat) const;
  bool isSeated(uint8_t seat) const { return seat < MAX_PLAYERS && players_[seat].seated; }
  bool isHost(uint8_t seat) const { return count_ > 0 && seat == hostSeat_; }
  int8_t hostSeat() const { return count_ > 0 ? static_cast<int8_t>(hostSeat_) : -1; }
  bool isAway(uint8_t seat, uint32_t nowMs) const;

  // Game selection ----------------------------------------------------------
  GameId selected() const { return selected_; }
  void selectGame(GameId game);
  void cycleGame(int8_t delta);
  bool canStart() const;
  bool startGame();
  void endGame();
  bool inLobby() const { return game_ == nullptr; }
  Game* game() { return game_; }
  const Game* game() const { return game_; }

  // Play --------------------------------------------------------------------
  // Routes an action to the round, or handles the lobby and post-game verbs the
  // host drives. Returns true when something changed.
  bool applyAction(uint8_t seat, const Action& action);
  uint32_t version() const { return version_; }
  void bump() { version_++; }
  void reseed(uint32_t seed) { rng_.reseed(seed); }

  // Views -------------------------------------------------------------------
  // The document a phone polls, from the point of view of `seat` (-1 for a
  // browser that has not joined). Returns the number of bytes written.
  size_t writeStateJson(char* out, size_t cap, int8_t seat, uint32_t nowMs) const;
  // The banner for the shared screen.
  void statusLine(char* out, size_t cap) const;

 private:
  void destroyGame();
  void rehost();

  alignas(8) unsigned char storage_[GAME_STORAGE_BYTES] = {};
  Game* game_ = nullptr;

  Player players_[MAX_PLAYERS];
  uint8_t count_ = 0;
  uint8_t hostSeat_ = 0;
  GameId selected_ = GameId::Undercover;
  uint32_t version_ = 1;
  Rng rng_;
};

}  // namespace party
