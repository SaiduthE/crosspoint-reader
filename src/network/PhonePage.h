#pragma once

#include <WebServer.h>

#include <cstddef>

// Helpers every "phone page" server shares -- file transfer, Wi-Fi setup, and
// whatever the device serves to a phone next. A page is HTML gzipped into
// flash by scripts/build_html.py (a *.generated.h beside the source), so it
// is immutable for the life of a firmware image and its ETag is stable.
namespace PhonePage {

// Sends a build-time gzipped asset, answering a matching If-None-Match with
// 304 so the phone reuses its cache on every navigation after the first.
// The server must collect the "If-None-Match" header (WebServer::collectHeaders).
inline void sendStatic(WebServer& server, const char* data, size_t len, const char* etag, const char* contentType) {
  if (server.header("If-None-Match") == etag) {
    server.sendHeader("ETag", etag);
    server.sendHeader("Cache-Control", "no-cache");
    server.send(304);
    return;
  }
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("ETag", etag);
  // no-cache: the browser may cache, but must revalidate (conditional GET)
  // before reuse -- this is what unlocks the 304 above.
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, contentType, data, len);
}

// Captive-portal behaviour for an unmatched URI on an access point: every
// phone OS probes a known URL after joining (generate_204, hotspot-detect.html,
// connecttest.txt, ...) and pops its sign-in sheet when the answer is not the
// one it expects. A 302 to the page is that answer. API paths still 404 so a
// page's own XHR errors surface instead of being redirected to HTML.
// Returns true when the request was answered.
inline bool redirectCaptive(WebServer& server, const char* pageUrl = "/") {
  if (server.uri().startsWith("/api/")) return false;
  server.sendHeader("Location", pageUrl, true);
  server.send(302, "text/plain", "");
  return true;
}

}  // namespace PhonePage
