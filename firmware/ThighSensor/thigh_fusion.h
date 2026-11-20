// thigh_fusion.h - complementary filter for thigh tilt (sitting vs standing), and its window
// features. No hardware here, so it is also compiled and tested on a PC (tests/thigh_fusion_test).
//
// Why a complementary filter (decided Oct 2026, instead of a Kalman filter): it is a few lines,
// has one tuning value (alpha), runs easily at 40 Hz on the ESP32, and is enough for thigh tilt.
//
// What it does, every sample:
//   * the gyroscope is smooth but drifts: rotate the last gravity estimate by the measured turn
//   * the accelerometer does not drift but is noisy and jolted by steps: it pulls the estimate
//     gently back toward the measured gravity direction
//   g = alpha * (g rotated by gyro) + (1 - alpha) * (accel direction)
// alpha = 0.97 at 40 Hz trusts the gyro for short moves and the accelerometer over ~0.8 s
// (time constant = alpha * dt / (1 - alpha)). During hard impacts (|a| far from 1 g) the
// accelerometer is not used for that sample.
//
// Tilt = angle between the current gravity direction and the gravity direction recorded while
// STANDING (calibration). Standing ~0 deg, sitting ~80-90 deg, whatever way the box is strapped
// on, as long as it does not move on the leg after calibration.
#pragma once
#include <math.h>

#define TF_ALPHA     0.97f     // complementary filter weight on the gyro path
#define TF_ACC_MIN_G 0.75f     // use the accelerometer only when |a| is in this range
#define TF_ACC_MAX_G 1.25f

struct CompFilter {
  float g[3];                  // estimated gravity direction in the sensor's axes (unit vector)
  bool ready;
  unsigned long used, skipped; // accelerometer samples used / skipped (impacts)
};

static inline void tf_reset(CompFilter *cf) {
  cf->g[0] = 0; cf->g[1] = 0; cf->g[2] = 1; cf->ready = false; cf->used = 0; cf->skipped = 0;
}

static inline void tf_normalize(float *v) {
  float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (n > 1e-6f) { v[0] /= n; v[1] /= n; v[2] /= n; }
}

// a = accelerometer (g), w = gyroscope (deg/s), dt = seconds since the last sample
static inline void tf_update(CompFilter *cf, const float a[3], const float w[3], float dt) {
  float an = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  if (!cf->ready) {            // first sample: start from the accelerometer
    if (an < 1e-3f) return;
    cf->g[0] = a[0] / an; cf->g[1] = a[1] / an; cf->g[2] = a[2] / an;
    cf->ready = true;
    return;
  }
  // 1. gyro path: a fixed direction seen from a turning sensor moves by -w x g
  const float k = 0.017453293f * dt;                     // deg/s -> rad over dt
  float wx = w[0] * k, wy = w[1] * k, wz = w[2] * k;
  float gp[3] = {cf->g[0] - (wy * cf->g[2] - wz * cf->g[1]),
                 cf->g[1] - (wz * cf->g[0] - wx * cf->g[2]),
                 cf->g[2] - (wx * cf->g[1] - wy * cf->g[0])};
  tf_normalize(gp);
  // 2. accelerometer path, only when it mostly measures gravity
  if (an >= TF_ACC_MIN_G && an <= TF_ACC_MAX_G) {
    for (int i = 0; i < 3; i++) gp[i] = TF_ALPHA * gp[i] + (1.0f - TF_ALPHA) * (a[i] / an);
    tf_normalize(gp);
    cf->used++;
  } else {
    cf->skipped++;
  }
  cf->g[0] = gp[0]; cf->g[1] = gp[1]; cf->g[2] = gp[2];
}

// Angle (deg) between the current gravity direction and the standing reference.
static inline float tf_tilt_deg(const CompFilter *cf, const float ref[3]) {
  float d = cf->g[0] * ref[0] + cf->g[1] * ref[1] + cf->g[2] * ref[2];
  if (d > 1) d = 1;
  if (d < -1) d = -1;
  return acosf(d) * 57.29578f;
}

// Tilt from the accelerometer alone (no filter): angle between this sample's acceleration
// direction and the standing reference. Matches how the public training data was processed.
static inline float tf_tilt_acc_deg(const float a[3], const float ref[3]) {
  float n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  if (n < 1e-6f) return 0;
  float d = (a[0] * ref[0] + a[1] * ref[1] + a[2] * ref[2]) / n;
  if (d > 1) d = 1;
  if (d < -1) d = -1;
  return acosf(d) * 57.29578f;
}

// Mean and standard deviation (divide by N) of the tilt over one window.
static inline void tf_tilt_stats(const float *tilt, int n, float *mean, float *std) {
  double s = 0, v = 0;
  for (int i = 0; i < n; i++) s += tilt[i];
  double m = n ? s / n : 0;
  for (int i = 0; i < n; i++) v += (tilt[i] - m) * (tilt[i] - m);
  *mean = (float)m;
  *std = n ? (float)sqrt(v / n) : 0;
}
