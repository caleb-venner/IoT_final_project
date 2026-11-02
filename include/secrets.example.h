// secrets.example.h
// Purpose: Template for secrets.h. Copy to include/secrets.h (git-ignored) and fill in.
// OWNER:   Caleb
// Node:    camera node + watering node (one secrets.h shared by both envs)

#pragma once
#define WIFI_SSID     "your-ssid"   // camera node only uses this to find the router channel
#define WIFI_PASS     "your-password"
#define MQTT_HOST     "broker.emqx.io"
#define MQTT_PORT     1883
#define MQTT_USER     "emqx"
#define MQTT_PASS     "public"
#define MQTT_CLIENT_ID "watering-node"
