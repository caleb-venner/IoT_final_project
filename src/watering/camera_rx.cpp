// camera_rx.cpp
// Purpose: Local Wi-Fi HTTP receiver for PlantHealthMsg and 320x240 JPEGs from the camera node
//          (POST /health), 2-image ring buffer (latest + previous), UDP discovery responder,
//          and remote HTTP capture trigger.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <stddef.h>
#include <stdlib.h>
#include "camera_rx.h"

struct ImageSlot {
  uint8_t* buf;
  bool     valid;
  uint32_t seq;
  uint8_t  state;
  float    confidence;
  uint32_t rx_ms;
  size_t   bytes;
};

static const uint16_t kDiscoveryPort = 4210;
static WiFiUDP s_udp;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static PlantHealthMsg s_latest = {};
static uint32_t s_rx_ms = 0;
static bool s_have = false;
static char s_cam_ip[20] = "";

// Diagnostic counters
static uint32_t s_health_rx_count = 0;
static uint32_t s_img_complete_count = 0;
static uint32_t s_last_img_seq = 0;
static size_t   s_last_img_bytes = 0;

// 2 completed image slots (0 = latest, 1 = previous) + 1 staging buffer for incoming HTTP uploads
static ImageSlot s_slots[2] = {};
static uint8_t* s_staging_buf = nullptr;
static size_t s_http_upload_len = 0;

bool camera_rx_init() {
  if (!s_staging_buf) s_staging_buf = (uint8_t*)malloc(PLANT_HTTP_MAX_IMG_BYTES);
  if (!s_slots[0].buf) s_slots[0].buf = (uint8_t*)malloc(PLANT_HTTP_MAX_IMG_BYTES);
  if (!s_slots[1].buf) s_slots[1].buf = (uint8_t*)malloc(PLANT_HTTP_MAX_IMG_BYTES);

  if (!s_staging_buf || !s_slots[0].buf || !s_slots[1].buf) {
    Serial.println("[camera-link] WARNING: failed to allocate JPEG preview buffers");
  }

  s_udp.begin(kDiscoveryPort);
  Serial.printf("[camera-link] HTTP /health receiver + UDP discovery ready on port %u\n",
                (unsigned)kDiscoveryPort);
  return true;
}

void camera_rx_poll() {
  int pkt_len = s_udp.parsePacket();
  if (pkt_len <= 0) return;

  char buf[32] = {0};
  int n = s_udp.read(buf, sizeof(buf) - 1);
  if (n > 0 && strncmp(buf, "BASIL_DISCOVER", 14) == 0) {
    IPAddress remote = s_udp.remoteIP();
    String rem_str = remote.toString();
    portENTER_CRITICAL(&s_mux);
    snprintf(s_cam_ip, sizeof(s_cam_ip), "%s", rem_str.c_str());
    portEXIT_CRITICAL(&s_mux);

    s_udp.beginPacket(remote, s_udp.remotePort());
    s_udp.write(reinterpret_cast<const uint8_t*>("BASIL_WATER"), 11);
    s_udp.endPacket();
  }
}

bool camera_rx_latest(PlantHealthMsg* out, uint32_t* age_ms) {
  portENTER_CRITICAL(&s_mux);
  bool have = s_have;
  if (have) {
    *out = s_latest;
    *age_ms = millis() - s_rx_ms;
  }
  portEXIT_CRITICAL(&s_mux);
  return have;
}

