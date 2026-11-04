// Pulsed-power resistive soil moisture sensor driver with runtime calibration and smoothing.

#pragma once
#include <stdint.h>

void moisture_init();
int moisture_read_raw();          // performs a pulsed ADC reading and returns raw value
float moisture_read_pct();        // performs a pulsed reading, updates filter, and returns 0-100%

// Cached getters & calibration helpers for WebUI / telemetry
int moisture_last_raw();          // returns most recent raw ADC reading without pulsing sensor
float moisture_last_pct();        // filtered % recomputed with current calibration, without pulsing sensor
uint32_t moisture_last_read_ms(); // millis() timestamp of last physical reading (0 if never read)
