// espnow_tx.h
// Purpose: ESP-NOW sender for PlantHealthMsg (camera node -> watering node).
// OWNER:   Caleb
// Node:    camera node (Freenove ESP32-WROVER CAM)

#pragma once
#include "inference.h"

// Scans for ssid_for_channel to match the router's channel (no Wi-Fi connection is made),
// falling back to ESPNOW_CHANNEL, then initialises ESP-NOW and registers the peer.
bool espnow_tx_init(const char* ssid_for_channel);
bool espnow_tx_send(const HealthResult& r);
