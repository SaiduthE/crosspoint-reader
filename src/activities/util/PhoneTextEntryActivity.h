#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "activities/Activity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "network/PhonePortal.h"
#include "network/TextEntryServer.h"

// One text field typed on a phone. Already on a network, the reader serves the
// page on its station address and the phone stays where it is; otherwise it
// raises "eMinimal Text" with a passphrase generated once and kept, so a phone
// that joined before rejoins by itself. Same constructor and result contract
// as KeyboardEntryActivity: KeyboardResult{text}, or isCancelled.
//
// Leaves the radio as it found it and does not restart the device: the
// caller is still on the stack waiting for the text.
class PhoneTextEntryActivity final : public Activity {
 public:
  explicit PhoneTextEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                  std::string initialText = "", size_t maxLength = 0,
                                  InputType inputType = InputType::Text);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  // DNS and HTTP are answered from loop().
  bool skipLoopDelay() override { return server.isRunning(); }

 private:
  enum class Phase : uint8_t { Waiting, PhoneJoined, Received, Error };

  bool start();
  void cancel();
  // Generates and saves the hotspot passphrase on first use.
  static void ensurePassphrase();

  std::string title;
  std::string initialText;
  size_t maxLength;
  InputType inputType;

  PhonePortal portal;
  TextEntryServer server;
  Phase phase = Phase::Waiting;
  bool radioWasOff = false;
  // Served on the station link: no hotspot, no join step on the panel.
  bool stationMode = false;
  std::string stationUrl;
  // The text for the caller, and what the panel shows of it (built on the
  // loop task so the render task never reads `received` while it is moved).
  std::string received;
  std::string receivedShown;
  unsigned long receivedAt = 0;
  unsigned long lastActivityAt = 0;
  unsigned long lastStationPoll = 0;
  unsigned long stationsGoneAt = 0;
  uint32_t seenRequests = 0;
};
