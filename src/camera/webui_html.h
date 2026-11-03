// webui_html.h
// Purpose: Embedded PROGMEM single-page WebUI for the Camera Node (http://basil-cam.local/),
//          organised into two tabs:
//            1. Live Camera & Classification
//            2. OTA Firmware Update
// OWNER:   Caleb
// Node:    camera node (Freenove ESP32-WROVER CAM)

#pragma once
#include <Arduino.h>

static const char kCamIndexHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Basil Camera Node</title>
<style>
  :root {
    --bg: #0f172a; --card: #1e293b; --border: #334155;
    --text: #f8fafc; --muted: #94a3b8; --accent: #38bdf8;
    --green: #22c55e; --amber: #f59e0b;
  }
  * { box-sizing: border-box; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background: var(--bg); color: var(--text); max-width: 760px; margin: 20px auto; padding: 0 16px;
  }
  header {
    display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center;
    gap: 10px; margin-bottom: 14px; padding-bottom: 12px; border-bottom: 1px solid var(--border);
  }
  h1 { font-size: 1.35rem; margin: 0; }
  h2 { font-size: 1.05rem; color: var(--accent); margin: 0 0 12px 0; }
  .meta { font-size: 0.84rem; color: var(--muted); }
  .tabs { display: flex; gap: 8px; margin-bottom: 16px; border-bottom: 1px solid var(--border); padding-bottom: 10px; }
  .tab-btn {
    background: var(--card); color: var(--muted); border: 1px solid var(--border);
    padding: 8px 16px; border-radius: 8px; cursor: pointer; font-weight: 600; font-size: 0.88rem;
  }
  .tab-btn.active { background: #0284c7; color: #fff; border-color: var(--accent); }
  .tab-pane { display: none; }
  .tab-pane.active { display: block; }
  .card { background: var(--card); border: 1px solid var(--border); border-radius: 10px; padding: 16px; margin-bottom: 16px; }
  button {
    background: #0284c7; color: #fff; border: 1px solid var(--accent);
    padding: 8px 14px; border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 0.86rem;
  }
  button:hover { background: #0369a1; }
  code { color: var(--accent); }
  .frame-wrap {
    position: relative; width: 100%; max-width: 480px; aspect-ratio: 4 / 3;
    background: #020617; border-radius: 8px; overflow: hidden; border: 1px solid var(--border); margin-top: 12px;
  }
  .frame-wrap img { width: 100%; height: 100%; object-fit: contain; display: block; transform: scaleX(-1); }
  .roi-box {
    position: absolute; top: 0; bottom: 0; right: 20%; width: 55%;
    border: 2px dashed rgba(56, 189, 248, 0.9); background: rgba(56, 189, 248, 0.06); pointer-events: none;
  }
</style>
</head>
<body>
  <header>
    <div>
      <h1>📷 Basil Edge-ML Camera Node</h1>
      <div class="meta" id="net-meta">Loading...</div>
    </div>
  </header>

  <nav class="tabs">
    <button class="tab-btn active" id="tab-btn-live" onclick="switchTab('live')">📊 Live Camera &amp; Classification</button>
    <button class="tab-btn" id="tab-btn-ota" onclick="switchTab('ota')">⚙️ OTA Firmware Update</button>
  </nav>

  <div class="tab-pane active" id="tab-live">
    <div class="card">
      <h2>Latest Edge-ML Inference &amp; Frame</h2>
      <p id="inf-meta" style="margin:0 0 12px 0;">--</p>
      <button onclick="triggerCapture()">📸 Capture, Classify &amp; Push to Watering Node Now</button>
      <div class="frame-wrap" id="frame-box" style="display:none;">
        <img id="cam-img" alt="Latest capture">
        <div class="roi-box"></div>
      </div>
    </div>
  </div>

  <div class="tab-pane" id="tab-ota">
    <div class="card">
      <h2>⬆️ Camera Node OTA Firmware Update</h2>
      <p class="meta">Upload a compiled <code>.pio/build/camera/firmware.bin</code> file to update the camera node over Wi-Fi.</p>
      <form method="POST" action="/api/ota" enctype="multipart/form-data">
        <input type="file" name="firmware" accept=".bin" required style="color:var(--muted);">
        <button type="submit">Upload &amp; Flash (.bin)</button>
      </form>
    </div>
  </div>

<script>
let lastSeq = null;

function switchTab(tab) {
  document.getElementById('tab-live').classList.toggle('active', tab === 'live');
  document.getElementById('tab-ota').classList.toggle('active', tab === 'ota');
  document.getElementById('tab-btn-live').classList.toggle('active', tab === 'live');
  document.getElementById('tab-btn-ota').classList.toggle('active', tab === 'ota');
}

async function fetchStatus() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    document.getElementById('net-meta').innerHTML =
      `IP: <code>${d.ip}</code> | Ch: <code>${d.channel}</code> | Watering Node IP: <code>${d.watering_ip}</code> | Last HTTP POST: <code>${d.last_http_code}</code>`;
    document.getElementById('inf-meta').innerHTML =
      `State: <strong>${d.state.toUpperCase()}</strong> (${(d.conf * 100).toFixed(1)}% conf, ${d.inference_ms} ms CNN, ${d.age_s}s ago, seq #${d.seq})`;
    if (d.has_jpg) {
      document.getElementById('frame-box').style.display = 'block';
      if (lastSeq !== d.seq) {
        lastSeq = d.seq;
        document.getElementById('cam-img').src = `/latest.jpg?seq=${d.seq}&t=${Date.now()}`;
      }
    }
  } catch (e) {}
}

async function triggerCapture() {
  await fetch('/capture', { method: 'POST' });
  setTimeout(fetchStatus, 600);
  setTimeout(fetchStatus, 1300);
}

fetchStatus();
setInterval(fetchStatus, 2000);
</script>
</body>
</html>)rawliteral";
