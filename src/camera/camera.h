// Camera sensor initialisation and frame capture wrapper around esp_camera.

#pragma once
#include "esp_camera.h"

bool camera_init();
camera_fb_t* camera_capture();      // returns nullptr on failure
void camera_release(camera_fb_t* fb);
