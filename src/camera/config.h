// config.h
// Purpose: Compile-time configuration for the camera node (pins, timing, ESP-NOW).
// OWNER:   Caleb
// Node:    camera node (Freenove ESP32-WROVER CAM)

#pragma once
#include <stdint.h>

// Wi-Fi SSID is only used to find the router's channel (the camera node never connects).
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found, using secrets.example.h (copy it to include/secrets.h and fill in)"
#include "secrets.example.h"
#endif

// ---------- Timing ----------
#define INFERENCE_INTERVAL_MS 60000UL
#define WARMUP_FRAMES         2

// ---------- ESP-NOW (OWNER: Caleb) ----------
// Used if the SSID scan fails. Must match the router the watering node is on.
#define ESPNOW_CHANNEL 1
// Destination MAC. Broadcast by default; set to the watering node's STA MAC to unicast.
#define ESPNOW_PEER_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

// ---------- Camera pins (CAMERA_MODEL_WROVER_KIT map) ----------
#define CAM_PIN_PWDN  -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK  21
#define CAM_PIN_SIOD  26
#define CAM_PIN_SIOC  27
#define CAM_PIN_Y9    35
#define CAM_PIN_Y8    34
#define CAM_PIN_Y7    39
#define CAM_PIN_Y6    36
#define CAM_PIN_Y5    19
#define CAM_PIN_Y4    18
#define CAM_PIN_Y3     5
#define CAM_PIN_Y2     4
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF  23
#define CAM_PIN_PCLK  22
