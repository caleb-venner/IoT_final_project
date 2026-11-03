// main.cpp
// Purpose: Watering node main firmware with WebUI (http://basil-water.local/),
//          NVS-persisted calibration/thresholds, manual mL pump dosing,
//          toggleable automatic irrigation, local Wi-Fi HTTP camera receiver (/health),
//          runtime-configurable MQTT, and OTA firmware updates (/api/ota).
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "config.h"
#include "moisture.h"
#include "pump.h"
#include "control.h"
#include "camera_rx.h"
#include "mqtt_pub.h"
#include "plant_msgs.h"
#include "webui_html.h"

static WebServer s_server(80);
static uint32_t s_last_control_ms = 0;
static uint32_t s_last_telemetry_ms = 0;
static const char* s_last_decision_reason = "boot";  // shown in WebUI; always points to a literal

static const char* state_name(uint8_t s) {
  switch (s) {
    case PLANT_HEALTHY: return "healthy";
    case PLANT_WILTED:  return "wilted";
    default:            return "unknown";
  }
}

// Pulses the sensor, updates the moving-average filter, and logs the result.
static float read_moisture() {
  float pct = moisture_read_pct();
  Serial.printf("[sensor] Raw ADC: %4d | Calibrated Moisture: %5.1f%%\n", moisture_last_raw(), pct);
  return pct;
}

// Periodic cycle: fresh moisture reading + camera health -> control decision -> optional auto dose.
// Kept separate from read_moisture() so manual reads (WebUI, calibration, serial) never start the pump.
static void sense_and_control() {
  float pct = read_moisture();

  PlantHealthMsg health;
  uint32_t age_ms = 0;
  bool have_health = camera_rx_latest(&health, &age_ms);

  ControlInputs in = {
    pct,
    have_health,
    have_health ? (PlantState)health.state : PLANT_UNKNOWN,
    have_health ? health.confidence : 0.0f,
    age_ms,
    pump_is_on(),
  };

  ControlDecision d = control_decide(in);
  s_last_decision_reason = d.reason;

  if (d.water) {
    uint32_t ran_ms = pump_run_ms(d.duration_ms, false);  // actual run time after the auto cap
    if (ran_ms == 0) return;
    float ml = (ran_ms / 1000.0f) * g_cfg.ml_per_sec;
    Serial.printf("[control] AUTO WATER: %lu ms (~%.1f mL, reason: %s, moisture: %.1f%%)\n",
                  (unsigned long)ran_ms, ml, d.reason, pct);
    mqtt_publish_event(d.reason, ran_ms, ml);
  }
}

// Publishes a snapshot of cached sensor/pump/camera state (no new sensor pulse).
static void publish_current_telemetry() {
  PlantHealthMsg health;
  uint32_t age_ms = 0;
  bool have_health = camera_rx_latest(&health, &age_ms);

  Telemetry t = {
    moisture_last_pct(),
    moisture_last_raw(),
    pump_is_on(),
    have_health ? state_name(health.state) : "unknown",
    have_health ? health.confidence : 0.0f,
    have_health ? (int32_t)(age_ms / 1000) : -1,
    pump_total_ml(),
  };
  mqtt_publish_telemetry(t);
}

