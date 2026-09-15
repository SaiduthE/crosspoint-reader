#include "Ludo.h"

#include <cstdio>
#include <cstring>

namespace party {

namespace {

// The shared ring, clockwise, as 15x15 board cells. Index 0 is player 0's start
// square; every player starts 13 cells further round. Generated once and
// asserted by test/party_games: consecutive cells always touch, and the four
// diagonal steps are the track wrapping the corners of the centre square.
constexpr Ludo::Cell kTrack[Ludo::TRACK] = {
    {0, 6},  {1, 6},  {2, 6},  {3, 6},  {4, 6},  {5, 6}, {6, 5}, {6, 4},  {6, 3},  {6, 2},  {6, 1},  {6, 0},  {7, 0},
    {8, 0},  {8, 1},  {8, 2},  {8, 3},  {8, 4},  {8, 5}, {9, 6}, {10, 6}, {11, 6}, {12, 6}, {13, 6}, {14, 6}, {14, 7},
    {14, 8}, {13, 8}, {12, 8}, {11, 8}, {10, 8}, {9, 8}, {8, 9}, {8, 10}, {8, 11}, {8, 12}, {8, 13}, {8, 14}, {7, 14},
    {6, 14}, {6, 13}, {6, 12}, {6, 11}, {6, 10}, {6, 9}, {5, 8}, {4, 8},  {3, 8},  {2, 8},  {1, 8},  {0, 8},  {0, 7},
};

// The five-cell run into the centre, entered from the ring cell before the
// player's own start square.
constexpr Ludo::Cell kHome[4][5] = {
    {
        {1, 7},
        {2, 7},
        {3, 7},
        {4, 7},
        {5, 7},
    },
    {
        {7, 1},
        {7, 2},
        {7, 3},
        {7, 4},
        {7, 5},
    },
    {
        {13, 7},
        {12, 7},
        {11, 7},
        {10, 7},
        {9, 7},
    },
    {
        {7, 13},
        {7, 12},
        {7, 11},
        {7, 10},
        {7, 9},
    },
};

// Four parking spots inside each corner yard.
constexpr Ludo::Cell kYard[4][Ludo::TOKENS] = {
    {
        {1, 1},
        {4, 1},
        {1, 4},
        {4, 4},
    },
    {
        {10, 1},
        {13, 1},
        {10, 4},
        {13, 4},
    },
    {
        {10, 10},
        {13, 10},
        {10, 13},
        {13, 13},
    },
    {
        {1, 10},
        {4, 10},
        {1, 13},
        {4, 13},
    },
};

// Own start squares plus the four star squares, eight steps on from each.
constexpr uint8_t kSafe[] = {0, 8, 13, 21, 26, 34, 39, 47};

}  // namespace

Ludo::Cell Ludo::trackCell(const uint8_t index) { return kTrack[index % TRACK]; }

Ludo::Cell Ludo::homeCell(const uint8_t player, const uint8_t step) { return kHome[player % 4][step % 5]; }

Ludo::Cell Ludo::yardCell(const uint8_t player, const uint8_t token) { return kYard[player % 4][token % TOKENS]; }

uint8_t Ludo::absoluteCell(const uint8_t player, const int8_t position) {
  return static_cast<uint8_t>((startIndex(player) + static_cast<uint8_t>(position)) % TRACK);
}

bool Ludo::isSafeCell(const uint8_t trackIndex) {
  for (const uint8_t safe : kSafe) {
    if (safe == trackIndex) return true;
  }
  return false;
}

void Ludo::start(const uint8_t playerCount, Rng& rng) {
  count_ = playerCount < 2 ? 2 : (playerCount > 4 ? 4 : playerCount);
  for (uint8_t p = 0; p < 4; p++) {
    for (uint8_t t = 0; t < TOKENS; t++) pos_[p][t] = IN_YARD;
  }
  turn_ = 0;
  die_ = 0;
  sixStreak_ = 0;
  phase_ = Phase::Roll;
  winner_ = -1;
  rng_.reseed(rng.next());
  snprintf(message_, sizeof(message_), "Seat 1 rolls first - a six brings a token out");
}

int8_t Ludo::position(const uint8_t player, const uint8_t token) const {
  if (player >= count_ || token >= TOKENS) return IN_YARD;
  return pos_[player][token];
}

uint8_t Ludo::tokensHome(const uint8_t player) const {
  if (player >= count_) return 0;
  uint8_t done = 0;
  for (uint8_t t = 0; t < TOKENS; t++) {
    if (pos_[player][t] == static_cast<int8_t>(HOME)) done++;
  }
  return done;
}

bool Ludo::canMove(const uint8_t player, const uint8_t token, const uint8_t die) const {
  const int8_t at = pos_[player][token];
  if (at == static_cast<int8_t>(HOME)) return false;
  if (at == IN_YARD) return die == 6;
  // The home square has to be reached exactly.
  return at + static_cast<int8_t>(die) <= static_cast<int8_t>(HOME);
}

uint8_t Ludo::legalMoves(const uint8_t player, const uint8_t die, uint8_t* out) const {
  uint8_t found = 0;
  if (player >= count_ || die == 0) return 0;
  for (uint8_t t = 0; t < TOKENS; t++) {
    if (canMove(player, t, die)) out[found++] = t;
  }
  return found;
}

bool Ludo::captureAt(const uint8_t mover, const uint8_t trackIndex) {
  if (isSafeCell(trackIndex)) return false;
  bool captured = false;
  for (uint8_t p = 0; p < count_; p++) {
    if (p == mover) continue;
    for (uint8_t t = 0; t < TOKENS; t++) {
      const int8_t at = pos_[p][t];
      if (at < 0 || at > static_cast<int8_t>(LAST_TRACK)) continue;
      if (absoluteCell(p, at) != trackIndex) continue;
      pos_[p][t] = IN_YARD;
      captured = true;
    }
  }
  return captured;
}

void Ludo::advanceTurn() {
  turn_ = static_cast<uint8_t>((turn_ + 1) % count_);
  sixStreak_ = 0;
  phase_ = Phase::Roll;
}

void Ludo::finishRoll() {
  uint8_t moves[TOKENS];
  if (legalMoves(turn_, die_, moves) == 0) {
    snprintf(message_, sizeof(message_), "Seat %u rolled %u - nothing to move", turn_ + 1, die_);
    advanceTurn();
    return;
  }
  phase_ = Phase::Move;
  snprintf(message_, sizeof(message_), "Seat %u rolled %u", turn_ + 1, die_);
}

bool Ludo::apply(const uint8_t seat, const Action& action, const bool isHost) {
  (void)isHost;  // every phase here is gated by whose turn it is
  if (phase_ == Phase::Over || seat >= count_ || seat != turn_) return false;

  switch (action.verb) {
    case Verb::Roll: {
      if (phase_ != Phase::Roll) return false;
      die_ = static_cast<uint8_t>(1 + rng_.below(6));
      if (die_ == 6) {
        sixStreak_++;
        if (sixStreak_ >= 3) {
          // Three sixes in a row forfeits the turn, or a lucky streak never ends.
          snprintf(message_, sizeof(message_), "Seat %u rolled three sixes - turn lost", turn_ + 1);
          advanceTurn();
          return true;
        }
      }
      finishRoll();
      return true;
    }

    case Verb::Move: {
      if (phase_ != Phase::Move) return false;
      const int16_t token = action.a;
      if (token < 0 || token >= TOKENS) return false;
      if (!canMove(seat, static_cast<uint8_t>(token), die_)) return false;

      int8_t& at = pos_[seat][token];
      if (at == IN_YARD) {
        at = 0;
      } else {
        at = static_cast<int8_t>(at + die_);
      }

      bool extraTurn = die_ == 6;
      if (at <= static_cast<int8_t>(LAST_TRACK) && captureAt(seat, absoluteCell(seat, at))) {
        snprintf(message_, sizeof(message_), "Seat %u sends a token home - roll again", seat + 1);
        extraTurn = true;
      } else if (at == static_cast<int8_t>(HOME)) {
        snprintf(message_, sizeof(message_), "Seat %u brings a token home (%u of 4)", seat + 1, tokensHome(seat));
        extraTurn = true;
      }

      if (tokensHome(seat) == TOKENS) {
        winner_ = static_cast<int8_t>(seat);
        phase_ = Phase::Over;
        snprintf(message_, sizeof(message_), "Seat %u is home with all four", seat + 1);
        return true;
      }

      if (extraTurn) {
        phase_ = Phase::Roll;
        if (die_ != 6) sixStreak_ = 0;  // only a six extends the streak
      } else {
        advanceTurn();
      }
      return true;
    }

    default:
      return false;
  }
}

void Ludo::statusLine(char* out, const size_t cap) const {
  switch (phase_) {
    case Phase::Roll:
      snprintf(out, cap, "Seat %u to roll - %s", turn_ + 1, message_);
      return;
    case Phase::Move:
      snprintf(out, cap, "Seat %u rolled %u - pick a token", turn_ + 1, die_);
      return;
    case Phase::Over:
      snprintf(out, cap, "%s", message_);
      return;
  }
}

void Ludo::writeView(JsonBuf& out, const int8_t seat) const {
  const bool playing = seat >= 0 && seat < static_cast<int8_t>(count_);

  out.ch('{');
  out.keyStr("phase", phase_ == Phase::Roll ? "roll" : phase_ == Phase::Move ? "move" : "over");
  out.ch(',');
  out.keyNum("turn", turn_);
  out.ch(',');
  out.keyNum("me", playing ? seat : -1);
  out.ch(',');
  out.keyNum("die", die_);
  out.ch(',');
  out.keyNum("winner", winner_);
  out.ch(',');
  out.keyStr("msg", message_);
  out.ch(',');
  out.keyBool("myTurn", playing && seat == static_cast<int8_t>(turn_));
  out.ch(',');

  out.key("moves");
  out.ch('[');
  if (playing && phase_ == Phase::Move && seat == static_cast<int8_t>(turn_)) {
    uint8_t moves[TOKENS];
    const uint8_t found = legalMoves(turn_, die_, moves);
    for (uint8_t i = 0; i < found; i++) {
      if (i) out.ch(',');
      out.num(moves[i]);
    }
  }
  out.ch(']');
  out.ch(',');

  out.key("tokens");
  out.ch('[');
  for (uint8_t p = 0; p < count_; p++) {
    if (p) out.ch(',');
    out.ch('[');
    for (uint8_t t = 0; t < TOKENS; t++) {
      if (t) out.ch(',');
      out.num(pos_[p][t]);
    }
    out.ch(']');
  }
  out.ch(']');
  out.ch('}');
}

}  // namespace party
