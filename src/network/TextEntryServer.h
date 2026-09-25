#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class WebServer;

// The page a phone opens to type one text field for the device: the page
// reads the field (title, current text, limits), the phone posts the typed
// text once, and the owner collects it. Serves on port 80 of whatever link is
// up -- a PhonePortal's access point or the station -- so only one of this,
// WifiSetupServer and CrossPointWebServer may run at a time.
class TextEntryServer {
 public:
  enum class Kind : uint8_t { Text, Password, Url };

  struct Field {
    std::string title;
    // Pre-fills the page; never sent for Password.
    std::string text;
    // UTF-8 bytes accepted; 0 = MAX_TEXT_BYTES.
    size_t maxBytes = 0;
    Kind kind = Kind::Text;
  };

  static constexpr size_t MAX_TEXT_BYTES = 256;

  TextEntryServer();
  ~TextEntryServer();
  TextEntryServer(const TextEntryServer&) = delete;
  TextEntryServer& operator=(const TextEntryServer&) = delete;

  bool begin(Field field);
  void handleClient();
  void end();
  bool isRunning() const { return running; }

  // True once the phone has posted text that the owner has not taken yet.
  bool hasSubmission() const { return pending; }
  // Moves the posted text out. Later posts are refused (409): one field, one
  // answer.
  std::string takeSubmission();
  // Requests answered since begin(); the owner reads a change as a phone
  // being there, which is all a station-mode page can tell it.
  uint32_t requestCount() const { return requests; }

 private:
  void handlePage();
  void handleStylesheet();
  void handleStatus();
  void handleSubmit();
  void handleNotFound();
  size_t byteLimit() const;

  std::unique_ptr<WebServer> server;
  bool running = false;
  Field field;
  bool received = false;
  bool pending = false;
  std::string submission;
  uint32_t requests = 0;
};
