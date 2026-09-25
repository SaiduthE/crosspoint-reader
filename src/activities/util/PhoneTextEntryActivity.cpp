#include "PhoneTextEntryActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/PhoneJoinPanel.h"
#include "components/UITheme.h"

namespace {
constexpr const char* TEXT_AP_SSID = "eMinimal Text";
constexpr const char* TEXT_AP_HOSTNAME = "eminimal";
constexpr uint8_t TEXT_AP_CHANNEL = 1;
constexpr uint8_t TEXT_AP_MAX_CLIENTS = 2;
constexpr unsigned long STATION_POLL_MS = 500;
// A phone that re-associates is back within a few seconds; only a longer
// absence counts as gone.
constexpr unsigned long STATION_GONE_MS = 8000;
// The panel shows what arrived for this long before the caller gets it; the
// page's HTTP reply has long left by then.
constexpr unsigned long RESULT_HOLD_MS = 1200;
// No phone on the hotspot and no page request for this long: abandoned.
constexpr unsigned long PHONE_IDLE_MS = 5UL * 60UL * 1000UL;
// Code points of the received text shown on the panel.
constexpr size_t SHOWN_CHARS = 40;

TextEntryServer::Kind kindFor(const InputType type) {
  switch (type) {
    case InputType::Password:
      return TextEntryServer::Kind::Password;
    case InputType::Url:
      return TextEntryServer::Kind::Url;
    case InputType::Text:
    default:
      return TextEntryServer::Kind::Text;
  }
}

// Masked per code point for passwords; cut at SHOWN_CHARS code points.
std::string shownText(const std::string& text, const bool masked) {
  std::string out;
  size_t chars = 0;
  size_t i = 0;
  while (i < text.size()) {
    if (chars == SHOWN_CHARS) {
      out += "...";
      break;
    }
    size_t next = i + 1;
    while (next < text.size() && (static_cast<uint8_t>(text[next]) & 0xC0) == 0x80) next++;
    if (masked) {
      out += '*';
    } else {
      out.append(text, i, next - i);
    }
    chars++;
    i = next;
  }
  return out;
}
}  // namespace

PhoneTextEntryActivity::PhoneTextEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::string title, std::string initialText, const size_t maxLength,
                                               const InputType inputType)
    : Activity("PhoneTextEntry", renderer, mappedInput),
      title(std::move(title)),
      initialText(std::move(initialText)),
      maxLength(maxLength),
      inputType(inputType) {}

void PhoneTextEntryActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("TXT", "Phone entry: free heap %u bytes, largest block %u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));

  // The radio and the server need the room more than the SD-font caches,
  // which rebuild on demand (same release as File Transfer).
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
  }

  phase = Phase::Waiting;
  received.clear();
  receivedShown.clear();
  stationUrl.clear();
  seenRequests = 0;
  stationsGoneAt = 0;
  lastStationPoll = 0;
  lastActivityAt = millis();
  if (!start()) phase = Phase::Error;
  requestUpdate();
}

bool PhoneTextEntryActivity::start() {
  radioWasOff = WiFi.getMode() == WIFI_MODE_NULL;
  WiFi.persistent(false);

  stationMode = !radioWasOff && WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  if (stationMode) {
    // Disable WiFi sleep to improve responsiveness, as CrossPointWebServer.cpp:138
    // does; nothing restores it on exit, since the caller's WiFi owner restarts
    // it on leaving its own screen.
    WiFi.setSleep(false);
    const IPAddress ip = WiFi.localIP();
    char url[32];
    snprintf(url, sizeof(url), "http://%d.%d.%d.%d/", ip[0], ip[1], ip[2], ip[3]);
    stationUrl = url;
    LOG_INF("TXT", "Already on a network: serving at %s", url);
  } else {
    ensurePassphrase();
    PhonePortal::Config config;
    config.ssid = TEXT_AP_SSID;
    config.passphrase = SETTINGS.phoneTextPassphrase;
    config.channel = TEXT_AP_CHANNEL;
    config.maxClients = TEXT_AP_MAX_CLIENTS;
    config.keepStation = !radioWasOff;
    config.hostname = TEXT_AP_HOSTNAME;
    if (!portal.begin(config)) {
      LOG_ERR("TXT", "Hotspot would not come up");
      return false;
    }
  }

  TextEntryServer::Field field;
  field.title = title;
  field.text = initialText;
  field.maxBytes = maxLength;
  field.kind = kindFor(inputType);
  if (!server.begin(std::move(field))) {
    LOG_ERR("TXT", "Text page server would not start");
    portal.end();
    return false;
  }
  return true;
}

