// main.cpp
// Purpose: Watering node main firmware with WebUI (http://basil-water.local/),
//          NVS-persisted calibration/thresholds, manual mL pump dosing,
//          toggleable automatic irrigation, ESP-NOW receiver, and runtime-configurable MQTT.
// OWNER:   Caleb
// Node:    watering node (ESP32 DevKitC)

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "moisture.h"
#include "pump.h"
#include "control.h"
#include "espnow_rx.h"
#include "mqtt_pub.h"
#include "plant_msgs.h"

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
  bool have_health = espnow_rx_latest(&health, &age_ms);

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
  bool have_health = espnow_rx_latest(&health, &age_ms);

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

  // Camera / ESP-NOW state
  PlantHealthMsg health;
  uint32_t age_ms = 0;
  bool have_health = espnow_rx_latest(&health, &age_ms);
  doc["health_valid"] = have_health;
  doc["health_state"] = have_health ? state_name(health.state) : "unknown";
  doc["health_conf"] = serialized(String(have_health ? health.confidence : 0.0f, 2));
  doc["health_age_s"] = have_health ? (int32_t)(age_ms / 1000UL) : -1;

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

  String out;
  serializeJson(doc, out);
  s_server.send(200, "application/json", out);
}

static const char kIndexHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Basil Watering Node</title>
<style>
  :root {
    --bg: #0f172a; --card: #1e293b; --border: #334155;
    --text: #f8fafc; --muted: #94a3b8; --accent: #38bdf8;
    --green: #22c55e; --amber: #f59e0b; --red: #ef4444;
  }
  * { box-sizing: border-box; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background: var(--bg); color: var(--text); margin: 0; padding: 20px;
  }
  .container { max-width: 1000px; margin: 0 auto; }
  header {
    display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center;
    gap: 12px; margin-bottom: 20px; padding-bottom: 14px; border-bottom: 1px solid var(--border);
  }
  h1 { font-size: 1.4rem; margin: 0; display: flex; align-items: center; gap: 10px; }
  h2 { font-size: 1.05rem; margin: 0 0 14px 0; color: var(--accent); }
  .meta { font-size: 0.82rem; color: var(--muted); }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 16px; margin-bottom: 16px; }
  .card {
    background: var(--card); border: 1px solid var(--border);
    border-radius: 10px; padding: 16px;
  }
  .big-val { font-size: 2rem; font-weight: 700; line-height: 1.1; }
  .sub-val { font-size: 0.85rem; color: var(--muted); margin-top: 4px; }
  .bar-wrap { width: 100%; height: 10px; background: #0f172a; border-radius: 5px; overflow: hidden; margin: 12px 0; }
  .bar-fill { height: 100%; background: var(--green); width: 0%; transition: width 0.3s, background 0.3s; }
  .btn-row { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 12px; }
  button {
    background: #334155; color: var(--text); border: 1px solid #475569;
    padding: 8px 13px; border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 0.85rem;
  }
  button:hover { background: #475569; }
  button.primary { background: #0284c7; border-color: #38bdf8; }
  button.primary:hover { background: #0369a1; }
  button.success { background: #16a34a; border-color: #22c55e; }
  button.success:hover { background: #15803d; }
  button.danger { background: #dc2626; border-color: #ef4444; }
  button.danger:hover { background: #b91c1c; }
  .input-inline {
    display: flex; align-items: center; gap: 8px; margin-top: 10px;
  }
  input[type="number"], input[type="text"], input[type="password"] {
    background: #0f172a; color: var(--text); border: 1px solid var(--border);
    border-radius: 6px; padding: 7px 10px; font-size: 0.9rem; width: 100%;
  }
  .form-grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 12px;
  }
  label { display: block; font-size: 0.8rem; color: var(--muted); margin-bottom: 4px; }
  .badge {
    display: inline-block; padding: 3px 9px; border-radius: 999px;
    font-size: 0.75rem; font-weight: 700; text-transform: uppercase;
  }
  .badge-on { background: rgba(34,197,94,0.2); color: #4ade80; border: 1px solid #22c55e; }
  .badge-off { background: rgba(148,163,184,0.15); color: #94a3b8; border: 1px solid #475569; }
  .badge-warn { background: rgba(245,158,11,0.2); color: #fbbf24; border: 1px solid #f59e0b; }
  .kv-row { display: flex; justify-content: space-between; font-size: 0.86rem; padding: 5px 0; border-bottom: 1px solid rgba(255,255,255,0.05); }
  .toast {
    position: fixed; bottom: 20px; right: 20px; background: #0284c7; color: #fff;
    padding: 10px 16px; border-radius: 8px; font-weight: 600; font-size: 0.88rem;
    display: none; box-shadow: 0 4px 12px rgba(0,0,0,0.4); z-index: 100;
  }
</style>
</head>
<body>
<div class="container">
  <header>
    <div>
      <h1>🌿 Basil Watering Node</h1>
      <div class="meta" id="net-meta">Connecting...</div>
    </div>
    <div style="display:flex;gap:8px;align-items:center;">
      <span id="auto-badge" class="badge badge-off">AUTO: OFF</span>
      <span id="mqtt-badge" class="badge badge-off">MQTT: OFF</span>
    </div>
  </header>

  <div class="grid">
    <!-- Soil Moisture Card -->
    <div class="card">
      <h2>Soil Moisture Sensor</h2>
      <div style="display:flex;justify-content:space-between;align-items:baseline;">
        <div class="big-val" id="m-pct">--.- %</div>
        <div style="font-size:1.1rem;font-weight:600;color:var(--accent);">Raw: <span id="m-raw">----</span></div>
      </div>
      <div class="bar-wrap"><div class="bar-fill" id="m-bar"></div></div>
      <div class="sub-val" id="m-age">Last read: never</div>
      <div class="btn-row">
        <button class="primary" onclick="triggerMeasure()">⚡ Measure Now (15ms Pulse)</button>
      </div>
      <div class="btn-row">
        <button onclick="setRawCal('dry')" title="Save current Raw ADC as Dry">Set Current Raw as DRY</button>
        <button onclick="setRawCal('wet')" title="Save current Raw ADC as WET">Set Current Raw as WET</button>
      </div>
    </div>

    <!-- Pump Control Card -->
    <div class="card">
      <h2>Peristaltic Pump Control</h2>
      <div style="display:flex;justify-content:space-between;align-items:baseline;">
        <div class="big-val" id="p-state">IDLE</div>
        <div style="font-size:1.05rem;color:var(--accent);">Total: <span id="p-total">0.0</span> mL</div>
      </div>
      <div class="sub-val" id="p-rem">Ready for manual or automatic dosing</div>

      <div class="btn-row">
        <button class="primary" onclick="runPumpMl(5)">+5 mL</button>
        <button class="primary" onclick="runPumpMl(10)">+10 mL</button>
        <button class="primary" onclick="runPumpMl(25)">+25 mL</button>
        <button class="primary" onclick="runPumpMl(50)">+50 mL</button>
        <button class="danger" onclick="stopPump()">⏹ STOP</button>
      </div>

      <div class="input-inline">
        <input type="number" id="custom-ml" value="15" min="1" max="300" step="0.5" style="width:95px;">
        <button class="primary" onclick="runCustomMl()">Pump Custom mL</button>
        <button onclick="resetTotalMl()">Reset Total</button>
      </div>

      <hr style="border:0;border-top:1px solid var(--border);margin:12px 0;">
      <div style="font-size:0.8rem;color:var(--muted);margin-bottom:6px;">Flow Rate Calibration Helper:</div>
      <div class="input-inline" style="margin-top:4px;">
        <button onclick="runPumpMs(5000)">1. Run 5.0s</button>
        <input type="number" id="cal-measured-ml" placeholder="mL collected" step="0.1" style="width:115px;">
        <button onclick="saveFlowCal()">2. Save mL/s</button>
      </div>
    </div>

    <!-- Automation & ESP-NOW Card -->
    <div class="card">
      <h2>Automation &amp; Camera Link</h2>
      <div class="kv-row">
        <span>Automatic Watering</span>
        <button id="btn-toggle-auto" onclick="toggleAutoWater()">Enable Auto Water</button>
      </div>
      <div class="kv-row">
        <span>Control Status</span>
        <strong id="c-reason">--</strong>
      </div>
      <div class="kv-row">
        <span>Cooldown Remaining</span>
        <span><strong id="c-cooldown">0s</strong> <button style="padding:3px 7px;font-size:0.75rem;margin-left:6px;" onclick="resetCooldown()">Clear</button></span>
      </div>
      <div class="kv-row">
        <span>Camera Health (ESP-NOW)</span>
        <strong id="cam-state">unknown</strong>
      </div>
      <div class="kv-row">
        <span>Camera Confidence / Age</span>
        <span id="cam-meta">--</span>
      </div>
      <div class="kv-row">
        <span>Watering Node MAC</span>
        <code id="node-mac" style="color:var(--accent);">--</code>
      </div>
    </div>
  </div>

  <!-- Calibration & Thresholds Form -->
  <div class="card" style="margin-bottom:16px;">
    <h2>Sensor Calibration &amp; Watering Thresholds (Persisted in Flash)</h2>
    <div class="form-grid">
      <div>
        <label>Dry Raw ADC (0%)</label>
        <input type="number" id="cfg-raw-dry">
      </div>
      <div>
        <label>Wet Raw ADC (100%)</label>
        <input type="number" id="cfg-raw-wet">
      </div>
      <div>
        <label>Pump Flow Rate (mL / sec)</label>
        <input type="number" step="0.05" id="cfg-ml-sec">
      </div>
      <div>
        <label>Auto Water Dose (mL)</label>
        <input type="number" step="0.5" id="cfg-dose-ml">
      </div>
      <div>
        <label>Dry Threshold (% to start)</label>
        <input type="number" step="0.5" id="cfg-low-pct">
      </div>
      <div>
        <label>Wet Limit (% never water)</label>
        <input type="number" step="0.5" id="cfg-high-pct">
      </div>
      <div>
        <label>Min Wilt Confidence (0-1)</label>
        <input type="number" step="0.05" min="0" max="1" id="cfg-wilt-conf">
      </div>
      <div>
        <label>Soak Cooldown (seconds)</label>
        <input type="number" id="cfg-cooldown-s">
      </div>
      <div>
        <label>Max Auto Pump Cap (ms)</label>
        <input type="number" step="500" id="cfg-max-run-ms">
      </div>
      <div>
        <label>Auto Sense Interval (seconds)</label>
        <input type="number" min="5" id="cfg-sense-int-s">
      </div>
    </div>
    <div class="btn-row" style="margin-top:14px;">
      <button class="success" onclick="saveConfig()">💾 Save Calibration &amp; Thresholds</button>
      <button onclick="resetDefaults()">Restore Defaults</button>
    </div>
  </div>

  <!-- MQTT Config Card -->
  <div class="card">
    <h2>MQTT Broker Configuration</h2>
    <div class="form-grid">
      <div>
        <label>MQTT Telemetry</label>
        <button id="btn-toggle-mqtt" onclick="toggleMqtt()" style="width:100%;">Enable MQTT</button>
      </div>
      <div>
        <label>Broker Host / IP</label>
        <input type="text" id="cfg-mqtt-host">
      </div>
      <div>
        <label>Port (1883 plain / 8883 TLS)</label>
        <input type="number" id="cfg-mqtt-port">
      </div>
      <div>
        <label>Username</label>
        <input type="text" id="cfg-mqtt-user">
      </div>
      <div>
        <label>Password</label>
        <input type="password" id="cfg-mqtt-pass">
      </div>
      <div>
        <label>Client ID</label>
        <input type="text" id="cfg-mqtt-cid">
      </div>
      <div>
        <label>Telemetry Interval (seconds)</label>
        <input type="number" min="2" id="cfg-telem-int-s">
      </div>
    </div>
    <div class="btn-row" style="margin-top:14px;">
      <button class="success" onclick="saveConfig()">💾 Save MQTT Settings</button>
      <button class="primary" onclick="testMqttPublish()">📡 Publish Test Telemetry Now</button>
    </div>
  </div>
</div>

<div class="toast" id="toast">Saved!</div>

<script>
let lastState = null;
let formLoaded = false;

function showToast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.style.display = 'block';
  setTimeout(() => { t.style.display = 'none'; }, 2400);
}

function populateForm(cfg) {
  document.getElementById('cfg-raw-dry').value = cfg.raw_dry;
  document.getElementById('cfg-raw-wet').value = cfg.raw_wet;
  document.getElementById('cfg-ml-sec').value = cfg.ml_per_sec;
  document.getElementById('cfg-dose-ml').value = cfg.water_dose_ml;
  document.getElementById('cfg-low-pct').value = cfg.moisture_low_pct;
  document.getElementById('cfg-high-pct').value = cfg.moisture_high_pct;
  document.getElementById('cfg-wilt-conf').value = cfg.wilt_conf_min;
  document.getElementById('cfg-cooldown-s').value = cfg.pump_cooldown_s;
  document.getElementById('cfg-max-run-ms').value = cfg.pump_max_run_ms;
  document.getElementById('cfg-sense-int-s').value = cfg.sense_interval_s;
  document.getElementById('cfg-mqtt-host').value = cfg.mqtt_host;
  document.getElementById('cfg-mqtt-port').value = cfg.mqtt_port;
  document.getElementById('cfg-mqtt-user').value = cfg.mqtt_user;
  // Password is write-only: the field stays blank and is only sent when the user types a new one.
  const pass = document.getElementById('cfg-mqtt-pass');
  pass.value = '';
  pass.placeholder = cfg.mqtt_pass_set ? '(unchanged)' : '(none)';
  document.getElementById('cfg-mqtt-cid').value = cfg.mqtt_client_id;
  document.getElementById('cfg-telem-int-s').value = cfg.telemetry_interval_s;
}

function renderStatus(d) {
  lastState = d;
  document.getElementById('net-meta').textContent =
    `IP: ${d.ip} | Ch: ${d.channel} | RSSI: ${d.rssi} dBm | Uptime: ${d.uptime_s}s`;
  document.getElementById('node-mac').textContent = d.mac;

  // Moisture
  document.getElementById('m-pct').textContent = `${d.moisture_pct.toFixed(1)} %`;
  document.getElementById('m-raw').textContent = d.moisture_raw;
  document.getElementById('m-age').textContent =
    d.moisture_age_s >= 0 ? `Last pulsed read: ${d.moisture_age_s}s ago` : 'No sensor read yet (click Measure Now)';

  const bar = document.getElementById('m-bar');
  bar.style.width = `${Math.min(100, Math.max(0, d.moisture_pct))}%`;
  if (d.moisture_pct < d.config.moisture_low_pct) bar.style.background = 'var(--red)';
  else if (d.moisture_pct >= d.config.moisture_high_pct) bar.style.background = 'var(--accent)';
  else bar.style.background = 'var(--green)';

  // Pump
  const pState = document.getElementById('p-state');
  if (d.pump_on) {
    pState.textContent = 'PUMPING';
    pState.style.color = 'var(--green)';
    document.getElementById('p-rem').textContent = `Remaining: ${(d.pump_remaining_ms / 1000).toFixed(1)} s`;
  } else {
    pState.textContent = 'IDLE';
    pState.style.color = 'var(--text)';
    document.getElementById('p-rem').textContent = `Flow rate: ${d.config.ml_per_sec} mL/s`;
  }
  document.getElementById('p-total').textContent = d.water_ml_total.toFixed(1);

  // Auto & Control
  const autoOn = d.config.auto_water_enabled;
  const autoBadge = document.getElementById('auto-badge');
  autoBadge.textContent = autoOn ? 'AUTO: ON' : 'AUTO: OFF';
  autoBadge.className = 'badge ' + (autoOn ? 'badge-on' : 'badge-off');

  const btnAuto = document.getElementById('btn-toggle-auto');
  btnAuto.textContent = autoOn ? 'Disable Auto Water' : 'Enable Auto Water';
  btnAuto.className = autoOn ? 'danger' : 'success';

  document.getElementById('c-reason').textContent = d.decision_reason;
  document.getElementById('c-cooldown').textContent = `${d.cooldown_remaining_s}s`;

  // Camera
  document.getElementById('cam-state').textContent = d.health_state;
  document.getElementById('cam-meta').textContent =
    d.health_valid ? `${(d.health_conf * 100).toFixed(0)}% conf (${d.health_age_s}s ago)` : 'No packets received';

  // MQTT
  const mqttEn = d.config.mqtt_enabled;
  const mqttBadge = document.getElementById('mqtt-badge');
  if (!mqttEn) {
    mqttBadge.textContent = 'MQTT: OFF';
    mqttBadge.className = 'badge badge-off';
  } else if (d.mqtt_connected) {
    mqttBadge.textContent = 'MQTT: CONNECTED';
    mqttBadge.className = 'badge badge-on';
  } else {
    mqttBadge.textContent = `MQTT: DISCONNECTED (rc=${d.mqtt_rc})`;
    mqttBadge.className = 'badge badge-warn';
  }

  const btnMqtt = document.getElementById('btn-toggle-mqtt');
  btnMqtt.textContent = mqttEn ? 'MQTT Enabled (Click to Disable)' : 'MQTT Disabled (Click to Enable)';
  btnMqtt.className = mqttEn ? 'success' : '';

  if (!formLoaded) {
    populateForm(d.config);
    formLoaded = true;
  }
}

async function fetchStatus() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    renderStatus(d);
  } catch (e) {}
}

async function triggerMeasure() {
  const r = await fetch('/api/measure', { method: 'POST' });
  const d = await r.json();
  renderStatus(d);
  showToast(`Measured: Raw ${d.moisture_raw} (${d.moisture_pct.toFixed(1)}%)`);
}

async function runPumpMl(ml) {
  const r = await fetch(`/api/pump?ml=${ml}`, { method: 'POST' });
  const d = await r.json();
  renderStatus(d);
  showToast(`Pumping ${ml} mL...`);
}

async function runCustomMl() {
  const ml = parseFloat(document.getElementById('custom-ml').value);
  if (ml > 0) await runPumpMl(ml);
}

async function runPumpMs(ms) {
  const r = await fetch(`/api/pump?ms=${ms}`, { method: 'POST' });
  const d = await r.json();
  renderStatus(d);
  showToast(`Running pump for ${(ms/1000).toFixed(1)} s...`);
}

async function stopPump() {
  const r = await fetch('/api/pump?stop=1', { method: 'POST' });
  const d = await r.json();
  renderStatus(d);
  showToast('Pump stopped');
}

async function resetTotalMl() {
  const r = await fetch('/api/pump?reset_total=1', { method: 'POST' });
  const d = await r.json();
  renderStatus(d);
  showToast('Total volume reset');
}

async function saveFlowCal() {
  const measured = parseFloat(document.getElementById('cal-measured-ml').value);
  if (!measured || measured <= 0) {
    alert('Run the pump for 5.0s first, then enter the mL collected.');
    return;
  }
  const rate = +(measured / 5.0).toFixed(2);
  document.getElementById('cfg-ml-sec').value = rate;
  await saveConfig();
}

async function setRawCal(kind) {
  const rRead = await fetch('/api/measure', { method: 'POST' });
  const dRead = await rRead.json();
  if (kind === 'dry') document.getElementById('cfg-raw-dry').value = dRead.moisture_raw;
  else document.getElementById('cfg-raw-wet').value = dRead.moisture_raw;
  await saveConfig();
}

async function toggleAutoWater() {
  if (!lastState) return;
  const next = !lastState.config.auto_water_enabled;
  const r = await fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ auto_water_enabled: next })
  });
  const d = await r.json();
  renderStatus(d);
  showToast(`Auto Watering ${next ? 'ENABLED' : 'DISABLED'}`);
}

async function toggleMqtt() {
  if (!lastState) return;
  const next = !lastState.config.mqtt_enabled;
  const r = await fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ mqtt_enabled: next })
  });
  const d = await r.json();
  renderStatus(d);
  showToast(`MQTT ${next ? 'ENABLED' : 'DISABLED'}`);
}

