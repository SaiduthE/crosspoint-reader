#pragma once

#include <NetworkEvents.h>

#include <cstdint>
#include <string>
#include <vector>

#include "PhonePortal.h"
#include "WifiSetupServer.h"

// Joining a home network when the device cannot take a password: the device
// raises a portal, the phone types the password into the page, and the device
// tries the network with the portal still up so the phone sees the outcome.
// See vault 04 §11. This is the state machine; WifiSelectionActivity paints it
// and decides what "done" means.
class WifiPhoneSetup {
 public:
  enum class Phase {
    WAITING_FOR_PHONE,  // portal up, nothing attached yet
    PHONE_JOINED,       // a phone is on the portal, nothing submitted yet
    CONNECTING,         // join in flight
    CONNECTED,          // joined; the portal stays up until the phone has seen it
    FAILED,             // join failed; the portal stays up for another try
    DONE,               // connected and the phone has seen it -- the caller may end()
    ERROR               // the portal or its server would not come up
  };

  enum class FailReason { NONE, WRONG_PASSWORD, NOT_FOUND, TIMEOUT };

  struct Target {
    std::string ssid;
    uint8_t channel;  // 0 when unknown
  };

  // presetSsid pre-fills the page (empty for a hidden network the phone names);
  // visible is the last scan, for the page's pick-list and for the channel to
  // raise the portal on.
  WifiPhoneSetup(std::string presetSsid, std::vector<Target> visible);
  ~WifiPhoneSetup();
  WifiPhoneSetup(const WifiPhoneSetup&) = delete;
  WifiPhoneSetup& operator=(const WifiPhoneSetup&) = delete;

  bool begin();
  // Pumps DNS, HTTP and the join attempt. Returns the phase afterwards.
  Phase loop();
  // Drops the portal and its server. A CONNECTED station survives; an
  // attempt still in flight is abandoned.
  void end();

  Phase phase() const { return currentPhase; }
  FailReason failReason() const { return reason; }
  const PhonePortal& portal() const { return portalLink; }
  const std::string& ssid() const { return joinedSsid; }
  const std::string& password() const { return joinedPassword; }
  const std::string& ip() const { return joinedIp; }
  int attempts() const { return attemptCount; }

 private:
  void onSubmit(const std::string& ssid, const std::string& password);
  void startAttempt();
  void pollAttempt();
  void fail(FailReason why);
  uint8_t channelFor(const std::string& ssid) const;
  static const char* pageErrorText(FailReason why);

  std::string presetSsid;
  std::vector<Target> visible;

  PhonePortal portalLink;
  WifiSetupServer server;
  Phase currentPhase = Phase::WAITING_FOR_PHONE;
  FailReason reason = FailReason::NONE;

  std::string joinedSsid;
  std::string joinedPassword;
  std::string joinedIp;
  int attemptCount = 0;
  bool attemptPending = false;
  unsigned long attemptStartedAt = 0;
  unsigned long connectedAt = 0;
  unsigned long lastStationPoll = 0;
  unsigned long stationsGoneAt = 0;

  // Written from the network event task, read from loop(): the station's
  // disconnect reasons since the attempt started. WiFi.status() alone cannot
  // name a wrong password (a 4-way handshake timeout reads as plain
  // WL_DISCONNECTED in this core), so the verdict comes from here.
  network_event_handle_t eventHandle = 0;
  volatile int disconnectEvents = 0;
  volatile uint8_t lastDisconnectReason = 0;
  volatile unsigned long lastDisconnectAt = 0;
};
