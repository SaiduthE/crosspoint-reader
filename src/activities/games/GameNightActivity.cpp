#include "GameNightActivity.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <I18n.h>
#include <WiFi.h>
#include <esp_random.h>

#include <algorithm>

#include "GameBoards.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrUtils.h"
#include "util/TaskWatchdog.h"

namespace {
constexpr const char* AP_SSID = "CrossPoint-Games";
constexpr const char* AP_HOSTNAME = "crosspoint";
constexpr uint8_t AP_CHANNEL = 1;
// One seat per phone, so the access point has to hold the whole table.
constexpr uint8_t AP_MAX_CONNECTIONS = party::MAX_PLAYERS;
constexpr uint16_t DNS_PORT = 53;
constexpr int QR_MAX_SIZE = 198;

DNSServer* dnsServer = nullptr;

void stopDnsServer() {
  if (!dnsServer) return;
  dnsServer->stop();
  delete dnsServer;
  dnsServer = nullptr;
}
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
  stopDnsServer();
  MDNS.end();

  // Tearing WiFi down leaves the heap fragmented enough to hurt the reader, and
  // file transfer already answers that the same way.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.softAPdisconnect(true);
    delay(30);
    silentRestart();
  }
  LOG_DBG("GAMES", "Free heap at onExit: %d bytes", ESP.getFreeHeap());
}

void GameNightActivity::startAccessPoint() {
  LOG_DBG("GAMES", "Starting game night access point");
  WiFi.mode(WIFI_AP);
  delay(100);

  if (!WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS)) {
    LOG_ERR("GAMES", "Failed to start the access point");
    onGoHome();
    return;
  }
  delay(100);

  const IPAddress ip = WiFi.softAPIP();
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  apIp = buffer;
  apSsid = AP_SSID;
  // Wi-Fi network config QR, per the zxing barcode contents spec.
  joinPayload = std::string("WIFI:T:nopass;S:") + AP_SSID + ";;";
  pageUrl = std::string("http://") + apIp + "/";

  MDNS.end();
  if (MDNS.begin(AP_HOSTNAME)) {
    LOG_DBG("GAMES", "mDNS started: http://%s.local/", AP_HOSTNAME);
  }

  // Captive portal: every DNS lookup resolves here, so joining the network is
  // enough to land on the game page.
  stopDnsServer();
  dnsServer = new (std::nothrow) DNSServer();
  if (dnsServer) {
    dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer->start(DNS_PORT, "*", ip);
  } else {
    LOG_ERR("GAMES", "OOM: DNS server, phones will need the URL typed in");
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

  if (dnsServer) dnsServer->processNextRequest();

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
  const int smallHeight = renderer.getLineHeight(SMALL_FONT_ID);

  // Two codes: one joins the network, one opens the page. Phones that follow
  // the captive portal never need the second, and plenty do not.
  const int columnWidth = content.width / 2;
  // Two fifths of the content band, so the seat list underneath still has room
  // on a landscape panel.
  const int qrSize = std::min({QR_MAX_SIZE, columnWidth - metrics.verticalSpacing * 2, content.height * 2 / 5});
  const int qrTop = content.y + lineHeight + 4;

  renderer.drawText(UI_10_FONT_ID, content.x, content.y, tr(STR_GAMES_SCAN_JOIN), true, EpdFontFamily::BOLD);
  QrUtils::drawQrCode(renderer, Rect(content.x, qrTop, qrSize, qrSize), joinPayload);
  renderer.drawText(SMALL_FONT_ID, content.x, qrTop + qrSize + 2, apSsid.c_str());

  const int rightX = content.x + columnWidth;
  renderer.drawText(UI_10_FONT_ID, rightX, content.y, tr(STR_GAMES_SCAN_OPEN), true, EpdFontFamily::BOLD);
  QrUtils::drawQrCode(renderer, Rect(rightX, qrTop, qrSize, qrSize), pageUrl);
  renderer.drawText(SMALL_FONT_ID, rightX, qrTop + qrSize + 2, pageUrl.c_str());

  const int listTop = qrTop + qrSize + smallHeight + metrics.verticalSpacing * 2;
  renderer.drawText(UI_10_FONT_ID, content.x, listTop, tr(STR_GAMES_PLAYERS), true, EpdFontFamily::BOLD);
  games::drawSeatList(
      renderer, session,
      Rect(content.x, listTop + lineHeight + 4, content.width, content.y + content.height - (listTop + lineHeight + 4)),
      millis());
}

void GameNightActivity::renderRound(const Rect& content) const {
  Rect board = content;
  if (session.game() && session.game()->isOver()) {
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawText(UI_10_FONT_ID, content.x, content.y + content.height - lineHeight, tr(STR_GAMES_ROUND_OVER), true,
                      EpdFontFamily::BOLD);
    board.height -= lineHeight + 4;
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
    GUI.drawSideButtonHints(renderer, tr(STR_GAMES_PREV_GAME), tr(STR_GAMES_NEXT_GAME));
  } else {
    renderRound(content);
  }

  const auto labels = mappedInput.mapLabels(inLobby ? tr(STR_EXIT) : tr(STR_GAMES_END_ROUND),
                                            inLobby ? tr(STR_GAMES_START) : tr(STR_GAMES_NEXT), "", "");
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
