#!/usr/bin/env python3
"""
sim_publish.py — publish realistic fake telemetry/events/status to MQTT so the
whole cloud chain (Mosquitto -> Telegraf -> InfluxDB -> Grafana) can be tested
without the ESP32 hardware.

It mimics the watering node's payloads exactly (see include/plant_msgs.h and
src/watering/mqtt_pub.cpp):

  farm/basil1/telemetry  every 10 s   JSON
  farm/basil1/events     when it waters JSON
  farm/basil1/status     retained "online" on connect, "offline" last will

Usage:
  pip install paho-mqtt
  python sim_publish.py --host localhost --port 1883 --user basil --password change-me-mqtt

  # faster demo (publish every 2 s, simulated time 60x):
  python sim_publish.py --interval 2 --speed 60
"""
import argparse
import json
import math
import random
import sys
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:
    sys.exit("paho-mqtt not installed. Run: pip install paho-mqtt")

PLANT = "basil_aida_caleb"
T_TELEMETRY = f"farm/{PLANT}/telemetry"
T_EVENTS = f"farm/{PLANT}/events"
T_STATUS = f"farm/{PLANT}/status"


def parse_args():
    p = argparse.ArgumentParser(description="Simulate the watering node over MQTT.")
    p.add_argument("--host", default="broker.emqx.io")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--user", default="emqx")
    p.add_argument("--password", default="public")
    p.add_argument("--interval", type=float, default=10.0,
                   help="seconds between telemetry publishes (default 10)")
    p.add_argument("--speed", type=float, default=1.0,
                   help="simulated-time multiplier for the moisture curve (default 1)")
    return p.parse_args()


def main():
    args = parse_args()

    client = mqtt.Client(client_id="sim-watering-node")
    if args.user:
        client.username_pw_set(args.user, args.password)

    # Retained last will: broker publishes this if we disconnect uncleanly.
    client.will_set(T_STATUS, payload="offline", qos=1, retain=True)

    print(f"[sim] connecting to {args.host}:{args.port} ...", flush=True)
    client.connect(args.host, args.port, keepalive=30)
    client.loop_start()

    # Match the firmware: retained "online" on connect.
    client.publish(T_STATUS, "online", qos=1, retain=True)
    print(f"[sim] {T_STATUS} online (retained)", flush=True)

    # Simulation state
    moisture = 55.0            # start wet
    water_ml_total = 0.0
    health_states = ["healthy", "healthy", "healthy", "wilted", "unknown"]
    t = 0.0

    LOW, HIGH = 30.0, 55.0     # thresholds from config.h
    DOSE_MS = 4000             # WATER_DOSE_MS
    ML_PER_SEC = 6.25          # PUMP_ML_PER_SEC

    try:
        while True:
            # Drift moisture down over time, with a little noise.
            t += args.interval * args.speed
            moisture -= 0.8 * args.speed * (args.interval / 10.0)
            moisture += random.uniform(-0.4, 0.4)

            pump_on = 0
            health_state = random.choices(health_states,
                                          weights=[6, 6, 6, 2, 1])[0]
            health_conf = round(random.uniform(0.70, 0.98), 2)
            health_age_s = random.randint(2, 45)

            # Watering decision (simplified mirror of control_decide()):
            dry = moisture < LOW
            borderline_wilted = (LOW <= moisture < HIGH
                                 and health_state == "wilted"
                                 and health_conf >= 0.70)
            if dry or borderline_wilted:
                pump_on = 1
                ml = round(ML_PER_SEC * (DOSE_MS / 1000.0), 1)
                water_ml_total = round(water_ml_total + ml, 1)
                moisture = min(100.0, moisture + 22.0)  # watering recharges soil
                reason = "dry" if dry else "dry+wilted"
                event = {
                    "event": "pump_run",
                    "reason": reason,
                    "duration_ms": DOSE_MS,
                    "ml": ml,
                }
                client.publish(T_EVENTS, json.dumps(event), qos=0)
                print(f"[sim] {T_EVENTS} {json.dumps(event)}", flush=True)

            moisture = max(0.0, min(100.0, moisture))
            moisture_raw = int(200 + (moisture / 100.0) * (2800 - 200))

            telemetry = {
                "moisture_pct": round(moisture, 1),
                "moisture_raw": moisture_raw,
                "pump": pump_on,
                "health_state": health_state,
                "health_conf": health_conf,
                "health_age_s": health_age_s,
                "water_ml_total": water_ml_total,
            }
            client.publish(T_TELEMETRY, json.dumps(telemetry), qos=0)
            print(f"[sim] {T_TELEMETRY} {json.dumps(telemetry)}", flush=True)

            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n[sim] stopping, publishing offline ...", flush=True)
        client.publish(T_STATUS, "offline", qos=1, retain=True)
        time.sleep(0.5)
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
