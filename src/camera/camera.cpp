// camera.cpp
// Purpose: Camera sensor init and frame capture via esp_camera (supports OV7670/OV2640 in RGB565 mode
//          with idle XCLK clock gating to eliminate inductor coil whine between captures).
// OWNER:   Caleb
// Node:    camera node (Freenove ESP32-WROVER CAM)

#include <Arduino.h>
#include "camera.h"
#include "config.h"

static void silence_camera_xclk() {
  gpio_matrix_out(CAM_PIN_XCLK, SIG_GPIO_OUT_IDX, false, false);
  pinMode(CAM_PIN_XCLK, OUTPUT);
  digitalWrite(CAM_PIN_XCLK, LOW);
}

static void wake_camera_xclk() {
  ledcAttachPin(CAM_PIN_XCLK, LEDC_CHANNEL_0);
  delay(80);
}

bool camera_init() {
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

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("[camera] esp_camera_init failed: 0x%x (%s)\n",
                  (unsigned)err, esp_err_to_name(err));
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_gain_ctrl(s, 1);
  }

  silence_camera_xclk();
  return true;
}

camera_fb_t* camera_capture() {
  wake_camera_xclk();

  for (int i = 0; i < WARMUP_FRAMES; i++) {
    camera_fb_t* warmup = esp_camera_fb_get();
    if (warmup) {
      esp_camera_fb_return(warmup);
    }
    delay(50);
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    silence_camera_xclk();
  }
  return fb;
}

void camera_release(camera_fb_t* fb) {
  if (fb) {
    esp_camera_fb_return(fb);
  }
  silence_camera_xclk();
}