// Full node state + config as JSON; every /api/* endpoint replies with this so the UI re-renders.
static void send_status_json() {
  JsonDocument doc;

  // Sensor state
  uint32_t last_read = moisture_last_read_ms();
  doc["moisture_raw"] = moisture_last_raw();
  doc["moisture_pct"] = serialized(String(moisture_last_pct(), 1));
  doc["moisture_age_s"] = (last_read > 0) ? (int32_t)((millis() - last_read) / 1000UL) : -1;

  // Pump state
  doc["pump_on"] = pump_is_on();
  doc["pump_remaining_ms"] = pump_remaining_ms();
  doc["water_ml_total"] = serialized(String(pump_total_ml(), 1));

  // Camera state (received over local Wi-Fi HTTP POST /health)
  PlantHealthMsg health;
  uint32_t age_ms = 0;
  bool have_health = camera_rx_latest(&health, &age_ms);
  doc["health_valid"] = have_health;
  doc["health_state"] = have_health ? state_name(health.state) : "unknown";
  doc["health_conf"] = serialized(String(have_health ? health.confidence : 0.0f, 2));
  doc["health_age_s"] = have_health ? (int32_t)(age_ms / 1000UL) : -1;

  // Recent camera images (slot 0 = latest, slot 1 = previous)
  JsonArray imgs = doc["cam_images"].to<JsonArray>();
  for (int slot = 0; slot < 2; ++slot) {
    CameraImageMeta meta = {};
    if (camera_rx_get_image(slot, &meta, nullptr) && meta.valid) {
      JsonObject img = imgs.add<JsonObject>();
      img["slot"]  = slot;
      img["seq"]   = meta.seq;
      img["state"] = state_name(meta.state);
      img["conf"]  = serialized(String(meta.confidence, 2));
      img["age_s"] = (int32_t)(meta.age_ms / 1000UL);
      img["bytes"] = (uint32_t)meta.bytes;
    }
  }

  CameraLinkDiag diag = {};
  camera_rx_get_diag(&diag);
  JsonObject d_obj = doc["cam_diag"].to<JsonObject>();
  d_obj["cam_ip"]            = diag.cam_ip_str;
  d_obj["health_rx_count"]   = diag.health_rx_count;
  d_obj["img_complete_count"]= diag.img_complete_count;
  d_obj["last_img_seq"]      = diag.last_img_seq;
  d_obj["last_img_bytes"]    = (uint32_t)diag.last_img_bytes;

  // Control state
  doc["decision_reason"] = s_last_decision_reason;
  doc["cooldown_remaining_s"] = control_cooldown_remaining_s();

  // Network & MQTT status
  doc["ip"] = WiFi.localIP().toString();
  doc["mac"] = WiFi.macAddress();
  doc["channel"] = WiFi.channel();
  doc["rssi"] = WiFi.RSSI();
  doc["uptime_s"] = millis() / 1000UL;
  doc["mqtt_connected"] = mqtt_connected();
  doc["mqtt_rc"] = mqtt_last_rc();

  // Configuration
  JsonObject cfg = doc["config"].to<JsonObject>();
  cfg["auto_water_enabled"]   = g_cfg.auto_water_enabled;
  cfg["mqtt_enabled"]         = g_cfg.mqtt_enabled;
  cfg["raw_dry"]              = g_cfg.raw_dry;
  cfg["raw_wet"]              = g_cfg.raw_wet;
  cfg["ml_per_sec"]           = serialized(String(g_cfg.ml_per_sec, 2));
  cfg["water_dose_ml"]        = serialized(String(g_cfg.water_dose_ml, 1));
  cfg["pump_max_run_ms"]      = g_cfg.pump_max_run_ms;
  cfg["pump_cooldown_s"]      = g_cfg.pump_cooldown_s;
  cfg["moisture_low_pct"]     = serialized(String(g_cfg.moisture_low_pct, 1));
  cfg["moisture_high_pct"]    = serialized(String(g_cfg.moisture_high_pct, 1));
  cfg["wilt_conf_min"]        = serialized(String(g_cfg.wilt_conf_min, 2));
  cfg["sense_interval_s"]     = g_cfg.sense_interval_s;
  cfg["telemetry_interval_s"] = g_cfg.telemetry_interval_s;
  cfg["mqtt_host"]            = g_cfg.mqtt_host;
  cfg["mqtt_port"]            = g_cfg.mqtt_port;
  cfg["mqtt_user"]            = g_cfg.mqtt_user;
  cfg["mqtt_pass_set"]        = g_cfg.mqtt_pass[0] != '\0';  // never echo the password over HTTP
  cfg["mqtt_client_id"]       = g_cfg.mqtt_client_id;
  cfg["mqtt_topic_telemetry"] = g_cfg.mqtt_topic_telemetry;
  cfg["mqtt_topic_events"]    = g_cfg.mqtt_topic_events;
  cfg["mqtt_topic_status"]    = g_cfg.mqtt_topic_status;

  String out;
  serializeJson(doc, out);
  s_server.send(200, "application/json", out);
}

// Assigns a JSON field to dst when present (ArduinoJson converts the type). Absent/null fields
// leave the current value untouched, so the UI can send partial updates (e.g. a single toggle).
template <typename T>
static void update_field(JsonVariantConst v, T& dst) {
  if (!v.isNull()) dst = v.as<T>();
}