void PhoneTextEntryActivity::ensurePassphrase() {
  // WPA2 needs at least 8 characters; a shorter stored value (hand-edited
  // settings) is replaced the same way as a missing one.
  if (strnlen(SETTINGS.phoneTextPassphrase, sizeof(SETTINGS.phoneTextPassphrase)) >= PhonePortal::PASSPHRASE_LENGTH) {
    return;
  }
  const std::string fresh = PhonePortal::generatePassphrase();
  snprintf(SETTINGS.phoneTextPassphrase, sizeof(SETTINGS.phoneTextPassphrase), "%s", fresh.c_str());
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("TXT", "Could not save the hotspot passphrase; the phone will ask to join again next time");
    return;
  }
  LOG_INF("TXT", "Generated the text-entry hotspot passphrase");
}

void PhoneTextEntryActivity::onExit() {
  Activity::onExit();
  LOG_INF("TXT", "Leaving: free heap %u bytes, largest block %u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  server.end();
  portal.end();
  // No silent restart like the other Wi-Fi screens: the caller is waiting for
  // this result. The radio goes back to how it was found.
  if (radioWasOff) WiFi.mode(WIFI_OFF);
  LOG_INF("TXT", "Radio %s: free heap %u bytes, largest block %u", radioWasOff ? "off" : "left on",
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

void PhoneTextEntryActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void PhoneTextEntryActivity::loop() {
  if (phase == Phase::Error) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      cancel();
    }
    return;
  }

  portal.loop();
  server.handleClient();
  const unsigned long now = millis();

  if (phase == Phase::Received) {
    if (now - receivedAt >= RESULT_HOLD_MS) {
      setResult(KeyboardResult{std::move(received)});
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }

  if (server.hasSubmission()) {
    received = server.takeSubmission();
    receivedShown = shownText(received, inputType == InputType::Password);
    receivedAt = now;
    phase = Phase::Received;
    requestUpdate();
    return;
  }

  if (server.requestCount() != seenRequests) {
    seenRequests = server.requestCount();
    lastActivityAt = now;
  }

  if (now - lastStationPoll >= STATION_POLL_MS) {
    lastStationPoll = now;
    // A station-mode page cannot count phones: its first request is the phone.
    const bool onHotspot = portal.stationCount() > 0;
    const bool attached = stationMode ? seenRequests > 0 : onHotspot;
    if (onHotspot) lastActivityAt = now;
    if (attached) {
      stationsGoneAt = 0;
      if (phase == Phase::Waiting) {
        LOG_DBG("TXT", "A phone is here");
        phase = Phase::PhoneJoined;
        requestUpdate();
      }
    } else if (phase == Phase::PhoneJoined) {
      if (stationsGoneAt == 0) stationsGoneAt = now;
      if (now - stationsGoneAt > STATION_GONE_MS) {
        LOG_DBG("TXT", "The phone left the hotspot");
        phase = Phase::Waiting;
        stationsGoneAt = 0;
        requestUpdate();
      }
    }
  }

  if (now - lastActivityAt > PHONE_IDLE_MS) {
    LOG_INF("TXT", "No phone for %lu s; closing", PHONE_IDLE_MS / 1000UL);
    cancel();
  }
}

void PhoneTextEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto& theme = UITheme::getInstance();
  const auto& metrics = theme.getMetrics();
  const Rect screen = theme.getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());

  const int top = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  const Rect bounds{screen.x, top, screen.width, screen.y + screen.height - top};

  PhoneJoinPanel::Content content;
  std::string altUrl;
  switch (phase) {
    case Phase::Error:
      content.headline = tr(STR_PHONE_SETUP_ERROR);
      break;
    case Phase::Received:
      content.headline = tr(STR_PHONE_TEXT_RECEIVED);
      content.status = receivedShown.c_str();
      break;
    case Phase::Waiting:
    case Phase::PhoneJoined:
    default:
      content.headline = tr(STR_PHONE_TEXT_HEADLINE);
      content.status = phase == Phase::PhoneJoined ? tr(STR_PHONE_TEXT_JOINED) : tr(STR_PHONE_SETUP_WAITING);
      if (stationMode) {
        // The phone is already on this network: one QR, the page.
        content.pageUrl = stationUrl;
      } else {
        // The captive popup opens the page by itself; the second QR is for a
        // phone that does not pop it.
        content.portal = &portal;
        content.pageUrl = portal.pageUrl();
        altUrl = std::string(tr(STR_OR_HTTP_PREFIX)) + TEXT_AP_HOSTNAME + ".local/";
        content.pageAlt = altUrl.c_str();
      }
      break;
  }
  PhoneJoinPanel::draw(renderer, bounds, content);

  if (phase != Phase::Received) {
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
