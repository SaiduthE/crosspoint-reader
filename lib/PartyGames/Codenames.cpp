#include "Codenames.h"

#include <cstdio>
#include <cstring>

#include "WordBank.h"

namespace party {

namespace {
const char* teamName(const uint8_t team) { return team == Codenames::RED ? "Red" : "Blue"; }
}  // namespace

void Codenames::start(const uint8_t playerCount, Rng& rng) {
  count_ = playerCount > MAX_PLAYERS ? MAX_PLAYERS : playerCount;

  // Distinct words: rejection sampling over 25 draws from a ~380 word bank is a
  // few hundred comparisons, cheaper than shuffling the whole bank.
  const uint16_t bank = words::codeWordCount();
  for (uint8_t i = 0; i < TILES; i++) {
    bool unique;
    uint16_t pick;
    do {
      pick = static_cast<uint16_t>(rng.below(bank));
      unique = true;
      for (uint8_t j = 0; j < i; j++) {
        if (word_[j] == pick) unique = false;
      }
    } while (!unique);
    word_[i] = pick;
    revealed_[i] = false;
  }

  // The team that starts gets the extra agent.
  turn_ = rng.below(2) ? BLUE : RED;
  const uint8_t other = turn_ == RED ? BLUE : RED;
  uint8_t keys[TILES];
  uint8_t at = 0;
  for (uint8_t i = 0; i < 9; i++) keys[at++] = turn_;
  for (uint8_t i = 0; i < 8; i++) keys[at++] = other;
  keys[at++] = ASSASSIN;
  while (at < TILES) keys[at++] = NEUTRAL;
  rng.shuffle(keys, TILES);
  for (uint8_t i = 0; i < TILES; i++) key_[i] = keys[i];

  // Split the seats at random, then the first seat of each team spymasters.
  uint8_t order[MAX_PLAYERS];
  for (uint8_t i = 0; i < count_; i++) order[i] = i;
  rng.shuffle(order, count_);
  const uint8_t half = static_cast<uint8_t>(count_ / 2);
  for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
    team_[i] = 0;
    spymaster_[i] = false;
  }
  for (uint8_t i = 0; i < count_; i++) {
    const uint8_t seat = order[i];
    team_[seat] = i < half ? RED : BLUE;
    spymaster_[seat] = (i == 0) || (i == half);
  }

  phase_ = Phase::Clue;
  clue_[0] = '\0';
  clueCount_ = 0;
  guessesLeft_ = 0;
  winner_ = 0;
  snprintf(message_, sizeof(message_), "%s spymaster gives the first clue", teamName(turn_));
}

const char* Codenames::tileWord(const uint8_t tile) const { return tile < TILES ? words::codeWord(word_[tile]) : ""; }

uint8_t Codenames::remaining(const uint8_t team) const {
  uint8_t left = 0;
  for (uint8_t i = 0; i < TILES; i++) {
    if (key_[i] == team && !revealed_[i]) left++;
  }
  return left;
}

void Codenames::endTurn() {
  turn_ = turn_ == RED ? BLUE : RED;
  phase_ = Phase::Clue;
  clue_[0] = '\0';
  clueCount_ = 0;
  guessesLeft_ = 0;
}

void Codenames::reveal(const uint8_t tile) {
  revealed_[tile] = true;
  const uint8_t hit = key_[tile];
  const uint8_t guessing = turn_;
  const uint8_t other = guessing == RED ? BLUE : RED;

  if (hit == ASSASSIN) {
    winner_ = other;
    phase_ = Phase::Over;
    snprintf(message_, sizeof(message_), "%s hit the assassin - %s wins", teamName(guessing), teamName(other));
    return;
  }

  if (hit == guessing) {
    if (remaining(guessing) == 0) {
      winner_ = guessing;
      phase_ = Phase::Over;
      snprintf(message_, sizeof(message_), "%s found every agent", teamName(guessing));
      return;
    }
    snprintf(message_, sizeof(message_), "%s: correct, %u agent%s left", teamName(guessing), remaining(guessing),
             remaining(guessing) == 1 ? "" : "s");
    if (guessesLeft_ > 0) guessesLeft_--;
    if (guessesLeft_ == 0) endTurn();
    return;
  }

  if (hit == other) {
    if (remaining(other) == 0) {
      winner_ = other;
      phase_ = Phase::Over;
      snprintf(message_, sizeof(message_), "%s handed %s the win", teamName(guessing), teamName(other));
      return;
    }
    snprintf(message_, sizeof(message_), "That one was %s - turn over", teamName(other));
    endTurn();
    return;
  }

  snprintf(message_, sizeof(message_), "Bystander - turn over");
  endTurn();
}

