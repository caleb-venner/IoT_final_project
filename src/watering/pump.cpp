// pump.cpp
// Purpose: Non-blocking (millis-based) peristaltic pump driver with mL dosing support.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#include <Arduino.h>
#include "pump.h"
#include "config.h"

static bool s_on = false;
static uint32_t s_start_ms = 0;
static uint32_t s_duration_ms = 0;
static float s_total_ml = 0.0f;   // volume from completed runs (in-progress run added on read)

static void pump_write(bool on) {
  digitalWrite(PUMP_PIN, (on == PUMP_ACTIVE_HIGH) ? HIGH : LOW);
}

// Estimated volume for a run duration at the current calibrated flow rate.
static float ms_to_ml(uint32_t ms) {
  return (ms / 1000.0f) * g_cfg.ml_per_sec;
}

// Switches the pump off and adds the delivered volume to the running total.
static void pump_off(const char* label) {
  uint32_t elapsed = millis() - s_start_ms;
  pump_write(false);
  s_on = false;
  float delivered = ms_to_ml(elapsed);
  s_total_ml += delivered;
  Serial.printf("[pump] %s after %lu ms (+%.1f mL, total %.1f mL)\n",
                label, (unsigned long)elapsed, delivered, s_total_ml);
}

void pump_init() {
  pinMode(PUMP_PIN, OUTPUT);
  pump_write(false);
}

uint32_t pump_run_ms(uint32_t ms, bool manual) {
  if (ms == 0) return 0;

  // Automatic requests never interrupt a run; manual requests restart with the new duration.
  if (s_on) {
    if (!manual) return 0;
    pump_off("RESTARTED");
  }

  uint32_t max_ms = manual ? (uint32_t)PUMP_MANUAL_MAX_RUN_MS : g_cfg.pump_max_run_ms;
  s_duration_ms = min(ms, max_ms);
  s_start_ms = millis();
  s_on = true;
  pump_write(true);

  Serial.printf("[pump] ON for %lu ms (~%.1f mL)\n",
                (unsigned long)s_duration_ms, ms_to_ml(s_duration_ms));
  return s_duration_ms;
}

uint32_t pump_run_ml(float ml, bool manual) {
  if (!(ml > 0.0f)) return 0;  // also rejects NaN from bad input
  // Clamp before the float->uint32 cast so oversized requests cannot overflow.
  float ms = min((ml / g_cfg.ml_per_sec) * 1000.0f + 0.5f, (float)PUMP_MANUAL_MAX_RUN_MS);
  return pump_run_ms((uint32_t)ms, manual);
}

void pump_stop() {
  if (s_on) pump_off("STOPPED");
  else pump_write(false);  // defensive: force the output low regardless of state
}

bool pump_is_on() {
  return s_on;
}

uint32_t pump_remaining_ms() {
  if (!s_on) return 0;
  uint32_t elapsed = millis() - s_start_ms;
  return (elapsed >= s_duration_ms) ? 0 : s_duration_ms - elapsed;
}

void pump_update() {
  if (s_on && millis() - s_start_ms >= s_duration_ms) pump_off("OFF");
}

float pump_total_ml() {
  return s_on ? s_total_ml + ms_to_ml(millis() - s_start_ms) : s_total_ml;
}

void pump_reset_total_ml() {
  s_total_ml = 0.0f;
}
