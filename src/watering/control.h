// control.h
// Purpose: Watering decision logic (moisture + camera health, hysteresis, cooldown).
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#pragma once
#include <stdint.h>
#include "plant_msgs.h"

struct ControlInputs {
  float      moisture_pct;
  bool       health_valid;   // a health message has been received
  PlantState health_state;
  float      health_conf;
  uint32_t   health_age_ms;
  bool       pump_on;
};

struct ControlDecision {
  bool        water;
  uint32_t    duration_ms;
  const char* reason;        // published in the MQTT event / displayed in WebUI
};

void control_init();
// Assumes the caller runs the pump whenever water == true (starts the cooldown).
ControlDecision control_decide(const ControlInputs& in);

// Cooldown inspection and manual reset for WebUI bench testing
uint32_t control_cooldown_remaining_s();
void control_reset_cooldown();
