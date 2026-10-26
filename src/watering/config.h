// config.h
// Purpose: Compile-time configuration for the watering node (pins, calibration, thresholds).
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#pragma once
#include <stdint.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found, using secrets.example.h (copy it to include/secrets.h and fill in)"
#include "secrets.example.h"
#endif

// ---------- Pins ----------
#define MOISTURE_PIN      34    // TODO(Caleb): verify. Must be ADC1; ADC2 can't be used with Wi-Fi on.
#define PUMP_PIN          25    // TODO(Caleb): verify
#define PUMP_ACTIVE_HIGH  true  // TODO(Caleb): verify against relay/MOSFET driver

// ---------- Moisture calibration (capacitive: drier = higher raw) ----------
#define MOISTURE_RAW_DRY  3000  // TODO(Caleb): verify, raw reading in air / bone-dry soil
#define MOISTURE_RAW_WET  1300  // TODO(Caleb): verify, raw reading in water / saturated soil
#define MOISTURE_SAMPLES  16    // ADC samples averaged per read
#define MOISTURE_FILTER_N 8     // moving-average window (reads)

// ---------- Watering thresholds ----------
#define MOISTURE_LOW_PCT  30.0f    // TODO(Caleb): verify, below = dry
#define MOISTURE_HIGH_PCT 55.0f    // TODO(Caleb): verify, above = wet, never water
#define WILT_CONF_MIN     0.7f     // TODO(Caleb): verify
#define HEALTH_MAX_AGE_MS 300000UL // TODO(Caleb): verify, older camera data is ignored

// ---------- Pump ----------
#define PUMP_MAX_RUN_MS   5000UL   // TODO(Caleb): verify, hard cap per cycle
#define PUMP_COOLDOWN_MS  300000UL // TODO(Caleb): verify, soak time between cycles
#define WATER_DOSE_MS     4000UL   // TODO(Caleb): verify, run time per watering
#define PUMP_ML_PER_SEC   6.25f    // TODO(Caleb): verify, placeholder until calibrated

// ---------- Timing ----------
#define CONTROL_INTERVAL_MS   1000UL
#define TELEMETRY_INTERVAL_MS 10000UL // TODO(Caleb): verify