bool camera_rx_get_image(int slot, CameraImageMeta* meta_out, const uint8_t** jpg_out) {
  if (slot < 0 || slot > 1) return false;
  portENTER_CRITICAL(&s_mux);
  const ImageSlot& s = s_slots[slot];
  bool ok = s.valid && s.buf != nullptr && s.bytes > 0;
  if (ok) {
    if (meta_out) {
      meta_out->valid = true;
      meta_out->seq = s.seq;
      meta_out->state = s.state;
      meta_out->confidence = s.confidence;
      meta_out->age_ms = millis() - s.rx_ms;
      meta_out->bytes = s.bytes;
    }
    if (jpg_out) {
      *jpg_out = s.buf;
    }
  }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

void camera_rx_get_diag(CameraLinkDiag* out) {
  if (!out) return;
  portENTER_CRITICAL(&s_mux);
  snprintf(out->cam_ip_str, sizeof(out->cam_ip_str), "%s", s_cam_ip[0] ? s_cam_ip : "not discovered");
  out->health_rx_count = s_health_rx_count;
  out->img_complete_count = s_img_complete_count;
  out->last_img_seq = s_last_img_seq;
  out->last_img_bytes = s_last_img_bytes;
  portEXIT_CRITICAL(&s_mux);
}

void camera_rx_http_upload_begin() {
  portENTER_CRITICAL(&s_mux);
  s_http_upload_len = 0;
  portEXIT_CRITICAL(&s_mux);
}

void camera_rx_http_upload_write(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;
  portENTER_CRITICAL(&s_mux);
  if (s_staging_buf && (s_http_upload_len + len <= PLANT_HTTP_MAX_IMG_BYTES)) {
    memcpy(s_staging_buf + s_http_upload_len, data, len);
    s_http_upload_len += len;
  }
  portEXIT_CRITICAL(&s_mux);
}

void camera_rx_http_upload_end(uint8_t state, float conf, uint32_t seq) {
  bool promoted = false;
  size_t total_bytes = 0;
  portENTER_CRITICAL(&s_mux);
  if (s_staging_buf && s_http_upload_len >= 2 &&
      s_staging_buf[0] == 0xFF && s_staging_buf[1] == 0xD8) {
    uint8_t* recycled_buf = s_slots[1].buf;
    s_slots[1] = s_slots[0];

    s_slots[0].buf = s_staging_buf;
    s_slots[0].valid = true;
    s_slots[0].seq = seq;
    s_slots[0].state = state;
    s_slots[0].confidence = conf;
    s_slots[0].rx_ms = millis();
    s_slots[0].bytes = s_http_upload_len;

    s_staging_buf = recycled_buf;
    s_img_complete_count++;
    s_last_img_seq = seq;
    s_last_img_bytes = s_slots[0].bytes;
    total_bytes = s_slots[0].bytes;
    promoted = true;
  }
  s_http_upload_len = 0;
  portEXIT_CRITICAL(&s_mux);

  if (promoted) {
    Serial.printf("[http] Stored camera JPEG seq=%lu (%u bytes, state=%u, conf=%.2f)\n",
                  (unsigned long)seq, (unsigned)total_bytes, (unsigned)state, conf);
  }
}

void camera_rx_ingest_http_health(uint8_t state, float conf, uint32_t seq, uint32_t inf_ms, const char* cam_ip) {
  portENTER_CRITICAL(&s_mux);
  s_latest.version = PLANT_MSG_VERSION;
  s_latest.state = state;
  s_latest.confidence = conf;
  s_latest.seq = seq;
  s_latest.inference_ms = inf_ms;
  s_rx_ms = millis();
  s_have = true;
  s_health_rx_count++;
  if (cam_ip && cam_ip[0] != '\0') {
    snprintf(s_cam_ip, sizeof(s_cam_ip), "%s", cam_ip);
  }
  portEXIT_CRITICAL(&s_mux);

  Serial.printf("[http] POST /health: seq=%lu state=%u conf=%.2f inf=%lums from %s\n",
                (unsigned long)seq, (unsigned)state, conf, (unsigned long)inf_ms,
                (cam_ip && cam_ip[0]) ? cam_ip : "client");
}

bool camera_rx_request_capture() {
  if (WiFi.status() != WL_CONNECTED) return false;

  char cam_ip[20] = "";
  portENTER_CRITICAL(&s_mux);
  snprintf(cam_ip, sizeof(cam_ip), "%s", s_cam_ip);
  portEXIT_CRITICAL(&s_mux);

  // If camera IP is not yet cached, try resolving basil-cam.local via mDNS
  if (cam_ip[0] == '\0') {
    IPAddress mdns_ip = MDNS.queryHost("basil-cam", 1200);
    if (mdns_ip != INADDR_NONE && (uint32_t)mdns_ip != 0) {
      snprintf(cam_ip, sizeof(cam_ip), "%s", mdns_ip.toString().c_str());
      portENTER_CRITICAL(&s_mux);
      snprintf(s_cam_ip, sizeof(s_cam_ip), "%s", cam_ip);
      portEXIT_CRITICAL(&s_mux);
    }
  }

  // Also broadcast a UDP trigger on port 4210 so the camera node hears it even before mDNS resolves
  s_udp.beginPacket(IPAddress(255, 255, 255, 255), kDiscoveryPort);
  s_udp.write(reinterpret_cast<const uint8_t*>("BASIL_CAPTURE"), 13);
  s_udp.endPacket();

  if (cam_ip[0] == '\0') {
    Serial.println("[camera-link] Sent UDP broadcast capture trigger (camera IP not yet cached)");
    return true;
  }

  HTTPClient http;
  http.setTimeout(2000);
  String url = String("http://") + cam_ip + "/capture?watering_ip=" + WiFi.localIP().toString();
  bool ok = false;
  if (http.begin(url)) {
    int code = http.POST("");
    ok = (code == 200);
    http.end();
  }
  Serial.printf("[camera-link] Triggered capture at %s -> %s\n", url.c_str(), ok ? "OK" : "UDP fallback");
  return ok;
}
