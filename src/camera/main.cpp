// Camera node entry point. Runs periodic and on-demand Edge-ML wilt classification,
// sends health results and 320x240 JPEG frames over Wi-Fi HTTP to the watering node,
// and serves a local WebUI and OTA updater at http://basil-cam.local/.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "img_converters.h"
#include "config.h"
#include "camera.h"
#include "inference.h"
#include "http_tx.h"
#include "webui_html.h"

static TaskHandle_t s_inference_task = nullptr;
static WebServer s_server(80);

static portMUX_TYPE s_jpg_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t* s_latest_jpg = nullptr;
static size_t s_latest_jpg_len = 0;
static HealthResult s_latest_res = {PLANT_UNKNOWN, 0.0f, 0};
static uint32_t s_latest_ms = 0;

static bool run_single_inference() {
  camera_fb_t* fb = camera_capture();
  if (!fb) {
    Serial.println("[camera] capture failed");
    return false;
  }

  HealthResult res = inference_run(fb);

  // Encode full 320x240 frame to JPEG (~8-12 KB) for HTTP POST /health & WebUI preview
  uint8_t* jpg_buf = nullptr;
  size_t jpg_len = 0;
  if (!frame2jpg(fb, 60, &jpg_buf, &jpg_len) || jpg_len > PLANT_HTTP_MAX_IMG_BYTES) {
    if (jpg_buf) {
      free(jpg_buf);
      jpg_buf = nullptr;
      jpg_len = 0;
    }
    frame2jpg(fb, 35, &jpg_buf, &jpg_len);
  }
  camera_release(fb);

  // Cache latest JPEG on the camera node for http://basil-cam.local/latest.jpg
  if (jpg_buf && jpg_len > 0 && jpg_len <= PLANT_HTTP_MAX_IMG_BYTES && s_latest_jpg) {
    portENTER_CRITICAL(&s_jpg_mux);
    memcpy(s_latest_jpg, jpg_buf, jpg_len);
    s_latest_jpg_len = jpg_len;
    s_latest_res = res;
    s_latest_ms = millis();
    portEXIT_CRITICAL(&s_jpg_mux);
  }

  Serial.printf(
      "[camera] state=%s (%u) conf=%.2f inference_ms=%lu jpg=%uB\n",
      res.state == PLANT_WILTED ? "WILTED" : (res.state == PLANT_HEALTHY ? "HEALTHY" : "UNKNOWN"),
      (unsigned)res.state,
      res.confidence,
      (unsigned long)res.inference_ms,
      (unsigned)jpg_len);
  bool acked = http_tx_send(res, jpg_buf, jpg_len);
  if (jpg_buf) {
    free(jpg_buf);
  }
  return acked;
}

static void inference_task(void* pvParameters) {
  (void)pvParameters;
  for (;;) {
    bool acked = run_single_inference();
    // If watering node is still booting (shared 5V supply) or unreachable, retry in 5s
    // instead of waiting the full 60s INFERENCE_INTERVAL_MS.
    uint32_t wait_ms = acked ? INFERENCE_INTERVAL_MS : 5000UL;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
  }
}

