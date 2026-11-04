// Pulsed-power soil moisture sensing (15 ms excitation on GPIO 26, 16x ADC oversampling
// on GPIO 34) with runtime dry/wet calibration and moving-average filtering.

#include <Arduino.h>
#include "moisture.h"
#include "config.h"

// The filter stores raw ADC values (not %), so a calibration change is applied to the
// whole window on the next recompute without discarding history.
static int s_raw_window[MOISTURE_FILTER_N];
static int s_count = 0;            // valid entries in the window (saturates at MOISTURE_FILTER_N)
static int s_index = 0;            // next write position (ring buffer)
static int s_last_raw = 0;
static float s_last_pct = 0.0f;
static uint32_t s_last_read_ms = 0;

// Maps a raw reading to 0-100 % using the runtime dry/wet calibration points.
static float raw_to_pct(int raw) {
  int dry = g_cfg.raw_dry;
  int wet = g_cfg.raw_wet;
  if (wet == dry) return 0.0f;  // guarded by config_sanitise(); kept to avoid divide-by-zero

  // Single formula covers both resistive (wet > dry) and capacitive (dry > wet) sensors.
  float pct = 100.0f * (float)(raw - dry) / (float)(wet - dry);
  return constrain(pct, 0.0f, 100.0f);
}

// Moving average of the calibrated % across the window.
static void recompute_pct() {
  if (s_count == 0) {
    s_last_pct = 0.0f;
    return;
  }
  float sum = 0.0f;
  for (int i = 0; i < s_count; i++) {
    sum += raw_to_pct(s_raw_window[i]);
  }
  s_last_pct = sum / (float)s_count;
}

void moisture_init() {
  pinMode(MOISTURE_PWR_PIN, OUTPUT);
  digitalWrite(MOISTURE_PWR_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(MOISTURE_PIN, ADC_11db);
}

int moisture_read_raw() {
  // Power the sensor briefly (15 ms) to prevent trace electrolysis
  digitalWrite(MOISTURE_PWR_PIN, HIGH);
  delay(15);

  uint32_t sum = 0;
  for (int i = 0; i < MOISTURE_SAMPLES; i++) {
    sum += analogRead(MOISTURE_PIN);
    delayMicroseconds(100);
  }

  digitalWrite(MOISTURE_PWR_PIN, LOW);
  s_last_raw = (int)(sum / MOISTURE_SAMPLES);
  s_last_read_ms = millis();
  return s_last_raw;
}

float moisture_read_pct() {
  int raw = moisture_read_raw();
  s_raw_window[s_index] = raw;
  s_index = (s_index + 1) % MOISTURE_FILTER_N;
  if (s_count < MOISTURE_FILTER_N) s_count++;

  recompute_pct();
  return s_last_pct;
}

int moisture_last_raw() {
  return s_last_raw;
}

float moisture_last_pct() {
  recompute_pct();  // picks up any calibration change since the last physical read
  return s_last_pct;
}

uint32_t moisture_last_read_ms() {
  return s_last_read_ms;
}
