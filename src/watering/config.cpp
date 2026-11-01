// config.cpp
// Purpose: Load and persist watering node configuration in ESP32 NVS (Preferences).
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#include "config.h"
#include <Preferences.h>

WateringConfig g_cfg;
static Preferences s_prefs;
static const char* kPrefsNamespace = "watering";
// NVS keys are limited to 15 characters, hence the abbreviated names below.

void config_reset_defaults() {
  g_cfg.auto_water_enabled   = false; // Disabled by default for safe bench/initial setup
  g_cfg.mqtt_enabled         = false; // Disabled by default to avoid reconnect spam until configured

  g_cfg.raw_dry              = MOISTURE_RAW_DRY;
  g_cfg.raw_wet              = MOISTURE_RAW_WET;

  g_cfg.ml_per_sec           = PUMP_ML_PER_SEC;
  g_cfg.water_dose_ml        = (WATER_DOSE_MS / 1000.0f) * PUMP_ML_PER_SEC; // 25.0 mL
  g_cfg.pump_max_run_ms      = PUMP_MAX_RUN_MS;
  g_cfg.pump_cooldown_s      = PUMP_COOLDOWN_MS / 1000UL;

  g_cfg.moisture_low_pct     = MOISTURE_LOW_PCT;
  g_cfg.moisture_high_pct    = MOISTURE_HIGH_PCT;
  g_cfg.wilt_conf_min        = WILT_CONF_MIN;
  g_cfg.sense_interval_s     = CONTROL_INTERVAL_MS / 1000UL;
  g_cfg.telemetry_interval_s = TELEMETRY_INTERVAL_MS / 1000UL;

  snprintf(g_cfg.mqtt_host, sizeof(g_cfg.mqtt_host), "%s", MQTT_HOST);
  g_cfg.mqtt_port = MQTT_PORT;
  snprintf(g_cfg.mqtt_user, sizeof(g_cfg.mqtt_user), "%s", MQTT_USER);
  snprintf(g_cfg.mqtt_pass, sizeof(g_cfg.mqtt_pass), "%s", MQTT_PASS);
  snprintf(g_cfg.mqtt_client_id, sizeof(g_cfg.mqtt_client_id), "%s", MQTT_CLIENT_ID);
}

void config_load() {
  config_reset_defaults();

  // Read-only open fails on first boot (namespace not yet created); defaults then stand.
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    Serial.println("[config] NVS namespace not initialised yet; using defaults.");
    return;
  }

  g_cfg.auto_water_enabled   = s_prefs.getBool("auto_water", g_cfg.auto_water_enabled);
  g_cfg.mqtt_enabled         = s_prefs.getBool("mqtt_en", g_cfg.mqtt_enabled);

  g_cfg.raw_dry              = s_prefs.getInt("raw_dry", g_cfg.raw_dry);
  g_cfg.raw_wet              = s_prefs.getInt("raw_wet", g_cfg.raw_wet);

  g_cfg.ml_per_sec           = s_prefs.getFloat("ml_per_s", g_cfg.ml_per_sec);
  g_cfg.water_dose_ml        = s_prefs.getFloat("dose_ml", g_cfg.water_dose_ml);
  g_cfg.pump_max_run_ms      = s_prefs.getUInt("max_run_ms", g_cfg.pump_max_run_ms);
  g_cfg.pump_cooldown_s      = s_prefs.getUInt("cooldown_s", g_cfg.pump_cooldown_s);

  g_cfg.moisture_low_pct     = s_prefs.getFloat("m_low_pct", g_cfg.moisture_low_pct);
  g_cfg.moisture_high_pct    = s_prefs.getFloat("m_high_pct", g_cfg.moisture_high_pct);
  g_cfg.wilt_conf_min        = s_prefs.getFloat("wilt_conf", g_cfg.wilt_conf_min);
  g_cfg.sense_interval_s     = s_prefs.getUInt("sense_int_s", g_cfg.sense_interval_s);
  g_cfg.telemetry_interval_s = s_prefs.getUInt("telem_int_s", g_cfg.telemetry_interval_s);

  String host = s_prefs.getString("mqtt_host", g_cfg.mqtt_host);
  snprintf(g_cfg.mqtt_host, sizeof(g_cfg.mqtt_host), "%s", host.c_str());
  g_cfg.mqtt_port = s_prefs.getUShort("mqtt_port", g_cfg.mqtt_port);

  String user = s_prefs.getString("mqtt_user", g_cfg.mqtt_user);
  snprintf(g_cfg.mqtt_user, sizeof(g_cfg.mqtt_user), "%s", user.c_str());

  String pass = s_prefs.getString("mqtt_pass", g_cfg.mqtt_pass);
  snprintf(g_cfg.mqtt_pass, sizeof(g_cfg.mqtt_pass), "%s", pass.c_str());

  String cid = s_prefs.getString("mqtt_cid", g_cfg.mqtt_client_id);
  snprintf(g_cfg.mqtt_client_id, sizeof(g_cfg.mqtt_client_id), "%s", cid.c_str());

  s_prefs.end();
  config_sanitise();
  Serial.println("[config] Loaded configuration from NVS.");
}