// String variant for the fixed-size char buffers in WateringConfig (truncates safely).
static void update_field(JsonVariantConst v, char* dst, size_t n) {
  const char* s = v.as<const char*>();
  if (s) snprintf(dst, n, "%s", s);
}

// True if any setting that requires an MQTT reconnect differs between a and b.
static bool mqtt_settings_differ(const WateringConfig& a, const WateringConfig& b) {
  return a.mqtt_enabled != b.mqtt_enabled || a.mqtt_port != b.mqtt_port ||
          strcmp(a.mqtt_host, b.mqtt_host) != 0 || strcmp(a.mqtt_user, b.mqtt_user) != 0 ||
          strcmp(a.mqtt_pass, b.mqtt_pass) != 0 || strcmp(a.mqtt_client_id, b.mqtt_client_id) != 0 ||
          strcmp(a.mqtt_topic_status, b.mqtt_topic_status) != 0;
}

// POST /api/config: "?reset=1" restores defaults, otherwise applies a (partial) JSON body.
static void handle_config_post() {
  WateringConfig before = g_cfg;

  if (s_server.hasArg("reset")) {
    config_reset_defaults();
  } else {
    JsonDocument doc;
    if (deserializeJson(doc, s_server.arg("plain"))) {
      s_server.send(400, "application/json", "{\"error\":\"invalid JSON\"}");
      return;
    }
    update_field(doc["auto_water_enabled"],   g_cfg.auto_water_enabled);
    update_field(doc["mqtt_enabled"],         g_cfg.mqtt_enabled);
    update_field(doc["raw_dry"],              g_cfg.raw_dry);
    update_field(doc["raw_wet"],              g_cfg.raw_wet);
    update_field(doc["ml_per_sec"],           g_cfg.ml_per_sec);
    update_field(doc["water_dose_ml"],        g_cfg.water_dose_ml);
    update_field(doc["moisture_low_pct"],     g_cfg.moisture_low_pct);
    update_field(doc["moisture_high_pct"],    g_cfg.moisture_high_pct);
    update_field(doc["wilt_conf_min"],        g_cfg.wilt_conf_min);
    update_field(doc["pump_cooldown_s"],      g_cfg.pump_cooldown_s);
    update_field(doc["pump_max_run_ms"],      g_cfg.pump_max_run_ms);
    update_field(doc["sense_interval_s"],     g_cfg.sense_interval_s);
    update_field(doc["telemetry_interval_s"], g_cfg.telemetry_interval_s);
    update_field(doc["mqtt_port"],            g_cfg.mqtt_port);
    update_field(doc["mqtt_host"],            g_cfg.mqtt_host,            sizeof(g_cfg.mqtt_host));
    update_field(doc["mqtt_user"],            g_cfg.mqtt_user,            sizeof(g_cfg.mqtt_user));
    update_field(doc["mqtt_pass"],            g_cfg.mqtt_pass,            sizeof(g_cfg.mqtt_pass));
    update_field(doc["mqtt_client_id"],       g_cfg.mqtt_client_id,       sizeof(g_cfg.mqtt_client_id));
    update_field(doc["mqtt_topic_telemetry"], g_cfg.mqtt_topic_telemetry, sizeof(g_cfg.mqtt_topic_telemetry));
    update_field(doc["mqtt_topic_events"],    g_cfg.mqtt_topic_events,    sizeof(g_cfg.mqtt_topic_events));
    update_field(doc["mqtt_topic_status"],    g_cfg.mqtt_topic_status,    sizeof(g_cfg.mqtt_topic_status));
  }

  config_sanitise();  // single validation point for both reset and user input
  config_save();
  if (mqtt_settings_differ(before, g_cfg)) mqtt_apply_config();
  send_status_json();
}

