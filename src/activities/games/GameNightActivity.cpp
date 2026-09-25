#include "GameNightActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <I18n.h>
#include <WiFi.h>
#include <esp_random.h>

#include "GameBoards.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "components/PhoneJoinPanel.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/FiveButtonInput.h"
#include "util/TaskWatchdog.h"

namespace {
constexpr const char* AP_SSID = "eMinimal Games";
constexpr const char* AP_HOSTNAME = "eminimal";
constexpr uint8_t AP_CHANNEL = 1;
// One seat per phone, so the access point has to hold the whole table.
constexpr uint8_t AP_MAX_CONNECTIONS = party::MAX_PLAYERS;
}  // namespace

void GameNightActivity::onEnter() {
  Activity::onEnter();
  LOG_DBG("GAMES", "Free heap at onEnter: %d bytes", ESP.getFreeHeap());

  // Same heap-critical transition as file transfer: WiFi plus the HTTP server
  // have to fit in what is left, and the SD font caches are rebuildable.
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->releaseSdFontCaches();
  }

  session.reseed(esp_random());
  state = State::AP_STARTING;
  paintedVersion = 0;
  paintsSinceFullRefresh = 0;
  requestUpdate();

  startAccessPoint();
}

void GameNightActivity::onExit() {
  Activity::onExit();
  state = State::SHUTTING_DOWN;

  server.reset();
  // Read before the portal drops the AP: that leaves the mode NULL too.
  const bool wifiWasOn = WiFi.getMode() != WIFI_MODE_NULL;
  portal.end();

  // Tearing WiFi down leaves the heap fragmented enough to hurt the reader, and
  // file transfer already answers that the same way.
  if (wifiWasOn) silentRestart();
  LOG_DBG("GAMES", "Free heap at onExit: %d bytes", ESP.getFreeHeap());
}

void GameNightActivity::startAccessPoint() {
  LOG_DBG("GAMES", "Starting game night access point");
  // Open, with the captive DNS that makes joining the network enough to land
  // on the game page.
  PhonePortal::Config config;
  config.ssid = AP_SSID;
  config.channel = AP_CHANNEL;
  config.maxClients = AP_MAX_CONNECTIONS;
  config.hostname = AP_HOSTNAME;
  if (!portal.begin(config)) {
    LOG_ERR("GAMES", "Failed to start the access point");
    onGoHome();
    return;
  }

  startServer();
}

void GameNightActivity::startServer() {
  server = std::make_unique<GameServer>(session);
  server->begin();
  if (!server->isRunning()) {
    LOG_ERR("GAMES", "Failed to start the game server");
    server.reset();
    onGoHome();
    return;
  }
  state = State::RUNNING;
  requestUpdate();
}

void GameNightActivity::hostAction(const party::Verb verb) {
  const int8_t host = session.hostSeat();
  if (host < 0) return;
  party::Action action;
  action.verb = verb;
  session.applyAction(static_cast<uint8_t>(host), action);
}

