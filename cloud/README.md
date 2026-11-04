# cloud/ — MQTT → Telegraf → InfluxDB → Grafana

OWNER: Aida

The cloud chain runs as a Docker Compose stack (InfluxDB + Telegraf + Grafana).
The **MQTT broker is the public EMQX test broker** (`broker.emqx.io:1883`) — there is
no local broker container. The watering node (or the test simulator) publishes to
EMQX; Telegraf subscribes and writes to InfluxDB; Grafana presents it.

```
watering node ──MQTT──▶ broker.emqx.io ──▶ Telegraf ──▶ InfluxDB ──▶ Grafana
   (ESP32)               (public :1883)                  :8086        :3000
```

| Service   | Image                   | URL / port             |
|-----------|-------------------------|------------------------|
| EMQX      | public `broker.emqx.io` | `broker.emqx.io:1883`  |
| InfluxDB  | `influxdb:2.7`          | http://localhost:8086  |
| Telegraf  | `telegraf:1.30`         | (no port; bridge)      |
| Grafana   | `grafana/grafana:11.1.0`| http://localhost:3000  |

> **Public broker warning.** `broker.emqx.io` is shared and unauthenticated — anyone
> can read/write your topics. That's why we use a unique topic base,
> `farm/basil_aida_caleb/...`. Fine for a demo/assignment; do not send anything
> sensitive.

## Topics & payloads

Single source of truth: `include/plant_msgs.h`.

| Topic | Payload | → InfluxDB measurement |
|---|---|---|
| `farm/basil_aida_caleb/telemetry` | `{"moisture_pct":41.5,"moisture_raw":2310,"pump":0,"health_state":"healthy","health_conf":0.91,"health_age_s":12,"water_ml_total":180}` | `telemetry` |
| `farm/basil_aida_caleb/events` | `{"event":"pump_run","reason":"dry+wilted","duration_ms":4000,"ml":25}` | `events` |
| `farm/basil_aida_caleb/status` | `online` / `offline` (retained, last will) | `status` (field `online` = 1/0) |

In `telemetry`, `health_state` is stored as a **tag**; everything else is a numeric
**field**. The `plant` tag (`basil_aida_caleb`) is parsed from the topic.

## Run it

Requires Docker Desktop (or Docker Engine + the compose plugin).

```sh
cd cloud
cp .env.example .env            # Windows cmd: copy .env.example .env
```

Edit `.env` and set your own `INFLUX_TOKEN`, `INFLUX_PASSWORD`, `GRAFANA_PASSWORD`.
The MQTT values (`broker.emqx.io` / `emqx` / `public`) can stay as-is.

Bring the stack up:

```sh
docker compose up -d
docker compose ps                 # influxdb/grafana running, telegraf running
docker compose logs -f telegraf   # should connect to broker.emqx.io, no errors
```

- Grafana: http://localhost:3000 (login from `GRAFANA_USER` / `GRAFANA_PASSWORD`).
  The InfluxDB datasource and the **Smart Irrigation — Basil** dashboard are
  provisioned automatically (folder "Smart Irrigation").
- InfluxDB: http://localhost:8086 (login from `INFLUX_USERNAME` / `INFLUX_PASSWORD`).

Stop: `docker compose down` (add `-v` to also wipe stored data).

## Test the chain without hardware

A simulator publishes realistic telemetry/events/status to the public broker:

```sh
pip install paho-mqtt                 # Windows: py -3 -m pip install paho-mqtt
python tools/sim_publish.py           # Windows: py -3 tools\sim_publish.py

# defaults already target broker.emqx.io on the farm/basil_aida_caleb/... topics.
# faster demo:
python tools/sim_publish.py --interval 2 --speed 60
```

Watch the raw MQTT stream (uses the public EMQX broker directly, so no compose
network needed):

```sh
docker run --rm eclipse-mosquitto:2 \
  mosquitto_sub -h broker.emqx.io -p 1883 -t 'farm/basil_aida_caleb/#' -v
```

Then open Grafana — the moisture curve, pump timeline, water-per-day bars, health
confidence and online status should populate within ~10 s.

## Point the device at the broker

In `include/secrets.h` on the watering node:

```c
#define MQTT_HOST "broker.emqx.io"
#define MQTT_PORT 1883
#define MQTT_USER "emqx"        // ignored by the public broker, but harmless
#define MQTT_PASS "public"
```

The topic names are already set in `include/plant_msgs.h`
(`farm/basil_aida_caleb/...`). Port `8883` in the firmware would select TLS; the
public broker's plain port is `1883`, so use `1883`.

## Files

