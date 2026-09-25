#include "GameBoards.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"

namespace games {

namespace {

using party::Battleship;
using party::Codenames;
using party::GameSession;
using party::Ludo;
using party::Undercover;

constexpr int TILE_GAP = 4;

// Hand-placed spacing below is written at 1x; the panel's UI scale applies.
int px(const int v) { return UITheme::scaledPx(v); }

// Centres one line of text in a box. Everything drawn here is a short label, so
// there is no wrapping: the caller sizes the box from getTextWidth() when it
// matters.
void centerText(const GfxRenderer& renderer, const int fontId, const Rect& box, const char* text,
                const bool black = true, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int width = renderer.getTextWidth(fontId, text, style);
  const int x = box.x + (box.width - width) / 2;
  const int y = box.y + (box.height - renderer.getLineHeight(fontId)) / 2;
  renderer.drawText(fontId, x, y, text, black, style);
}

// Picks the largest UI font whose rendering of `text` still fits `maxWidth`.
int fontThatFits(const GfxRenderer& renderer, const char* text, const int maxWidth) {
  if (renderer.getTextWidth(UI_10_FONT_ID, text) <= maxWidth) return UI_10_FONT_ID;
  return SMALL_FONT_ID;
}

void drawSwatch(const GfxRenderer& renderer, const Rect& box, const uint8_t style) {
  switch (style) {
    case 0:  // solid
      renderer.fillRect(box.x, box.y, box.width, box.height, true);
      break;
    case 1:  // outline
      renderer.drawRect(box.x, box.y, box.width, box.height, 2, true);
      break;
    case 2:  // light hatch
      renderer.fillRectDither(box.x, box.y, box.width, box.height, Color::LightGray);
      renderer.drawRect(box.x, box.y, box.width, box.height, 1, true);
      break;
    default:  // dark hatch
      renderer.fillRectDither(box.x, box.y, box.width, box.height, Color::DarkGray);
      renderer.drawRect(box.x, box.y, box.width, box.height, 1, true);
      break;
  }
}

const char* seatName(const GameSession& session, const uint8_t seat) {
  static char fallback[12];
  if (session.isSeated(seat)) return session.player(seat).name;
  snprintf(fallback, sizeof(fallback), "Seat %u", seat + 1);
  return fallback;
}

void undercoverStatus(const GameSession& session, const Undercover& game, char* out, const size_t cap) {
  switch (game.phase()) {
    case Undercover::Phase::Describe:
      snprintf(out, cap, tr(STR_UC_DESCRIBE_FORMAT), game.roundNumber());
      return;
    case Undercover::Phase::Vote:
      snprintf(out, cap, tr(STR_UC_VOTE_FORMAT), game.roundNumber());
      return;
    case Undercover::Phase::Reveal: {
      const int8_t out_seat = game.lastEliminated();
      if (out_seat < 0) {
        snprintf(out, cap, "%s", tr(STR_UC_TIE_VOTE));
      } else {
        const uint8_t seat = static_cast<uint8_t>(out_seat);
        snprintf(
            out, cap,
            I18N.get(game.isUndercover(seat) ? StrId::STR_UC_OUT_UNDERCOVER_FORMAT : StrId::STR_UC_OUT_CIVILIAN_FORMAT),
            seatName(session, seat));
      }
      return;
    }
    case Undercover::Phase::Over:
      snprintf(out, cap, "%s",
               I18N.get(game.winner() == 1 ? StrId::STR_UC_CIVILIANS_WIN : StrId::STR_UC_UNDERCOVER_WINS));
      return;
  }
}

void codenamesStatus(const Codenames& game, char* out, const size_t cap) {
  const char* onTurn = I18N.get(game.turn() == Codenames::RED ? StrId::STR_CN_TEAM_RED : StrId::STR_CN_TEAM_BLUE);
  switch (game.phase()) {
    case Codenames::Phase::Clue:
      snprintf(out, cap, tr(STR_CN_CLUE_FORMAT), onTurn);
      return;
    case Codenames::Phase::Guess:
      snprintf(out, cap, tr(STR_CN_GUESS_FORMAT), onTurn, game.clue(), game.clueCount(), game.guessesLeft());
      return;
    case Codenames::Phase::Over:
      snprintf(out, cap, tr(STR_CN_WINS_FORMAT),
               I18N.get(game.winner() == Codenames::RED ? StrId::STR_CN_TEAM_RED : StrId::STR_CN_TEAM_BLUE));
      return;
  }
}

void battleshipStatus(const GameSession& session, const Battleship& game, char* out, const size_t cap) {
  switch (game.phase()) {
    case Battleship::Phase::Place:
      snprintf(out, cap, "%s", tr(STR_BS_PLACING));
      return;
    case Battleship::Phase::Fire:
      snprintf(out, cap, tr(STR_BS_TURN_FORMAT), seatName(session, game.turn()));
      return;
    case Battleship::Phase::Over:
      snprintf(out, cap, tr(STR_BS_WINS_FORMAT),
               seatName(session, static_cast<uint8_t>(std::max<int8_t>(game.winner(), 0))));
      return;
  }
}

void ludoStatus(const GameSession& session, const Ludo& game, char* out, const size_t cap) {
  switch (game.phase()) {
    case Ludo::Phase::Roll:
      snprintf(out, cap, tr(STR_LUDO_ROLL_FORMAT), seatName(session, game.turn()));
      return;
    case Ludo::Phase::Move:
      snprintf(out, cap, tr(STR_LUDO_MOVE_FORMAT), seatName(session, game.turn()), game.die());
      return;
    case Ludo::Phase::Over:
      snprintf(out, cap, tr(STR_LUDO_WINS_FORMAT),
               seatName(session, static_cast<uint8_t>(std::max<int8_t>(game.winner(), 0))));
      return;
  }
}

// --- undercover --------------------------------------------------------------

void drawUndercover(const GfxRenderer& renderer, const GameSession& session, const Undercover& game,
                    const Rect& content) {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int rowHeight = lineHeight + px(14);
  const int gap = px(TILE_GAP);
  const int columns = content.width > 2 * content.height ? 2 : 1;
  const int columnWidth = (content.width - (columns - 1) * gap) / columns;

  for (uint8_t seat = 0; seat < game.seats(); seat++) {
    const int column = columns == 1 ? 0 : seat % columns;
    const int row = columns == 1 ? seat : seat / columns;
    const Rect box(content.x + column * (columnWidth + gap), content.y + row * rowHeight, columnWidth, rowHeight - gap);
    if (box.y + box.height > content.y + content.height) break;

    const bool out = !game.alive(seat);
    renderer.drawRect(box.x, box.y, box.width, box.height, out ? 1 : 2, true);

    char label[40];
    snprintf(label, sizeof(label), "%u  %s", seat + 1, session.player(seat).name);
    renderer.drawText(UI_10_FONT_ID, box.x + px(10), box.y + (box.height - lineHeight) / 2, label, true,
                      out ? EpdFontFamily::ITALIC : EpdFontFamily::REGULAR);

    char right[24];
    if (out) {
      snprintf(right, sizeof(right), "out%s", game.isUndercover(seat) ? " - undercover" : "");
    } else if (game.phase() == Undercover::Phase::Describe) {
      snprintf(right, sizeof(right), "%s", game.ready(seat) ? "ready" : "...");
    } else {
      snprintf(right, sizeof(right), "%u vote%s", game.votesAgainst(seat), game.votesAgainst(seat) == 1 ? "" : "s");
    }
    const int rightWidth = renderer.getTextWidth(SMALL_FONT_ID, right);
    renderer.drawText(SMALL_FONT_ID, box.x + box.width - rightWidth - px(10),
                      box.y + (box.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2, right);

    if (out) {
      const int middle = box.y + box.height / 2;
      renderer.drawLine(box.x + px(6), middle, box.x + box.width - px(6), middle, true);
    }
  }
}

// --- codenames ---------------------------------------------------------------

void drawCodenames(const GfxRenderer& renderer, const Codenames& game, const Rect& content) {
  const int legendHeight = renderer.getLineHeight(SMALL_FONT_ID) + px(10);
  const int gridHeight = content.height - legendHeight;
  const int gap = px(TILE_GAP);
  const int crossInset = px(4);
  const int tileWidth = (content.width - (Codenames::GRID - 1) * gap) / Codenames::GRID;
  const int tileHeight = (gridHeight - (Codenames::GRID - 1) * gap) / Codenames::GRID;

  for (uint8_t tile = 0; tile < Codenames::TILES; tile++) {
    const int column = tile % Codenames::GRID;
    const int row = tile / Codenames::GRID;
    const Rect box(content.x + column * (tileWidth + gap), content.y + row * (tileHeight + gap), tileWidth, tileHeight);
    const char* tileText = game.tileWord(tile);
    const int fontId = fontThatFits(renderer, tileText, box.width - px(8));

    if (!game.revealed(tile)) {
      renderer.drawRect(box.x, box.y, box.width, box.height, 1, true);
      centerText(renderer, fontId, box, tileText);
      continue;
    }

    switch (game.owner(tile)) {
      case Codenames::RED:
        renderer.fillRect(box.x, box.y, box.width, box.height, true);
        centerText(renderer, fontId, box, tileText, false, EpdFontFamily::BOLD);
        break;
      case Codenames::BLUE:
        renderer.fillRectDither(box.x, box.y, box.width, box.height, Color::LightGray);
        renderer.drawRect(box.x, box.y, box.width, box.height, 2, true);
        centerText(renderer, fontId, box, tileText, true, EpdFontFamily::BOLD);
        break;
      case Codenames::ASSASSIN:
        renderer.fillRect(box.x, box.y, box.width, box.height, true);
        renderer.drawLine(box.x + crossInset, box.y + crossInset, box.x + box.width - crossInset,
                          box.y + box.height - crossInset, 2, false);
        renderer.drawLine(box.x + box.width - crossInset, box.y + crossInset, box.x + crossInset,
                          box.y + box.height - crossInset, 2, false);
        centerText(renderer, fontId, box, tileText, false, EpdFontFamily::BOLD);
        break;
      default:
        renderer.drawRect(box.x, box.y, box.width, box.height, 1, true);
        centerText(renderer, fontId, box, tileText);
        renderer.drawLine(box.x + px(6), box.y + box.height / 2, box.x + box.width - px(6), box.y + box.height / 2,
                          true);
        break;
    }
  }

  // Legend: on a monochrome panel the teams are a fill pattern, so say which.
  const int legendY = content.y + gridHeight + px(4);
  const int swatch = renderer.getLineHeight(SMALL_FONT_ID) - px(2);
  int x = content.x;
  const auto legendEntry = [&](const uint8_t style, const char* text) {
    drawSwatch(renderer, Rect(x, legendY, swatch, swatch), style);
    x += swatch + px(4);
    renderer.drawText(SMALL_FONT_ID, x, legendY, text);
    x += renderer.getTextWidth(SMALL_FONT_ID, text) + px(14);
  };

  char red[24];
  char blue[24];
  snprintf(red, sizeof(red), "Red %u left", game.remaining(Codenames::RED));
  snprintf(blue, sizeof(blue), "Blue %u left", game.remaining(Codenames::BLUE));
  legendEntry(0, red);
  legendEntry(2, blue);
  legendEntry(1, "bystander");
}

// --- battleship --------------------------------------------------------------

void drawFleetGrid(const GfxRenderer& renderer, const Battleship& game, const uint8_t player, const Rect& box,
                   const char* title) {
  const int titleHeight = renderer.getLineHeight(SMALL_FONT_ID) + px(2);
  const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, "10") + px(3);
  const int available = std::min(box.width - labelWidth, box.height - titleHeight - titleHeight);
  const int cell = available / Battleship::SIZE;
  const int gridLeft = box.x + labelWidth;
  const int gridTop = box.y + titleHeight * 2;

  renderer.drawText(SMALL_FONT_ID, box.x, box.y, title, true, EpdFontFamily::BOLD);

  for (uint8_t i = 0; i < Battleship::SIZE; i++) {
    char label[3];
    snprintf(label, sizeof(label), "%c", static_cast<char>('A' + i));
    centerText(renderer, SMALL_FONT_ID, Rect(gridLeft + i * cell, gridTop - titleHeight, cell, titleHeight), label);
    snprintf(label, sizeof(label), "%u", i + 1);
    const int width = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, gridLeft - width - px(3),
                      gridTop + i * cell + (cell - renderer.getLineHeight(SMALL_FONT_ID)) / 2, label);
  }

