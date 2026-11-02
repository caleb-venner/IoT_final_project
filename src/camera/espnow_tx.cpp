// espnow_tx.cpp
// Purpose: ESP-NOW sender for PlantHealthMsg. Matches the router's channel by SSID scan.
// OWNER:   Caleb
// Node:    camera node (Freenove ESP32-WROVER CAM)

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "espnow_tx.h"
#include "config.h"
#include "plant_msgs.h"

static const uint8_t kPeerMac[6] = ESPNOW_PEER_MAC;
static uint32_t s_seq = 0;

static int find_channel(const char* ssid) {
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == ssid) {
      int ch = WiFi.channel(i);
      WiFi.scanDelete();
      return ch;
    }
  }
  WiFi.scanDelete();
  return -1;
}

bool espnow_tx_init(const char* ssid_for_channel) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  int ch = find_channel(ssid_for_channel);
  if (ch < 1) {
    Serial.printf("[espnow] SSID '%s' not found, using fallback channel %d\n",
                  ssid_for_channel, ESPNOW_CHANNEL);
    ch = ESPNOW_CHANNEL;
  } else {
    Serial.printf("[espnow] SSID '%s' is on channel %d\n", ssid_for_channel, ch);
  }

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[espnow] init failed");
    return false;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kPeerMac, 6);
  peer.channel = 0;  // 0 = current channel
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[espnow] add peer failed");
    return false;
  }

  Serial.printf("[espnow] ready, my MAC %s\n", WiFi.macAddress().c_str());
  return true;
}

bool espnow_tx_send(const HealthResult& r) {
  PlantHealthMsg msg;
  msg.version = PLANT_MSG_VERSION;
  msg.state = r.state;
  msg.confidence = r.confidence;
  msg.seq = s_seq++;
  msg.inference_ms = r.inference_ms;

  esp_err_t err = esp_now_send(kPeerMac, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
  Serial.printf("[espnow] send seq=%lu state=%u conf=%.2f inf=%lums -> %s\n",
                (unsigned long)msg.seq, msg.state, msg.confidence,
                (unsigned long)msg.inference_ms, err == ESP_OK ? "ok" : esp_err_to_name(err));
  return err == ESP_OK;
}
