#pragma once

#include <BoardConfig.h>

// True on boards with no touch and no Left/Right front buttons (e-Minimal): the
// UI must navigate and enter text with only Up/Down/Confirm/Back/Power. Runtime
// check rather than a device #if — same predicate WifiSelectionActivity's
// phoneEntersText() already used, describes the hardware rather than the product.
namespace five_button {
inline bool active() {
  const auto& p = BoardConfig::ACTIVE;
  if (BoardConfig::hasTouch()) return false;
  if (p.inputStyle != BoardConfig::InputStyle::DigitalButtons) return false;
  return p.input.left == BoardConfig::PIN_UNASSIGNED || p.input.right == BoardConfig::PIN_UNASSIGNED;
}
}  // namespace five_button