  const int sunkPip = px(2);
  const int missPip = px(3);
  for (uint8_t y = 0; y < Battleship::SIZE; y++) {
    for (uint8_t x = 0; x < Battleship::SIZE; x++) {
      const Rect cellBox(gridLeft + x * cell, gridTop + y * cell, cell, cell);
      renderer.drawRect(cellBox.x, cellBox.y, cellBox.width, cellBox.height, 1, true);
      switch (game.shotAt(player, x, y)) {
        case Battleship::HIT:
          renderer.fillRect(cellBox.x + 1, cellBox.y + 1, cellBox.width - 2, cellBox.height - 2, true);
          if (game.sunkAt(player, x, y)) {
            // A sunk ship's cells keep a white pip so a whole hull reads as one
            // shape instead of a blob of separate hits.
            renderer.fillRect(cellBox.x + (cellBox.width - sunkPip) / 2, cellBox.y + (cellBox.height - sunkPip) / 2,
                              sunkPip, sunkPip, false);
          }
          break;
        case Battleship::MISS:
          renderer.fillRect(cellBox.x + (cellBox.width - missPip) / 2, cellBox.y + (cellBox.height - missPip) / 2,
                            missPip, missPip, true);
          break;
        default:
          break;
      }
    }
  }

  char footer[32];
  snprintf(footer, sizeof(footer), "%u afloat%s", game.shipsLeft(player),
           game.phase() == Battleship::Phase::Place ? (game.ready(player) ? " - ready" : " - placing") : "");
  renderer.drawText(SMALL_FONT_ID, box.x, gridTop + Battleship::SIZE * cell + px(2), footer);
}

void drawBattleship(const GfxRenderer& renderer, const GameSession& session, const Battleship& game,
                    const Rect& content) {
  const bool sideBySide = content.width >= content.height;
  const int gap = px(TILE_GAP * 4);
  const int halfWidth = sideBySide ? (content.width - gap) / 2 : content.width;
  const int halfHeight = sideBySide ? content.height : (content.height - gap) / 2;

  for (uint8_t player = 0; player < Battleship::PLAYERS; player++) {
    char title[32];
    snprintf(title, sizeof(title), "%s%s", session.isSeated(player) ? session.player(player).name : "empty seat",
             game.phase() == Battleship::Phase::Fire && game.turn() == player ? "  <- to fire" : "");
    const Rect box(content.x + (sideBySide ? player * (halfWidth + gap) : 0),
                   content.y + (sideBySide ? 0 : player * (halfHeight + gap)), halfWidth, halfHeight);
    drawFleetGrid(renderer, game, player, box, title);
  }
}

// --- ludo --------------------------------------------------------------------

// Token fills, one per seat, distinct without colour: solid, outline, light
// hatch, dark hatch.
void drawToken(const GfxRenderer& renderer, const uint8_t player, const Rect& cellBox, const bool stacked) {
  const int inset = std::max(2, cellBox.width / 8);
  Rect box(cellBox.x + inset, cellBox.y + inset, cellBox.width - inset * 2, cellBox.height - inset * 2);
  if (box.width < 6 || box.height < 6) return;
  const int radius = box.width / 2;

  if (stacked) {
    // A second ring behind the token says "more than one here".
    renderer.drawRoundedRect(box.x - 2, box.y - 2, box.width, box.height, 1, radius, true);
  }

  bool digitBlack = true;
  switch (player) {
    case 0:
      renderer.fillRoundedRect(box.x, box.y, box.width, box.height, radius, Color::Black);
      digitBlack = false;
      break;
    case 1:
      renderer.fillRoundedRect(box.x, box.y, box.width, box.height, radius, Color::White);
      renderer.drawRoundedRect(box.x, box.y, box.width, box.height, 2, radius, true);
      break;
    case 2:
      renderer.fillRoundedRect(box.x, box.y, box.width, box.height, radius, Color::LightGray);
      renderer.drawRoundedRect(box.x, box.y, box.width, box.height, 1, radius, true);
      break;
    default:
      renderer.fillRoundedRect(box.x, box.y, box.width, box.height, radius, Color::DarkGray);
      renderer.drawRoundedRect(box.x, box.y, box.width, box.height, 1, radius, true);
      digitBlack = false;
      break;
  }

  char digit[2];
  snprintf(digit, sizeof(digit), "%u", player + 1);
  centerText(renderer, SMALL_FONT_ID, box, digit, digitBlack, EpdFontFamily::BOLD);
}

void drawLudo(const GfxRenderer& renderer, const GameSession& session, const Ludo& game, const Rect& content) {
  const bool sideLegend = content.width > content.height + px(140);
  const int boardSide = std::min(sideLegend ? content.width - px(150) : content.width,
                                 sideLegend ? content.height : content.height - px(90));
  const int cell = boardSide / Ludo::GRID;
  const int boardLeft = content.x + (sideLegend ? 0 : (content.width - cell * Ludo::GRID) / 2);
  const int boardTop = content.y;

  const auto cellRect = [&](const Ludo::Cell& at) {
    return Rect(boardLeft + at.col * cell, boardTop + at.row * cell, cell, cell);
  };

  // Yards, one 6x6 block per corner.
  static constexpr uint8_t YARD_COL[4] = {0, 9, 9, 0};
  static constexpr uint8_t YARD_ROW[4] = {0, 0, 9, 9};
  for (uint8_t player = 0; player < 4; player++) {
    const Rect yard(boardLeft + YARD_COL[player] * cell, boardTop + YARD_ROW[player] * cell, cell * 6, cell * 6);
    renderer.drawRect(yard.x, yard.y, yard.width, yard.height, player < game.players() ? 2 : 1, true);
  }

  // The shared ring, with a pip on the squares no one can be captured on.
  for (uint8_t index = 0; index < Ludo::TRACK; index++) {
    const Rect box = cellRect(Ludo::trackCell(index));
    renderer.drawRect(box.x, box.y, box.width, box.height, 1, true);
    if (Ludo::isSafeCell(index)) {
      renderer.drawRect(box.x + box.width / 3, box.y + box.height / 3, box.width / 3, box.height / 3, 1, true);
    }
  }

  // Home columns, hatched to match their owner's token.
  for (uint8_t player = 0; player < game.players(); player++) {
    for (uint8_t step = 0; step < 5; step++) {
      const Rect box = cellRect(Ludo::homeCell(player, step));
      drawSwatch(renderer, box, player);
    }
  }

  const Rect home(boardLeft + 6 * cell, boardTop + 6 * cell, cell * 3, cell * 3);
  renderer.drawRect(home.x, home.y, home.width, home.height, 2, true);
  centerText(renderer, SMALL_FONT_ID, home, "HOME");

  // Tokens last so they sit above the board.
  for (uint8_t player = 0; player < game.players(); player++) {
    uint8_t finished = 0;
    for (uint8_t token = 0; token < Ludo::TOKENS; token++) {
      const int8_t at = game.position(player, token);
      Rect box;
      bool stacked = false;
      if (at == Ludo::IN_YARD) {
        box = cellRect(Ludo::yardCell(player, token));
      } else if (at >= static_cast<int8_t>(Ludo::HOME)) {
        // Park finished tokens in a row inside the centre square.
        box = Rect(home.x + 2 + finished * (home.width - 4) / 4, home.y + home.height / 2, (home.width - 4) / 4,
                   home.height / 2 - 2);
        finished++;
      } else if (at > static_cast<int8_t>(Ludo::LAST_TRACK)) {
        box = cellRect(Ludo::homeCell(player, static_cast<uint8_t>(at - 52)));
      } else {
        box = cellRect(Ludo::trackCell(Ludo::absoluteCell(player, at)));
        for (uint8_t other = 0; other < Ludo::TOKENS; other++) {
          if (other != token && game.position(player, other) == at) stacked = true;
        }
      }
      drawToken(renderer, player, box, stacked);
    }
  }

  // Legend: which seat is which token, and how far along they are.
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID) + px(4);
  const int legendLeft = sideLegend ? boardLeft + cell * Ludo::GRID + px(12) : content.x;
  int legendY = sideLegend ? content.y : boardTop + cell * Ludo::GRID + px(8);
  for (uint8_t player = 0; player < game.players(); player++) {
    const Rect swatch(legendLeft, legendY, lineHeight - px(6), lineHeight - px(6));
    drawToken(renderer, player, Rect(swatch.x - 2, swatch.y - 2, swatch.width + 4, swatch.height + 4), false);
    char label[48];
    snprintf(label, sizeof(label), "%s%s  %u home", session.isSeated(player) ? session.player(player).name : "empty",
             game.turn() == player ? " *" : "", game.tokensHome(player));
    renderer.drawText(SMALL_FONT_ID, legendLeft + lineHeight, legendY, label, true,
                      game.turn() == player ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    legendY += lineHeight;
  }
  if (game.die() > 0) {
    char die[24];
    snprintf(die, sizeof(die), "Die: %u", game.die());
    renderer.drawText(UI_10_FONT_ID, legendLeft, legendY + px(4), die, true, EpdFontFamily::BOLD);
  }
}

}  // namespace

