#pragma once

#include "PartyGames.h"

namespace party {

// One game round. Implementations hold plain data only (no heap, no Arduino) so
// GameSession can build them in place in its own storage and the host tests can
// drive them directly.
class Game {
 public:
  virtual ~Game() = default;

  virtual GameId id() const = 0;

  // Deal a fresh round for the first `playerCount` seats.
  virtual void start(uint8_t playerCount, Rng& rng) = 0;

  // Applies a decoded action from `seat`. `isHost` carries the table's
  // authority: the device's own buttons act as the host, so a game that gates a
  // phase change on the host must test this rather than assume seat 0. Returns
  // true when state changed, which is what bumps the session version and
  // repaints the shared screen.
  virtual bool apply(uint8_t seat, const Action& action, bool isHost) = 0;

  virtual bool isOver() const = 0;

  // One-line banner for the shared screen, written into a caller-owned buffer.
  virtual void statusLine(char* out, size_t cap) const = 0;

  // The phone's view of the round as a JSON object, including whatever only
  // `seat` is allowed to see. seat < 0 is a spectator.
  virtual void writeView(JsonBuf& out, int8_t seat) const = 0;
};

struct GameMeta {
  GameId id;
  const char* key;    // wire identifier, e.g. "codenames"
  const char* title;  // English title; the device UI draws the translated one
  uint8_t minPlayers;
  uint8_t maxPlayers;
};

const GameMeta& gameMeta(GameId id);
// Menu order, 0..GAME_COUNT-1. Returns GameId::None when out of range.
GameId gameAt(uint8_t index);
uint8_t gameIndex(GameId id);

}  // namespace party
