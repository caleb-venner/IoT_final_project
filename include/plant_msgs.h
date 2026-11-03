// plant_msgs.h
// Purpose: Agreed contract between nodes: local HTTP health message and MQTT topic names.
//          Change only by agreement between Caleb and Aida.
// OWNER:   Shared (Health struct: Caleb, MQTT topic design: Aida)
// Node:    camera node, watering node, tools/*

#pragma once
#include <stdint.h>

// ---------- Local Wi-Fi HTTP: camera node -> watering node (POST /health) ----------
#define PLANT_MSG_VERSION 1
#define PLANT_HTTP_MAX_IMG_BYTES 24576 // 24 KB max JPEG buffer per image slot for HTTP POST /health uploads

enum PlantState : uint8_t {
  PLANT_HEALTHY = 0,
  PLANT_WILTED  = 1,
  PLANT_UNKNOWN = 2,
};

typedef struct __attribute__((packed)) {
  uint8_t  version;       // PLANT_MSG_VERSION
  uint8_t  state;         // PlantState
  float    confidence;    // 0.0 - 1.0
  uint32_t seq;           // increments every send
  uint32_t inference_ms;  // inference duration
} PlantHealthMsg;

// ---------- MQTT topics (proposed; OWNER of design: Aida) ----------
#define MQTT_TOPIC_TELEMETRY "farm/basil_aida_caleb/telemetry"
#define MQTT_TOPIC_EVENTS    "farm/basil_aida_caleb/events"
#define MQTT_TOPIC_STATUS    "farm/basil_aida_caleb/status"
