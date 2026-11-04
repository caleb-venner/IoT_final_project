// Local Wi-Fi HTTP transmitter and UDP discovery client for the camera node.
// Discovers the watering node via UDP broadcast (port 4210) or mDNS (basil-water.local)
// and uploads PlantHealthMsg metadata + 320x240 JPEG frames to POST /health.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include "http_tx.h"
#include "config.h"
#include "plant_msgs.h"

static const uint16_t kDiscoveryPort = 4210;
static WiFiUDP s_udp;
static uint32_t s_seq = 0;
static TaskHandle_t s_notify_task = nullptr;
static volatile uint32_t s_watering_ipv4 = 0;
static int s_last_http_code = 0;

void http_tx_set_watering_ip(const IPAddress& ip) {
  if ((uint32_t)ip != 0 && ip != INADDR_NONE) {
    s_watering_ipv4 = (uint32_t)ip;
  }
}

void http_tx_poll() {
  int pkt_len = s_udp.parsePacket();
  if (pkt_len <= 0) return;

  char buf[32] = {0};
  int n = s_udp.read(buf, sizeof(buf) - 1);
  if (n <= 0) return;

  IPAddress rem = s_udp.remoteIP();
  if ((uint32_t)rem != 0) {
    s_watering_ipv4 = (uint32_t)rem;
  }

  if (strncmp(buf, "BASIL_CAPTURE", 13) == 0) {
    Serial.printf("[camera-link] Remote capture requested via UDP from %s\n", rem.toString().c_str());
    if (s_notify_task != nullptr) {
      xTaskNotifyGive(s_notify_task);
    }
  }
}

static IPAddress resolve_watering_ip() {
  if (s_watering_ipv4 != 0) {
    return IPAddress(s_watering_ipv4);
  }
  if (WiFi.status() != WL_CONNECTED) {
    return IPAddress(0, 0, 0, 0);
  }

  // 1. Fast UDP broadcast discovery on port 4210 (< 20 ms on local subnet, immune to mDNS filtering)
  s_udp.beginPacket(IPAddress(255, 255, 255, 255), kDiscoveryPort);
  s_udp.write(reinterpret_cast<const uint8_t*>("BASIL_DISCOVER"), 14);
  s_udp.endPacket();

  uint32_t t0 = millis();
  while (millis() - t0 < 150) {
    int pkt_len = s_udp.parsePacket();
    if (pkt_len > 0) {
      char buf[32] = {0};
      int n = s_udp.read(buf, sizeof(buf) - 1);
      if (n > 0 && strncmp(buf, "BASIL_WATER", 11) == 0) {
        IPAddress found = s_udp.remoteIP();
        s_watering_ipv4 = (uint32_t)found;
        Serial.printf("[http] Discovered watering node via UDP -> %s\n", found.toString().c_str());
        return found;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  // 2. Fallback to mDNS query (basil-water.local)
  IPAddress mdns_ip = MDNS.queryHost(WATERING_MDNS_HOSTNAME, 1500);
  if (mdns_ip != INADDR_NONE && (uint32_t)mdns_ip != 0) {
    s_watering_ipv4 = (uint32_t)mdns_ip;
    Serial.printf("[http] Resolved %s.local -> %s\n",
                  WATERING_MDNS_HOSTNAME, mdns_ip.toString().c_str());
    return mdns_ip;
  }

  return IPAddress(0, 0, 0, 0);
}

bool http_tx_init(const char* ssid) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("[wifi] Connecting to '%s'", ssid ? ssid : WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0 < 12000UL)) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] Connected! IP: %s (ch %d, MAC %s)\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.channel(),
                  WiFi.macAddress().c_str());
    if (MDNS.begin(CAM_MDNS_HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[wifi] mDNS started: http://%s.local/\n", CAM_MDNS_HOSTNAME);
    }
  } else {
    Serial.println("[wifi] Initial connect timed out; will keep retrying in background.");
  }

  s_udp.begin(kDiscoveryPort);
  return true;
}

void http_tx_set_notify_task(TaskHandle_t task) {
  s_notify_task = task;
}

bool http_tx_send(const HealthResult& r, const uint8_t* jpg_buf, size_t jpg_len) {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0 < 4000UL)) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (WiFi.status() != WL_CONNECTED) {
      return false;
    }
  }

  PlantHealthMsg msg;
  msg.version = PLANT_MSG_VERSION;
  msg.state = r.state;
  msg.confidence = r.confidence;
  msg.seq = s_seq++;
  msg.inference_ms = r.inference_ms;

  IPAddress target_ip = resolve_watering_ip();
  if ((uint32_t)target_ip == 0) {
    Serial.println("[http] Watering node IP not resolved yet");
    return false;
  }

  const char* state_str = (msg.state == PLANT_WILTED) ? "wilted" :
                          ((msg.state == PLANT_HEALTHY) ? "healthy" : "unknown");
  String cam_ip = WiFi.localIP().toString();

  char url[192];
  snprintf(url, sizeof(url),
           "http://%s/health?state=%u&conf=%.3f&seq=%lu&inf_ms=%lu&ip=%s",
           target_ip.toString().c_str(),
           (unsigned)msg.state,
           msg.confidence,
           (unsigned long)msg.seq,
           (unsigned long)msg.inference_ms,
           cam_ip.c_str());

  HTTPClient http;
  http.setTimeout(4000);
  if (!http.begin(url)) {
    return false;
  }

  int code = -1;
  if (jpg_buf != nullptr && jpg_len > 0) {
    const char* boundary = "----BasilCamBoundary7MA4YWxkTrZu0gW";
    String head = String("--") + boundary + "\r\n"
                  "Content-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\n"
                  "Content-Type: image/jpeg\r\n\r\n";
    String tail = String("\r\n--") + boundary + "--\r\n";

    size_t total_len = head.length() + jpg_len + tail.length();
    uint8_t* body = (uint8_t*)(psramFound() ? ps_malloc(total_len) : malloc(total_len));
    if (body) {
      memcpy(body, head.c_str(), head.length());
      memcpy(body + head.length(), jpg_buf, jpg_len);
      memcpy(body + head.length() + jpg_len, tail.c_str(), tail.length());

      http.addHeader("Content-Type", String("multipart/form-data; boundary=") + boundary);
      code = http.POST(body, total_len);
      free(body);
    }
  } else {
    char json[160];
    snprintf(json, sizeof(json),
             "{\"state\":\"%s\",\"conf\":%.3f,\"seq\":%lu,\"inference_ms\":%lu,\"cam_ip\":\"%s\"}",
             state_str, msg.confidence, (unsigned long)msg.seq,
             (unsigned long)msg.inference_ms, cam_ip.c_str());
    http.addHeader("Content-Type", "application/json");
    code = http.POST((uint8_t*)json, strlen(json));
  }

  http.end();
  s_last_http_code = code;

  Serial.printf("[http] POST %s (jpg=%uB) -> HTTP %d\n",
                url, (unsigned)jpg_len, code);

  if (code != 200) {
    // Re-discover watering node IP on next send in case DHCP reassigned it
    s_watering_ipv4 = 0;
  }
  return (code == 200);
}

uint32_t http_tx_seq() {
  return s_seq;
}

String http_tx_watering_ip() {
  if (s_watering_ipv4 == 0) return "discovering...";
  return IPAddress(s_watering_ipv4).toString();
}

int http_tx_last_http_code() {
  return s_last_http_code;
}
