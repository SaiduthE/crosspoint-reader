#include "WifiPhoneSetup.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>

namespace {
constexpr const char* SETUP_AP_SSID = "eMinimal Setup";
constexpr uint8_t SETUP_AP_MAX_CLIENTS = 2;
// One join, scan included. The panel keyboard path allows 15 s; the phone
// path pays a scan in AP+STA mode on top, and a wrong password fails fast.
constexpr unsigned long ATTEMPT_TIMEOUT_MS = 20000;
// After a join, hold the portal this long for the page to fetch the result
// before the caller drops it -- the phone usually polls within a second.
constexpr unsigned long RESULT_HOLD_MS = 6000;
constexpr unsigned long STATION_POLL_MS = 500;
// A phone that re-associates (the portal's channel moved under it) is back
// within a few seconds; only a longer absence counts as gone.
constexpr unsigned long STATION_GONE_MS = 8000;
// The core retries a failed join once on its own; a first disconnect is
// final only when a second follows or nothing else happens for this long.
constexpr unsigned long RETRY_GRACE_MS = 4000;

bool isPasswordReason(const uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_802_1X_AUTH_FAILED:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return true;
    default:
      return false;
  }
}
}  // namespace

WifiPhoneSetup::WifiPhoneSetup(std::string preset, std::vector<Target> networks)
    : presetSsid(std::move(preset)), visible(std::move(networks)) {}

WifiPhoneSetup::~WifiPhoneSetup() { end(); }

