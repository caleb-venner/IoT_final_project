// Wi-Fi + MQTT client interface for publishing JSON telemetry, pump events, and node status.

#pragma once
#include <stdint.h>

struct Telemetry {
  float       moisture_pct;
  int         moisture_raw;
  bool        pump_on;
  const char* health_state;   // "healthy" / "wilted" / "unknown"
  float       health_conf;
  int32_t     health_age_s;   // -1 if no camera message yet
  float       water_ml_total;
};

// Connects Wi-Fi (blocking, with timeout) and initialises MQTT if g_cfg.mqtt_enabled is true.
void mqtt_init();
// Re-applies runtime MQTT settings from g_cfg (host, port, credentials, enabled state).
void mqtt_apply_config();
// Keeps MQTT alive with throttled reconnects (no-op if g_cfg.mqtt_enabled is false).
// Pass allow_reconnect = false to skip the blocking connect attempt (e.g. while the pump runs).
void mqtt_loop(bool allow_reconnect);
bool mqtt_connected();
int  mqtt_last_rc();

void mqtt_publish_telemetry(const Telemetry& t);
void mqtt_publish_event(const char* reason, uint32_t duration_ms, float ml);
