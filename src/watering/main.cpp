// main.cpp
// Purpose: Watering node entry point. STUB.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#include <Arduino.h>
#include "config.h"
#include "moisture.h"
#include "pump.h"
#include "control.h"
#include "espnow_rx.h"
#include "mqtt_pub.h"

void setup() {
  Serial.begin(115200);
  // TODO(Caleb): moisture_init(), pump_init(), control_init(),
  //              mqtt_init() (Wi-Fi first, so ESP-NOW is on the router's channel), espnow_rx_init().
}

void loop() {
  // TODO(Caleb): non-blocking loop: mqtt_loop(), pump_update();
  //              every CONTROL_INTERVAL_MS: read moisture, espnow_rx_latest(), control_decide(),
  //              run pump + mqtt_publish_event() on water;
  //              every TELEMETRY_INTERVAL_MS: mqtt_publish_telemetry().
  //              Under MOCK_SENSORS: slowly drying moisture curve that rises after each pump run.
}
