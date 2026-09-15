# Party games

Game Night turns the reader into the shared board for a table of people. The
device raises its own open access point and shows two QR codes: one joins the
Wi-Fi, one opens the page. Every phone that joins gets a private view of the
same session — a dice cup, a secret word, a spymaster's key — while the e-ink
panel shows the one thing everybody is allowed to see.

Nothing leaves the room: there is no internet leg, no account and no server
beyond the reader itself.

## Starting a game night

1. **File Transfer → Game Night** on the device.
2. The reader starts the `CrossPoint-Games` access point (open, up to eight
   stations) and shows the lobby.
3. Players scan the left QR to join the network. Most phones then open the page
   by themselves through the captive portal; the right QR (and
   `http://192.168.4.1/`) is there for the ones that do not.
4. The first player to join is the host. The host picks the game, from their
   phone or with the device's own buttons, and starts it.

### Device controls

| Screen | Button | Action |
|--------|--------|--------|
| Lobby | Up / Down | Previous / next game |
| Lobby | Confirm | Start the selected game |
| Lobby | Back | Leave game night |
| Round | Confirm | The host's "next": deal again, or push the phase along |
| Round | Back | End the round and return to the lobby |

The device acts with the host's authority, so a round never stalls because the
host's phone is in someone's pocket.

## The games

| Game | Seats | Plays | The phone holds |
|------|-------|-------|-----------------|
| Undercover | 3–8 | everyone | your secret word, and your vote |
| Codenames | 4–8 | everyone | the colour key, for the two spymasters |
| Battleship | 2 | first two seats | your fleet and your shots |
| Ludo | 2–4 | first four seats | the dice and your tokens |

Extra players in the lobby are spectators for the two-seat and four-seat games:
they still see the board on the shared screen.

**Undercover.** Everyone gets the same word except the undercover, who gets a
close neighbour of it (two undercovers from six players up). Players describe
their word out loud, then vote on their phones. Civilians win when the last
undercover is out; the undercover wins on reaching parity. A tied vote
eliminates nobody.

**Codenames.** Standard rules: 25 words, 9 agents for the team that starts,
8 for the other, 7 bystanders, one assassin. The spymaster's clue buys one more
guess than the number given, a wrong colour ends the turn, and the assassin ends
the game. On a monochrome panel the teams are a fill pattern rather than a
colour, so the board carries a legend: Red is solid, Blue is hatched, a
bystander is struck through, the assassin is crossed out.

**Battleship.** Carrier, battleship, cruiser, submarine, destroyer on a 10×10
grid. Place them by tapping your phone, or press Ready and the rest drop in at
random. One shot per turn, hit or miss. The shared screen shows both grids with
the ships hidden — only hits, misses and sunk hulls.

**Ludo.** Four tokens each, a six to leave the yard, an exact roll to reach
home, capture on an unsafe square, an extra turn for a six, a capture or a token
coming home, and three sixes in a row forfeits the turn.

## Architecture

| Piece | Where | Depends on |
|-------|-------|------------|
| Rules, seats, JSON views | `lib/PartyGames/` | nothing — plain C++ |
| HTTP surface | `src/network/GameServer.cpp` | Arduino `WebServer` |
| Phone page | `src/network/html/PlayPage.html` | nothing, baked into flash |
| Screen, access point, lifecycle | `src/activities/games/GameNightActivity.cpp` | the activity stack |
| Board pixels | `src/activities/games/GameBoards.cpp` | `GfxRenderer` |

The split is the point. `lib/PartyGames` has no Arduino and no renderer in it,
so the rules run under `test/party_games` on a host and a rule change is
provable without a device. Everything that needs pixels or a socket lives on the
device side of the line.

`GameServer` is deliberately not `CrossPointWebServer`: the file manager,
WebDAV and the WebSocket upload path have no business in front of a room full of
guests, and they cost heap that the access point and the session need.

## Endpoints

