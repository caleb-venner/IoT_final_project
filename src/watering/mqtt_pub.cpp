// mqtt_pub.cpp
// Purpose: Wi-Fi + MQTT connection and JSON publishing of telemetry and pump events. STUB.
// OWNER:   Caleb (publish code), Aida (topic/payload design)
// Node:    watering node (ESP32 DevKitC)

// TODO: topic names and payload fields must match Aida's Telegraf config
//       (cloud/telegraf/telegraf.conf). Change them in include/plant_msgs.h / here together.

#include <Arduino.h>
#include "mqtt_pub.h"
#include "config.h"
#include "plant_msgs.h"

void mqtt_init() {
  // TODO(Caleb): connect Wi-Fi (WIFI_SSID/WIFI_PASS), set up PubSubClient with
  //              last will "offline" (retained) on MQTT_TOPIC_STATUS, publish "online" on connect.
}

void mqtt_loop() {
  // TODO(Caleb): non-blocking reconnect + client loop.
}

bool mqtt_connected() {
  // TODO(Caleb)
  return false;
}

void mqtt_publish_telemetry(const Telemetry& t) {
  // TODO(Caleb): ArduinoJson payload to MQTT_TOPIC_TELEMETRY (see README "MQTT topics").
  (void)t;
}

void mqtt_publish_event(const char* reason, uint32_t duration_ms, float ml) {
  // TODO(Caleb): ArduinoJson payload to MQTT_TOPIC_EVENTS.
  (void)reason;
  (void)duration_ms;
  (void)ml;
}