bool Codenames::apply(const uint8_t seat, const Action& action, const bool isHost) {
  (void)isHost;  // every phase here is gated by whose turn it is
  if (seat >= count_ || phase_ == Phase::Over) return false;
  if (team_[seat] != turn_) return false;

  switch (action.verb) {
    case Verb::Clue: {
      if (phase_ != Phase::Clue || !spymaster_[seat]) return false;
      if (action.text[0] == '\0') return false;
      const int16_t n = action.a;
      if (n < 0 || n > 9) return false;
      snprintf(clue_, sizeof(clue_), "%s", action.text);
      clueCount_ = static_cast<uint8_t>(n);
      // The traditional bonus guess: a team may always try one more than the
      // number given.
      guessesLeft_ = static_cast<uint8_t>(n + 1);
      phase_ = Phase::Guess;
      snprintf(message_, sizeof(message_), "%s: %s %u", teamName(turn_), clue_, clueCount_);
      return true;
    }

    case Verb::Guess: {
      if (phase_ != Phase::Guess || spymaster_[seat]) return false;
      const int16_t tile = action.a;
      if (tile < 0 || tile >= TILES || revealed_[tile]) return false;
      reveal(static_cast<uint8_t>(tile));
      return true;
    }

    case Verb::Pass: {
      if (phase_ != Phase::Guess || spymaster_[seat]) return false;
      snprintf(message_, sizeof(message_), "%s passed", teamName(turn_));
      endTurn();
      return true;
    }

    default:
      return false;
  }
}

void Codenames::statusLine(char* out, const size_t cap) const {
  switch (phase_) {
    case Phase::Clue:
      snprintf(out, cap, "%s spymaster: give a clue (Red %u - Blue %u)", teamName(turn_), remaining(RED),
               remaining(BLUE));
      return;
    case Phase::Guess:
      snprintf(out, cap, "%s: \"%s %u\" - %u guess%s left", teamName(turn_), clue_, clueCount_, guessesLeft_,
               guessesLeft_ == 1 ? "" : "es");
      return;
    case Phase::Over:
      snprintf(out, cap, "%s", message_);
      return;
  }
}

void Codenames::writeView(JsonBuf& out, const int8_t seat) const {
  const bool seated = seat >= 0 && seat < static_cast<int8_t>(count_);
  const bool spy = seated && spymaster_[seat];

  out.ch('{');
  out.keyStr("phase", phase_ == Phase::Clue ? "clue" : phase_ == Phase::Guess ? "guess" : "over");
  out.ch(',');
  out.keyNum("turn", turn_);
  out.ch(',');
  out.keyNum("myTeam", seated ? team_[seat] : 0);
  out.ch(',');
  out.keyBool("spymaster", spy);
  out.ch(',');
  out.keyStr("clue", clue_);
  out.ch(',');
  out.keyNum("clueCount", clueCount_);
  out.ch(',');
  out.keyNum("guessesLeft", guessesLeft_);
  out.ch(',');
  out.keyNum("red", remaining(RED));
  out.ch(',');
  out.keyNum("blue", remaining(BLUE));
  out.ch(',');
  out.keyNum("winner", winner_);
  out.ch(',');
  out.keyStr("msg", message_);
  out.ch(',');

  out.key("tiles");
  out.ch('[');
  for (uint8_t i = 0; i < TILES; i++) {
    if (i) out.ch(',');
    out.ch('{');
    out.keyStr("w", tileWord(i));
    out.ch(',');
    out.keyBool("r", revealed_[i]);
    out.ch(',');
    // The key is the spymaster's alone until a tile is turned over.
    out.keyNum("k", (spy || revealed_[i]) ? key_[i] : -1);
    out.ch('}');
  }
  out.ch(']');
  out.ch('}');
}

}  // namespace party