Served on port 80 while Game Night is running. Anything that is not `/api/*`
redirects to `/`, which is what makes the captive portal pop the page open.

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/` | The phone page (gzipped, ETagged) |
| `GET` | `/api/game/state?t=<token>` | The caller's view of the session |
| `POST` | `/api/game/join?name=<name>` | Take a seat; returns a token |
| `POST` | `/api/game/act?t=<token>&v=<verb>&a=&b=&c=&s=` | Play |
| `POST` | `/api/game/leave?t=<token>` | Give the seat back |

`t` is a 32-bit hex token from `esp_random()`, kept in the phone's
`localStorage` so a reload does not consume a second seat. The token is a seat
key, not a credential: the network is open, and the threat model is a room of
people you invited.

Every action replies with the state it produced, so a tap does not wait out the
poll interval. Phones poll `state` about once a second; a poll costs one
serialization into a single fixed buffer, no allocation.

Verbs: `ready`, `next`, `pick`, `clue`, `guess`, `pass`, `place`, `fire`,
`vote`, `roll`, `move`. The session refuses anything that is not that seat's to
do, so an edited URL gets a 200 and no change.

### State document

```json
{
  "v": 42, "seat": 2, "name": "Ann", "host": false,
  "game": "codenames", "title": "Codenames", "phase": "play",
  "canStart": true, "minPlayers": 4, "maxPlayers": 8,
  "status": "Red: OCEAN 2 - 3 guesses left",
  "players": [{"s": 0, "n": "Ann", "host": true, "away": false}],
  "over": false,
  "g": { "...": "the game's own view, private parts included" }
}
```

`v` is the session version. It moves only when something changed, which is what
the shared screen repaints on — a phone polling eight times a second never costs
a panel refresh.

In the lobby the document carries a `games` array instead of `g`, so the host's
phone can offer the picker without knowing what this firmware was built with.

## Costs

- **Flash**: about 5 KB for the phone page (gzipped), 5 KB of word lists, and
  the code. Measure a build before and after if it matters on a 16 MB part.
- **DRAM**: the session is 640 bytes of in-place game storage plus eight seats;
  the server holds one 3 KB JSON buffer. The active game is built in place with
  placement new rather than on the heap, because a party churns through rounds
  and a repeated new/delete of a ~500 byte object is exactly the fragmentation
  a reader with WiFi up cannot afford.
- **Panel**: repaints are rate-limited to one per 700 ms and promoted to a full
  refresh every eighth paint to clear the ghosting that fast refreshes leave.

## Adding a game

1. Subclass `party::Game` in `lib/PartyGames/`. Plain data members only: no
   heap, no Arduino, no `std::string`.
2. Add it to `GameId`, to the `kGames` table in `GameSession.cpp`, and to the
   `startGame()` switch. The `static_assert` there will tell you if it outgrew
   the in-place storage.
3. Write `writeView()` so a phone sees its own secrets and nobody else's. This
   is the part worth testing hardest.
4. Add a board painter in `GameBoards.cpp` and a case to `drawBoard()` and
   `statusFor()`.
5. Add a panel to `PlayPage.html`.
6. Add a title string to `lib/I18n/translations/english.yaml` and a case to
   `titleFor()`.
7. Test the rules in `test/party_games/PartyGamesTest.cpp`:

   ```bash
   cmake -S test -B build/test
   cmake --build build/test --target PartyGamesTest
   ./build/test/party_games/PartyGamesTest
   ```

## Not done yet

- **Access point only.** There is no "join my home network" mode; the reader
  always hosts. On a phone that means losing internet for the duration.
- **No touch input on the board.** Everything on the device is driven by the
  physical buttons, even on touch boards.
- **Board text is English.** The device's chrome and status line go through
  `tr()`; the phone page and the words on the tiles do not, the same way the
  rest of the web UI does not.
- **No text adventure.** It wants a story format on the SD card rather than
  another compiled-in rule set, which is a different piece of work.