static void setup_cam_web_server() {
  s_server.on("/", HTTP_GET, []() {
    s_server.send_P(200, "text/html", kCamIndexHtml);
  });

  s_server.on("/api/status", HTTP_GET, []() {
    HealthResult r;
    size_t jlen = 0;
    uint32_t age_s = 0;
    portENTER_CRITICAL(&s_jpg_mux);
    r = s_latest_res;
    jlen = s_latest_jpg_len;
    age_s = (s_latest_ms > 0) ? ((millis() - s_latest_ms) / 1000UL) : 0;
    portEXIT_CRITICAL(&s_jpg_mux);

    const char* sname = (r.state == PLANT_WILTED) ? "wilted" :
                        ((r.state == PLANT_HEALTHY) ? "healthy" : "unknown");
    String json = "{\"ip\":\"" + WiFi.localIP().toString() +
                  "\",\"channel\":" + String(WiFi.channel()) +
                  ",\"watering_ip\":\"" + http_tx_watering_ip() +
                  "\",\"last_http_code\":" + String(http_tx_last_http_code()) +
                  ",\"state\":\"" + String(sname) +
                  "\",\"conf\":" + String(r.confidence, 3) +
                  ",\"inference_ms\":" + String(r.inference_ms) +
                  ",\"age_s\":" + String(age_s) +
                  ",\"seq\":" + String(http_tx_seq()) +
                  ",\"has_jpg\":" + String(jlen > 0 ? "true" : "false") + "}";
    s_server.send(200, "application/json", json);
  });

  s_server.on("/latest.jpg", HTTP_GET, []() {
    if (!s_latest_jpg || s_latest_jpg_len == 0) {
      s_server.send(404, "text/plain", "No capture yet");
      return;
    }
    s_server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    s_server.send_P(200, "image/jpeg", reinterpret_cast<const char*>(s_latest_jpg), s_latest_jpg_len);
  });

  auto trigger_fn = []() {
    if (s_server.hasArg("watering_ip")) {
      IPAddress wip;
      if (wip.fromString(s_server.arg("watering_ip"))) {
        http_tx_set_watering_ip(wip);
      }
    }
    if (s_inference_task != nullptr) {
      xTaskNotifyGive(s_inference_task);
    }
    s_server.send(200, "application/json", "{\"ok\":true}");
  };
  s_server.on("/capture", HTTP_GET, trigger_fn);
  s_server.on("/capture", HTTP_POST, trigger_fn);

  s_server.on(
      "/api/ota", HTTP_POST,
      []() {
        bool ok = !Update.hasError();
        s_server.sendHeader("Connection", "close");
        s_server.send(ok ? 200 : 500, "text/plain", ok ? "OTA OK! Rebooting camera node..." : Update.errorString());
        if (ok) {
          delay(400);
          ESP.restart();
        }
      },
      []() {
        HTTPUpload& upload = s_server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          Serial.printf("[ota] Receiving camera firmware: %s\n", upload.filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (Update.end(true)) {
            Serial.printf("[ota] Success: %u bytes written. Rebooting...\n", (unsigned)upload.totalSize);
          } else {
            Update.printError(Serial);
          }
        }
      });

  s_server.begin();
}

void setup() {
  Serial.begin(115200);
  delay(800);  // Allow shared 5V rail to stabilise before initialising sensor & radio
  Serial.println("\n[camera] Initialising Smart Irrigation Camera Node...");

  s_latest_jpg = (uint8_t*)(psramFound() ? ps_malloc(PLANT_HTTP_MAX_IMG_BYTES) : malloc(PLANT_HTTP_MAX_IMG_BYTES));

  // Initialise camera FIRST before starting Wi-Fi radio to avoid DMA/power contention
  if (!camera_init()) {
    Serial.println("[camera] camera_init failed (will retry on capture)");
  }
  if (!inference_init()) {
    Serial.println("[camera] inference_init failed");
  }
  if (!http_tx_init(WIFI_SSID)) {
    Serial.println("[camera] http_tx_init failed");
  }

  setup_cam_web_server();
  Serial.println("[camera] Ready. Press 'i' or 'c' in Serial Monitor or visit http://basil-cam.local/ to trigger.");

  xTaskCreatePinnedToCore(
      inference_task,
      "inference_task",
      8192,
      nullptr,
      1,
      &s_inference_task,
      1);
  http_tx_set_notify_task(s_inference_task);
}

void loop() {
  s_server.handleClient();
  http_tx_poll();

  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if ((ch == 'i' || ch == 'I' || ch == 'c' || ch == 'C') && s_inference_task != nullptr) {
      Serial.println("[camera] Manual inference triggered via Serial...");
      xTaskNotifyGive(s_inference_task);
    }
  }
  vTaskDelay(pdMS_TO_TICKS(20));
}
