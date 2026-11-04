// Watering decision state machine evaluated using runtime configuration (g_cfg).

#include <Arduino.h>
#include "control.h"
#include "config.h"

static uint32_t s_last_water_ms = 0;   // millis() of the last automatic watering decision
static bool s_watered_once = false;    // false = no cooldown active (boot or manual reset)

void control_init() {
  s_watered_once = false;
}

// Seconds left in the post-watering soak period (rounded up), 0 if not cooling down.
uint32_t control_cooldown_remaining_s() {
  if (!s_watered_once) return 0;
  uint32_t cooldown_ms = g_cfg.pump_cooldown_s * 1000UL;
  uint32_t elapsed = millis() - s_last_water_ms;
  if (elapsed >= cooldown_ms) return 0;
  return (cooldown_ms - elapsed + 999UL) / 1000UL;
}

void control_reset_cooldown() {
  s_watered_once = false;
}

// Decision order: auto toggle -> pump busy -> too wet -> cooldown -> dry/wilted -> ok.
ControlDecision control_decide(const ControlInputs& in) {
  ControlDecision d = {false, 0, "ok"};

  if (!g_cfg.auto_water_enabled) {
    d.reason = "auto_disabled";
    return d;
  }

  // Stale or missing camera data -> moisture-only.
  bool camera_fresh = in.health_valid && in.health_age_ms <= HEALTH_MAX_AGE_MS;
  bool wilted = camera_fresh && in.health_state == PLANT_WILTED && in.health_conf >= g_cfg.wilt_conf_min;

  if (in.pump_on) {
    d.reason = "pump_running";
    return d;
  }
  if (in.moisture_pct >= g_cfg.moisture_high_pct) {
    d.reason = "wet";
    return d;
  }

  uint32_t cooldown_ms = g_cfg.pump_cooldown_s * 1000UL;
  if (s_watered_once && millis() - s_last_water_ms < cooldown_ms) {
    d.reason = "cooldown";
    return d;
  }

  if (in.moisture_pct < g_cfg.moisture_low_pct) {
    d.water = true;
    d.reason = wilted ? "dry+wilted" : "dry";
  } else if (wilted) {
    d.water = true;
    d.reason = "borderline+wilted";
  } else {
    d.reason = camera_fresh ? "ok" : "ok_moisture_only";
  }

  if (d.water) {
    // Dose duration from calibrated flow rate (sanitised > 0); the pump driver applies the run cap.
    d.duration_ms = (uint32_t)((g_cfg.water_dose_ml / g_cfg.ml_per_sec) * 1000.0f + 0.5f);
    s_last_water_ms = millis();
    s_watered_once = true;
  }
  return d;
}
