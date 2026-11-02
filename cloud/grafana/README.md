# cloud/grafana/

OWNER: Aida

The dashboard and datasource are **provisioned automatically** by the Docker Compose
stack (see `../README.md`). You do not have to import anything by hand.

- Datasource: InfluxDB v2 (Flux), uid `influxdb-basil`
  (`provisioning/datasources/influxdb.yml`).
- Dashboard: `provisioning/dashboards/basil-irrigation.json`, auto-loaded into the
  "Smart Irrigation" folder on startup.
- `basil-irrigation.json` in this folder is an exported reference copy (what you'd
  get from Dashboard → Share → Export).

## Panels

| # | Panel | Measurement / field |
|---|---|---|
| 1 | Soil moisture % over time (30/55 % threshold colours) | `telemetry.moisture_pct` |
| 2 | Current moisture gauge | `telemetry.moisture_pct` (last) |
| 3 | Pump state & watering events (state timeline + annotations) | `telemetry.pump`, `events` |
| 4 | Water delivered per day (mL) | `events.ml` summed per day |
| 5 | Plant health confidence over time | `telemetry.health_conf` |
| 6 | Current plant health (healthy/wilted/unknown) | `telemetry.health_state` tag |
| 7 | Node online/offline status | `status.online` (1/0) |
| 8 | Camera health-data age (s) | `telemetry.health_age_s` |
| 9 | Total water delivered (mL) | `telemetry.water_ml_total` |

Pump events show as orange annotations on the time-series panels (configured in the
dashboard's annotation query against the `events` measurement).

## On-device inference time

The intended "inference time (ms)" panel is **not** wired up, because the on-device
inference duration (`inference_ms` in the `PlantHealthMsg` ESP-NOW struct) is **not
published over MQTT** — the watering node only forwards `health_state`, `health_conf`
and `health_age_s`. Panel 8 ("Camera health-data age") is the closest available proxy
for edge-ML link freshness.

To graph real inference time, add one field to the telemetry payload in
`src/watering/mqtt_pub.cpp` (and the `Telemetry` struct), e.g.
`doc["inference_ms"] = t.inference_ms;`, add `inference_ms = "int"` to the telemetry
object in `telegraf/telegraf.conf`, then point panel 8 at
`telemetry.inference_ms`.

## Editing the dashboard

Edit in the Grafana UI, then re-export (Share → Export → "Export for sharing
externally") and overwrite both `basil-irrigation.json` files so the change is
committed and re-provisioned on the next `docker compose up`.
