#include "GameServer.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_random.h>

#include <cstdlib>

#include "html/PlayPageHtml.generated.h"

namespace {

struct VerbName {
  const char* name;
  party::Verb verb;
};

constexpr VerbName VERBS[] = {
    {"ready", party::Verb::Ready}, {"next", party::Verb::Next}, {"clue", party::Verb::Clue},
    {"guess", party::Verb::Guess}, {"pass", party::Verb::Pass}, {"place", party::Verb::Place},
    {"fire", party::Verb::Fire},   {"vote", party::Verb::Vote}, {"roll", party::Verb::Roll},
    {"move", party::Verb::Move},   {"pick", party::Verb::Pick},
};

party::Verb parseVerb(const String& name) {
  for (const VerbName& entry : VERBS) {
    if (name == entry.name) return entry.verb;
  }
  return party::Verb::None;
}

int16_t parseArg(WebServer& server, const char* name) {
  const String value = server.arg(name);
  if (value.isEmpty()) return -1;
  return static_cast<int16_t>(value.toInt());
}

uint32_t newToken() {
  uint32_t token = esp_random();
  // 0 marks a free seat, so it can never be handed out as a token.
  while (token == 0) token = esp_random();
  return token;
}

}  // namespace

void GameServer::begin() {
  if (running_) return;

  server_.reset(new (std::nothrow) WebServer(PORT));
  if (!server_) {
    LOG_ERR("GAMESRV", "OOM: WebServer");
    return;
  }

  server_->on("/", HTTP_GET, [this] { handlePlayPage(); });
  server_->on("/api/game/state", HTTP_GET, [this] { handleState(); });
  server_->on("/api/game/state", HTTP_POST, [this] { handleState(); });
  server_->on("/api/game/join", HTTP_POST, [this] { handleJoin(); });
  server_->on("/api/game/act", HTTP_POST, [this] { handleAction(); });
  server_->on("/api/game/leave", HTTP_POST, [this] { handleLeave(); });
  server_->onNotFound([this] { handleNotFound(); });

  const char* collected[] = {"If-None-Match"};
  server_->collectHeaders(collected, 1);
  server_->begin();
  running_ = true;
  LOG_DBG("GAMESRV", "Game server listening on port %u, free heap %d", PORT, ESP.getFreeHeap());
}

void GameServer::stop() {
  if (!running_) return;
  running_ = false;
  if (server_) {
    server_->stop();
    server_.reset();
  }
  LOG_DBG("GAMESRV", "Game server stopped, free heap %d", ESP.getFreeHeap());
}

void GameServer::handleClient() {
  if (running_ && server_) server_->handleClient();
}

void GameServer::handlePlayPage() {
  // The page is immutable for the life of a firmware image, so a strong ETag
  // keeps a phone that reloads mid-game off the flash entirely.
  if (server_->header("If-None-Match") == PlayPageHtmlETag) {
    server_->sendHeader("ETag", PlayPageHtmlETag);
    server_->sendHeader("Cache-Control", "no-cache");
    server_->send(304);
    return;
  }
  server_->sendHeader("Content-Encoding", "gzip");
  server_->sendHeader("ETag", PlayPageHtmlETag);
  server_->sendHeader("Cache-Control", "no-cache");
  server_->send_P(200, "text/html", PlayPageHtml, PlayPageHtmlCompressedSize);
}

int8_t GameServer::seatFromRequest() {
  const String raw = server_->arg("t");
  if (raw.isEmpty()) return -1;
  const uint32_t token = strtoul(raw.c_str(), nullptr, 16);
  const int8_t seat = session_.seatForToken(token);
  if (seat >= 0) session_.touch(static_cast<uint8_t>(seat), millis());
  return seat;
}

void GameServer::sendState(const int8_t seat) {
  const size_t written = session_.writeStateJson(json_, sizeof(json_), seat, millis());
  server_->sendHeader("Cache-Control", "no-store");
  server_->send_P(200, "application/json", json_, written);
}

void GameServer::handleState() { sendState(seatFromRequest()); }

void GameServer::handleJoin() {
  const uint32_t token = newToken();
  const int8_t seat = session_.join(server_->arg("name").c_str(), token, millis());
  if (seat < 0) {
    server_->send(200, "application/json", "{\"error\":\"No free seat right now\"}");
    return;
  }

  char body[48];
  snprintf(body, sizeof(body), "{\"t\":\"%08lx\",\"seat\":%d}", static_cast<unsigned long>(token), seat);
  server_->sendHeader("Cache-Control", "no-store");
  server_->send(200, "application/json", body);
  LOG_DBG("GAMESRV", "Seat %d joined as %s", seat, session_.player(static_cast<uint8_t>(seat)).name);
}

void GameServer::handleAction() {
  const int8_t seat = seatFromRequest();
  if (seat < 0) {
    server_->send(403, "application/json", "{\"error\":\"Join first\"}");
    return;
  }

  party::Action action;
  action.verb = parseVerb(server_->arg("v"));
  action.a = parseArg(*server_, "a");
  action.b = parseArg(*server_, "b");
  action.c = parseArg(*server_, "c");
  const String text = server_->arg("s");
  if (!text.isEmpty()) party::sanitizeName(text.c_str(), action.text, sizeof(action.text));

  session_.applyAction(static_cast<uint8_t>(seat), action);
  // Answer with the state the action produced: one round trip instead of
  // waiting out the phone's poll interval.
  sendState(seat);
}

void GameServer::handleLeave() {
  const int8_t seat = seatFromRequest();
  if (seat >= 0) session_.leave(static_cast<uint8_t>(seat));
  sendState(-1);
}

void GameServer::handleNotFound() {
  if (server_->method() == HTTP_OPTIONS) {
    server_->send(204, "text/plain", "");
    return;
  }
  // Everything that is not an API call goes to the page. Phones probe a handful
  // of well-known URLs to detect a captive portal; answering them with a
  // redirect is what pops the browser open by itself.
  if (!server_->uri().startsWith("/api/")) {
    server_->sendHeader("Location", "/", true);
    server_->send(302, "text/plain", "");
    return;
  }
  server_->send(404, "application/json", "{\"error\":\"Unknown endpoint\"}");
}
