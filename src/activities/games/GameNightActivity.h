#pragma once

#include <GameSession.h>

#include <memory>
#include <string>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "network/GameServer.h"
#include "network/PhonePortal.h"

// Game night: the reader becomes the table.
//
// It raises an open access point through PhonePortal, shows the join card
// (one QR joins the Wi-Fi, one opens the page), and serves every phone a view
// of the same session. The e-ink panel is the board everyone looks at; the
// phones hold whatever has to stay private - a dice cup, a secret word, a
// spymaster's key.
class GameNightActivity final : public Activity {
 public:
  explicit GameNightActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GameNight", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return server && server->isRunning(); }
  bool preventAutoSleep() override { return server && server->isRunning(); }

 private:
  enum class State : uint8_t { AP_STARTING, RUNNING, SHUTTING_DOWN };

  void startAccessPoint();
  void startServer();
  void renderLobby(const Rect& content) const;
  void renderRound(const Rect& content) const;
  // Applies `verb` with the table's authority, so the device's own buttons work
  // even when the host's phone is in someone's pocket.
  void hostAction(party::Verb verb);
  // Reads this frame's button events. Returns true when the activity is on its
  // way out and the caller must stop touching it.
  bool handleInput();

  State state = State::AP_STARTING;
  party::GameSession session;
  std::unique_ptr<GameServer> server;
  PhonePortal portal;

  // Repaints are driven by the session version, not by polling: a phone asking
  // for state 8 times a second must not cost a panel refresh.
  uint32_t paintedVersion = 0;
  unsigned long lastPaintMs = 0;
  uint8_t paintsSinceFullRefresh = 0;
  static constexpr unsigned long MIN_REPAINT_INTERVAL_MS = 700;
  // Fast refreshes leave ghosting; clear it on a cadence rather than on a timer
  // nobody is watching.
  static constexpr uint8_t FULL_REFRESH_EVERY = 8;
};
