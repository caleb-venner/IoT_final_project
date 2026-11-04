// On-device plant health classifier interface (healthy vs wilted).

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
HealthResult inference_run(camera_fb_t* fb);
