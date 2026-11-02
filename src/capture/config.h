// config.h
// Purpose: Configuration for SD + HTTP server training data collection.
// OWNER:   Caleb
// Node:    Freenove ESP32-WROVER CAM

#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found, using secrets.example.h (copy to include/secrets.h and fill in WIFI_SSID / WIFI_PASS)"
#include "secrets.example.h"
#endif

// mDNS hostname -> accessible at http://basil-cam.local/
#define MDNS_HOSTNAME "basil-cam"

// Capture interval: 5 minutes (300,000 ms)
#define CAPTURE_INTERVAL_MS 300000UL

// SD_MMC in 1-bit mode uses GPIO 2 (D0), 14 (CLK), 15 (CMD), which do not clash
// with the camera pins below (4-bit mode requires GPIO 4, which is camera Y2).
#define SD_ONE_BIT_MODE true

// Number of dummy frames to discard before saving so auto-exposure/white-balance settles.
#define WARMUP_FRAMES 2

// CAMERA_MODEL_WROVER_KIT pin map (Freenove ESP32-WROVER CAM)
#define WROVER_CAM_PIN_PWDN  -1
#define WROVER_CAM_PIN_RESET -1
#define WROVER_CAM_PIN_XCLK  21
#define WROVER_CAM_PIN_SIOD  26
#define WROVER_CAM_PIN_SIOC  27
#define WROVER_CAM_PIN_Y9    35
#define WROVER_CAM_PIN_Y8    34
#define WROVER_CAM_PIN_Y7    39
#define WROVER_CAM_PIN_Y6    36
#define WROVER_CAM_PIN_Y5    19
#define WROVER_CAM_PIN_Y4    18
#define WROVER_CAM_PIN_Y3     5
#define WROVER_CAM_PIN_Y2     4
#define WROVER_CAM_PIN_VSYNC 25
#define WROVER_CAM_PIN_HREF  23
#define WROVER_CAM_PIN_PCLK  22
