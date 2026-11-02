// main.cpp
// Purpose: Camera node entry point. Runs periodic and on-demand Edge-ML wilt classification
//          and transmits PlantHealthMsg over ESP-NOW to the watering node.
// OWNER:   Caleb
// Node:    camera node (Freenove ESP32-WROVER CAM)

#include <Arduino.h>
#include "config.h"
#include "camera.h"
#include "inference.h"
#include "espnow_tx.h"

static TaskHandle_t s_inference_task = nullptr;

static void run_single_inference() {
  camera_fb_t* fb = camera_capture();
  if (!fb) {
    Serial.println("[camera] capture failed");
  } else {
    HealthResult res = inference_run(fb);
    camera_release(fb);
    Serial.printf(
        "[camera] state=%s (%u) conf=%.2f inference_ms=%lu\n",
        res.state == PLANT_WILTED ? "WILTED" : (res.state == PLANT_HEALTHY ? "HEALTHY" : "UNKNOWN"),
        (unsigned)res.state,
        res.confidence,
        (unsigned long)res.inference_ms);
    espnow_tx_send(res);
  }
}

static void inference_task(void* pvParameters) {
  (void)pvParameters;
  for (;;) {
    run_single_inference();
    // Wait up to INFERENCE_INTERVAL_MS, or wake early if notified via Serial ('i' / 'c')
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(INFERENCE_INTERVAL_MS));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[camera] Initialising Smart Irrigation Camera Node...");

  if (!espnow_tx_init(WIFI_SSID)) {
    Serial.println("[camera] espnow_tx_init failed");
  }

  if (!camera_init()) {
    Serial.println("[camera] camera_init failed");
  }
  if (!inference_init()) {
    Serial.println("[camera] inference_init failed");
  }

  Serial.println("[camera] Ready. Press 'i' or 'c' in Serial Monitor to trigger immediate inference.");

  xTaskCreatePinnedToCore(
      inference_task,
      "inference_task",
      8192,
      nullptr,
      1,
      &s_inference_task,
      1);
}

void loop() {
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if ((ch == 'i' || ch == 'I' || ch == 'c' || ch == 'C') && s_inference_task != nullptr) {
      Serial.println("[camera] Manual inference triggered via Serial...");
      xTaskNotifyGive(s_inference_task);
    }
  }
  vTaskDelay(pdMS_TO_TICKS(50));
}
