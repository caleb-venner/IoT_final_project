// camera_rx.h
// Purpose: Local Wi-Fi HTTP receiver for PlantHealthMsg and 320x240 JPEG previews from the
//          camera node (POST /health), plus remote HTTP capture trigger and UDP IP discovery.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#pragma once
#include <stddef.h>
#include <stdint.h>
#include "plant_msgs.h"

struct CameraImageMeta {
  bool     valid;
  uint32_t seq;
  uint8_t  state;
  float    confidence;
  uint32_t age_ms;
  size_t   bytes;
};

struct CameraLinkDiag {
  char     cam_ip_str[20];
  uint32_t health_rx_count;
  uint32_t img_complete_count;
  uint32_t last_img_seq;
  size_t   last_img_bytes;
};

// Initialises the 2-slot JPEG preview ring buffer and UDP discovery listener.
bool camera_rx_init();
// Polls the lightweight UDP discovery socket so the camera node can find our IP without mDNS delay.
void camera_rx_poll();
// Copies the latest valid health message and its age. Returns false if nothing received yet.
bool camera_rx_latest(PlantHealthMsg* out, uint32_t* age_ms);
// Returns metadata and pointer to JPEG buffer for slot 0 (latest) or slot 1 (previous).
bool camera_rx_get_image(int slot, CameraImageMeta* meta_out, const uint8_t** jpg_out);
// Copies HTTP camera reception and image diagnostics.
void camera_rx_get_diag(CameraLinkDiag* out);
// Sends an HTTP POST /capture request to the camera node to trigger an immediate capture & inference.
bool camera_rx_request_capture();

// HTTP POST /health ingestion helpers (supports both JSON and multipart 320x240 JPEG uploads)
void camera_rx_http_upload_begin();
void camera_rx_http_upload_write(const uint8_t* data, size_t len);
void camera_rx_http_upload_end(uint8_t state, float conf, uint32_t seq);
void camera_rx_ingest_http_health(uint8_t state, float conf, uint32_t seq, uint32_t inf_ms, const char* cam_ip);
