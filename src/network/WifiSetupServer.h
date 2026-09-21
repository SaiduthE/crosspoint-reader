#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

class WebServer;

// The page a phone opens to type a Wi-Fi password for the device: one form
// with the SSID pre-filled, a POST, and a status the page polls until the
// device reports joined or failed. Runs on the PhonePortal's access point;
// WifiPhoneSetup owns both and drives the actual join.
class WifiSetupServer {
 public:
  enum class State { IDLE, CONNECTING, CONNECTED, FAILED };

  // Called from handleClient() when the phone submits; the owner starts the
  // join and reports back through setState().
  using SubmitHandler = std::function<void(const std::string& ssid, const std::string& password)>;

  WifiSetupServer();
  ~WifiSetupServer();
  WifiSetupServer(const WifiSetupServer&) = delete;
  WifiSetupServer& operator=(const WifiSetupServer&) = delete;

  // presetSsid pre-fills the form (empty for a hidden network the phone names);
  // visibleSsids feeds the form's pick-list.
  bool begin(std::string presetSsid, std::vector<std::string> visibleSsids, SubmitHandler onSubmit);
  void handleClient();
  void end();
  bool isRunning() const { return running; }

  void setState(State state, std::string error = std::string(), std::string ip = std::string());
  State state() const { return currentState; }
  // True once the phone has fetched a CONNECTED or FAILED status, so the
  // owner can drop the portal knowing the page showed the result.
  bool resultSeenByPhone() const { return resultSeen; }

 private:
  void handlePage() const;
  void handleStylesheet() const;
  void handleStatus();
  void handleNetworks() const;
  void handleSubmit();
  void handleNotFound() const;

  std::unique_ptr<WebServer> server;
  bool running = false;
  std::string presetSsid;
  std::string lastSsid;
  std::vector<std::string> visibleSsids;
  SubmitHandler onSubmit;

  State currentState = State::IDLE;
  std::string errorText;
  std::string ipText;
  bool resultSeen = false;
};
