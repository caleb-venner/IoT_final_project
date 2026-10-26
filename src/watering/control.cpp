// control.cpp
// Purpose: Watering decision logic, as summarised in README "Watering logic". STUB.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#include <Arduino.h>
#include "control.h"
#include "config.h"

void control_init() {
  // TODO(Caleb): reset any state (e.g. last watering time).
}

ControlDecision control_decide(const ControlInputs& in) {
  // TODO(Caleb): dry -> water; borderline + fresh wilted (>= WILT_CONF_MIN) -> water;
  //              wet -> never; respect PUMP_COOLDOWN_MS; stale camera -> moisture-only.
  (void)in;
  return {false, 0, "stub"};
}
