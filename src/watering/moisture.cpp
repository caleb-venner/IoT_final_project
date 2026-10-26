// moisture.cpp
// Purpose: Capacitive soil moisture sensing with calibration and smoothing. STUB.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#include <Arduino.h>
#include "moisture.h"
#include "config.h"

void moisture_init() {
  // TODO(Caleb): ADC resolution / attenuation for MOISTURE_PIN.
}

int moisture_read_raw() {
  // TODO(Caleb): average MOISTURE_SAMPLES readings.
  return 0;
}

float moisture_read_pct() {
  // TODO(Caleb): map raw -> % using MOISTURE_RAW_DRY / MOISTURE_RAW_WET, clamp 0-100,
  //              moving-average filter over MOISTURE_FILTER_N reads.
  return 0.0f;
}
