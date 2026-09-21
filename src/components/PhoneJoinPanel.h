#pragma once

#include <GfxRenderer.h>

#include <string>

#include "components/themes/BaseTheme.h"

class PhonePortal;

// The glass side of "the device raises a page, the phone opens it": up to
// two numbered QRs side by side -- join the network, open the page -- with
// the typed fallback under each, a headline above and a status line below.
// Every phone feature paints this card so a scan looks the same whether it
// is Wi-Fi setup, file transfer or a game.
namespace PhoneJoinPanel {

struct Content {
  const char* headline = nullptr;  // "Join HomeNet from your phone" (wraps to 2 lines)
  const char* status = nullptr;    // "Waiting for your phone..." (bold, wraps to 2 lines)
  // Step 1, join: the portal's network. nullptr when the phone is already on
  // the same network (a station-mode page).
  const PhonePortal* portal = nullptr;
  // Step 2, open: the page. Empty when joining is enough (the captive popup
  // opens it) and the caller wants one QR only.
  std::string pageUrl;
  const char* pageAlt = nullptr;  // a second line under the URL, e.g. "or http://eminimal.local/"
};

// Paints into bounds; returns the y just below what it drew.
int draw(const GfxRenderer& renderer, const Rect& bounds, const Content& content);

}  // namespace PhoneJoinPanel
