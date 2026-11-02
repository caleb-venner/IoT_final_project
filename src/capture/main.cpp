// main.cpp
// Purpose: Training data collection. Saves a JPEG to SD_MMC every CAPTURE_INTERVAL_MS
//          with sequential filenames (/img_00001.jpg, ...), logs metadata to /capture_log.csv,
//          and serves an HTTP interface over Wi-Fi (http://basil-cam.local/) to view/sync files.
//          Supports both native JPEG sensors (OV2640) and raw RGB565 sensors (OV7670/OV7725/GC0308 @ I2C 0x21)
//          via on-device frame2jpg() encoding.
// OWNER:   Caleb
// Node:    Freenove ESP32-WROVER CAM

#include <Arduino.h>
#include <vector>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "FS.h"
#include "SD_MMC.h"
#include "config.h"

static uint32_t s_index = 0;
static bool s_cam_ok = false;
static esp_err_t s_cam_err = ESP_OK;
static String s_sensor_name = "none";
static bool s_sensor_native_jpeg = false;
static String s_i2c_scan = "";
static bool s_sd_ok = false;
static bool s_wifi_disabled = false;
static WebServer s_server(80);

// Detach GPIO 21 from the LEDC peripheral and hold it LOW so the camera clock is 100% off when idle.
static void silence_camera_xclk() {
  gpio_matrix_out(WROVER_CAM_PIN_XCLK, SIG_GPIO_OUT_IDX, false, false);
  pinMode(WROVER_CAM_PIN_XCLK, OUTPUT);
  digitalWrite(WROVER_CAM_PIN_XCLK, LOW);
}

// Re-attach GPIO 21 to the 10 MHz LEDC clock so the camera sensor can capture a frame.
static void wake_camera_xclk() {
  ledcAttachPin(WROVER_CAM_PIN_XCLK, LEDC_CHANNEL_0);
  delay(80);
}

// Scan I2C on SIOD=26, SIOC=27 while driving XCLK on GPIO 21 to detect which sensor SCCB address responds.
static uint8_t scan_camera_i2c() {
  ledcSetup(LEDC_CHANNEL_0, 10000000, 1);
  ledcAttachPin(WROVER_CAM_PIN_XCLK, LEDC_CHANNEL_0);
  ledcWrite(LEDC_CHANNEL_0, 1);
  delay(20);

  Wire.begin(WROVER_CAM_PIN_SIOD, WROVER_CAM_PIN_SIOC, 100000);
  String found = "";
  uint8_t first_addr = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (first_addr == 0) first_addr = addr;
      if (found.length() > 0) found += ",";
      found += "0x" + String(addr, HEX);
    }
  }
  Wire.end();
  ledcDetachPin(WROVER_CAM_PIN_XCLK);
  s_i2c_scan = found.length() > 0 ? found : "none";
  Serial.printf("[capture] I2C scan on SDA=26/SCL=27: %s\n", s_i2c_scan.c_str());
  return first_addr;
}

