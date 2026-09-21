#pragma once

#include <WiFi.h>

// What the radio is doing right now, for a glance at the header: joined to a
// network, running a hotspot, or off. Off is the reading state -- CrossPoint
// brings Wi-Fi up only inside the network screens and restarts on leaving
// them, so this never says STATION over a book.
namespace NetworkLink {

enum class State { OFF, STATION, HOTSPOT };

inline State state() {
  const wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_MODE_NULL) return State::OFF;
  if ((mode & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED) return State::STATION;
  if (mode & WIFI_MODE_AP) return State::HOTSPOT;
  return State::OFF;
}

}  // namespace NetworkLink