void config_sanitise() {
  // ADC is 12-bit; equal dry/wet points would make the % mapping undefined.
  g_cfg.raw_dry = constrain(g_cfg.raw_dry, 0, 4095);
  g_cfg.raw_wet = constrain(g_cfg.raw_wet, 0, 4095);
  if (g_cfg.raw_dry == g_cfg.raw_wet) {
    g_cfg.raw_dry = MOISTURE_RAW_DRY;
    g_cfg.raw_wet = MOISTURE_RAW_WET;
  }

  if (!(g_cfg.ml_per_sec > 0.05f))    g_cfg.ml_per_sec = PUMP_ML_PER_SEC;  // also rejects NaN
  if (!(g_cfg.water_dose_ml > 0.0f))  g_cfg.water_dose_ml = (WATER_DOSE_MS / 1000.0f) * PUMP_ML_PER_SEC;
  if (g_cfg.pump_max_run_ms == 0)     g_cfg.pump_max_run_ms = PUMP_MAX_RUN_MS;
  g_cfg.pump_max_run_ms = min(g_cfg.pump_max_run_ms, (uint32_t)PUMP_MANUAL_MAX_RUN_MS);

  // Upper bounds keep the "* 1000" second-to-millisecond conversions from overflowing uint32_t.
  g_cfg.pump_cooldown_s      = min(g_cfg.pump_cooldown_s, (uint32_t)86400);
  g_cfg.sense_interval_s     = constrain(g_cfg.sense_interval_s, (uint32_t)5, (uint32_t)86400);
  g_cfg.telemetry_interval_s = constrain(g_cfg.telemetry_interval_s, (uint32_t)2, (uint32_t)86400);

  g_cfg.moisture_low_pct  = constrain(g_cfg.moisture_low_pct, 0.0f, 100.0f);
  g_cfg.moisture_high_pct = constrain(g_cfg.moisture_high_pct, 0.0f, 100.0f);
  g_cfg.wilt_conf_min     = constrain(g_cfg.wilt_conf_min, 0.0f, 1.0f);

  if (g_cfg.mqtt_port == 0) g_cfg.mqtt_port = MQTT_PORT;
  if (g_cfg.mqtt_client_id[0] == '\0') {
    snprintf(g_cfg.mqtt_client_id, sizeof(g_cfg.mqtt_client_id), "%s", MQTT_CLIENT_ID);
  }
}

void config_save() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    Serial.println("[config] ERROR: Failed to open NVS for writing.");
    return;
  }

  s_prefs.putBool("auto_water", g_cfg.auto_water_enabled);
  s_prefs.putBool("mqtt_en", g_cfg.mqtt_enabled);

  s_prefs.putInt("raw_dry", g_cfg.raw_dry);
  s_prefs.putInt("raw_wet", g_cfg.raw_wet);

  s_prefs.putFloat("ml_per_s", g_cfg.ml_per_sec);
  s_prefs.putFloat("dose_ml", g_cfg.water_dose_ml);
  s_prefs.putUInt("max_run_ms", g_cfg.pump_max_run_ms);
  s_prefs.putUInt("cooldown_s", g_cfg.pump_cooldown_s);

  s_prefs.putFloat("m_low_pct", g_cfg.moisture_low_pct);
  s_prefs.putFloat("m_high_pct", g_cfg.moisture_high_pct);
  s_prefs.putFloat("wilt_conf", g_cfg.wilt_conf_min);
  s_prefs.putUInt("sense_int_s", g_cfg.sense_interval_s);
  s_prefs.putUInt("telem_int_s", g_cfg.telemetry_interval_s);

  s_prefs.putString("mqtt_host", g_cfg.mqtt_host);
  s_prefs.putUShort("mqtt_port", g_cfg.mqtt_port);
  s_prefs.putString("mqtt_user", g_cfg.mqtt_user);
  s_prefs.putString("mqtt_pass", g_cfg.mqtt_pass);
  s_prefs.putString("mqtt_cid", g_cfg.mqtt_client_id);

  s_prefs.end();
  Serial.println("[config] Saved configuration to NVS.");
}
