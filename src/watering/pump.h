// pump.h
// Purpose: Non-blocking peristaltic pump driver with run cap and delivered-volume tracking.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#pragma once
#include <stdint.h>

void pump_init();
void pump_run_ms(uint32_t ms);  // clamped to PUMP_MAX_RUN_MS; ignored if already running
bool pump_is_on();
void pump_update();             // call every loop
float pump_total_ml();          // cumulative mL delivered since boot
