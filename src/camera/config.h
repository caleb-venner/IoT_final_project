// Compile-time configuration and pin assignments for the Freenove ESP32-WROVER camera node.

#pragma once
#include <stdint.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found, using secrets.example.h (copy it to include/secrets.h and fill in)"
#include "secrets.example.h"
#endif

// ---------- Timing ----------
#define INFERENCE_INTERVAL_MS 60000UL // Periodic capture & classification interval (60 s)
#define WARMUP_FRAMES         2       // Frames discarded before capture so AEC/AWB settle

// ---------- Local Wi-Fi / mDNS hostnames ----------
#define CAM_MDNS_HOSTNAME      "basil-cam"
#define WATERING_MDNS_HOSTNAME "basil-water"

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