// Initialise the camera sensor on the Freenove ESP32-WROVER-E CAM (CAMERA_MODEL_WROVER_KIT).
// Automatically selects PIXFORMAT_JPEG for OV2640/OV3660/OV5640 (I2C 0x30/0x3C) or
// PIXFORMAT_RGB565 + software JPEG encoding for OV7670/OV7725/GC0308/GC032A (I2C 0x21).
static bool camera_init() {
  uint8_t i2c_addr = scan_camera_i2c();
  // Sensors at 0x21 (OV7670, OV7725, GC0308, GC032A) do not have hardware JPEG encoders;
  // requesting PIXFORMAT_JPEG makes esp_camera_init() fail with 0x106 (ESP_ERR_NOT_SUPPORTED).
  s_sensor_native_jpeg = (i2c_addr == 0x30 || i2c_addr == 0x3C);

  camera_config_t cfg = {};
  cfg.pin_pwdn = WROVER_CAM_PIN_PWDN;
  cfg.pin_reset = WROVER_CAM_PIN_RESET;
  cfg.pin_xclk = WROVER_CAM_PIN_XCLK;
  cfg.pin_sccb_sda = WROVER_CAM_PIN_SIOD;
  cfg.pin_sccb_scl = WROVER_CAM_PIN_SIOC;
  cfg.pin_d7 = WROVER_CAM_PIN_Y9;
  cfg.pin_d6 = WROVER_CAM_PIN_Y8;
  cfg.pin_d5 = WROVER_CAM_PIN_Y7;
  cfg.pin_d4 = WROVER_CAM_PIN_Y6;
  cfg.pin_d3 = WROVER_CAM_PIN_Y5;
  cfg.pin_d2 = WROVER_CAM_PIN_Y4;
  cfg.pin_d1 = WROVER_CAM_PIN_Y3;
  cfg.pin_d0 = WROVER_CAM_PIN_Y2;
  cfg.pin_vsync = WROVER_CAM_PIN_VSYNC;
  cfg.pin_href = WROVER_CAM_PIN_HREF;
  cfg.pin_pclk = WROVER_CAM_PIN_PCLK;

  cfg.xclk_freq_hz = 10000000;
  cfg.ledc_timer = LEDC_TIMER_0;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.pixel_format = s_sensor_native_jpeg ? PIXFORMAT_JPEG : PIXFORMAT_RGB565;
  cfg.frame_size = FRAMESIZE_QVGA;  // 320x240
  cfg.jpeg_quality = 10;            // Used when sensor has native JPEG
  cfg.fb_count = 1;                 // 1 buffer + GRAB_WHEN_EMPTY prevents continuous background DMA coil whine
  cfg.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  cfg.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  s_cam_err = esp_camera_init(&cfg);
  if (s_cam_err != ESP_OK) {
    Serial.printf("[capture] esp_camera_init failed: 0x%x (%s)\n",
                  (unsigned)s_cam_err, esp_err_to_name(s_cam_err));
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    camera_sensor_info_t* info = esp_camera_sensor_get_info(&s->id);
    if (info && info->name) {
      s_sensor_name = String(info->name) + (s_sensor_native_jpeg ? " (JPEG)" : " (RGB565->JPEG)");
    } else {
      s_sensor_name = "PID 0x" + String(s->id.PID, HEX);
    }
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_gain_ctrl(s, 1);
  }
  // Detach XCLK between 5-minute captures so the camera is completely silent while idle
  silence_camera_xclk();
  Serial.printf("[capture] Camera initialised: %s\n", s_sensor_name.c_str());
  return true;
}

// Find the next unused image index on the SD card so power cycles never overwrite photos.
static void find_next_index() {
  s_index = 0;
  char path[32];
  do {
    s_index++;
    snprintf(path, sizeof(path), "/img_%05lu.jpg", (unsigned long)s_index);
  } while (SD_MMC.exists(path));
}

// Append a CSV record of each captured image for tracking uptime across power cycles.
static void log_capture(const char* filename, size_t bytes) {
  bool exists = SD_MMC.exists("/capture_log.csv");
  File log_file = SD_MMC.open("/capture_log.csv", FILE_APPEND);
  if (!log_file) {
    return;
  }
  if (!exists) {
    log_file.println("index,filename,uptime_s,bytes");
  }
  log_file.printf("%lu,%s,%lu,%u\n",
                  (unsigned long)s_index,
                  filename,
                  (unsigned long)(millis() / 1000UL),
                  (unsigned)bytes);
  log_file.close();
}

// Wake the camera clock, discard warmup frames so auto-exposure settles, and grab a fresh frame.
static camera_fb_t* grab_fresh_frame() {
  wake_camera_xclk();

  for (int i = 0; i < WARMUP_FRAMES; i++) {
    camera_fb_t* warmup = esp_camera_fb_get();
    if (warmup) {
      esp_camera_fb_return(warmup);
    }
    delay(50);
  }
  return esp_camera_fb_get();
}

static void release_and_sleep_camera(camera_fb_t* fb) {
  if (fb) {
    esp_camera_fb_return(fb);
  }
  silence_camera_xclk();
}

// Obtain a JPEG buffer from a camera_fb_t (either directly or via software frame2jpg conversion).
// If *needs_free is true on return, the caller must call free(*jpg_buf).
static bool get_jpeg_bytes(camera_fb_t* fb, uint8_t** jpg_buf, size_t* jpg_len, bool* needs_free) {
  if (fb->format == PIXFORMAT_JPEG) {
    *jpg_buf = fb->buf;
    *jpg_len = fb->len;
    *needs_free = false;
    return true;
  }
  // Convert RGB565 / YUV / Grayscale frame buffer to JPEG (quality 85 out of 100)
  *jpg_buf = nullptr;
  *jpg_len = 0;
  *needs_free = true;
  return frame2jpg(fb, 85, jpg_buf, jpg_len);
}

