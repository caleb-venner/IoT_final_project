// pump.h
// Purpose: Non-blocking peristaltic pump driver with mL dosing, run cap, and delivered-volume tracking.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#pragma once
#include <stdint.h>

void pump_init();
// Starts the pump and returns the actual (capped) run time in ms, or 0 if it did not start.
// Manual runs are capped at PUMP_MANUAL_MAX_RUN_MS and may restart an active run;
// automatic runs are capped at g_cfg.pump_max_run_ms and are ignored while running.
uint32_t pump_run_ms(uint32_t ms, bool manual);
uint32_t pump_run_ml(float ml, bool manual);  // converts mL to ms using g_cfg.ml_per_sec
void pump_stop();
bool pump_is_on();
uint32_t pump_remaining_ms();
void pump_update();             // call every loop; switches off when the run time elapses
float pump_total_ml();          // estimated cumulative mL delivered since boot (or last reset)
void pump_reset_total_ml();
