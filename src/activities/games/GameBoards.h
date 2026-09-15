#pragma once

#include <GameSession.h>
#include <GfxRenderer.h>

#include "components/themes/BaseTheme.h"

// The shared screen's half of party games: one board painter per game, plus the
// seat list the lobby and Undercover both use.
//
// Kept apart from lib/PartyGames on purpose. The rules are pure C++ and run
// under the host tests; everything here needs the renderer, so it lives with the
// activity that owns the frame.
namespace games {

// Paints the round in progress. Assumes session.game() is non-null.
void drawBoard(const GfxRenderer& renderer, const party::GameSession& session, const Rect& content);

// Seat list with host, away and (during Undercover) vote state.
void drawSeatList(const GfxRenderer& renderer, const party::GameSession& session, const Rect& content, uint32_t nowMs);

// The translated title of a game, for the header and the lobby picker.
const char* titleFor(party::GameId id);

// The translated one-line banner for the shared screen. The English text the
// games carry in statusLine() is the phones' copy; this is the device's.
void statusFor(const party::GameSession& session, char* out, size_t cap);

}  // namespace games