// Capture a fresh frame, encode to JPEG if needed, and save to SD_MMC.
static bool capture_one(String* saved_path = nullptr) {
  if (!s_cam_ok) {
    return false;
  }

  camera_fb_t* fb = grab_fresh_frame();
  if (!fb) {
    Serial.println("[capture] ERROR: camera capture failed");
    release_and_sleep_camera(nullptr);
    return false;
  }

  uint8_t* jpg_buf = nullptr;
  size_t jpg_len = 0;
  bool needs_free = false;
  if (!get_jpeg_bytes(fb, &jpg_buf, &jpg_len, &needs_free)) {
    Serial.println("[capture] ERROR: frame2jpg conversion failed");
    release_and_sleep_camera(fb);
    return false;
  }

  bool ok = false;
  if (s_sd_ok) {
    char path[32];
    snprintf(path, sizeof(path), "/img_%05lu.jpg", (unsigned long)s_index);
    File f = SD_MMC.open(path, FILE_WRITE);
    if (f) {
      size_t written = f.write(jpg_buf, jpg_len);
      f.close();
      log_capture(path, written);
      Serial.printf("[capture] saved %s (%u bytes, uptime %lus)\n",
                    path, (unsigned)written, (unsigned long)(millis() / 1000UL));
      if (saved_path) {
        *saved_path = String(path);
      }
      s_index++;
      ok = true;
    } else {
      Serial.printf("[capture] ERROR: failed to open %s for writing\n", path);
    }
  } else {
    Serial.printf("[capture] captured JPEG (%u bytes), SD not mounted\n", (unsigned)jpg_len);
  }

  if (needs_free && jpg_buf) {
    free(jpg_buf);
  }
  release_and_sleep_camera(fb);
  return ok;
}

// ---------- HTTP handlers ----------

// GET / -> Status page showing storage usage, latest saved SD photo, and list of captured files.
static void handle_root() {
  float used_mb = s_sd_ok ? (SD_MMC.usedBytes() / (1024.0f * 1024.0f)) : 0.0f;
  float total_mb = s_sd_ok ? (SD_MMC.totalBytes() / (1024.0f * 1024.0f)) : 0.0f;
  float pct = (total_mb > 0.0f) ? ((used_mb / total_mb) * 100.0f) : 0.0f;
  uint64_t free_bytes = (s_sd_ok && SD_MMC.totalBytes() > SD_MMC.usedBytes()) ? (SD_MMC.totalBytes() - SD_MMC.usedBytes()) : 0;
  unsigned long est_remaining = free_bytes / (15UL * 1024UL);

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Basil Capture Node</title>"
                "<style>body{font-family:system-ui,sans-serif;max-width:760px;margin:2rem auto;padding:0 1rem;}"
                "img{border:1px solid #ccc;border-radius:4px;max-width:100%;height:auto;}"
                "button{padding:0.5rem 1rem;font-size:1rem;cursor:pointer;}"
                ".danger{background:#fee;color:#a00;border:1px solid #c88;}"
                "code{background:#f4f4f4;padding:2px 6px;border-radius:3px;}</style></head><body>";
  html += "<h2>Basil Capture Node</h2>";
  html += "<p>Uptime: <code>" + String(millis() / 1000UL) + " s</code> | ";
  html += "Camera: <code>" + (s_cam_ok ? ("OK - " + s_sensor_name) : ("ERR 0x" + String(s_cam_err, HEX) + " (" + String(esp_err_to_name(s_cam_err)) + ", I2C=" + s_i2c_scan + ")")) + "</code> | ";
  html += "PSRAM: <code>" + String(psramFound() ? "yes" : "no") + "</code></p>";
  html += "<p>SD Status: <code>" + String(s_sd_ok ? "Mounted" : "Not Mounted") + "</code> | ";
  html += "Storage: <code>" + String(used_mb, 1) + " MB / " + String(total_mb, 0) + " MB (" + String(pct, 1) + "% used, ~" + String(est_remaining) + " photos capacity)</code> | ";
  html += "Photos: <code>" + String(s_index > 0 ? s_index - 1 : 0) + "</code> (next: <code>img_" + String(s_index) + "</code>)</p>";
  html += "<p><form action='/capture' method='POST' style='display:inline'><button type='submit'>Capture &amp; Save to SD Now</button></form> ";
  html += "<a href='/api/status'>JSON Status</a> | <a href='/api/files'>JSON File List</a> | <a href='/capture_log.csv'>Download CSV Log</a>";
  html += " | <form action='/api/clear?redirect=1' method='POST' style='display:inline;' onsubmit='return confirm(\"Are you sure you want to delete all captured photos from the SD card?\");'><button type='submit' class='danger' style='padding:0.4rem 0.8rem;font-size:0.9rem;'>Clear SD Photos</button></form></p>";

  if (s_sd_ok && s_index > 1) {
    char latest[32];
    snprintf(latest, sizeof(latest), "/img_%05lu.jpg", (unsigned long)(s_index - 1));
    html += "<div><h3>Latest Saved (" + String(latest) + ")</h3><img src='/latest.jpg?t=" + String(millis()) + "'><p><a href='" + String(latest) + "'>" + String(latest) + "</a></p></div>";
    html += "<h3>Recent Files on SD</h3><ul>";
    uint32_t last_idx = s_index - 1;
    uint32_t first_idx = (last_idx > 25) ? (last_idx - 25) : 1;
    for (uint32_t i = last_idx; i >= first_idx; i--) {
      char name[32];
      snprintf(name, sizeof(name), "/img_%05lu.jpg", (unsigned long)i);
      if (SD_MMC.exists(name)) {
        html += "<li><a href='" + String(name) + "'>" + String(name + 1) + "</a></li>";
      }
      if (i == 1) break;
    }
    html += "</ul>";
  } else {
    html += "<p>No images saved to SD card yet.</p>";
  }

  html += "</body></html>";
  s_server.send(200, "text/html", html);
}

