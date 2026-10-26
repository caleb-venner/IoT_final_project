// moisture.h
// Purpose: Capacitive soil moisture sensing with calibration and smoothing.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#pragma once

void moisture_init();
int moisture_read_raw();    // averaged raw ADC reading
float moisture_read_pct();  // calibrated, filtered, clamped 0-100
