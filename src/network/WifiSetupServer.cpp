#include "WifiSetupServer.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <WebServer.h>

#include "PhonePage.h"
#include "html/WifiSetupPageHtml.generated.h"
#include "html/css/appCss.generated.h"

namespace {
constexpr uint16_t HTTP_PORT = 80;

const char* stateName(const WifiSetupServer::State state) {
  switch (state) {
    case WifiSetupServer::State::CONNECTING:
      return "connecting";
    case WifiSetupServer::State::CONNECTED:
      return "connected";
    case WifiSetupServer::State::FAILED:
      return "failed";
    case WifiSetupServer::State::IDLE:
    default:
      return "idle";
  }
}
}  // namespace

WifiSetupServer::WifiSetupServer() = default;

WifiSetupServer::~WifiSetupServer() { end(); }

bool WifiSetupServer::begin(std::string preset, std::vector<std::string> ssids, SubmitHandler handler) {
  if (running) end();

  presetSsid = std::move(preset);
  lastSsid = presetSsid;
  visibleSsids = std::move(ssids);
  onSubmit = std::move(handler);
  currentState = State::IDLE;
  errorText.clear();
  ipText.clear();
  resultSeen = false;

  server.reset(new (std::nothrow) WebServer(HTTP_PORT));
  if (!server) {
    LOG_ERR("SETUP", "OOM: WebServer");
    return false;
  }

  server->on("/", HTTP_GET, [this] { handlePage(); });
  server->on("/css/app.css", HTTP_GET, [this] { handleStylesheet(); });
  server->on("/api/setup", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/setup", HTTP_POST, [this] { handleSubmit(); });
  server->on("/api/networks", HTTP_GET, [this] { handleNetworks(); });
  server->onNotFound([this] { handleNotFound(); });

  // Only If-None-Match is read (PhonePage::sendStatic); WebServer drops any
  // header it was not told to keep.
  const char* collected[] = {"If-None-Match"};
  server->collectHeaders(collected, 1);
  server->begin();
  running = true;
  LOG_DBG("SETUP", "Serving on port %u, free heap %d bytes", HTTP_PORT, ESP.getFreeHeap());
  return true;
}

void WifiSetupServer::handleClient() {
  if (running && server) server->handleClient();
}

void WifiSetupServer::end() {
  if (!running) return;
  running = false;
  if (server) {
    server->stop();
    server.reset();
  }
  LOG_DBG("SETUP", "Stopped, free heap %d bytes", ESP.getFreeHeap());
}

void WifiSetupServer::setState(const State state, std::string error, std::string ip) {
  currentState = state;
  errorText = std::move(error);
  ipText = std::move(ip);
  resultSeen = false;
}

void WifiSetupServer::handlePage() const {
  PhonePage::sendStatic(*server, WifiSetupPageHtml, sizeof(WifiSetupPageHtml), WifiSetupPageHtmlETag, "text/html");
}

void WifiSetupServer::handleStylesheet() const {
  PhonePage::sendStatic(*server, appCss, appCssCompressedSize, appCssETag, "text/css");
}

void WifiSetupServer::handleStatus() {
  JsonDocument doc;
  doc["state"] = stateName(currentState);
  doc["ssid"] = lastSsid;
  if (!errorText.empty()) doc["error"] = errorText;
  if (!ipText.empty()) doc["ip"] = ipText;

  char out[320];
  const size_t written = serializeJson(doc, out, sizeof(out));
  if (written >= sizeof(out)) {
    server->send(500, "text/plain", "status too long");
    return;
  }
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "application/json", out);

  if (currentState == State::CONNECTED || currentState == State::FAILED) resultSeen = true;
}

void WifiSetupServer::handleNetworks() const {
  // Streamed one name at a time: the list is a few hundred bytes at most,
  // but the pattern is the one the file server uses and it never spikes.
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "application/json", "");
  server->sendContent("[");
  JsonDocument doc;
  char out[96];
  bool first = true;
  for (const auto& ssid : visibleSsids) {
    doc.clear();
    doc.set(ssid);
    const size_t written = serializeJson(doc, out, sizeof(out));
    if (written >= sizeof(out)) continue;
    if (!first) server->sendContent(",");
    server->sendContent(out);
    first = false;
  }
  server->sendContent("]");
  server->sendContent("");
}

void WifiSetupServer::handleSubmit() {
  if (currentState == State::CONNECTING) {
    server->send(409, "text/plain", "Already connecting");
    return;
  }
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, server->arg("plain"));
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  const std::string ssid = doc["ssid"] | std::string();
  const std::string password = doc["password"] | std::string();
  if (ssid.empty() || ssid.size() > 32) {
    server->send(400, "text/plain", "Network name must be 1-32 characters");
    return;
  }
  if (password.size() > 63) {
    server->send(400, "text/plain", "Password must be at most 63 characters");
    return;
  }

  lastSsid = ssid;
  setState(State::CONNECTING);
  server->send(200, "application/json", "{\"state\":\"connecting\"}");
  LOG_DBG("SETUP", "Phone submitted credentials for %s (password %s)", ssid.c_str(), password.empty() ? "empty" : "set");

  if (onSubmit) onSubmit(ssid, password);
}

void WifiSetupServer::handleNotFound() const {
  if (PhonePage::redirectCaptive(*server)) return;
  server->send(404, "text/plain", "404 Not Found\n\nURI: " + server->uri() + "\n");
}
