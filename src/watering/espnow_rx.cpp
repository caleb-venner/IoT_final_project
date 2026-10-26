// espnow_rx.cpp
// Purpose: ESP-NOW receiver for PlantHealthMsg from the camera node. STUB.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#include <Arduino.h>
#include "espnow_rx.h"

bool espnow_rx_init() {
  // TODO(Caleb): esp_now_init() after Wi-Fi connects, register the receive callback.
  //              In the callback: check size + version, store msg and receive time
  //              (protect it, the callback runs in the Wi-Fi task).
  return false;
}

bool espnow_rx_latest(PlantHealthMsg* out, uint32_t* age_ms) {
  // TODO(Caleb): copy the latest message and its age.
  (void)out;
  (void)age_ms;
  return false;
}
