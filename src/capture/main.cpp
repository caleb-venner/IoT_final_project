// main.cpp
// Purpose: Training data collection. Saves a JPEG to SD (SD_MMC) every CAPTURE_INTERVAL_MS
//          with sequential filenames (/img_00001.jpg, ...).
// OWNER:   Aida (scaffold only)
// Node:    camera board (Freenove ESP32-WROVER CAM)

#include <Arduino.h>
#include "esp_camera.h"
#include "FS.h"
#include "SD_MMC.h"
#include "config.h"

static uint32_t s_index = 0;

static bool camera_init() {
  camera_config_t cfg = {};
  cfg.pin_pwdn = CAM_PIN_PWDN;     cfg.pin_reset = CAM_PIN_RESET;
  cfg.pin_xclk = CAM_PIN_XCLK;     cfg.pin_sccb_sda = CAM_PIN_SIOD;
  cfg.pin_sccb_scl = CAM_PIN_SIOC;
  cfg.pin_d7 = CAM_PIN_Y9;  cfg.pin_d6 = CAM_PIN_Y8;  cfg.pin_d5 = CAM_PIN_Y7;  cfg.pin_d4 = CAM_PIN_Y6;
  cfg.pin_d3 = CAM_PIN_Y5;  cfg.pin_d2 = CAM_PIN_Y4;  cfg.pin_d1 = CAM_PIN_Y3;  cfg.pin_d0 = CAM_PIN_Y2;
  cfg.pin_vsync = CAM_PIN_VSYNC;   cfg.pin_href = CAM_PIN_HREF;  cfg.pin_pclk = CAM_PIN_PCLK;
  cfg.xclk_freq_hz = 20000000;
  cfg.ledc_timer = LEDC_TIMER_0;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size = FRAMESIZE_QVGA;  // TODO(Aida): pick a resolution for training
  cfg.jpeg_quality = 10;
  cfg.fb_count = 1;
  cfg.fb_location = CAMERA_FB_IN_PSRAM;
  cfg.grab_mode = CAMERA_GRAB_LATEST;
  return esp_camera_init(&cfg) == ESP_OK;
}

// Continue numbering after any images already on the card.
static void find_next_index() {
  char path[32];
  do {
    s_index++;
    snprintf(path, sizeof(path), "/img_%05lu.jpg", (unsigned long)s_index);
  } while (SD_MMC.exists(path));
}

static void capture_one() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[capture] camera capture failed");
    return;
  }
  char path[32];
  snprintf(path, sizeof(path), "/img_%05lu.jpg", (unsigned long)s_index);
  File f = SD_MMC.open(path, FILE_WRITE);
  if (f) {
    f.write(fb->buf, fb->len);
    f.close();
    Serial.printf("[capture] saved %s (%u bytes)\n", path, (unsigned)fb->len);
    s_index++;
  } else {
    Serial.printf("[capture] failed to open %s\n", path);
  }
  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  if (!camera_init()) Serial.println("[capture] camera init failed");
  if (!SD_MMC.begin("/sdcard", SD_ONE_BIT_MODE)) {
    Serial.println("[capture] SD_MMC mount failed");
    return;
  }
  find_next_index();
  Serial.printf("[capture] starting at index %lu\n", (unsigned long)s_index);
}

void loop() {
  static uint32_t last = 0;
  if (last == 0 || millis() - last >= CAPTURE_INTERVAL_MS) {
    last = millis();
    capture_one();
  }
  delay(100);

  // TODO(Aida): labelling workflow (e.g. sort into healthy/ and wilted/ folders on the PC).
  // TODO(Aida): upload labelled images to Edge Impulse (Studio uploader or edge-impulse-uploader CLI).
}
