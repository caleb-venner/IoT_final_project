// mqtt_pub.h
// Purpose: Wi-Fi + MQTT connection and JSON publishing of telemetry and pump events.
// OWNER:   Caleb (publish code), Aida (topic/payload design)
// Node:    watering node (ESP32 DevKitC)

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

// Connects Wi-Fi (blocking, with timeout) then MQTT. Call in setup().
void mqtt_init();
// Keeps Wi-Fi and MQTT alive with throttled reconnects. Call every loop.
void mqtt_loop();
bool mqtt_connected();

void mqtt_publish_telemetry(const Telemetry& t);
void mqtt_publish_event(const char* reason, uint32_t duration_ms, float ml);
