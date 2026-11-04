// Compile-time defaults and runtime NVS-backed configuration for the watering node.

#pragma once
#include <Arduino.h>
#include <stdint.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found, using secrets.example.h (copy it to include/secrets.h and fill in)"
#include "secrets.example.h"
#endif

// ---------- mDNS Hostname ----------
// Accessible at http://basil-water.local/
#define MDNS_HOSTNAME     "basil-water"

// ---------- Pins ----------
#define MOISTURE_PIN      34    // ADC1_CH6 (input-only; functions concurrently with Wi-Fi)
#define MOISTURE_PWR_PIN  26    // Switched output pin: powered briefly (15 ms) during reads to prevent electrolysis
#define PUMP_PIN          25    // Connected to ULN2003A inputs (pins 1, 2, 3, 4)
#define PUMP_ACTIVE_HIGH  true  // ULN2003A is active-HIGH (HIGH turns pump on)

// ---------- Default Moisture Calibration ----------
// Resistive water level module: dry air = low raw (0-500), submerged/wet = high raw (1500-3500).
#define MOISTURE_RAW_DRY  200   // Default raw reading in dry soil / air
#define MOISTURE_RAW_WET  2800  // Default raw reading in saturated soil / water
#define MOISTURE_SAMPLES  16    // ADC samples averaged per read
#define MOISTURE_FILTER_N 8     // Moving-average window (reads)

// ---------- Default Watering Thresholds ----------
#define MOISTURE_LOW_PCT  30.0f    // Below = dry, trigger watering
#define MOISTURE_HIGH_PCT 55.0f    // Above = wet, never water
#define WILT_CONF_MIN     0.7f     // Minimum ML confidence to act on wilt
#define HEALTH_MAX_AGE_MS 300000UL // Ignore camera health messages older than 5 minutes

// ---------- Default Pump Parameters ----------
#define PUMP_MAX_RUN_MS        10000UL  // Max run duration for automatic watering cycles
#define PUMP_MANUAL_MAX_RUN_MS 60000UL  // Max safety cap for manual WebUI runs (60 s)
#define PUMP_COOLDOWN_MS       300000UL // Default soak time between auto-watering cycles (300 s)
#define WATER_DOSE_MS          4000UL   // Default run time per auto-watering cycle (25 mL @ 6.25 mL/s)
#define PUMP_ML_PER_SEC        6.25f    // Default flow rate (mL/s)

// ---------- Default Timing ----------
#define CONTROL_INTERVAL_MS    30000UL  // Default auto-sense & control interval (30 s)
#define TELEMETRY_INTERVAL_MS  60000UL  // Default MQTT telemetry interval (60 s, configurable in WebUI)

// ---------- Runtime Configuration (stored in ESP32 NVS Preferences) ----------
struct WateringConfig {
  // Mode toggles
  bool     auto_water_enabled;
  bool     mqtt_enabled;

  // Sensor calibration
  int      raw_dry;
  int      raw_wet;

  // Pump calibration & limits
  float    ml_per_sec;
  float    water_dose_ml;
  uint32_t pump_max_run_ms;
  uint32_t pump_cooldown_s;

  // Control thresholds & timing
  float    moisture_low_pct;
  float    moisture_high_pct;
  float    wilt_conf_min;
  uint32_t sense_interval_s;
  uint32_t telemetry_interval_s;

  // MQTT settings
  char     mqtt_host[64];
  uint16_t mqtt_port;
  char     mqtt_user[48];
  char     mqtt_pass[64];
  char     mqtt_client_id[32];
  char     mqtt_topic_telemetry[64];
  char     mqtt_topic_events[64];
  char     mqtt_topic_status[64];
};

extern WateringConfig g_cfg;

void config_load();            // defaults, then NVS overrides, then sanitise
void config_save();            // persists every field of g_cfg to NVS
void config_reset_defaults();  // resets g_cfg to compile-time defaults (does not save)
void config_sanitise();        // clamps g_cfg into safe ranges (call after any external edit)
