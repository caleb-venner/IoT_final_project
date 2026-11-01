// espnow_rx.cpp
// Purpose: ESP-NOW receiver. Validates PlantHealthMsg and stores the latest one for the loop.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "espnow_rx.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static PlantHealthMsg s_latest;
static uint32_t s_rx_ms = 0;
static bool s_have = false;
static uint8_t s_channel = 0;            // last known Wi-Fi channel (0 = not yet connected)
static const uint32_t kChannelCheckMs = 5000;

static void handle_rx(const uint8_t* data, int len) {
  if (len != sizeof(PlantHealthMsg)) return;
  PlantHealthMsg msg;
  memcpy(&msg, data, sizeof(msg));
  if (msg.version != PLANT_MSG_VERSION) return;

  // Runs in the Wi-Fi task; the main loop reads from another context.
  portENTER_CRITICAL(&s_mux);
  s_latest = msg;
  s_rx_ms = millis();
  s_have = true;
  portEXIT_CRITICAL(&s_mux);
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void on_recv(const esp_now_recv_info_t*, const uint8_t* data, int len) { handle_rx(data, len); }
#else
static void on_recv(const uint8_t*, const uint8_t* data, int len) { handle_rx(data, len); }
#endif

bool espnow_rx_init() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[espnow] init failed");
    return false;
  }
  esp_now_register_recv_cb(on_recv);
  if (WiFi.status() == WL_CONNECTED) s_channel = WiFi.channel();
  Serial.printf("[espnow] listening on channel %d, my MAC %s\n",
                WiFi.channel(), WiFi.macAddress().c_str());
  return true;
}

void espnow_rx_check_channel() {
  static uint32_t s_last_check_ms = 0;
  if (millis() - s_last_check_ms < kChannelCheckMs) return;
  s_last_check_ms = millis();
  if (WiFi.status() != WL_CONNECTED) return;  // radio is scanning; channel is meaningless

  uint8_t ch = WiFi.channel();
  if (ch == s_channel) return;
  if (s_channel == 0) {
    // Wi-Fi timed out at boot and connected later; the boot log showed the wrong channel.
    Serial.printf("[espnow] Wi-Fi connected late, now listening on channel %u\n", ch);
  } else {
    Serial.printf("[espnow] WARNING: router channel changed %u -> %u; camera node must re-scan\n",
                  s_channel, ch);
  }
  s_channel = ch;
}

bool espnow_rx_latest(PlantHealthMsg* out, uint32_t* age_ms) {
  portENTER_CRITICAL(&s_mux);
  bool have = s_have;
  if (have) {
    *out = s_latest;
    *age_ms = millis() - s_rx_ms;
  }
  portEXIT_CRITICAL(&s_mux);
  return have;
}
