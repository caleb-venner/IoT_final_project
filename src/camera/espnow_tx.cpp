// espnow_tx.cpp
// Purpose: ESP-NOW sender for PlantHealthMsg. STUB.
// OWNER:   Caleb
// Node:    camera node (Freenove ESP32-WROVER CAM)

#include <Arduino.h>
#include "espnow_tx.h"
#include "config.h"
#include "plant_msgs.h"

bool espnow_tx_init(const char* ssid_for_channel) {
  // TODO(Caleb): scan for ssid_for_channel to get the router's channel (fall back to
  //              ESPNOW_CHANNEL), set it with esp_wifi_set_channel(), esp_now_init(),
  //              add ESPNOW_PEER_MAC as a peer. Don't connect to Wi-Fi.
  (void)ssid_for_channel;
  return false;
}

bool espnow_tx_send(const HealthResult& r) {
  // TODO(Caleb): fill PlantHealthMsg (version, incrementing seq), esp_now_send(), log to Serial.
  (void)r;
  return false;
}
