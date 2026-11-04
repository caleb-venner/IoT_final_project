# Architecture

```mermaid
flowchart TB
  subgraph local["Local (Wi-Fi HTTP API)"]
    direction LR
    cam["<b>Camera node</b><br/>Freenove WROVER CAM<br/>• capture image<br/>• on-device ML<br/>(healthy / wilted)"]
    water["<b>Watering node</b><br/>ESP32 DevKitC<br/>• soil moisture<br/>• peristaltic pump<br/>• watering decision"]
    cam -- "HTTP POST /health" --> water
  end
  subgraph global["Global (Wi-Fi / Internet)"]
    direction RL
    broker["MQTT broker"] --> telegraf["Telegraf"] --> influx["InfluxDB"] --> grafana["Grafana"]
  end
  %% Subgraph-to-subgraph link: a node-to-node link across subgraphs makes Mermaid ignore their directions
  local -- "MQTT (from watering node)" --> global
```

## Sequence

1. **Camera capture**: camera node grabs a `320x240` RGB565 frame (`camera_capture()`) every `INFERENCE_INTERVAL_MS` (60 s) or on demand via HTTP/UDP trigger.
2. **Inference**: `inference_run()` crops the horizontal canopy ROI (`20%–75%`), downsamples to a `32x32` RGB planar float tensor, and runs `TinyBasilCNN` (`src/camera/model_weights.h`) to classify the plant as `healthy` or `wilted` with confidence.
3. **Local Wi-Fi HTTP API**: `http_tx_send()` encodes the `320x240` frame to JPEG and sends an HTTP `POST /health` request containing the `PlantHealthMsg` fields and image to the watering node.
4. **Control decision**: the watering node stores the latest reading and 2-image preview ring buffer (`camera_rx_latest()`), reads pulsed-power soil moisture, and `control_decide()` applies the dry / borderline+wilted / wet / cooldown / stale-camera rules using runtime thresholds stored in NVS (configurable via `http://basil-water.local/`).
5. **Pump**: `pump_run_ms()` / `pump_run_ml()` drives the 12V peristaltic pump via a 3-channel parallel ULN2003A non-blocking timer, capped at `max_run_ms`, and tracks delivered mL.
6. **MQTT**: telemetry every `telemetry_interval_s` (default 60 s, configurable) to `farm/basil_aida_caleb/telemetry`, an event per watering to `farm/basil_aida_caleb/events`, and online/offline (retained, last will) on `farm/basil_aida_caleb/status`.
7. **Telegraf**: subscribes to the topics and parses the JSON/status payloads.
8. **InfluxDB**: stores the time series (`telemetry`, `events`, `status`).
9. **Grafana**: renders the live dashboard.

## Related Documentation

- [Cloud Infrastructure & Local Docker Stack Guide](../cloud/README.md)

## Interfaces

Single source of truth: `include/plant_msgs.h`.

### Local Wi-Fi HTTP API: camera node → watering node (`POST /health`)

```c
typedef struct __attribute__((packed)) {
  uint8_t  version;       // message format version (1)
  uint8_t  state;         // 0 = healthy, 1 = wilted, 2 = unknown
  float    confidence;    // 0.0 – 1.0
  uint32_t seq;           // increments every send
  uint32_t inference_ms;  // time taken by on-device inference
} PlantHealthMsg;
```

Both nodes connect to the local Wi-Fi network and discover each other via mDNS (`basil-water.local` / `basil-cam.local`) and local UDP discovery.

### MQTT topics

| Topic | Payload (JSON) | When |
|---|---|---|
| `farm/basil_aida_caleb/telemetry` | `{"moisture_pct":41.5,"moisture_raw":2310,"pump":0,"health_state":"healthy","health_conf":0.91,"health_age_s":12,"water_ml_total":180}` | every 60 s (configurable) |
| `farm/basil_aida_caleb/events` | `{"event":"pump_run","reason":"dry+wilted","duration_ms":4000,"ml":25}` | on each watering |
| `farm/basil_aida_caleb/status` | `online` / `offline` (retained, last will) | on connect/disconnect |

`health_age_s` is `-1` until the first camera message arrives.
