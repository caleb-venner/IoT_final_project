// espnow_rx.h
// Purpose: ESP-NOW receiver for PlantHealthMsg from the camera node.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#pragma once
#include <stdint.h>
#include "plant_msgs.h"

// Call after Wi-Fi has connected so ESP-NOW shares the router's channel.
bool espnow_rx_init();
// Call every loop (self-throttled to 5 s). Logs when the Wi-Fi channel changes or Wi-Fi connects
// late. No re-init is needed: the STA radio follows the router and ESP-NOW receive needs no peer,
// but the camera node must move to the new channel to be heard.
void espnow_rx_check_channel();
// Copies the latest valid message and its age. Returns false if nothing received yet.
bool espnow_rx_latest(PlantHealthMsg* out, uint32_t* age_ms);
