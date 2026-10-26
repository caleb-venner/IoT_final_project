# cloud/

OWNER: Aida (templates only; fill in the TODOs)

Data path: watering node → **MQTT broker** → **Telegraf** → **InfluxDB** → **Grafana**.


## MQTT broker

| Item | Value |
|---|---|
| Provider / host | TODO(Aida) |
| Port | TODO(Aida) (8883 TLS / 1883 plain) |
| Auth | TODO(Aida) (username + password; stored in each device's `secrets.h`) |
| Topics | `farm/basil1/telemetry`, `farm/basil1/events`, `farm/basil1/status` (see `include/plant_msgs.h`) |

Quick check that the watering node is publishing:

```sh
mosquitto_sub -h <host> -p <port> -u <user> -P <pass> -t 'farm/#' -v
```

## InfluxDB

| Item | Value |
|---|---|
| URL | TODO(Aida) |
| Organisation | TODO(Aida) |
| Bucket | TODO(Aida) |
| Token | env var `INFLUX_TOKEN` (never committed) |

## Telegraf

> **Open decision: where Telegraf runs.** Caleb can host a Telegraf container at home
> (always on, next to the hardware) if that's easier than running it on Aida's machine.
> Aida still owns `telegraf.conf` and the topic/payload design; the container would just
> run that config unchanged, with secrets passed in as env vars. Decide together and record
> the outcome below.

- Runs on: TODO(Aida + Caleb) (Caleb's home container / laptop / Raspberry Pi / cloud VM)
- Config: `telegraf/telegraf.conf`
- Start:

  ```sh
  export INFLUX_TOKEN=...      # TODO(Aida)
  export MQTT_USER=... MQTT_PASS=...
  telegraf --config cloud/telegraf/telegraf.conf
  ```

## Grafana

- URL: TODO(Aida)
- Dashboards are exported as JSON into `grafana/`. See `grafana/README.md`.