// GET /latest.jpg -> Streams the most recently saved JPEG from the SD card.
static void handle_latest_jpg() {
  if (!s_sd_ok || s_index <= 1) {
    s_server.send(404, "text/plain", "No saved images on SD");
    return;
  }
  char path[32];
  snprintf(path, sizeof(path), "/img_%05lu.jpg", (unsigned long)(s_index - 1));
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    s_server.send(404, "text/plain", "Latest image not found");
    return;
  }
  s_server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  s_server.streamFile(f, "image/jpeg");
  f.close();
}

// POST or GET /capture -> Triggers an immediate SD save and redirects back to / (or returns JSON).
static void handle_trigger_capture() {
  String path;
  bool ok = capture_one(&path);
  if (s_server.method() == HTTP_POST) {
    s_server.sendHeader("Location", "/");
    s_server.send(303, "text/plain", "Redirecting...");
  } else {
    if (ok) {
      s_server.send(200, "application/json", "{\"ok\":true,\"file\":\"" + path + "\"}");
    } else {
      s_server.send(500, "application/json", "{\"ok\":false}");
    }
  }
}

// GET /api/status -> JSON summary of system, camera, SD mount and storage usage.
static void handle_api_status() {
  uint64_t total_bytes = s_sd_ok ? SD_MMC.totalBytes() : 0;
  uint64_t used_bytes = s_sd_ok ? SD_MMC.usedBytes() : 0;
  uint64_t free_bytes = (total_bytes > used_bytes) ? (total_bytes - used_bytes) : 0;
  uint32_t est_photos = free_bytes / (15UL * 1024UL);

  String json = "{\"uptime_s\":" + String(millis() / 1000UL) +
                ",\"camera_ok\":" + String(s_cam_ok ? "true" : "false") +
                ",\"sensor\":\"" + s_sensor_name + "\"" +
                ",\"sd_mounted\":" + String(s_sd_ok ? "true" : "false") +
                ",\"total_bytes\":" + String((unsigned long long)total_bytes) +
                ",\"used_bytes\":" + String((unsigned long long)used_bytes) +
                ",\"free_bytes\":" + String((unsigned long long)free_bytes) +
                ",\"est_photos_remaining\":" + String(est_photos) +
                ",\"photo_count\":" + String(s_index > 0 ? s_index - 1 : 0) +
                ",\"next_index\":" + String(s_index) + "}";
  s_server.send(200, "application/json", json);
}

