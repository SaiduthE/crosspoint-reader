#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class DNSServer;

// The device raises a page and a phone opens it. This owns the Wi-Fi side of
// that pattern -- the access point, the DNS catch-all that makes joining it
// enough to land on the page, mDNS, and the two strings a QR needs -- so a
// feature (Wi-Fi setup, file transfer, a game night, notes) only brings the
// page it serves. Paint the join screen with PhoneJoinPanel.
//
// The access point is open by default, as upstream's hotspot is. A page that
// carries a secret -- Wi-Fi setup carries the home password -- asks for WPA2
// with a passphrase generated per session; the QR carries it, so a scanned
// join costs nothing and only the typed fallback sees it. A feature the phone
// should rejoin by itself (text entry) passes a fixed, remembered passphrase.
class PhonePortal {
 public:
  static constexpr size_t PASSPHRASE_LENGTH = 8;

  struct Config {
    const char* ssid = "eMinimal";
    // WPA2 with a fresh 8-character passphrase per begin(); otherwise open.
    bool randomPassphrase = false;
    // WPA2 with this passphrase (8..63 characters) when non-empty; takes
    // precedence over randomPassphrase.
    const char* passphrase = nullptr;
    // 1..13. Raising the AP on the channel a station join will land on keeps
    // the phone attached through the join; an AP+STA radio has one channel.
    uint8_t channel = 1;
    uint8_t maxClients = 4;
    // WIFI_AP_STA instead of WIFI_AP: the station side stays usable while the
    // portal is up, which Wi-Fi setup needs to try the home network.
    bool keepStation = false;
    const char* hostname = "eminimal";
  };

  PhonePortal() = default;
  ~PhonePortal();
  PhonePortal(const PhonePortal&) = delete;
  PhonePortal& operator=(const PhonePortal&) = delete;

  // AP + captive DNS + mDNS. False when the AP would not come up.
  bool begin(const Config& config);
  // Answers pending DNS queries; call from the activity loop while up.
  void loop();
  // Drops the AP, DNS and mDNS. A station connection made meanwhile survives
  // (the mode falls back to STA); the caller decides what to do with it.
  void end();

  bool isUp() const { return up; }
  const std::string& ssid() const { return ssidStr; }
  // Empty for an open network.
  const std::string& passphrase() const { return passphraseStr; }
  // PASSPHRASE_LENGTH characters from an alphabet without 0/O, 1/l/I.
  static std::string generatePassphrase();
  const std::string& ip() const { return ipStr; }
  int stationCount() const;

  // Wi-Fi network config QR, per the zxing barcode-contents spec: a phone
  // camera offers "join this network".
  std::string joinQrPayload() const;
  // http://192.168.4.1/ -- the captive popup goes here by itself; this is the
  // typed fallback and the second QR when a feature wants one.
  std::string pageUrl() const;
  std::string hostnameUrl() const;

 private:
  bool up = false;
  std::string ssidStr;
  std::string passphraseStr;
  std::string ipStr;
  std::string hostnameStr;
  DNSServer* dns = nullptr;
};
