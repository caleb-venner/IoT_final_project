// main.cpp
// Purpose: Camera node entry point. STUB.
// OWNER:   Aida (Caleb provides espnow_tx_*)
// Node:    camera node (Freenove ESP32-WROVER CAM)

#include <Arduino.h>
#include "config.h"
#include "camera.h"
#include "inference.h"
#include "espnow_tx.h"

void setup() {
  Serial.begin(115200);
  // TODO(Aida): espnow_tx_init(WIFI_SSID), camera_init(), inference_init().
  // TODO(Aida): FreeRTOS task pinned to core 1 that runs, every INFERENCE_INTERVAL_MS:
  //             camera_capture() -> inference_run() -> camera_release() -> espnow_tx_send().
  //             Under MOCK_HEALTH, skip the camera and send alternating healthy/wilted results.
}

void loop() {
}
