// camera.h
// Purpose: OV2640 init and frame capture via esp_camera.
// OWNER:   Aida
// Node:    camera node (Freenove ESP32-WROVER CAM)

#pragma once
#include "esp_camera.h"

bool camera_init();
camera_fb_t* camera_capture();      // returns nullptr on failure
void camera_release(camera_fb_t* fb);