static void setup_web_server() {
  s_server.on("/", HTTP_GET, []() {
    s_server.send_P(200, "text/html", kIndexHtml);
  });

  s_server.on("/api/status", HTTP_GET, []() {
    send_status_json();
  });

  // Manual read only; never triggers automatic watering (used by calibration buttons).
  s_server.on("/api/measure", HTTP_POST, []() {
    read_moisture();
    send_status_json();
  });

  // Manual pump control: ?stop=1 | ?reset_total=1 | ?ml=<mL> | ?ms=<ms>
  s_server.on("/api/pump", HTTP_POST, []() {
    if (s_server.hasArg("stop")) {
      pump_stop();
    } else if (s_server.hasArg("reset_total")) {
      pump_reset_total_ml();
    } else if (s_server.hasArg("ml") || s_server.hasArg("ms")) {
      uint32_t ran_ms = 0;
      if (s_server.hasArg("ml")) {
        ran_ms = pump_run_ml(s_server.arg("ml").toFloat(), true);
      } else {
        long ms = s_server.arg("ms").toInt();
        if (ms > 0) ran_ms = pump_run_ms((uint32_t)ms, true);  // reject negatives before the cast
      }
      // Report the capped duration actually run, not the requested amount.
      if (ran_ms > 0) mqtt_publish_event("manual_webui", ran_ms, (ran_ms / 1000.0f) * g_cfg.ml_per_sec);
    }
    send_status_json();
  });

  s_server.on("/api/cooldown/reset", HTTP_POST, []() {
    control_reset_cooldown();
    send_status_json();
  });

  s_server.on("/api/mqtt/test", HTTP_POST, []() {
    publish_current_telemetry();
    send_status_json();
  });

  s_server.on("/api/camera/image", HTTP_GET, []() {
    int slot = s_server.hasArg("slot") ? s_server.arg("slot").toInt() : 0;
    CameraImageMeta meta = {};
    const uint8_t* jpg_buf = nullptr;
    if (!camera_rx_get_image(slot, &meta, &jpg_buf) || !jpg_buf || meta.bytes == 0) {
      s_server.send(404, "text/plain", "No image in requested slot");
      return;
    }
    s_server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    s_server.send_P(200, "image/jpeg", reinterpret_cast<const char*>(jpg_buf), meta.bytes);
  });

  s_server.on("/api/camera/capture", HTTP_POST, []() {
    camera_rx_request_capture();
    send_status_json();
  });

  // POST /health: local Wi-Fi endpoint for the camera node.
  // Supports both:
  //   1) Multipart JPEG + query args (?state=1&conf=0.98&seq=12&inf_ms=14&ip=192.168.x.x)
  //   2) JSON body {"state":"wilted","conf":0.87,"seq":12,"inference_ms":14,"cam_ip":"..."}
  auto health_finish_fn = []() {
    uint8_t state = PLANT_UNKNOWN;
    float conf = 0.0f;
    uint32_t seq = 0;
    uint32_t inf_ms = 0;
    String cam_ip = s_server.client().remoteIP().toString();

    if (s_server.hasArg("plain") && s_server.arg("plain").length() > 0) {
      JsonDocument doc;
      if (!deserializeJson(doc, s_server.arg("plain"))) {
        if (doc["state"].is<const char*>()) {
          String st = doc["state"].as<String>();
          st.toLowerCase();
          if (st == "wilted") state = PLANT_WILTED;
          else if (st == "healthy") state = PLANT_HEALTHY;
        } else if (doc["state"].is<int>()) {
          state = (uint8_t)doc["state"].as<int>();
        }
        conf = doc["conf"] | (doc["confidence"] | 0.0f);
        seq = doc["seq"] | 0U;
        inf_ms = doc["inference_ms"] | 0U;
        if (doc["cam_ip"].is<const char*>()) {
          cam_ip = doc["cam_ip"].as<String>();
        }
      }
    } else {
      if (s_server.hasArg("state")) {
        String st = s_server.arg("state");
        st.toLowerCase();
        if (st == "wilted" || st == "1") state = PLANT_WILTED;
        else if (st == "healthy" || st == "0") state = PLANT_HEALTHY;
      }
      if (s_server.hasArg("conf")) conf = s_server.arg("conf").toFloat();
      if (s_server.hasArg("seq")) seq = (uint32_t)s_server.arg("seq").toInt();
      if (s_server.hasArg("inf_ms")) inf_ms = (uint32_t)s_server.arg("inf_ms").toInt();
      if (s_server.hasArg("ip")) cam_ip = s_server.arg("ip");
    }

    camera_rx_ingest_http_health(state, conf, seq, inf_ms, cam_ip.c_str());
    camera_rx_http_upload_end(state, conf, seq);
    s_server.send(200, "application/json", "{\"ok\":true}");
  };

  auto health_upload_fn = []() {
    HTTPUpload& upload = s_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      camera_rx_http_upload_begin();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      camera_rx_http_upload_write(upload.buf, upload.currentSize);
    }
  };

  s_server.on("/health", HTTP_POST, health_finish_fn, health_upload_fn);
  s_server.on("/api/camera/push", HTTP_POST, health_finish_fn, health_upload_fn);

  // Multipart HTTP OTA firmware upload endpoint (/api/ota)
  s_server.on(
      "/api/ota", HTTP_POST,
      []() {
        bool ok = !Update.hasError();
        s_server.sendHeader("Connection", "close");
        s_server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
        if (ok) {
          delay(400);
          ESP.restart();
        }
      },
      []() {
        HTTPUpload& upload = s_server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          pump_stop();  // Safety: always turn off pump before flashing
          Serial.printf("[ota] Receiving firmware: %s\n", upload.filename.c_str());
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

  s_server.on("/api/config", HTTP_POST, handle_config_post);

  s_server.begin();
  Serial.printf("[webui] HTTP server listening on http://%s.local/ (http://%s/)\n",
                MDNS_HOSTNAME, WiFi.localIP().toString().c_str());
}

static void print_serial_menu() {
  Serial.println("\n=== Watering Node Serial & WebUI Interface ===");
  Serial.printf("  WebUI : http://%s.local/ or http://%s/\n",
                MDNS_HOSTNAME, WiFi.localIP().toString().c_str());
  Serial.println("  'm'   : Read moisture sensor now");
  Serial.println("  'p'   : Run pump for 2000 ms");
  Serial.println("  '1'   : Run pump for 1000 ms");
  Serial.println("  '3'   : Run pump for 3000 ms");
  Serial.println("  '5'   : Run pump for 5000 ms");
  Serial.println("  '0'/'s': Stop pump immediately");
  Serial.println("  '?'   : Show this menu");
  Serial.println("==============================================\n");
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n[watering-node] Initialising Smart Irrigation Watering Node...");

  config_load();
  moisture_init();
  pump_init();
  control_init();

  // Connect Wi-Fi (and MQTT if enabled in config)
  mqtt_init();

  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }

  camera_rx_init();
  camera_rx_request_capture();  // Request an immediate initial frame once Wi-Fi + HTTP are up
  setup_web_server();

  // Initial reading only. The first automatic decision waits one sense interval so a
  // brownout/reset loop (e.g. pump inrush) cannot re-water on every boot (cooldown is in RAM).
  read_moisture();
  s_last_control_ms = millis();
  s_last_telemetry_ms = millis();

  print_serial_menu();
}