bool GameNightActivity::handleInput() {
  if (mappedInput.wasHomeGesture()) {
    onGoHome();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (session.inLobby()) {
      onGoHome();
      return true;
    }
    // A round is running: the first Back drops the table into the lobby, the
    // second leaves game night.
    session.endGame();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (session.inLobby()) {
      session.startGame();
    } else {
      hostAction(party::Verb::Next);
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (session.inLobby()) session.cycleGame(-1);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (session.inLobby()) session.cycleGame(1);
  }
  return false;
}

void GameNightActivity::loop() {
  if (state != State::RUNNING || !server || !server->isRunning()) return;

  // Button events are one-shot and the request burst below pumps the input
  // itself, so read them before it runs and again inside it.
  if (handleInput()) return;

  portal.loop();

  // Same shape as the file-transfer loop: pump hard, reset the watchdog, and
  // keep reading buttons from inside the burst so the device stays responsive
  // while eight phones are polling.
  resetTaskWatchdogIfSubscribed();
  constexpr int MAX_ITERATIONS = 200;
  for (int i = 0; i < MAX_ITERATIONS && server->isRunning(); i++) {
    server->handleClient();
    if ((i & 0x1F) == 0x1F) resetTaskWatchdogIfSubscribed();
    if ((i & 0x3F) == 0x3F) {
      yield();
      mappedInput.update();
      if (handleInput()) return;
    }
  }

  // One repaint per change, never faster than the panel can usefully follow.
  if (session.version() != paintedVersion && millis() - lastPaintMs >= MIN_REPAINT_INTERVAL_MS) {
    paintedVersion = session.version();
    lastPaintMs = millis();
    requestUpdate();
  }
}

void GameNightActivity::renderLobby(const Rect& content) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  // The join card every phone screen paints, in the upper three fifths so the
  // seat list underneath still has room on a landscape panel. Phones that
  // follow the captive portal never need its second code, and plenty do not.
  const std::string hostnameUrl = portal.hostnameUrl();
  const std::string altUrl = std::string(tr(STR_OR_HTTP_PREFIX)) + hostnameUrl.substr(sizeof("http://") - 1);
  PhoneJoinPanel::Content card;
  card.portal = &portal;
  card.pageUrl = portal.pageUrl();
  card.pageAlt = altUrl.c_str();
  const Rect cardBounds(0, content.y, renderer.getScreenWidth(), content.height * 3 / 5);
  const int cardBottom = PhoneJoinPanel::draw(renderer, cardBounds, card);

  const int listTop = cardBottom + metrics.verticalSpacing * 2;
  renderer.drawText(UI_10_FONT_ID, content.x, listTop, tr(STR_GAMES_PLAYERS), true, EpdFontFamily::BOLD);
  const int seatsTop = listTop + lineHeight + metrics.verticalSpacing;
  const Rect seats(content.x, seatsTop, content.width, content.y + content.height - seatsTop);
  games::drawSeatList(renderer, session, seats, millis());
}

void GameNightActivity::renderRound(const Rect& content) const {
  Rect board = content;
  if (session.game() && session.game()->isOver()) {
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawText(UI_10_FONT_ID, content.x, content.y + content.height - lineHeight, tr(STR_GAMES_ROUND_OVER), true,
                      EpdFontFamily::BOLD);
    board.height -= lineHeight + UITheme::scaledPx(4);
  }
  games::drawBoard(renderer, session, board);
}

void GameNightActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  if (state == State::AP_STARTING) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_GAME_NIGHT), nullptr);
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                              tr(STR_STARTING_HOTSPOT));
    renderer.displayBuffer();
    return;
  }

  const bool inLobby = session.inLobby();
  char status[96];
  games::statusFor(session, status, sizeof(status));

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 inLobby ? tr(STR_GAME_NIGHT) : games::titleFor(session.selected()), nullptr);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    status);

  const int top = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int bottom = pageHeight - metrics.buttonHintsHeight;
  const Rect content(metrics.contentSidePadding, top, pageWidth - metrics.contentSidePadding * 2, bottom - top);

  if (inLobby) {
    renderLobby(content);
  } else {
    renderRound(content);
  }

  // Up/Down pick the game in the lobby: the side keys on the X4, the third and
  // fourth front keys on the five-button board.
  const bool fiveButton = five_button::active();
  if (inLobby && !fiveButton) {
    GUI.drawSideButtonHints(renderer, tr(STR_GAMES_PREV_GAME), tr(STR_GAMES_NEXT_GAME));
  }
  const char* upLabel = inLobby && fiveButton ? tr(STR_GAMES_PREV_GAME) : "";
  const char* downLabel = inLobby && fiveButton ? tr(STR_GAMES_NEXT_GAME) : "";
  const auto labels = mappedInput.mapLabels(inLobby ? tr(STR_EXIT) : tr(STR_GAMES_END_ROUND),
                                            inLobby ? tr(STR_GAMES_START) : tr(STR_GAMES_NEXT), upLabel, downLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Fast refreshes keep the table moving; a full one every few paints clears the
  // ghosting they leave behind.
  if (++paintsSinceFullRefresh >= FULL_REFRESH_EVERY) {
    paintsSinceFullRefresh = 0;
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  } else {
    renderer.displayBuffer();
  }
}
