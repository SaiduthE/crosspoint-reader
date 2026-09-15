#include "Battleship.h"

#include <cstdio>
#include <cstring>

namespace party {

namespace {
constexpr uint8_t kShipLengths[Battleship::SHIPS] = {5, 4, 3, 3, 2};
constexpr const char* kShipNames[Battleship::SHIPS] = {"Carrier", "Battleship", "Cruiser", "Submarine", "Destroyer"};
inline uint8_t cellIndex(const uint8_t x, const uint8_t y) { return static_cast<uint8_t>(y * Battleship::SIZE + x); }
}  // namespace

uint8_t Battleship::shipLength(const uint8_t ship) { return ship < SHIPS ? kShipLengths[ship] : 0; }

void Battleship::start(const uint8_t playerCount, Rng& rng) {
  (void)playerCount;  // always the first two seats; the rest spectate
  memset(board_, 0, sizeof(board_));
  memset(shots_, 0, sizeof(shots_));
  ready_[0] = ready_[1] = false;
  turn_ = 0;
  winner_ = -1;
  phase_ = Phase::Place;
  rng_.reseed(rng.next());
  snprintf(message_, sizeof(message_), "Place your fleet on your phone");
}

bool Battleship::canPlace(const uint8_t player, const uint8_t ship, const uint8_t x, const uint8_t y,
                          const bool horizontal) const {
  const uint8_t len = shipLength(ship);
  if (len == 0) return false;
  if (horizontal ? (x + len > SIZE) : (y + len > SIZE)) return false;
  for (uint8_t i = 0; i < len; i++) {
    const uint8_t cell =
        horizontal ? cellIndex(static_cast<uint8_t>(x + i), y) : cellIndex(x, static_cast<uint8_t>(y + i));
    const uint8_t occupant = board_[player][cell];
    if (occupant != 0 && occupant != ship + 1) return false;
  }
  return true;
}

void Battleship::clearShip(const uint8_t player, const uint8_t ship) {
  for (uint8_t i = 0; i < CELLS; i++) {
    if (board_[player][i] == ship + 1) board_[player][i] = 0;
  }
}

bool Battleship::placeShip(const uint8_t player, const uint8_t ship, const uint8_t x, const uint8_t y,
                           const bool horizontal) {
  if (!canPlace(player, ship, x, y, horizontal)) return false;
  clearShip(player, ship);
  const uint8_t len = shipLength(ship);
  for (uint8_t i = 0; i < len; i++) {
    const uint8_t cell =
        horizontal ? cellIndex(static_cast<uint8_t>(x + i), y) : cellIndex(x, static_cast<uint8_t>(y + i));
    board_[player][cell] = static_cast<uint8_t>(ship + 1);
  }
  return true;
}

void Battleship::autoPlace(const uint8_t player) {
  for (uint8_t ship = 0; ship < SHIPS; ship++) {
    bool placed = false;
    for (uint8_t i = 0; i < CELLS; i++) {
      if (board_[player][i] == ship + 1) placed = true;
    }
    if (placed) continue;
    // Bounded retries, then a deterministic sweep: a 10x10 grid with these five
    // ships always has room, but random probing alone has no upper bound.
    for (uint8_t attempt = 0; attempt < 100 && !placed; attempt++) {
      const uint8_t x = static_cast<uint8_t>(rng_.below(SIZE));
      const uint8_t y = static_cast<uint8_t>(rng_.below(SIZE));
      placed = placeShip(player, ship, x, y, rng_.below(2) != 0);
    }
    for (uint8_t cell = 0; cell < CELLS && !placed; cell++) {
      const uint8_t x = static_cast<uint8_t>(cell % SIZE);
      const uint8_t y = static_cast<uint8_t>(cell / SIZE);
      placed = placeShip(player, ship, x, y, true) || placeShip(player, ship, x, y, false);
    }
  }
}

bool Battleship::isSunk(const uint8_t player, const uint8_t ship) const {
  bool onBoard = false;
  for (uint8_t i = 0; i < CELLS; i++) {
    if (board_[player][i] != ship + 1) continue;
    onBoard = true;
    if (shots_[player][i] != HIT) return false;
  }
  // An unplaced ship is not a sunk one, or a fleet would read as destroyed
  // during placement.
  return onBoard;
}

bool Battleship::allSunk(const uint8_t player) const {
  for (uint8_t ship = 0; ship < SHIPS; ship++) {
    if (!isSunk(player, ship)) return false;
  }
  return true;
}

uint8_t Battleship::shotAt(const uint8_t player, const uint8_t x, const uint8_t y) const {
  if (player >= PLAYERS || x >= SIZE || y >= SIZE) return UNKNOWN;
  return shots_[player][cellIndex(x, y)];
}

bool Battleship::sunkAt(const uint8_t player, const uint8_t x, const uint8_t y) const {
  if (player >= PLAYERS || x >= SIZE || y >= SIZE) return false;
  const uint8_t occupant = board_[player][cellIndex(x, y)];
  return occupant != 0 && isSunk(player, static_cast<uint8_t>(occupant - 1));
}

uint8_t Battleship::shipsLeft(const uint8_t player) const {
  if (player >= PLAYERS) return 0;
  uint8_t afloat = 0;
  for (uint8_t ship = 0; ship < SHIPS; ship++) {
    if (!isSunk(player, ship)) afloat++;
  }
  return afloat;
}

bool Battleship::apply(const uint8_t seat, const Action& action, const bool isHost) {
  (void)isHost;  // every phase here is gated by whose turn it is
  if (seat >= PLAYERS || phase_ == Phase::Over) return false;

  switch (action.verb) {
    case Verb::Place: {
      if (phase_ != Phase::Place || ready_[seat]) return false;
      const int16_t ship = action.a;
      const int16_t x = action.b;
      const int16_t y = action.c;
      if (ship < 0 || ship >= SHIPS || x < 0 || x >= SIZE || y < 0 || y >= SIZE) return false;
      const bool horizontal = action.text[0] != 'v';
      return placeShip(seat, static_cast<uint8_t>(ship), static_cast<uint8_t>(x), static_cast<uint8_t>(y), horizontal);
    }

    case Verb::Ready: {
      if (phase_ != Phase::Place || ready_[seat]) return false;
      autoPlace(seat);  // anything the player did not place is dropped in at random
      ready_[seat] = true;
      if (ready_[0] && ready_[1]) {
        phase_ = Phase::Fire;
        turn_ = 0;
        snprintf(message_, sizeof(message_), "Fleets are out - seat 1 fires first");
      } else {
        snprintf(message_, sizeof(message_), "Waiting for the other captain");
      }
      return true;
    }

    case Verb::Fire: {
      if (phase_ != Phase::Fire || seat != turn_) return false;
      const int16_t x = action.a;
      const int16_t y = action.b;
      if (x < 0 || x >= SIZE || y < 0 || y >= SIZE) return false;
      const uint8_t target = static_cast<uint8_t>(1 - seat);
      const uint8_t cell = cellIndex(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
      if (shots_[target][cell] != UNKNOWN) return false;

      const uint8_t occupant = board_[target][cell];
      shots_[target][cell] = occupant ? HIT : MISS;
      if (occupant) {
        const uint8_t ship = static_cast<uint8_t>(occupant - 1);
        if (allSunk(target)) {
          winner_ = static_cast<int8_t>(seat);
          phase_ = Phase::Over;
          snprintf(message_, sizeof(message_), "Seat %u sank the last ship", seat + 1);
          return true;
        }
        if (isSunk(target, ship)) {
          snprintf(message_, sizeof(message_), "%s sunk!", kShipNames[ship]);
        } else {
          snprintf(message_, sizeof(message_), "Hit at %c%u", static_cast<char>('A' + x), y + 1);
        }
      } else {
        snprintf(message_, sizeof(message_), "Miss at %c%u", static_cast<char>('A' + x), y + 1);
      }
      // One shot per turn, hit or miss.
      turn_ = target;
      return true;
    }

    default:
      return false;
  }
}

void Battleship::statusLine(char* out, const size_t cap) const {
  switch (phase_) {
    case Phase::Place:
      snprintf(out, cap, "Placing fleets: seat 1 %s, seat 2 %s", ready_[0] ? "ready" : "...",
               ready_[1] ? "ready" : "...");
      return;
    case Phase::Fire:
      snprintf(out, cap, "Seat %u to fire - %s", turn_ + 1, message_);
      return;
    case Phase::Over:
      snprintf(out, cap, "%s", message_);
      return;
  }
}

void Battleship::writeView(JsonBuf& out, const int8_t seat) const {
  const bool playing = seat >= 0 && seat < static_cast<int8_t>(PLAYERS);

  out.ch('{');
  out.keyStr("phase", phase_ == Phase::Place ? "place" : phase_ == Phase::Fire ? "fire" : "over");
  out.ch(',');
  out.keyNum("me", playing ? seat : -1);
  out.ch(',');
  out.keyNum("turn", turn_);
  out.ch(',');
  out.keyNum("winner", winner_);
  out.ch(',');
  out.keyBool("ready", playing && ready_[seat]);
  out.ch(',');
  out.keyStr("msg", message_);
  out.ch(',');

  // Grids travel as one character per cell: 100 digits beats 100 JSON objects
  // on a link that repolls every second.
  out.key("fleet");
  out.ch('"');
  for (uint8_t i = 0; i < CELLS; i++) out.ch(playing ? static_cast<char>('0' + board_[seat][i]) : '0');
  out.ch('"');
  out.ch(',');

  out.key("incoming");
  out.ch('"');
  for (uint8_t i = 0; i < CELLS; i++) out.ch(playing ? static_cast<char>('0' + shots_[seat][i]) : '0');
  out.ch('"');
  out.ch(',');

  out.key("outgoing");
  out.ch('"');
  const uint8_t foe = playing ? static_cast<uint8_t>(1 - seat) : 0;
  for (uint8_t i = 0; i < CELLS; i++) out.ch(playing ? static_cast<char>('0' + shots_[foe][i]) : '0');
  out.ch('"');
  out.ch(',');

  out.key("ships");
  out.ch('[');
  for (uint8_t ship = 0; ship < SHIPS; ship++) {
    if (ship) out.ch(',');
    out.ch('{');
    out.keyStr("n", kShipNames[ship]);
    out.ch(',');
    out.keyNum("len", kShipLengths[ship]);
    out.ch(',');
    out.keyBool("sunk", playing && isSunk(seat, ship));
    out.ch('}');
  }
  out.ch(']');
  out.ch('}');
}

}  // namespace party