void loop() {
  uint32_t now = millis();

  pump_update();
  s_server.handleClient();
  camera_rx_poll();
  mqtt_loop(!pump_is_on());  // defer blocking reconnects so they cannot delay pump shut-off

  // 1. Periodic sensing (always, so telemetry stays fresh) and control (acts only when auto is on).
  if (now - s_last_control_ms >= g_cfg.sense_interval_s * 1000UL) {
    s_last_control_ms = now;
    sense_and_control();
  }

  // 2. Periodic MQTT telemetry (only active when mqtt_enabled is true)
  if (g_cfg.mqtt_enabled && (now - s_last_telemetry_ms >= g_cfg.telemetry_interval_s * 1000UL)) {
    s_last_telemetry_ms = now;
    publish_current_telemetry();
  }

  // 3. Serial console commands
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == 'm' || ch == 'M') {
      read_moisture();
    } else if (ch == 'p' || ch == 'P') {
      pump_run_ms(2000, true);
    } else if (ch == '1') {
      pump_run_ms(1000, true);
    } else if (ch == '3') {
      pump_run_ms(3000, true);
    } else if (ch == '5') {
      pump_run_ms(5000, true);
    } else if (ch == '0' || ch == 's' || ch == 'S') {
      pump_stop();
    } else if (ch == '?' || ch == 'h' || ch == 'H') {
      print_serial_menu();
    }
  }

  delay(5);
}
