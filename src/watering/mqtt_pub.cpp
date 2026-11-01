// mqtt_pub.cpp
// Purpose: Wi-Fi + runtime-configurable PubSubClient with auto-reconnect, last will, and JSON publishing.
// OWNER:   Caleb (publish code), Aida (topic/payload design)
// Node:    watering node (ESP32 DevKitC)

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mqtt_pub.h"
#include "config.h"
#include "plant_msgs.h"

static WiFiClient s_net_plain;        // used for plain MQTT (any port except 8883)
static WiFiClientSecure s_net_tls;    // used for MQTT over TLS (port 8883)
static PubSubClient s_mqtt;
static uint32_t s_last_attempt_ms = 0;
static int s_last_rc = 0;             // PubSubClient state() from the last failed connect (0 = ok)

static const uint32_t kWifiTimeoutMs = 15000;
static const uint32_t kReconnectMs = 5000;

// Single connection attempt with a retained "offline" last will on the status topic.
static bool mqtt_connect() {
  if (!g_cfg.mqtt_enabled || strlen(g_cfg.mqtt_host) == 0) return false;

  Serial.printf("[mqtt] connecting to %s:%u ... ", g_cfg.mqtt_host, g_cfg.mqtt_port);
  bool ok = false;
  if (strlen(g_cfg.mqtt_user) > 0) {
    ok = s_mqtt.connect(g_cfg.mqtt_client_id, g_cfg.mqtt_user, g_cfg.mqtt_pass,
                        MQTT_TOPIC_STATUS, 1, true, "offline");
  } else {
    ok = s_mqtt.connect(g_cfg.mqtt_client_id,
                        MQTT_TOPIC_STATUS, 1, true, "offline");
  }

  if (ok) {
    s_last_rc = 0;
    s_mqtt.publish(MQTT_TOPIC_STATUS, "online", true);
    Serial.println("ok");
  } else {
    s_last_rc = s_mqtt.state();
    Serial.printf("failed, state=%d\n", s_last_rc);
  }
  return ok;
}

void mqtt_apply_config() {
  if (s_mqtt.connected()) {
    s_mqtt.publish(MQTT_TOPIC_STATUS, "offline", true);
    s_mqtt.disconnect();
  }

  if (!g_cfg.mqtt_enabled) {
    Serial.println("[mqtt] Disabled via configuration.");
    return;
  }

  // Port 8883 selects TLS. setInsecure() skips broker certificate validation (encryption only).
  if (g_cfg.mqtt_port == 8883) {
    s_net_tls.setInsecure();
    s_mqtt.setClient(s_net_tls);
  } else {
    s_mqtt.setClient(s_net_plain);
  }

  s_mqtt.setServer(g_cfg.mqtt_host, g_cfg.mqtt_port);
  s_mqtt.setBufferSize(512);
  s_last_attempt_ms = millis() - kReconnectMs; // trigger immediate connect attempt in loop
}

void mqtt_init() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // Modem sleep causes ESP-NOW packet loss and slower WebUI response
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] connecting to %s", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < kWifiTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" ok, IP %s, channel %d\n", WiFi.localIP().toString().c_str(), WiFi.channel());
  } else {
    Serial.println(" timed out, will keep retrying in background");
  }
  WiFi.setAutoReconnect(true);

  mqtt_apply_config();
}

void mqtt_loop(bool allow_reconnect) {
  if (!g_cfg.mqtt_enabled) return;
  if (WiFi.status() != WL_CONNECTED) return;

  if (!s_mqtt.connected()) {
    // connect() blocks (TCP + optional TLS handshake), so it is throttled and can be deferred.
    if (allow_reconnect && millis() - s_last_attempt_ms >= kReconnectMs) {
      s_last_attempt_ms = millis();
      mqtt_connect();
    }
    return;
  }
  s_mqtt.loop();
}

bool mqtt_connected() {
  return g_cfg.mqtt_enabled && s_mqtt.connected();
}

int mqtt_last_rc() {
  return s_last_rc;
}

void mqtt_publish_telemetry(const Telemetry& t) {
  if (!g_cfg.mqtt_enabled || !s_mqtt.connected()) return;
  JsonDocument doc;
  doc["moisture_pct"] = serialized(String(t.moisture_pct, 1));
  doc["moisture_raw"] = t.moisture_raw;
  doc["pump"] = t.pump_on ? 1 : 0;
  doc["health_state"] = t.health_state;
  doc["health_conf"] = serialized(String(t.health_conf, 2));
  doc["health_age_s"] = t.health_age_s;
  doc["water_ml_total"] = serialized(String(t.water_ml_total, 1));

  char buf[384];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  s_mqtt.publish(MQTT_TOPIC_TELEMETRY, reinterpret_cast<const uint8_t*>(buf), n);
  Serial.printf("[mqtt] %s %s\n", MQTT_TOPIC_TELEMETRY, buf);
}

void mqtt_publish_event(const char* reason, uint32_t duration_ms, float ml) {
  if (!g_cfg.mqtt_enabled || !s_mqtt.connected()) return;
  JsonDocument doc;
  doc["event"] = "pump_run";
  doc["reason"] = reason;
  doc["duration_ms"] = duration_ms;
  doc["ml"] = serialized(String(ml, 1));

  char buf[256];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  s_mqtt.publish(MQTT_TOPIC_EVENTS, reinterpret_cast<const uint8_t*>(buf), n);
  Serial.printf("[mqtt] %s %s\n", MQTT_TOPIC_EVENTS, buf);
}