// GET /api/files -> JSON array of all images on the SD card plus total/used storage.
static void handle_api_files() {
  if (!s_sd_ok) {
    s_server.send(503, "application/json", "{\"error\":\"SD not mounted\"}");
    return;
  }
  uint64_t total_bytes = SD_MMC.totalBytes();
  uint64_t used_bytes = SD_MMC.usedBytes();

  s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server.send(200, "application/json", "");

  s_server.sendContent("{\"count\":" + String(s_index > 0 ? s_index - 1 : 0) +
                       ",\"total_bytes\":" + String((unsigned long long)total_bytes) +
                       ",\"used_bytes\":" + String((unsigned long long)used_bytes) +
                       ",\"files\":[");
  bool first = true;
  File root = SD_MMC.open("/");
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while (f) {
      String name = String(f.name());
      if (!name.startsWith("/")) {
        name = "/" + name;
      }
      if (!f.isDirectory() && (name.endsWith(".jpg") || name.endsWith(".csv"))) {
        String entry = (first ? "" : ",") + String("{\"name\":\"") + name +
                       "\",\"size\":" + String((unsigned long)f.size()) + "}";
        s_server.sendContent(entry);
        first = false;
      }
      f.close();
      f = root.openNextFile();
    }
    root.close();
  }
  s_server.sendContent("]}");
  s_server.sendContent("");
}

// POST or GET /api/clear -> Deletes captured JPEG files from SD card and resets next index to 1.
static void handle_api_clear() {
  if (!s_sd_ok) {
    s_server.send(503, "application/json", "{\"ok\":false,\"error\":\"SD not mounted\"}");
    return;
  }

  bool delete_csv = s_server.hasArg("delete_log") && (s_server.arg("delete_log") == "1" || s_server.arg("delete_log") == "true");
  std::vector<String> files_to_delete;
  File root = SD_MMC.open("/");
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while (f) {
      String name = String(f.name());
      if (!name.startsWith("/")) {
        name = "/" + name;
      }
      if (!f.isDirectory()) {
        if (name.endsWith(".jpg") || (delete_csv && name.endsWith(".csv"))) {
          files_to_delete.push_back(name);
        }
      }
      f.close();
      f = root.openNextFile();
    }
    root.close();
  }

  uint32_t deleted_count = 0;
  for (const auto& path : files_to_delete) {
    if (SD_MMC.remove(path)) {
      deleted_count++;
    }
  }

  find_next_index();

  if (s_server.hasArg("redirect")) {
    s_server.sendHeader("Location", "/");
    s_server.send(303, "text/plain", "Redirecting...");
    return;
  }

  uint64_t total_bytes = SD_MMC.totalBytes();
  uint64_t used_bytes = SD_MMC.usedBytes();
  String resp = "{\"ok\":true,\"deleted\":" + String(deleted_count) +
                ",\"next_index\":" + String(s_index) +
                ",\"total_bytes\":" + String((unsigned long long)total_bytes) +
                ",\"used_bytes\":" + String((unsigned long long)used_bytes) + "}";
  s_server.send(200, "application/json", resp);
}

// Fallback handler: serve any matching file directly from SD_MMC (e.g. /img_00001.jpg or /capture_log.csv).
static void handle_sd_file() {
  String uri = s_server.uri();
  if (s_sd_ok && SD_MMC.exists(uri)) {
    File f = SD_MMC.open(uri, FILE_READ);
    if (f && !f.isDirectory()) {
      String mime = "application/octet-stream";
      if (uri.endsWith(".jpg")) mime = "image/jpeg";
      else if (uri.endsWith(".csv")) mime = "text/csv";
      s_server.streamFile(f, mime);
      f.close();
      return;
    }
    if (f) f.close();
  }
  s_server.send(404, "text/plain", "Not found: " + uri);
}