const char* titleFor(const party::GameId id) {
  switch (id) {
    case party::GameId::Undercover:
      return tr(STR_GAME_UNDERCOVER);
    case party::GameId::Codenames:
      return tr(STR_GAME_CODENAMES);
    case party::GameId::Battleship:
      return tr(STR_GAME_BATTLESHIP);
    case party::GameId::Ludo:
      return tr(STR_GAME_LUDO);
    default:
      return "";
  }
}

void statusFor(const GameSession& session, char* out, const size_t cap) {
  if (!out || cap == 0) return;
  const party::Game* game = session.game();
  if (!game) {
    const party::GameMeta& meta = party::gameMeta(session.selected());
    if (session.playerCount() < meta.minPlayers) {
      snprintf(out, cap, tr(STR_GAMES_NEEDS_PLAYERS_FORMAT), titleFor(session.selected()), meta.minPlayers,
               session.playerCount());
    } else {
      snprintf(out, cap, tr(STR_GAMES_READY_FORMAT), titleFor(session.selected()), session.playerCount());
    }
    return;
  }

  switch (game->id()) {
    case party::GameId::Undercover:
      undercoverStatus(session, *static_cast<const Undercover*>(game), out, cap);
      return;
    case party::GameId::Codenames:
      codenamesStatus(*static_cast<const Codenames*>(game), out, cap);
      return;
    case party::GameId::Battleship:
      battleshipStatus(session, *static_cast<const Battleship*>(game), out, cap);
      return;
    case party::GameId::Ludo:
      ludoStatus(session, *static_cast<const Ludo*>(game), out, cap);
      return;
    default:
      out[0] = '\0';
      return;
  }
}