async function saveConfig() {
  const payload = {
    raw_dry: parseInt(document.getElementById('cfg-raw-dry').value, 10),
    raw_wet: parseInt(document.getElementById('cfg-raw-wet').value, 10),
    ml_per_sec: parseFloat(document.getElementById('cfg-ml-sec').value),
    water_dose_ml: parseFloat(document.getElementById('cfg-dose-ml').value),
    moisture_low_pct: parseFloat(document.getElementById('cfg-low-pct').value),
    moisture_high_pct: parseFloat(document.getElementById('cfg-high-pct').value),
    wilt_conf_min: parseFloat(document.getElementById('cfg-wilt-conf').value),
    pump_cooldown_s: parseInt(document.getElementById('cfg-cooldown-s').value, 10),
    pump_max_run_ms: parseInt(document.getElementById('cfg-max-run-ms').value, 10),
    sense_interval_s: parseInt(document.getElementById('cfg-sense-int-s').value, 10),
    mqtt_host: document.getElementById('cfg-mqtt-host').value.trim(),
    mqtt_port: parseInt(document.getElementById('cfg-mqtt-port').value, 10),
    mqtt_user: document.getElementById('cfg-mqtt-user').value.trim(),
    mqtt_client_id: document.getElementById('cfg-mqtt-cid').value.trim(),
    telemetry_interval_s: parseInt(document.getElementById('cfg-telem-int-s').value, 10)
  };
  const pass = document.getElementById('cfg-mqtt-pass').value;
  if (pass) payload.mqtt_pass = pass;
  const r = await fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  const d = await r.json();
  formLoaded = false;
  renderStatus(d);
  showToast('Configuration saved to NVS flash');
}

