// pump.cpp
// Purpose: Non-blocking (millis-based) peristaltic pump driver. STUB.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#include <Arduino.h>
#include "pump.h"
#include "config.h"

void pump_init() {
  // TODO(Caleb): PUMP_PIN as output, pump off (respect PUMP_ACTIVE_HIGH).
}

void pump_run_ms(uint32_t ms) {
  // TODO(Caleb): start pump, clamp to PUMP_MAX_RUN_MS, ignore if already running.
  (void)ms;
}

bool pump_is_on() {
  // TODO(Caleb)
  return false;
}

void pump_update() {
  // TODO(Caleb): turn off when the run time has elapsed, add delivered mL (PUMP_ML_PER_SEC).
}

float pump_total_ml() {
  // TODO(Caleb)
  return 0.0f;
}
