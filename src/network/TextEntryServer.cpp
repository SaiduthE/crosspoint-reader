#include "TextEntryServer.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <Memory.h>
#include <WebServer.h>

#include "PhonePage.h"
#include "html/TextEntryPageHtml.generated.h"
#include "html/css/appCss.generated.h"

namespace {
constexpr uint16_t HTTP_PORT = 80;
// Larger than any field's text once JSON-escaped; anything bigger is not ours.
constexpr size_t MAX_BODY_BYTES = 1024;

const char* kindName(const TextEntryServer::Kind kind) {
  switch (kind) {
    case TextEntryServer::Kind::Password:
      return "password";
    case TextEntryServer::Kind::Url:
      return "url";
    case TextEntryServer::Kind::Text:
    default:
      return "text";
  }
}

// Well-formed UTF-8 lead/continuation structure. Overlongs and surrogates are
// not policed: the renderers only need sequences they can step through.
bool isWellFormedUtf8(const char* s, const size_t len) {
  size_t i = 0;
  while (i < len) {
    const auto c = static_cast<uint8_t>(s[i]);
    if (c < 0x80) {
      i++;
      continue;
    }
    size_t extra;
    if (c >= 0xC2 && c <= 0xDF) {
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
    } else if (c >= 0xF0 && c <= 0xF4) {
      extra = 3;
    } else {
      return false;
    }
    if (len - i <= extra) return false;
    for (size_t k = 1; k <= extra; k++) {
      if ((static_cast<uint8_t>(s[i + k]) & 0xC0) != 0x80) return false;
    }
    i += extra + 1;
  }
  return true;
}

bool hasControlChars(const char* s, const size_t len) {
  for (size_t i = 0; i < len; i++) {
    const auto c = static_cast<uint8_t>(s[i]);
    if (c < 0x20 || c == 0x7F) return true;
  }
  return false;
}
}  // namespace

TextEntryServer::TextEntryServer() = default;

TextEntryServer::~TextEntryServer() { end(); }

bool TextEntryServer::begin(Field entry) {
  if (running) end();

  field = std::move(entry);
  received = false;
  pending = false;
  submission.clear();
  requests = 0;

  server = makeUniqueNoThrow<WebServer>(HTTP_PORT);
  if (!server) {
    LOG_ERR("TXT", "OOM: WebServer");
    return false;
  }

  server->on("/", HTTP_GET, [this] { handlePage(); });
  server->on("/css/app.css", HTTP_GET, [this] { handleStylesheet(); });
  server->on("/api/text", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/text", HTTP_POST, [this] { handleSubmit(); });
  server->onNotFound([this] { handleNotFound(); });

  // Only If-None-Match is read (PhonePage::sendStatic); WebServer drops any
  // header it was not told to keep.
  const char* collected[] = {"If-None-Match"};
  server->collectHeaders(collected, 1);
  server->begin();
  running = true;
  LOG_DBG("TXT", "Serving on port %u, free heap %d bytes", HTTP_PORT, ESP.getFreeHeap());
  return true;
}

void TextEntryServer::handleClient() {
  if (running && server) server->handleClient();
}

void TextEntryServer::end() {
  if (!running) return;
  running = false;
  if (server) {
    server->stop();
    server.reset();
  }
  LOG_DBG("TXT", "Server stopped, free heap %d bytes", ESP.getFreeHeap());
}

std::string TextEntryServer::takeSubmission() {
  pending = false;
  return std::move(submission);
}

size_t TextEntryServer::byteLimit() const { return field.maxBytes ? field.maxBytes : MAX_TEXT_BYTES; }

void TextEntryServer::handlePage() {
  requests++;
  PhonePage::sendStatic(*server, TextEntryPageHtml, sizeof(TextEntryPageHtml), TextEntryPageHtmlETag, "text/html");
}

void TextEntryServer::handleStylesheet() {
  requests++;
  PhonePage::sendStatic(*server, appCss, appCssCompressedSize, appCssETag, "text/css");
}

void TextEntryServer::handleStatus() {
  requests++;
  JsonDocument doc;
  doc["title"] = field.title;
  if (field.kind != Kind::Password) doc["text"] = field.text;
  doc["maxBytes"] = static_cast<uint32_t>(byteLimit());
  doc["kind"] = kindName(field.kind);
  doc["state"] = received ? "received" : "waiting";

  String out;
  serializeJson(doc, out);
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "application/json", out);
}

void TextEntryServer::handleSubmit() {
  requests++;
  if (received) {
    server->send(409, "text/plain", "The reader already has this text");
    return;
  }
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }
  const String body = server->arg("plain");
  if (body.length() > MAX_BODY_BYTES) {
    server->send(413, "text/plain", "Too long");
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["text"].is<const char*>()) {
    server->send(400, "text/plain", "Missing text");
    return;
  }
  const JsonString s = doc["text"].as<JsonString>();
  const char* text = s.c_str();
  const size_t len = s.size();
  if (len > byteLimit()) {
    server->send(400, "text/plain", "Too long for this field");
    return;
  }
  if (hasControlChars(text, len)) {
    server->send(400, "text/plain", "Line breaks and control characters are not allowed");
    return;
  }
  if (!isWellFormedUtf8(text, len)) {
    server->send(400, "text/plain", "Not valid UTF-8");
    return;
  }

  submission.assign(text, len);
  received = true;
  pending = true;
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "application/json", "{\"state\":\"received\"}");
  LOG_INF("TXT", "Phone sent %u bytes", static_cast<unsigned>(len));
}

void TextEntryServer::handleNotFound() {
  requests++;
  if (PhonePage::redirectCaptive(*server)) return;
  server->send(404, "text/plain", "404 Not Found\n\nURI: " + server->uri() + "\n");
}