async function resetDefaults() {
  if (!confirm('Restore all calibration and threshold settings to defaults?')) return;
  const r = await fetch('/api/config?reset=1', { method: 'POST' });
  const d = await r.json();
  formLoaded = false;
  renderStatus(d);
  showToast('Defaults restored');
}

async function resetCooldown() {
  const r = await fetch('/api/cooldown/reset', { method: 'POST' });
  const d = await r.json();
  renderStatus(d);
  showToast('Cooldown cleared');
}

async function testMqttPublish() {
  const r = await fetch('/api/mqtt/test', { method: 'POST' });
  const d = await r.json();
  renderStatus(d);
  showToast(d.mqtt_connected ? 'Telemetry published!' : 'MQTT not connected');
}

fetchStatus();
setInterval(fetchStatus, 1500);
</script>
</body>
</html>)rawliteral";

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
         strcmp(a.mqtt_pass, b.mqtt_pass) != 0 || strcmp(a.mqtt_client_id, b.mqtt_client_id) != 0;
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
    update_field(doc["mqtt_host"],      g_cfg.mqtt_host,      sizeof(g_cfg.mqtt_host));
    update_field(doc["mqtt_user"],      g_cfg.mqtt_user,      sizeof(g_cfg.mqtt_user));
    update_field(doc["mqtt_pass"],      g_cfg.mqtt_pass,      sizeof(g_cfg.mqtt_pass));
    update_field(doc["mqtt_client_id"], g_cfg.mqtt_client_id, sizeof(g_cfg.mqtt_client_id));
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

  espnow_rx_init();
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
  mqtt_loop(!pump_is_on());  // defer blocking reconnects so they cannot delay pump shut-off
  espnow_rx_check_channel();

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
