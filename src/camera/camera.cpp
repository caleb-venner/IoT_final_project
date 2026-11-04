// Camera sensor initialisation and frame capture via esp_camera, with automatic
// re-init recovery if a shared-5V rail dip wedges the DVP frame grabber.

#include <Arduino.h>
#include "camera.h"
#include "config.h"

static bool s_initialised = false;

static camera_config_t make_camera_config() {
  camera_config_t cfg = {};
  cfg.pin_pwdn     = CAM_PIN_PWDN;
  cfg.pin_reset    = CAM_PIN_RESET;
  cfg.pin_xclk     = CAM_PIN_XCLK;
  cfg.pin_sccb_sda = CAM_PIN_SIOD;
  cfg.pin_sccb_scl = CAM_PIN_SIOC;
  cfg.pin_d7       = CAM_PIN_Y9;
  cfg.pin_d6       = CAM_PIN_Y8;
  cfg.pin_d5       = CAM_PIN_Y7;
  cfg.pin_d4       = CAM_PIN_Y6;
  cfg.pin_d3       = CAM_PIN_Y5;
  cfg.pin_d2       = CAM_PIN_Y4;
  cfg.pin_d1       = CAM_PIN_Y3;
  cfg.pin_d0       = CAM_PIN_Y2;
  cfg.pin_vsync    = CAM_PIN_VSYNC;
  cfg.pin_href     = CAM_PIN_HREF;
  cfg.pin_pclk     = CAM_PIN_PCLK;
  cfg.xclk_freq_hz = 10000000;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.pixel_format = PIXFORMAT_RGB565;
  cfg.frame_size   = FRAMESIZE_QVGA;
  cfg.jpeg_quality = 12;
  cfg.fb_count     = 1;
  cfg.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  return cfg;
}

bool camera_init() {
  camera_config_t cfg = make_camera_config();

  for (int attempt = 1; attempt <= 3; ++attempt) {
    esp_err_t err = esp_camera_init(&cfg);
    if (err == ESP_OK) {
      sensor_t* s = esp_camera_sensor_get();
      if (s) {
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);
        s->set_gain_ctrl(s, 1);
      }
      s_initialised = true;
      Serial.printf("[camera] esp_camera_init succeeded (attempt %d)\n", attempt);
      return true;
    }
    Serial.printf("[camera] esp_camera_init attempt %d failed: 0x%x (%s)\n",
                  attempt, (unsigned)err, esp_err_to_name(err));
    esp_camera_deinit();
    delay(250);
  }

  s_initialised = false;
  return false;
}

camera_fb_t* camera_capture() {
  if (!s_initialised && !camera_init()) {
    return nullptr;
  }

  for (int i = 0; i < WARMUP_FRAMES; i++) {
    camera_fb_t* warmup = esp_camera_fb_get();
    if (warmup) {
      esp_camera_fb_return(warmup);
    }
    delay(40);
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    // Self-heal if a 5V rail voltage dip wedged the DVP sensor state machine
    Serial.println("[camera] frame grab timed out, re-initialising sensor...");
    esp_camera_deinit();
    s_initialised = false;
    delay(150);
    if (camera_init()) {
      delay(60);
      fb = esp_camera_fb_get();
    }
  }
  return fb;
}

void camera_release(camera_fb_t* fb) {
  if (fb) {
    esp_camera_fb_return(fb);
  }
}