static void wifi_and_http_init() {
  WiFi.mode(WIFI_STA);
  // Enable Wi-Fi modem power save and lower TX power to eliminate LDO/capacitor whine
  WiFi.setSleep(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  Serial.printf("[http] Connecting to Wi-Fi SSID '%s'", WIFI_SSID);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000UL) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[http] Connected! IP: http://%s/\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin(MDNS_HOSTNAME)) {
      Serial.printf("[http] mDNS responder started: http://%s.local/\n", MDNS_HOSTNAME);
    }
  } else {
    Serial.println("[http] Wi-Fi not connected yet (will keep retrying in background). SD capture still runs.");
  }

  s_server.on("/", HTTP_GET, handle_root);
  s_server.on("/favicon.ico", HTTP_GET, []() { s_server.send(204); });
  s_server.on("/latest.jpg", HTTP_GET, handle_latest_jpg);
  s_server.on("/capture", HTTP_GET, handle_trigger_capture);
  s_server.on("/capture", HTTP_POST, handle_trigger_capture);
  s_server.on("/api/status", HTTP_GET, handle_api_status);
  s_server.on("/api/files", HTTP_GET, handle_api_files);
  s_server.on("/api/clear", HTTP_GET, handle_api_clear);
  s_server.on("/api/clear", HTTP_POST, handle_api_clear);
  s_server.onNotFound(handle_sd_file);
  s_server.begin();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[capture] Initialising SD + HTTP capture node...");

  // Initialise camera FIRST, then mount SD_MMC, and restore GPIO 4 (WROVER_CAM_PIN_Y2)
  // to camera D0 after SD_MMC.begin().
  s_cam_ok = camera_init();
  if (!s_cam_ok) {
    Serial.println("[capture] FATAL: camera_init() failed. Check camera ribbon cable.");
  }

  s_sd_ok = SD_MMC.begin("/sdcard", SD_ONE_BIT_MODE);
  if (s_cam_ok) {
    pinMode(WROVER_CAM_PIN_Y2, INPUT);
    gpio_matrix_in(WROVER_CAM_PIN_Y2, I2S0I_DATA_IN0_IDX, false);
  }
  if (!s_sd_ok) {
    Serial.println("[capture] WARNING: SD_MMC mount failed.");
  } else {
    uint64_t card_mb = SD_MMC.cardSize() / (1024ULL * 1024ULL);
    uint64_t used_mb = SD_MMC.usedBytes() / (1024ULL * 1024ULL);
    Serial.printf("[capture] SD card mounted: %llu MB total, %llu MB used.\n", card_mb, used_mb);
    find_next_index();
    Serial.printf("[capture] Starting at index %lu (/img_%05lu.jpg), interval = %lu s\n",
                  (unsigned long)s_index,
                  (unsigned long)s_index,
                  (unsigned long)(CAPTURE_INTERVAL_MS / 1000UL));
  }

  wifi_and_http_init();
  Serial.println("[capture] Tip: Press 'c' in Serial Monitor or visit http://basil-cam.local/ to trigger/view photos.");
}

void loop() {
  s_server.handleClient();

  // Reconnect Wi-Fi non-blocking if connection drops
  static uint32_t last_wifi_check = 0;
  static bool mdns_started = false;
  if (WiFi.status() == WL_CONNECTED && !mdns_started) {
    if (MDNS.begin(MDNS_HOSTNAME)) {
      mdns_started = true;
      Serial.printf("[http] Wi-Fi connected: http://%s/ (http://%s.local/)\n",
                    WiFi.localIP().toString().c_str(), MDNS_HOSTNAME);
    }
  } else if (!s_wifi_disabled && WiFi.status() != WL_CONNECTED && millis() - last_wifi_check >= 30000UL) {
    last_wifi_check = millis();
    WiFi.reconnect();
  }

  // Periodic SD capture
  static uint32_t last_capture_ms = 0;
  uint32_t now = millis();
  if (s_cam_ok && s_sd_ok && (last_capture_ms == 0 || (now - last_capture_ms >= CAPTURE_INTERVAL_MS))) {
    last_capture_ms = now;
    capture_one();
  }

  // Manual test capture & acoustic diagnostics via Serial
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == 'c' || ch == 'C') {
      Serial.println("[capture] Manual capture triggered via Serial:");
      capture_one();
    } else if (ch == 'i' || ch == 'I') {
      Serial.printf("[http] IP: %s (http://%s.local/)\n",
                    WiFi.localIP().toString().c_str(), MDNS_HOSTNAME);
    } else if (ch == 'w' || ch == 'W') {
      if (!s_wifi_disabled) {
        s_wifi_disabled = true;
        WiFi.mode(WIFI_OFF);
        Serial.println("[diag] Wi-Fi turned OFF (press 'w' again to turn ON)");
      } else {
        s_wifi_disabled = false;
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(true);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        WiFi.setTxPower(WIFI_POWER_11dBm);
        Serial.println("[diag] Wi-Fi turned back ON");
      }
    } else if (ch == 'x' || ch == 'X') {
      static bool xclk_off = true;
      if (xclk_off) {
        gpio_matrix_out(WROVER_CAM_PIN_XCLK, SIG_GPIO_OUT_IDX, false, false);
        pinMode(WROVER_CAM_PIN_XCLK, OUTPUT);
        digitalWrite(WROVER_CAM_PIN_XCLK, LOW);
        Serial.println("[diag] Camera XCLK pin detached & forced LOW");
      } else {
        ledcAttachPin(WROVER_CAM_PIN_XCLK, LEDC_CHANNEL_0);
        Serial.println("[diag] Camera XCLK re-attached");
      }
      xclk_off = !xclk_off;
    }
  }

  delay(5);
}
