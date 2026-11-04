// On-device plant health classifier executing TinyBasilCNN weights from model_weights.h.

#include <Arduino.h>
#include <math.h>
#include "img_converters.h"
#include "inference.h"
#include "model_weights.h"

static float s_input[3 * MODEL_INPUT_H * MODEL_INPUT_W];
static float s_conv_buf[8 * MODEL_INPUT_H * MODEL_INPUT_W]; // 8192 floats: reused for conv1 (8x32x32) and conv2 (16x16x16)
static float s_pool1[8 * 16 * 16];
static float s_pool2[16 * 8 * 8];
static float s_avg[16 * 4 * 4];

bool inference_init() {
  return true;
}

// Downsample arbitrary RGB565 or RGB888 buffer into 32x32 planar float [0.0, 1.0],
// cropping horizontally to the basil canopy ROI [20% .. 75% of width] so the
// picture frame (left 0..20%) and pot (right 75..100%) are excluded.
static bool extract_rgb_planar_32x32(const camera_fb_t* fb, float* out_chw) {
  if (!fb || !fb->buf || fb->width == 0 || fb->height == 0) {
    return false;
  }

  const int src_w = fb->width;
  const int src_h = fb->height;
  const int roi_x0 = (src_w * 20) / 100;
  const int roi_x1 = (src_w * 75) / 100;
  const int roi_w  = (roi_x1 > roi_x0) ? (roi_x1 - roi_x0) : src_w;

  if (fb->format == PIXFORMAT_RGB565) {
    const uint8_t* buf = fb->buf;
    for (int dy = 0; dy < MODEL_INPUT_H; ++dy) {
      int y0 = (dy * src_h) / MODEL_INPUT_H;
      int y1 = ((dy + 1) * src_h) / MODEL_INPUT_H;
      if (y1 <= y0) y1 = y0 + 1;

      for (int dx = 0; dx < MODEL_INPUT_W; ++dx) {
        int x0 = roi_x0 + (dx * roi_w) / MODEL_INPUT_W;
        int x1 = roi_x0 + ((dx + 1) * roi_w) / MODEL_INPUT_W;
        if (x1 <= x0) x1 = x0 + 1;

        float r_sum = 0.0f, g_sum = 0.0f, b_sum = 0.0f;
        int count = 0;
        for (int sy = y0; sy < y1; ++sy) {
          for (int sx = x0; sx < x1; ++sx) {
            int idx = (sy * src_w + sx) * 2;
            // Camera RGB565 is big-endian in memory: high byte first, low byte second
            uint16_t px = ((uint16_t)buf[idx] << 8) | (uint16_t)buf[idx + 1];
            uint8_t r5 = (px >> 11) & 0x1F;
            uint8_t g6 = (px >> 5)  & 0x3F;
            uint8_t b5 =  px        & 0x1F;
            r_sum += (float)((r5 << 3) | (r5 >> 2)) / 255.0f;
            g_sum += (float)((g6 << 2) | (g6 >> 4)) / 255.0f;
            b_sum += (float)((b5 << 3) | (b5 >> 2)) / 255.0f;
            count++;
          }
        }
        int out_idx = dy * MODEL_INPUT_W + dx;
        out_chw[0 * (MODEL_INPUT_H * MODEL_INPUT_W) + out_idx] = r_sum / count;
        out_chw[1 * (MODEL_INPUT_H * MODEL_INPUT_W) + out_idx] = g_sum / count;
        out_chw[2 * (MODEL_INPUT_H * MODEL_INPUT_W) + out_idx] = b_sum / count;
      }
    }
    return true;
  } else if (fb->format == PIXFORMAT_JPEG) {
    size_t rgb_len = (size_t)src_w * (size_t)src_h * 3;
    uint8_t* rgb_buf = (uint8_t*)ps_malloc(rgb_len);
    if (!rgb_buf) return false;

    if (!fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_buf)) {
      free(rgb_buf);
      return false;
    }

    for (int dy = 0; dy < MODEL_INPUT_H; ++dy) {
      int sy = (dy * src_h) / MODEL_INPUT_H;
      for (int dx = 0; dx < MODEL_INPUT_W; ++dx) {
        int sx = roi_x0 + (dx * roi_w) / MODEL_INPUT_W;
        int idx = (sy * src_w + sx) * 3;
        int out_idx = dy * MODEL_INPUT_W + dx;
        out_chw[0 * (MODEL_INPUT_H * MODEL_INPUT_W) + out_idx] = rgb_buf[idx + 0] / 255.0f;
        out_chw[1 * (MODEL_INPUT_H * MODEL_INPUT_W) + out_idx] = rgb_buf[idx + 1] / 255.0f;
        out_chw[2 * (MODEL_INPUT_H * MODEL_INPUT_W) + out_idx] = rgb_buf[idx + 2] / 255.0f;
      }
    }
    free(rgb_buf);
    return true;
  }

  return false;
}

