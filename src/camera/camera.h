// camera.h
// Purpose: Camera sensor init and frame capture via esp_camera.
// OWNER:   Caleb
// Node:    camera node (Freenove ESP32-WROVER CAM)

#pragma once
#include "esp_camera.h"

bool camera_init();
camera_fb_t* camera_capture();      // returns nullptr on failure
void camera_release(camera_fb_t* fb);
