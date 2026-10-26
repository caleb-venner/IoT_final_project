// inference.h
// Purpose: On-device plant health classifier interface (healthy / wilted).
// OWNER:   Aida
// Node:    camera node (Freenove ESP32-WROVER CAM)

#pragma once
#include <stdint.h>
#include "esp_camera.h"
#include "plant_msgs.h"

struct HealthResult {
  PlantState state;
  float      confidence;
  uint32_t   inference_ms;
};

bool inference_init();
HealthResult inference_run(camera_fb_t* fb);  // TODO(Aida): Edge Impulse classifier
