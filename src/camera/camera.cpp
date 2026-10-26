// camera.cpp
// Purpose: OV2640 init and frame capture via esp_camera. STUB.
// OWNER:   Aida
// Node:    camera node (Freenove ESP32-WROVER CAM)

#include <Arduino.h>
#include "camera.h"
#include "config.h"

bool camera_init() {
  // TODO(Aida): esp_camera_init() with the CAM_PIN_* map from config.h,
  //             a 96x96-friendly frame size and the frame buffer in PSRAM.
  return false;
}

camera_fb_t* camera_capture() {
  // TODO(Aida): esp_camera_fb_get()
  return nullptr;
}

void camera_release(camera_fb_t* fb) {
  // TODO(Aida): esp_camera_fb_return(fb)
  (void)fb;
}
