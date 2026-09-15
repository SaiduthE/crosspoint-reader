#include "Undercover.h"

#include <cstdio>
#include <cstring>

#include "WordBank.h"

namespace party {

namespace {
constexpr uint8_t ROLE_CIVILIAN = 0;
constexpr uint8_t ROLE_UNDERCOVER = 1;
}  // namespace

void Undercover::start(const uint8_t playerCount, Rng& rng) {
  count_ = playerCount > MAX_PLAYERS ? MAX_PLAYERS : playerCount;
  pair_ = static_cast<uint16_t>(rng.below(words::pairCount()));
  phase_ = Phase::Describe;
  round_ = 1;
  lastEliminated_ = -1;
  winner_ = 0;
  message_[0] = '\0';

  uint8_t order[MAX_PLAYERS];
  for (uint8_t i = 0; i < count_; i++) order[i] = i;
  rng.shuffle(order, count_);

  // Six or more players carry two undercovers, or one impostor is swamped.
  const uint8_t undercovers = count_ >= 6 ? 2 : 1;
  for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
    role_[i] = ROLE_CIVILIAN;
    alive_[i] = i < count_;
    ready_[i] = false;
    vote_[i] = -1;
  }
  for (uint8_t i = 0; i < undercovers && i < count_; i++) role_[order[i]] = ROLE_UNDERCOVER;
}

uint8_t Undercover::votesAgainst(const uint8_t seat) const {
  uint8_t total = 0;
  for (uint8_t i = 0; i < count_; i++) {
    if (vote_[i] == static_cast<int8_t>(seat)) total++;
  }
  return total;
}

void Undercover::setMessage(const char* text) { snprintf(message_, sizeof(message_), "%s", text); }

void Undercover::tallyVotes() {
  int8_t leader = -1;
  uint8_t best = 0;
  bool tied = false;
  for (uint8_t i = 0; i < count_; i++) {
    if (!alive_[i]) continue;
    const uint8_t votes = votesAgainst(i);
    if (votes > best) {
      best = votes;
      leader = static_cast<int8_t>(i);
      tied = false;
    } else if (votes == best && votes > 0) {
      tied = true;
    }
  }

  if (leader < 0 || best == 0 || tied) {
    lastEliminated_ = -1;
    setMessage("Tied vote - nobody leaves");
    phase_ = Phase::Reveal;
    return;
  }

  alive_[leader] = false;
  lastEliminated_ = leader;
  phase_ = Phase::Reveal;
  checkWinner();
}

void Undercover::checkWinner() {
  uint8_t civilians = 0;
  uint8_t undercovers = 0;
  for (uint8_t i = 0; i < count_; i++) {
    if (!alive_[i]) continue;
    if (role_[i] == ROLE_UNDERCOVER) {
      undercovers++;
    } else {
      civilians++;
    }
  }

  if (undercovers == 0) {
    winner_ = 1;
    phase_ = Phase::Over;
    setMessage("Civilians win");
  } else if (undercovers >= civilians) {
    winner_ = 2;
    phase_ = Phase::Over;
    setMessage("Undercover wins");
  }
}

void Undercover::nextRound() {
  round_++;
  phase_ = Phase::Describe;
  lastEliminated_ = -1;
  message_[0] = '\0';
  for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
    ready_[i] = false;
    vote_[i] = -1;
  }
}

bool Undercover::apply(const uint8_t seat, const Action& action, const bool isHost) {
  if (seat >= count_) return false;

  switch (action.verb) {
    case Verb::Ready: {
      if (phase_ != Phase::Describe || !alive_[seat] || ready_[seat]) return false;
      ready_[seat] = true;
      bool allReady = true;
      for (uint8_t i = 0; i < count_; i++) {
        if (alive_[i] && !ready_[i]) allReady = false;
      }
      if (allReady) phase_ = Phase::Vote;
      return true;
    }

    case Verb::Vote: {
      if (phase_ != Phase::Vote || !alive_[seat]) return false;
      const int16_t target = action.a;
      if (target < 0 || target >= count_ || !alive_[target] || target == seat) return false;
      if (vote_[seat] == static_cast<int8_t>(target)) return false;
      vote_[seat] = static_cast<int8_t>(target);

      bool allVoted = true;
      for (uint8_t i = 0; i < count_; i++) {
        if (alive_[i] && vote_[i] < 0) allVoted = false;
      }
      if (allVoted) tallyVotes();
      return true;
    }

    case Verb::Next: {
      // The host drives the pace: skip straight to the vote, or move past a
      // reveal. On the device that is whoever is holding it.
      if (!isHost) return false;
      if (phase_ == Phase::Describe) {
        phase_ = Phase::Vote;
        return true;
      }
      if (phase_ == Phase::Vote) {
        tallyVotes();
        return true;
      }
      if (phase_ == Phase::Reveal) {
        nextRound();
        return true;
      }
      return false;
    }

    default:
      return false;
  }
}

void Undercover::statusLine(char* out, const size_t cap) const {
  switch (phase_) {
    case Phase::Describe:
      snprintf(out, cap, "Round %u - describe your word out loud", round_);
      return;
    case Phase::Vote:
      snprintf(out, cap, "Round %u - vote on your phone", round_);
      return;
    case Phase::Reveal:
      if (lastEliminated_ >= 0) {
        snprintf(out, cap, "Seat %d is out - %s", lastEliminated_ + 1,
                 role_[lastEliminated_] == ROLE_UNDERCOVER ? "undercover!" : "a civilian");
      } else {
        snprintf(out, cap, "%s", message_);
      }
      return;
    case Phase::Over:
      snprintf(out, cap, "%s", message_);
      return;
  }
}

void Undercover::writeView(JsonBuf& out, const int8_t seat) const {
  const bool seated = seat >= 0 && seat < static_cast<int8_t>(count_);
  out.ch('{');
  out.keyStr("phase", phase_ == Phase::Describe ? "describe"
                      : phase_ == Phase::Vote   ? "vote"
                      : phase_ == Phase::Reveal ? "reveal"
                                                : "over");
  out.ch(',');
  out.keyNum("round", round_);
  out.ch(',');
  out.keyStr("msg", message_);
  out.ch(',');
  out.keyNum("winner", winner_);
  out.ch(',');

  // The player's own word, never the role: not knowing which side you are on is
  // the whole game.
  out.key("word");
  if (seated) {
    out.str(role_[seat] == ROLE_UNDERCOVER ? words::pairUndercover(pair_) : words::pairCivilian(pair_));
  } else {
    out.str("");
  }
  out.ch(',');
  out.keyBool("alive", seated && alive_[seat]);
  out.ch(',');
  out.keyNum("myVote", seated ? vote_[seat] : -1);
  out.ch(',');
  out.keyBool("ready", seated && ready_[seat]);
  out.ch(',');

  out.key("seats");
  out.ch('[');
  for (uint8_t i = 0; i < count_; i++) {
    if (i) out.ch(',');
    out.ch('{');
    out.keyBool("alive", alive_[i]);
    out.ch(',');
    out.keyBool("ready", ready_[i]);
    out.ch(',');
    out.keyNum("votes", votesAgainst(i));
    out.ch(',');
    // Roles stay hidden until the round is over.
    out.keyNum("role", phase_ == Phase::Over || !alive_[i] ? role_[i] : -1);
    out.ch('}');
  }
  out.ch(']');
  out.ch('}');
}

}  // namespace party
