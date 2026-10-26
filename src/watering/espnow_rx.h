// espnow_rx.h
// Purpose: ESP-NOW receiver for PlantHealthMsg from the camera node.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#pragma once
#include <stdint.h>
#include "plant_msgs.h"

// Call after Wi-Fi has connected so ESP-NOW shares the router's channel.
bool espnow_rx_init();
// Copies the latest valid message and its age. Returns false if nothing received yet.
bool espnow_rx_latest(PlantHealthMsg* out, uint32_t* age_ms);
