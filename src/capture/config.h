// config.h
// Purpose: Configuration for the capture-to-SD data collection tool.
// OWNER:   Aida
// Node:    camera board (Freenove ESP32-WROVER CAM)

#pragma once

#define CAPTURE_INTERVAL_MS 300000UL  // 5 min

// SD_MMC in 1-bit mode uses GPIO 2 (D0), 14 (CLK), 15 (CMD), which don't clash with the
// camera pins below. 4-bit mode would need GPIO 4, which is camera Y2 on this board.
// TODO(Aida): check whether your board has an SD slot or you need an external breakout.
#define SD_ONE_BIT_MODE true

// CAMERA_MODEL_WROVER_KIT pin map. TODO(Aida): verify against Freenove's tutorial.
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