static void conv2d_3x3_pad1_relu(
    const float* in, int in_c, int h, int w,
    const float* weights, const float* bias,
    float* out, int out_c) {
  for (int oc = 0; oc < out_c; ++oc) {
    const float b = bias[oc];
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        float sum = b;
        for (int ic = 0; ic < in_c; ++ic) {
          const float* in_ch = in + ic * (h * w);
          const float* w_ch = weights + (oc * in_c + ic) * 9;
          for (int ky = -1; ky <= 1; ++ky) {
            int sy = y + ky;
            if (sy < 0 || sy >= h) continue;
            for (int kx = -1; kx <= 1; ++kx) {
              int sx = x + kx;
              if (sx < 0 || sx >= w) continue;
              sum += in_ch[sy * w + sx] * w_ch[(ky + 1) * 3 + (kx + 1)];
            }
          }
        }
        out[oc * (h * w) + y * w + x] = (sum > 0.0f) ? sum : 0.0f;
      }
    }
  }
}

static void maxpool2d_2x2(const float* in, int c, int h, int w, float* out) {
  int out_h = h / 2;
  int out_w = w / 2;
  for (int ch = 0; ch < c; ++ch) {
    const float* in_ch = in + ch * (h * w);
    float* out_ch = out + ch * (out_h * out_w);
    for (int y = 0; y < out_h; ++y) {
      for (int x = 0; x < out_w; ++x) {
        float v00 = in_ch[(2 * y)     * w + (2 * x)];
        float v01 = in_ch[(2 * y)     * w + (2 * x + 1)];
        float v10 = in_ch[(2 * y + 1) * w + (2 * x)];
        float v11 = in_ch[(2 * y + 1) * w + (2 * x + 1)];
        float m0 = (v00 > v01) ? v00 : v01;
        float m1 = (v10 > v11) ? v10 : v11;
        out_ch[y * out_w + x] = (m0 > m1) ? m0 : m1;
      }
    }
  }
}

static void avgpool2d_2x2(const float* in, int c, int h, int w, float* out) {
  int out_h = h / 2;
  int out_w = w / 2;
  for (int ch = 0; ch < c; ++ch) {
    const float* in_ch = in + ch * (h * w);
    float* out_ch = out + ch * (out_h * out_w);
    for (int y = 0; y < out_h; ++y) {
      for (int x = 0; x < out_w; ++x) {
        float sum = in_ch[(2 * y)     * w + (2 * x)]
                  + in_ch[(2 * y)     * w + (2 * x + 1)]
                  + in_ch[(2 * y + 1) * w + (2 * x)]
                  + in_ch[(2 * y + 1) * w + (2 * x + 1)];
        out_ch[y * out_w + x] = 0.25f * sum;
      }
    }
  }
}

HealthResult inference_run(camera_fb_t* fb) {
  uint32_t t0 = millis();

  if (!extract_rgb_planar_32x32(fb, s_input)) {
    return {PLANT_UNKNOWN, 0.0f, millis() - t0};
  }

  // Layer 1: Conv(3->8, 3x3) + ReLU -> MaxPool(2x2) [32x32 -> 16x16]
  conv2d_3x3_pad1_relu(s_input, 3, 32, 32, CONV1_WEIGHT, CONV1_BIAS, s_conv_buf, 8);
  maxpool2d_2x2(s_conv_buf, 8, 32, 32, s_pool1);

  // Layer 2: Conv(8->16, 3x3) + ReLU -> MaxPool(2x2) [16x16 -> 8x8]
  conv2d_3x3_pad1_relu(s_pool1, 8, 16, 16, CONV2_WEIGHT, CONV2_BIAS, s_conv_buf, 16);
  maxpool2d_2x2(s_conv_buf, 16, 16, 16, s_pool2);

  // Layer 3: AvgPool(2x2) [8x8 -> 4x4, 256 features]
  avgpool2d_2x2(s_pool2, 16, 8, 8, s_avg);

  // Layer 4: Linear(256 -> 2)
  float logits[2] = {FC_BIAS[0], FC_BIAS[1]};
  for (int i = 0; i < 256; ++i) {
    logits[0] += s_avg[i] * FC_WEIGHT[0 * 256 + i];
    logits[1] += s_avg[i] * FC_WEIGHT[1 * 256 + i];
  }

  // Softmax probabilities (Class 0 = healthy, Class 1 = wilted)
  float max_l = (logits[0] > logits[1]) ? logits[0] : logits[1];
  float e0 = expf(logits[0] - max_l);
  float e1 = expf(logits[1] - max_l);
  float prob_healthy = e0 / (e0 + e1);
  float prob_wilted  = e1 / (e0 + e1);

  HealthResult out;
  out.inference_ms = millis() - t0;
  if (prob_wilted >= 0.50f) {
    out.state      = PLANT_WILTED;
    out.confidence = prob_wilted;
  } else {
    out.state      = PLANT_HEALTHY;
    out.confidence = prob_healthy;
  }
  return out;
}