```
cloud/
├── docker-compose.yml          # InfluxDB + Telegraf + Grafana (no broker container)
├── .env.example                # copy to .env and fill in (git-ignored)
├── telegraf/
│   └── telegraf.conf           # EMQX -> InfluxDB mapping (3 inputs, enum processor)
├── grafana/
│   ├── basil-irrigation.json   # exported dashboard (reference copy)
│   └── provisioning/
│       ├── datasources/influxdb.yml
│       └── dashboards/
│           ├── dashboards.yml
│           └── basil-irrigation.json   # auto-loaded on startup
└── tools/
    └── sim_publish.py          # fake-data publisher for testing
```


## Integration with the real ESP32 nodes

The cloud side does **not** change when the real hardware comes online. Caleb's
watering node publishes to the same broker and topics the simulator used, so
Telegraf picks it up automatically and Grafana fills from real data.

```
Caleb's hardware                                 Your cloud (unchanged)
┌──────────────────┐                             ┌───────────────────────────────┐
│ Camera node      │                             │                               │
│  HTTP /health ──┐│                             │  Telegraf → InfluxDB → Grafana│
│ Watering node   ▼│  MQTT publish               │  (subscribes to the topics)   │
│   moisture+pump ─┼──▶ broker.emqx.io ◀─────────┼── Telegraf                    │
└──────────────────┘   farm/basil_aida_caleb/*   └───────────────────────────────┘
```

### The camera node does not use MQTT

Important architectural point (see `docs/architecture.md`): the **camera node does
not publish to MQTT**. It sends its health result (`healthy`/`wilted` + confidence)
to the **watering node over the local Wi-Fi HTTP API (`POST /health`)**. The watering node merges that
into its own telemetry and publishes it under the existing fields `health_state`,
`health_conf`, and `health_age_s`.

So camera/edge-ML data reaches the dashboard **through the watering node's
telemetry** — it already drives the "Plant health confidence", "Current plant
health", and "Camera health-data age" panels. No separate topic or Telegraf input is
needed for the camera.

For the full system both nodes must run: camera → HTTP `/health` → watering → MQTT → cloud.
Only the watering node talks to the cloud.

### What Caleb does (device side)

1. Pull the latest repo (so `include/plant_msgs.h` has the `farm/basil_aida_caleb/*`
   topics).
2. Set the broker in `include/secrets.h`:
   ```c
   #define MQTT_HOST "broker.emqx.io"
   #define MQTT_PORT 1883
   #define MQTT_USER "emqx"
   #define MQTT_PASS "public"
   ```
3. Flash both nodes:
   ```sh
   pio run -e camera   -t upload     # camera node (HTTP /health sender)
   pio run -e watering -t upload     # watering node (sensing + pump + MQTT)
   pio device monitor
   ```
   On the watering node's serial monitor, `[mqtt] farm/basil_aida_caleb/telemetry {...}`
   lines confirm it is publishing.

### What you do (cloud side)

1. Keep the stack running: `docker compose up -d`.
2. **Stop the simulator** so fake data doesn't mix with real data:
   ```powershell
   Get-Process py            # find the sim_publish.py PID
   Stop-Process -Id <PID>
   ```
3. That's it — real telemetry lands in the `ESP32` bucket (tagged
   `plant=basil_aida_caleb`) exactly like the simulated data, and the dashboard
   updates.

### Verifying the real device is connected

- **Serial monitor (Caleb):** `[mqtt] ... ok` and telemetry lines printing.
- **MQTTX Web (you):** https://mqttx.app/web → connect `ws://broker.emqx.io:8083` →
  subscribe `farm/basil_aida_caleb/#` → his real messages appear.
- **Grafana (you):** the "Node online status" panel shows ONLINE (driven by the
  watering node's retained `status` message + last-will), and moisture reflects the
  real sensor.

### Notes

- Both sides are on the **public** EMQX broker, so no network or credential
  coordination is needed beyond the broker host/port and the shared topics — Caleb
  can be on his Wi-Fi and you on yours.
- The retained `status` message means the online/offline panel genuinely tracks
  whether Caleb's hardware is powered and connected — useful as a live indicator in
  the demo video.

### Gotchas we hit during setup (keep these correct in `.env`)

- `INFLUX_ORG` must be the InfluxDB **org ID** (e.g. `44c5277b575fdead`), not the org
  name. With the name, InfluxDB returns `404 organization not found` and silently
  drops every write. Find the ID with:
  `docker exec basil-cloud-influxdb-1 influx org list --token "$INFLUX_TOKEN"`.
- The compose file reads `MQTT_PORT` (not `MQTT_TCP_PORT`); set `MQTT_PORT=1883`.