bool WifiPhoneSetup::begin() {
  PhonePortal::Config config;
  config.ssid = SETUP_AP_SSID;
  config.randomPassphrase = true;  // the page carries the home password
  config.maxClients = SETUP_AP_MAX_CLIENTS;
  config.keepStation = true;
  // Raise the portal where the join will land: an AP+STA radio has one
  // channel, and moving it mid-join knocks the phone off the page.
  const uint8_t channel = channelFor(presetSsid);
  config.channel = channel ? channel : 1;

  if (!portalLink.begin(config)) {
    currentPhase = Phase::ERROR;
    return false;
  }

  std::vector<std::string> names;
  names.reserve(visible.size());
  for (const auto& target : visible) names.push_back(target.ssid);

  const bool serving = server.begin(presetSsid, std::move(names), [this](const std::string& ssid, const std::string& password) {
    onSubmit(ssid, password);
  });
  if (!serving) {
    portalLink.end();
    currentPhase = Phase::ERROR;
    return false;
  }

  eventHandle = WiFi.onEvent(
      [this](arduino_event_id_t, arduino_event_info_t info) {
        const uint8_t why = info.wifi_sta_disconnected.reason;
        // Our own disconnect() ahead of an attempt; not a verdict.
        if (why == WIFI_REASON_ASSOC_LEAVE) return;
        lastDisconnectReason = why;
        lastDisconnectAt = millis();
        disconnectEvents = disconnectEvents + 1;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  currentPhase = Phase::WAITING_FOR_PHONE;
  reason = FailReason::NONE;
  attemptCount = 0;
  attemptPending = false;
  lastStationPoll = 0;
  stationsGoneAt = 0;
  return true;
}

WifiPhoneSetup::Phase WifiPhoneSetup::loop() {
  if (currentPhase == Phase::ERROR || currentPhase == Phase::DONE) return currentPhase;

  portalLink.loop();
  server.handleClient();

  // The submit handler only records the request; the join starts here, after
  // the HTTP response has left, so the station scan cannot delay it.
  if (attemptPending) {
    attemptPending = false;
    startAttempt();
  }

  const unsigned long now = millis();
  switch (currentPhase) {
    case Phase::WAITING_FOR_PHONE:
    case Phase::PHONE_JOINED:
      if (now - lastStationPoll >= STATION_POLL_MS) {
        lastStationPoll = now;
        const bool attached = portalLink.stationCount() > 0;
        if (attached) {
          stationsGoneAt = 0;
          if (currentPhase == Phase::WAITING_FOR_PHONE) {
            LOG_DBG("SETUP", "A phone joined the portal");
            currentPhase = Phase::PHONE_JOINED;
          }
        } else if (currentPhase == Phase::PHONE_JOINED) {
          if (stationsGoneAt == 0) stationsGoneAt = now;
          if (now - stationsGoneAt > STATION_GONE_MS) {
            LOG_DBG("SETUP", "The phone left the portal");
            currentPhase = Phase::WAITING_FOR_PHONE;
            stationsGoneAt = 0;
          }
        }
      }
      break;
    case Phase::CONNECTING:
      pollAttempt();
      break;
    case Phase::CONNECTED:
      if (server.resultSeenByPhone() || now - connectedAt > RESULT_HOLD_MS) {
        LOG_DBG("SETUP", "Result %s by the phone; setup done", server.resultSeenByPhone() ? "seen" : "not fetched");
        currentPhase = Phase::DONE;
      }
      break;
    case Phase::FAILED:
    case Phase::DONE:
    case Phase::ERROR:
      break;
  }
  return currentPhase;
}

void WifiPhoneSetup::end() {
  if (currentPhase == Phase::CONNECTING) {
    WiFi.disconnect(false);
    LOG_DBG("SETUP", "Join abandoned");
  }
  if (eventHandle) {
    WiFi.removeEvent(eventHandle);
    eventHandle = 0;
  }
  server.end();
  portalLink.end();
  if (currentPhase != Phase::DONE && currentPhase != Phase::CONNECTED) currentPhase = Phase::ERROR;
}

void WifiPhoneSetup::onSubmit(const std::string& ssid, const std::string& password) {
  joinedSsid = ssid;
  joinedPassword = password;
  joinedIp.clear();
  attemptPending = true;
}

void WifiPhoneSetup::startAttempt() {
  attemptCount++;
  currentPhase = Phase::CONNECTING;
  reason = FailReason::NONE;
  attemptStartedAt = millis();
  server.setState(WifiSetupServer::State::CONNECTING);

  const uint8_t channel = channelFor(joinedSsid);
  LOG_DBG("SETUP", "Attempt %d: joining %s on channel %u with the portal up", attemptCount, joinedSsid.c_str(), channel);

  // Credentials live in WifiCredentialStore; nothing goes to the SDK's NVS.
  WiFi.persistent(false);
  // A failed join must stay failed: the driver's own retries would hop the
  // radio and the phone with it, and the page would never see the verdict.
  WiFi.setAutoReconnect(false);
  // Stops a previous attempt; keeps the radio -- and so the portal -- up.
  WiFi.disconnect(false);
  delay(50);

  // With the channel known a fast scan finds the network at once; unknown
  // (a hidden network the phone named) means the full sweep, and the
  // portal may move channel under the phone. The page waits that out.
  WiFi.setScanMethod(channel ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    char hostname[sizeof("eMinimal-") + 12];
    snprintf(hostname, sizeof(hostname), "eMinimal-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    WiFi.setHostname(hostname);
  }

  disconnectEvents = 0;
  lastDisconnectReason = 0;
  lastDisconnectAt = 0;
  WiFi.begin(joinedSsid.c_str(), joinedPassword.empty() ? nullptr : joinedPassword.c_str(), channel);
}

void WifiPhoneSetup::pollAttempt() {
  const wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    const IPAddress ip = WiFi.localIP();
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    joinedIp = buffer;
    connectedAt = millis();
    currentPhase = Phase::CONNECTED;
    // Back to the default the file server relies on for blips.
    WiFi.setAutoReconnect(true);
    server.setState(WifiSetupServer::State::CONNECTED, std::string(), joinedIp);
    LOG_DBG("SETUP", "Joined %s as %s (channel %d, RSSI %d) after %lu ms", joinedSsid.c_str(), joinedIp.c_str(),
            WiFi.channel(), WiFi.RSSI(), millis() - attemptStartedAt);
    return;
  }

  const int events = disconnectEvents;
  if (events > 0) {
    const uint8_t why = lastDisconnectReason;
    if (why == WIFI_REASON_NO_AP_FOUND) {
      fail(FailReason::NOT_FOUND);
      return;
    }
    // Anything else gets the core's one automatic retry before it counts.
    if (events >= 2 || millis() - lastDisconnectAt > RETRY_GRACE_MS) {
      LOG_DBG("SETUP", "Station gave up: reason %u after %d disconnect(s)", why, events);
      fail(isPasswordReason(why) ? FailReason::WRONG_PASSWORD : FailReason::TIMEOUT);
      return;
    }
  }

  if (millis() - attemptStartedAt > ATTEMPT_TIMEOUT_MS) {
    fail(FailReason::TIMEOUT);
  }
}

void WifiPhoneSetup::fail(const FailReason why) {
  WiFi.disconnect(false);  // no lingering retry; the portal stays up
  reason = why;
  currentPhase = Phase::FAILED;
  server.setState(WifiSetupServer::State::FAILED, pageErrorText(why));
  LOG_DBG("SETUP", "Join failed: %s (after %lu ms)", pageErrorText(why), millis() - attemptStartedAt);
}

uint8_t WifiPhoneSetup::channelFor(const std::string& ssid) const {
  if (ssid.empty()) return 0;
  for (const auto& target : visible) {
    if (target.ssid == ssid) return target.channel;
  }
  return 0;
}

// What the page shows; the panel says the same through tr().
const char* WifiPhoneSetup::pageErrorText(const FailReason why) {
  switch (why) {
    case FailReason::WRONG_PASSWORD:
      return "The network refused the password.";
    case FailReason::NOT_FOUND:
      return "The reader cannot see that network.";
    case FailReason::TIMEOUT:
      return "No answer from the network in 20 seconds.";
    case FailReason::NONE:
    default:
      return "";
  }
}
