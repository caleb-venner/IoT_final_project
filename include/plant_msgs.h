// plant_msgs.h
// Purpose: Agreed contract between nodes: ESP-NOW health message and MQTT topic names.
//          Change only by agreement between Caleb and Aida.
// OWNER:   Shared (ESP-NOW struct: Caleb, MQTT topic design: Aida)
// Node:    camera node, watering node, tools/*

#pragma once
#include <stdint.h>

// ---------- ESP-NOW: camera node -> watering node ----------
#define PLANT_MSG_VERSION 1

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

static_assert(sizeof(PlantHealthMsg) <= 250, "ESP-NOW payload limit");

// ---------- MQTT topics (proposed; OWNER of design: Aida) ----------
#define MQTT_TOPIC_TELEMETRY "farm/basil1/telemetry"
#define MQTT_TOPIC_EVENTS    "farm/basil1/events"
#define MQTT_TOPIC_STATUS    "farm/basil1/status"
