#pragma once

#include "Game.h"

namespace party {

// Undercover: everyone holds the same word except the undercover, who holds a
// close neighbour of it. Players describe their word out loud, then vote on
// their phones. The shared screen is the tally board.
class Undercover final : public Game {
 public:
  enum class Phase : uint8_t { Describe, Vote, Reveal, Over };

  GameId id() const override { return GameId::Undercover; }
  void start(uint8_t playerCount, Rng& rng) override;
  bool apply(uint8_t seat, const Action& action, bool isHost) override;
  bool isOver() const override { return phase_ == Phase::Over; }
  void statusLine(char* out, size_t cap) const override;
  void writeView(JsonBuf& out, int8_t seat) const override;

  // Shared-screen accessors.
  Phase phase() const { return phase_; }
  // Not round(): Arduino.h defines a function-like round() macro.
  uint8_t roundNumber() const { return round_; }
  uint8_t seats() const { return count_; }
  bool alive(uint8_t seat) const { return seat < count_ && alive_[seat]; }
  bool ready(uint8_t seat) const { return seat < count_ && ready_[seat]; }
  bool isUndercover(uint8_t seat) const { return seat < count_ && role_[seat] == 1; }
  uint8_t votesAgainst(uint8_t seat) const;
  int8_t lastEliminated() const { return lastEliminated_; }
  const char* message() const { return message_; }
  // 0 while playing, 1 civilians won, 2 undercover won.
  uint8_t winner() const { return winner_; }

 private:
  void tallyVotes();
  void checkWinner();
  void nextRound();
  void setMessage(const char* text);

  uint8_t count_ = 0;
  uint8_t role_[MAX_PLAYERS] = {};  // 0 civilian, 1 undercover
  bool alive_[MAX_PLAYERS] = {};
  bool ready_[MAX_PLAYERS] = {};
  int8_t vote_[MAX_PLAYERS] = {};  // seat voted for, -1 when not cast
  uint16_t pair_ = 0;
  Phase phase_ = Phase::Describe;
  uint8_t round_ = 1;
  int8_t lastEliminated_ = -1;
  uint8_t winner_ = 0;
  char message_[64] = {};
};

}  // namespace party
