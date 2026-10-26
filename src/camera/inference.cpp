// inference.cpp
// Purpose: On-device plant health classifier. STUB: always returns PLANT_UNKNOWN.
// OWNER:   Aida
// Node:    camera node (Freenove ESP32-WROVER CAM)

#include <Arduino.h>
#include "inference.h"

bool inference_init() {
  // TODO(Aida): model / buffer setup.
  return true;
}

HealthResult inference_run(camera_fb_t* fb) {
  (void)fb;
  // TODO(Aida): Edge Impulse run_classifier() goes here (library export goes in lib/).
  // TODO(Aida): colour-heuristic fallback (share of yellow/brown pixels) if the model isn't ready.
  return {PLANT_UNKNOWN, 0.0f, 0};
}
