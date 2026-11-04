// Embedded PROGMEM WebUI for the watering node (http://basil-water.local/),
// split into Monitoring & Control and Configuration & OTA tabs.

#pragma once
#include <Arduino.h>

static const char kIndexHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Basil Watering Node</title>
<style>
  :root {
    --bg: #121417;
    --card: #181b20;
    --border: #262b33;
    --text: #e2e8f0;
    --muted: #8b95a5;
    --accent: #6b90b2;
    --green: #5c997b;
    --amber: #b89050;
    --red: #b06060;
  }
  * { box-sizing: border-box; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background: var(--bg); color: var(--text); margin: 0; padding: 24px 16px;
    line-height: 1.45;
  }
  .container { max-width: 980px; margin: 0 auto; }
  header {
    display: flex; flex-wrap: wrap; justify-content: space-between; align-items: baseline;
    gap: 12px; margin-bottom: 16px; padding-bottom: 12px; border-bottom: 1px solid var(--border);
  }
  h1 { font-size: 1.25rem; font-weight: 600; margin: 0; letter-spacing: -0.01em; }
  h2 { font-size: 0.95rem; font-weight: 600; margin: 0 0 12px 0; color: var(--text); }
  .meta { font-size: 0.8rem; color: var(--muted); }
  .tabs {
    display: flex; gap: 20px; margin-bottom: 20px; border-bottom: 1px solid var(--border);
  }
  .tab-btn {
    background: transparent; color: var(--muted); border: none;
    border-bottom: 2px solid transparent; border-radius: 0;
    padding: 8px 2px; cursor: pointer; font-weight: 500; font-size: 0.9rem;
  }
  .tab-btn:hover { color: var(--text); background: transparent; }
  .tab-btn.active {
    color: var(--text); border-bottom-color: var(--accent); background: transparent;
  }
  .tab-pane { display: none; }
  .tab-pane.active { display: block; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(290px, 1fr)); gap: 14px; margin-bottom: 14px; }
  .card {
    background: var(--card); border: 1px solid var(--border);
    border-radius: 4px; padding: 16px;
  }
  .big-val { font-size: 1.75rem; font-weight: 600; line-height: 1.1; }
  .sub-val { font-size: 0.82rem; color: var(--muted); margin-top: 4px; }
  .bar-wrap { width: 100%; height: 6px; background: var(--bg); border-radius: 3px; overflow: hidden; margin: 12px 0; border: 1px solid var(--border); }
  .bar-fill { height: 100%; background: var(--green); width: 0%; transition: width 0.3s, background 0.3s; }
  .btn-row { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 12px; }
  button {
    background: #20242b; color: var(--text); border: 1px solid #323944;
    padding: 7px 12px; border-radius: 4px; cursor: pointer; font-weight: 500; font-size: 0.83rem;
  }
  button:hover { background: #282d36; }
  button.primary { background: #263849; border-color: #456380; color: #dbe7f3; }
  button.primary:hover { background: #2f4559; }
  button.success { background: #223a30; border-color: #3d6855; color: #d8ede3; }
  button.success:hover { background: #2a473b; }
  button.danger { background: #422626; border-color: #704040; color: #f2dede; }
  button.danger:hover { background: #502e2e; }
  .input-inline {
    display: flex; align-items: center; gap: 8px; margin-top: 10px;
  }
  input[type="number"], input[type="text"], input[type="password"] {
    background: var(--bg); color: var(--text); border: 1px solid var(--border);
    border-radius: 4px; padding: 6px 9px; font-size: 0.86rem; width: 100%;
  }
  input[type="number"]:focus, input[type="text"]:focus, input[type="password"]:focus {
    outline: none; border-color: var(--accent);
  }
  .form-grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(195px, 1fr)); gap: 12px;
  }
  label { display: block; font-size: 0.78rem; color: var(--muted); margin-bottom: 4px; }
  .badge {
    display: inline-block; padding: 2px 8px; border-radius: 3px;
    font-size: 0.72rem; font-weight: 600; text-transform: uppercase; letter-spacing: 0.02em;
  }
  .badge-on { background: rgba(92,153,123,0.14); color: #84b89e; border: 1px solid rgba(92,153,123,0.35); }
  .badge-off { background: rgba(139,149,165,0.1); color: var(--muted); border: 1px solid var(--border); }
  .badge-warn { background: rgba(184,144,80,0.14); color: #cca86e; border: 1px solid rgba(184,144,80,0.35); }
  .kv-row { display: flex; justify-content: space-between; align-items: center; font-size: 0.84rem; padding: 6px 0; border-bottom: 1px solid var(--border); }
  .kv-row:last-child { border-bottom: none; }
  .cam-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 14px; margin-top: 12px; }
  .cam-box { background: var(--bg); border: 1px solid var(--border); border-radius: 4px; padding: 10px; }
  .cam-frame {
    position: relative; width: 100%; aspect-ratio: 4 / 3; background: #0c0e10;
    border-radius: 3px; overflow: hidden; display: flex; align-items: center; justify-content: center;
  }
  .cam-frame img { width: 100%; height: 100%; object-fit: contain; display: none; transform: scaleX(-1); }
  .cam-empty { color: var(--muted); font-size: 0.82rem; text-align: center; padding: 16px; }
  .roi-overlay {
    position: absolute; top: 0; bottom: 0; right: 20%; width: 55%;
    border: 1px dashed rgba(107, 144, 178, 0.8); background: rgba(107, 144, 178, 0.05);
    pointer-events: none; display: flex; align-items: flex-start; justify-content: center;
  }
  .roi-label {
    background: rgba(18, 20, 23, 0.85); color: var(--accent); font-size: 0.66rem;
    font-weight: 600; padding: 2px 6px; border-radius: 0 0 3px 3px; letter-spacing: 0.03em;
  }
  .toast {
    position: fixed; bottom: 20px; right: 20px; background: var(--card); color: var(--text);
    border: 1px solid var(--accent); padding: 9px 14px; border-radius: 4px;
    font-weight: 500; font-size: 0.84rem; display: none; z-index: 100;
  }
</style>
</head>
<body>
<div class="container">
  <header>
    <div>
      <h1>Basil Watering Node</h1>
      <div class="meta" id="net-meta">Connecting...</div>
    </div>
    <div style="display:flex;gap:8px;align-items:center;">
      <span id="auto-badge" class="badge badge-off">AUTO: OFF</span>
      <span id="mqtt-badge" class="badge badge-off">MQTT: OFF</span>
    </div>
  </header>

  <nav class="tabs">
    <button class="tab-btn active" id="tab-btn-monitor" onclick="switchTab('monitor')">Monitoring &amp; Control</button>
    <button class="tab-btn" id="tab-btn-config" onclick="switchTab('config')">Configuration &amp; OTA</button>
  </nav>

  <!-- ==================== TAB 1: MONITORING & CONTROL ==================== -->
  <div class="tab-pane active" id="tab-monitor">
    <div class="grid">
      <!-- Soil Moisture Card -->
      <div class="card">
        <h2>Soil Moisture Sensor</h2>
        <div style="display:flex;justify-content:space-between;align-items:baseline;">
          <div class="big-val" id="m-pct">--.- %</div>
          <div style="font-size:0.95rem;color:var(--muted);">Raw: <span id="m-raw" style="color:var(--text);">----</span></div>
        </div>
        <div class="bar-wrap"><div class="bar-fill" id="m-bar"></div></div>
        <div class="sub-val" id="m-age">Last read: never</div>
        <div class="btn-row">
          <button class="primary" onclick="triggerMeasure()">Measure Now (15ms Pulse)</button>
        </div>
      </div>

      <!-- Pump Control Card -->
      <div class="card">
        <h2>Peristaltic Pump Control</h2>
        <div style="display:flex;justify-content:space-between;align-items:baseline;">
          <div class="big-val" id="p-state">IDLE</div>
          <div style="font-size:0.95rem;color:var(--muted);">Total: <span id="p-total" style="color:var(--text);">0.0</span> mL</div>
        </div>
        <div class="sub-val" id="p-rem">Ready for manual or automatic dosing</div>

        <div class="btn-row">
          <button class="primary" onclick="runPumpMl(5)">+5 mL</button>
          <button class="primary" onclick="runPumpMl(10)">+10 mL</button>
          <button class="primary" onclick="runPumpMl(25)">+25 mL</button>
          <button class="primary" onclick="runPumpMl(50)">+50 mL</button>
          <button class="danger" onclick="stopPump()">Stop</button>
        </div>

        <div class="input-inline">
          <input type="number" id="custom-ml" value="15" min="1" max="300" step="0.5" style="width:90px;">
          <button class="primary" onclick="runCustomMl()">Pump Custom mL</button>
          <button onclick="resetTotalMl()">Reset Total</button>
        </div>
      </div>

      <!-- Automation & Camera Link Card -->
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
          <span><strong id="c-cooldown">0s</strong> <button style="padding:2px 7px;font-size:0.74rem;margin-left:6px;" onclick="resetCooldown()">Clear</button></span>
        </div>
        <div class="kv-row">
          <span>Camera Health (Wi-Fi HTTP)</span>
          <strong id="cam-state">unknown</strong>
        </div>
        <div class="kv-row">
          <span>Camera Confidence / Age</span>
          <span id="cam-meta">--</span>
        </div>
        <div class="kv-row">
          <span>Watering Node MAC</span>
          <code id="node-mac" style="color:var(--muted);">--</code>
        </div>
      </div>
    </div>

    <!-- Camera Alignment & Recent Captures Card -->
    <div class="card">
      <div style="display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;gap:10px;">
        <div>
          <h2 style="margin-bottom:4px;">Camera Alignment &amp; Recent Captures</h2>
          <div class="meta">Displays the 2 most recent 320x240 frames pushed to <code>POST /health</code>. Align the basil canopy inside the dashed ROI box (20%–75% width).</div>
        </div>
        <div style="display:flex;align-items:center;gap:12px;">
          <label style="display:flex;align-items:center;gap:6px;margin:0;cursor:pointer;color:var(--text);font-size:0.82rem;">
            <input type="checkbox" id="toggle-roi" checked onchange="toggleRoiOverlay(this.checked)">
            Show Canopy ROI (20%–75%)
          </label>
          <button class="primary" onclick="requestCameraCapture()">Capture &amp; Classify Now</button>
        </div>
      </div>

      <div class="meta" id="cam-diag-line" style="margin-top:10px;padding:6px 10px;background:var(--bg);border-radius:3px;border:1px solid var(--border);font-family:monospace;">
        Camera IP: not discovered | Health Updates: 0 | Complete Images: 0 | Last Frame: none
      </div>

      <div class="cam-grid">
        <div class="cam-box">
          <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
            <span style="font-size:0.85rem;font-weight:500;">Latest Capture (Slot 1)</span>
            <span id="cam-slot0-badge" class="badge badge-off">Waiting</span>
          </div>
          <div class="cam-frame">
            <img id="cam-slot0-img" alt="Latest camera capture">
            <div id="cam-slot0-empty" class="cam-empty">No image received yet.<br>Click "Capture &amp; Classify Now" or wait for the next 60s cycle.</div>
            <div class="roi-overlay"><span class="roi-label">CNN CANOPY ROI (20%–75%)</span></div>
          </div>
          <div class="sub-val" id="cam-slot0-meta" style="margin-top:8px;">--</div>
        </div>

        <div class="cam-box">
          <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
            <span style="font-size:0.85rem;font-weight:500;">Previous Capture (Slot 2)</span>
            <span id="cam-slot1-badge" class="badge badge-off">None</span>
          </div>
          <div class="cam-frame">
            <img id="cam-slot1-img" alt="Previous camera capture">
            <div id="cam-slot1-empty" class="cam-empty">Waiting for a second frame...</div>
            <div class="roi-overlay"><span class="roi-label">CNN CANOPY ROI (20%–75%)</span></div>
          </div>
          <div class="sub-val" id="cam-slot1-meta" style="margin-top:8px;">--</div>
        </div>
      </div>
    </div>
  </div>

  <!-- ==================== TAB 2: CONFIGURATION & OTA ==================== -->
  <div class="tab-pane" id="tab-config">
    <!-- Calibration & Thresholds Form -->
    <div class="card" style="margin-bottom:14px;">
      <h2>Sensor Calibration &amp; Watering Thresholds</h2>
      <div class="btn-row" style="margin-top:0;margin-bottom:14px;">
        <button onclick="setRawCal('dry')" title="Measure &amp; save current Raw ADC as Dry">Set Current Raw as DRY (0%)</button>
        <button onclick="setRawCal('wet')" title="Measure &amp; save current Raw ADC as Wet">Set Current Raw as WET (100%)</button>
        <button onclick="runPumpMs(5000)">Flow Cal: 1. Run Pump 5.0s</button>
        <input type="number" id="cal-measured-ml" placeholder="mL collected in 5s" step="0.1" style="width:155px;">
        <button onclick="saveFlowCal()">Flow Cal: 2. Save mL/s</button>
      </div>
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
        <button class="success" onclick="saveConfig()">Save Calibration &amp; Thresholds</button>
        <button onclick="resetDefaults()">Restore Defaults</button>
      </div>
    </div>

    <!-- MQTT Config Card -->
    <div class="card" style="margin-bottom:14px;">
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
        <div>
          <label>Telemetry Topic</label>
          <input type="text" id="cfg-topic-telem">
        </div>
        <div>
          <label>Events Topic</label>
          <input type="text" id="cfg-topic-events">
        </div>
        <div>
          <label>Status Topic (LWT)</label>
          <input type="text" id="cfg-topic-status">
        </div>
      </div>
      <div class="btn-row" style="margin-top:14px;">
        <button class="success" onclick="saveConfig()">Save MQTT Settings</button>
        <button class="primary" onclick="testMqttPublish()">Publish Test Telemetry Now</button>
      </div>
    </div>

    <!-- OTA Firmware Update Card -->
    <div class="card">
      <h2>OTA Firmware Update</h2>
      <div class="meta" style="margin-bottom:10px;">
        Upload a compiled <code>.pio/build/watering/firmware.bin</code> image to update the watering node over Wi-Fi.
      </div>
      <div class="input-inline">
        <input type="file" id="ota-file" accept=".bin" style="color:var(--muted);font-size:0.83rem;">
        <button class="primary" id="btn-ota" onclick="uploadOtaFirmware()">Upload &amp; Flash (.bin)</button>
        <span id="ota-status" class="meta"></span>
      </div>
    </div>
  </div>
</div>

<div class="toast" id="toast">Saved</div>

<script>
let lastState = null;
let formLoaded = false;
let renderedImgSeq = [null, null];

function switchTab(tab) {
  document.getElementById('tab-monitor').classList.toggle('active', tab === 'monitor');
  document.getElementById('tab-config').classList.toggle('active', tab === 'config');
  document.getElementById('tab-btn-monitor').classList.toggle('active', tab === 'monitor');
  document.getElementById('tab-btn-config').classList.toggle('active', tab === 'config');
}

function toggleRoiOverlay(show) {
  document.querySelectorAll('.roi-overlay').forEach(el => {
    el.style.display = show ? 'flex' : 'none';
  });
}

async function requestCameraCapture() {
  await fetch('/api/camera/capture', { method: 'POST' });
  showToast('Requested immediate capture from camera node');
  setTimeout(fetchStatus, 500);
  setTimeout(fetchStatus, 1100);
  setTimeout(fetchStatus, 1800);
}

async function uploadOtaFirmware() {
  const fileInput = document.getElementById('ota-file');
  const statusEl = document.getElementById('ota-status');
  const btn = document.getElementById('btn-ota');
  if (!fileInput.files || fileInput.files.length === 0) {
    alert('Select a compiled firmware.bin file first.');
    return;
  }
  const file = fileInput.files[0];
  if (!confirm(`Flash '${file.name}' (${(file.size / 1024).toFixed(0)} KB) to Watering Node and reboot?`)) return;

  btn.disabled = true;
  statusEl.textContent = 'Uploading firmware...';
  const formData = new FormData();
  formData.append('firmware', file, file.name);

  try {
    const r = await fetch('/api/ota', { method: 'POST', body: formData });
    if (r.ok) {
      statusEl.textContent = 'Success. Rebooting node in 5s...';
      showToast('OTA Flash Complete. Rebooting...');
      setTimeout(() => location.reload(), 5500);
    } else {
      const txt = await r.text();
      statusEl.textContent = `Failed: ${txt}`;
      btn.disabled = false;
    }
  } catch (e) {
    statusEl.textContent = 'Rebooting...';
    setTimeout(() => location.reload(), 5500);
  }
}

function renderCameraSlot(slotIdx, info) {
  const imgEl = document.getElementById(`cam-slot${slotIdx}-img`);
  const emptyEl = document.getElementById(`cam-slot${slotIdx}-empty`);
  const badgeEl = document.getElementById(`cam-slot${slotIdx}-badge`);
  const metaEl = document.getElementById(`cam-slot${slotIdx}-meta`);

  if (!info) {
    imgEl.style.display = 'none';
    emptyEl.style.display = 'block';
    badgeEl.textContent = slotIdx === 0 ? 'Waiting' : 'None';
    badgeEl.className = 'badge badge-off';
    metaEl.textContent = '--';
    return;
  }

  emptyEl.style.display = 'none';
  imgEl.style.display = 'block';
  if (renderedImgSeq[slotIdx] !== info.seq) {
    renderedImgSeq[slotIdx] = info.seq;
    imgEl.src = `/api/camera/image?slot=${slotIdx}&seq=${info.seq}&t=${Date.now()}`;
  }

  const isWilted = info.state === 'wilted';
  const isHealthy = info.state === 'healthy';
  badgeEl.textContent = `${info.state.toUpperCase()} (${(info.conf * 100).toFixed(0)}%)`;
  badgeEl.className = 'badge ' + (isWilted ? 'badge-warn' : (isHealthy ? 'badge-on' : 'badge-off'));
  metaEl.textContent = `Frame #${info.seq} | Received ${info.age_s}s ago | ${(info.bytes / 1024).toFixed(1)} KB JPEG`;
}

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
  const pass = document.getElementById('cfg-mqtt-pass');
  pass.value = '';
  pass.placeholder = cfg.mqtt_pass_set ? '(unchanged)' : '(none)';
  document.getElementById('cfg-mqtt-cid').value = cfg.mqtt_client_id;
  document.getElementById('cfg-telem-int-s').value = cfg.telemetry_interval_s;
  document.getElementById('cfg-topic-telem').value = cfg.mqtt_topic_telemetry;
  document.getElementById('cfg-topic-events').value = cfg.mqtt_topic_events;
  document.getElementById('cfg-topic-status').value = cfg.mqtt_topic_status;
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
    d.health_valid ? `${(d.health_conf * 100).toFixed(0)}% conf (${d.health_age_s}s ago)` : 'No updates received yet';

  const imgs = d.cam_images || [];
  renderCameraSlot(0, imgs.find(x => x.slot === 0) || null);
  renderCameraSlot(1, imgs.find(x => x.slot === 1) || null);

  if (d.cam_diag) {
    const dg = d.cam_diag;
    const frameStr = dg.last_img_bytes > 0
      ? `#${dg.last_img_seq} (${(dg.last_img_bytes / 1024).toFixed(1)} KB)`
      : 'none';
    const ipHtml = (dg.cam_ip && dg.cam_ip !== 'not discovered')
      ? `<a href="http://${dg.cam_ip}/" target="_blank" style="color:var(--accent);">${dg.cam_ip}</a>`
      : 'not discovered';
    document.getElementById('cam-diag-line').innerHTML =
      `Camera IP: ${ipHtml} | Health Updates: ${dg.health_rx_count} | Complete Images: ${dg.img_complete_count} | Last Frame: ${frameStr}`;
  }

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
    telemetry_interval_s: parseInt(document.getElementById('cfg-telem-int-s').value, 10),
    mqtt_topic_telemetry: document.getElementById('cfg-topic-telem').value.trim(),
    mqtt_topic_events: document.getElementById('cfg-topic-events').value.trim(),
    mqtt_topic_status: document.getElementById('cfg-topic-status').value.trim()
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
  showToast(d.mqtt_connected ? 'Telemetry published' : 'MQTT not connected');
}

fetchStatus();
setInterval(fetchStatus, 1500);
</script>
</body>
</html>)rawliteral";
