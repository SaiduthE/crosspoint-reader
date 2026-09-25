#include "PhonePortal.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_random.h>

namespace {
constexpr uint16_t DNS_PORT = 53;

// Escapes the characters the WIFI: QR grammar reserves (\ ; , " :).
std::string qrEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 4);
  for (const char c : in) {
    if (c == '\\' || c == ';' || c == ',' || c == '"' || c == ':') out += '\\';
    out += c;
  }
  return out;
}
}  // namespace

// No 0/O, 1/l/I: this is the string someone types from the panel when their
// camera does not read the QR.
std::string PhonePortal::generatePassphrase() {
  static constexpr char ALPHABET[] = "ABCDEFGHJKMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789";
  constexpr uint32_t ALPHABET_SIZE = sizeof(ALPHABET) - 1;
  std::string out;
  out.reserve(PASSPHRASE_LENGTH);
  for (size_t i = 0; i < PASSPHRASE_LENGTH; i++) {
    out += ALPHABET[esp_random() % ALPHABET_SIZE];
  }
  return out;
}

PhonePortal::~PhonePortal() { end(); }

bool PhonePortal::begin(const Config& config) {
  if (up) end();

  ssidStr = config.ssid ? config.ssid : "";
  hostnameStr = config.hostname ? config.hostname : "";
  if (config.passphrase && config.passphrase[0] != '\0') {
    passphraseStr = config.passphrase;
  } else {
    passphraseStr = config.randomPassphrase ? generatePassphrase() : std::string();
  }
  ipStr.clear();

  LOG_DBG("PORTAL", "Raising %s on channel %u (%s, %s), free heap %d bytes", ssidStr.c_str(), config.channel,
          passphraseStr.empty() ? "open" : "WPA2", config.keepStation ? "AP+STA" : "AP", ESP.getFreeHeap());

  WiFi.mode(config.keepStation ? WIFI_AP_STA : WIFI_AP);
  delay(100);

  const uint8_t channel = (config.channel >= 1 && config.channel <= 13) ? config.channel : 1;
  const bool started = WiFi.softAP(ssidStr.c_str(), passphraseStr.empty() ? nullptr : passphraseStr.c_str(), channel,
                                   false, config.maxClients);
  if (!started) {
    LOG_ERR("PORTAL", "softAP failed");
    return false;
  }
  delay(100);  // let the AP netif settle before reading its address

  const IPAddress apIp = WiFi.softAPIP();
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d", apIp[0], apIp[1], apIp[2], apIp[3]);
  ipStr = buffer;

  if (!hostnameStr.empty()) {
    MDNS.end();
    if (MDNS.begin(hostnameStr.c_str())) {
      LOG_DBG("PORTAL", "mDNS: http://%s.local/", hostnameStr.c_str());
    } else {
      LOG_DBG("PORTAL", "mDNS failed to start");
    }
  }

  // Every lookup resolves here, so the phone's connectivity probe lands on the
  // page's server and its sign-in sheet opens by itself.
  dns = new (std::nothrow) DNSServer();
  if (dns) {
    dns->setErrorReplyCode(DNSReplyCode::NoError);
    dns->start(DNS_PORT, "*", apIp);
  } else {
    LOG_ERR("PORTAL", "OOM: DNS server; the phone needs the URL typed in");
  }

  up = true;
  LOG_DBG("PORTAL", "Up at %s, free heap %d bytes", ipStr.c_str(), ESP.getFreeHeap());
  return true;
}

void PhonePortal::loop() {
  if (dns) dns->processNextRequest();
}

void PhonePortal::end() {
  if (!up) return;
  up = false;

  if (dns) {
    dns->stop();
    delete dns;
    dns = nullptr;
  }
  if (!hostnameStr.empty()) MDNS.end();

  // enableAP(false) underneath: AP -> off, AP+STA -> STA.
  WiFi.softAPdisconnect(true);
  delay(30);
  LOG_DBG("PORTAL", "Down, free heap %d bytes", ESP.getFreeHeap());
}

int PhonePortal::stationCount() const { return up ? static_cast<int>(WiFi.softAPgetStationNum()) : 0; }

std::string PhonePortal::joinQrPayload() const {
  // https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config-android-ios-11
  if (passphraseStr.empty()) {
    return "WIFI:T:nopass;S:" + qrEscape(ssidStr) + ";;";
  }
  return "WIFI:T:WPA;S:" + qrEscape(ssidStr) + ";P:" + qrEscape(passphraseStr) + ";;";
}

std::string PhonePortal::pageUrl() const { return "http://" + ipStr + "/"; }

std::string PhonePortal::hostnameUrl() const { return "http://" + hostnameStr + ".local/"; }
