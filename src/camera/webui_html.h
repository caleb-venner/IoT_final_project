// Embedded PROGMEM WebUI for the camera node (http://basil-cam.local/),
// providing live inference status, frame preview, and OTA firmware upload.

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
    --bg: #121417;
    --card: #181b20;
    --border: #262b33;
    --text: #e2e8f0;
    --muted: #8b95a5;
    --accent: #6b90b2;
    --green: #5c997b;
    --amber: #b89050;
  }
  * { box-sizing: border-box; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background: var(--bg); color: var(--text); max-width: 760px; margin: 24px auto; padding: 0 16px;
    line-height: 1.45;
  }
  header {
    display: flex; flex-wrap: wrap; justify-content: space-between; align-items: baseline;
    gap: 10px; margin-bottom: 16px; padding-bottom: 12px; border-bottom: 1px solid var(--border);
  }
  h1 { font-size: 1.25rem; font-weight: 600; margin: 0; }
  h2 { font-size: 0.95rem; font-weight: 600; color: var(--text); margin: 0 0 12px 0; }
  .meta { font-size: 0.82rem; color: var(--muted); }
  .tabs { display: flex; gap: 20px; margin-bottom: 20px; border-bottom: 1px solid var(--border); }
  .tab-btn {
    background: transparent; color: var(--muted); border: none;
    border-bottom: 2px solid transparent; border-radius: 0;
    padding: 8px 2px; cursor: pointer; font-weight: 500; font-size: 0.9rem;
  }
  .tab-btn:hover { color: var(--text); background: transparent; }
  .tab-btn.active { color: var(--text); border-bottom-color: var(--accent); background: transparent; }
  .tab-pane { display: none; }
  .tab-pane.active { display: block; }
  .card { background: var(--card); border: 1px solid var(--border); border-radius: 4px; padding: 16px; margin-bottom: 14px; }
  button {
    background: #263849; color: #dbe7f3; border: 1px solid #456380;
    padding: 7px 13px; border-radius: 4px; cursor: pointer; font-weight: 500; font-size: 0.84rem;
  }
  button:hover { background: #2f4559; }
  code { color: var(--accent); }
  .frame-wrap {
    position: relative; width: 100%; max-width: 480px; aspect-ratio: 4 / 3;
    background: #0c0e10; border-radius: 3px; overflow: hidden; border: 1px solid var(--border); margin-top: 12px;
  }
  .frame-wrap img { width: 100%; height: 100%; object-fit: contain; display: block; transform: scaleX(-1); }
  .roi-box {
    position: absolute; top: 0; bottom: 0; right: 20%; width: 55%;
    border: 1px dashed rgba(107, 144, 178, 0.8); background: rgba(107, 144, 178, 0.05); pointer-events: none;
  }
</style>
</head>
<body>
  <header>
    <div>
      <h1>Basil Edge-ML Camera Node</h1>
      <div class="meta" id="net-meta">Loading...</div>
    </div>
  </header>

  <nav class="tabs">
    <button class="tab-btn active" id="tab-btn-live" onclick="switchTab('live')">Live Camera &amp; Classification</button>
    <button class="tab-btn" id="tab-btn-ota" onclick="switchTab('ota')">OTA Firmware Update</button>
  </nav>

  <div class="tab-pane active" id="tab-live">
    <div class="card">
      <h2>Latest Edge-ML Inference &amp; Frame</h2>
      <p id="inf-meta" style="margin:0 0 12px 0;font-size:0.88rem;">--</p>
      <button onclick="triggerCapture()">Capture, Classify &amp; Push to Watering Node Now</button>
      <div class="frame-wrap" id="frame-box" style="display:none;">
        <img id="cam-img" alt="Latest capture">
        <div class="roi-box"></div>
      </div>
    </div>
  </div>

  <div class="tab-pane" id="tab-ota">
    <div class="card">
      <h2>Camera Node OTA Firmware Update</h2>
      <p class="meta">Upload a compiled <code>.pio/build/camera/firmware.bin</code> file to update the camera node over Wi-Fi.</p>
      <form method="POST" action="/api/ota" enctype="multipart/form-data">
        <input type="file" name="firmware" accept=".bin" required style="color:var(--muted);font-size:0.84rem;">
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
