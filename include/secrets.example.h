// secrets.example.h
// Purpose: Template for secrets.h. Copy to include/secrets.h (git-ignored) and fill in.
// OWNER:   Caleb
// Node:    camera node + watering node (one secrets.h shared by both envs)

#pragma once
#define WIFI_SSID     "your-ssid"   // camera node only uses this to find the router channel
#define WIFI_PASS     "your-password"
#define MQTT_HOST     "broker.example.com"
#define MQTT_PORT     8883
#define MQTT_USER     "user"
#define MQTT_PASS     "pass"
#define MQTT_CLIENT_ID "watering-node"