void drawSeatList(const GfxRenderer& renderer, const GameSession& session, const Rect& content, const uint32_t nowMs) {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int rowHeight = lineHeight + px(8);
  int y = content.y;

  if (session.playerCount() == 0) {
    renderer.drawText(UI_10_FONT_ID, content.x, y, tr(STR_GAMES_NO_PLAYERS), true, EpdFontFamily::ITALIC);
    return;
  }

  for (uint8_t seat = 0; seat < party::MAX_PLAYERS; seat++) {
    if (!session.isSeated(seat)) continue;
    if (y + rowHeight > content.y + content.height) break;

    char label[48];
    snprintf(label, sizeof(label), "%u  %s%s%s", seat + 1, session.player(seat).name,
             session.isHost(seat) ? "  (host)" : "", session.isAway(seat, nowMs) ? "  (away)" : "");
    renderer.drawText(UI_10_FONT_ID, content.x, y, label);
    y += rowHeight;
  }
}

void drawBoard(const GfxRenderer& renderer, const GameSession& session, const Rect& content) {
  const party::Game* game = session.game();
  if (!game) return;

  switch (game->id()) {
    case party::GameId::Undercover:
      drawUndercover(renderer, session, *static_cast<const Undercover*>(game), content);
      return;
    case party::GameId::Codenames:
      drawCodenames(renderer, *static_cast<const Codenames*>(game), content);
      return;
    case party::GameId::Battleship:
      drawBattleship(renderer, session, *static_cast<const Battleship*>(game), content);
      return;
    case party::GameId::Ludo:
      drawLudo(renderer, session, *static_cast<const Ludo*>(game), content);
      return;
    default:
      return;
  }
}

}  // namespace games
