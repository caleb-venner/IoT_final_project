// Local Wi-Fi HTTP client and UDP discovery interface for sending inference results
// and 320x240 JPEG frames from the camera node to the watering node (POST /health).

#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "inference.h"

// Connects to Wi-Fi (WIFI_SSID / WIFI_PASS), starts mDNS (basil-cam.local), and opens UDP discovery.
bool http_tx_init(const char* ssid);

// Polls UDP port 4210 for remote capture triggers ("BASIL_CAPTURE") or discovery responses.
void http_tx_poll();

// Registers the inference task handle so incoming remote capture triggers wake it immediately.
void http_tx_set_notify_task(TaskHandle_t task);

// Allows setting the watering node's IP directly (e.g. from an HTTP /capture?watering_ip=... call).
void http_tx_set_watering_ip(const IPAddress& ip);

// Sends PlantHealthMsg + full 320x240 JPEG to the watering node via HTTP POST /health.
// Returns true if HTTP 200 OK was returned by the watering node.
bool http_tx_send(const HealthResult& r, const uint8_t* jpg_buf = nullptr, size_t jpg_len = 0);

// Status helpers for the camera node WebUI
uint32_t http_tx_seq();
String http_tx_watering_ip();
int http_tx_last_http_code();
