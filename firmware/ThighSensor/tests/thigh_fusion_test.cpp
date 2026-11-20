// thigh_fusion_test.cpp - checks the complementary filter on a PC with a simulated thigh.
//   g++ -O2 -o thigh_fusion_test thigh_fusion_test.cpp && ./thigh_fusion_test
// Scenario (40 Hz): stand 5 s, sit down (thigh turns 90 deg in 1 s), sit 10 s, stand up,
// walk 20 s (thigh swings +-25 deg at 0.9 Hz, step impacts, sensor noise, gyro bias 1.5 deg/s).
// Compares the filter with accelerometer-only and gyro-only tilt against the true angle.
#include <cstdio>
#include <cmath>
#include <random>
#include "../thigh_fusion.h"

int main() {
  const float fs = 40, dt = 1 / fs, D2R = 0.0174533f;
  std::mt19937 rng(7);
  std::normal_distribution<float> an(0, 0.03f), gn(0, 2.0f);
  const float bias = 1.5f;                         // deg/s gyro bias
  CompFilter cf; tf_reset(&cf);
  float ref[3] = {0, 0, 1};                        // standing pose = calibration
  float gyroOnly = 0;
  struct Seg { const char *name; float t0, t1; double errC, errA, errG; int n; };
  Seg seg[] = {{"standing", 0, 5, 0, 0, 0, 0}, {"sitting", 7, 16, 0, 0, 0, 0},
               {"walking", 19, 39, 0, 0, 0, 0}};
  float prevTh = 0;
  for (int i = 0; i < (int)(39 * fs); i++) {
    float t = i * dt, th;
    if (t < 5) th = 0;
    else if (t < 6) th = 90 * (t - 5);
    else if (t < 16) th = 90;
    else if (t < 17) th = 90 * (17 - t);
    else if (t < 19) th = 0;
    else th = 25 * sinf(2 * 3.14159f * 0.9f * (t - 19));
    float thd = (th - prevTh) / dt;                // true turn rate (deg/s)
    prevTh = th;
    float s = sinf(th * D2R), c = cosf(th * D2R);
    float lin = (t > 19) ? 0.6f * powf(fmaxf(0.f, sinf(2 * 3.14159f * 1.8f * t)), 8) : 0;  // step jolts
    float a[3] = {an(rng), s + an(rng) + 0.3f * lin, c + an(rng) + lin};
    float w[3] = {thd + bias + gn(rng), gn(rng), gn(rng)};
    tf_update(&cf, a, w, dt);
    float tC = tf_tilt_deg(&cf, ref);
    float aN = sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
    float tA = acosf(fminf(1.f, fmaxf(-1.f, a[2] / aN))) / D2R;
    gyroOnly += w[0] * dt;
    float trueTilt = fabsf(th);
    for (auto &g : seg)
      if (t >= g.t0 && t < g.t1) {
        g.errC += fabsf(tC - trueTilt); g.errA += fabsf(tA - trueTilt);
        g.errG += fabsf(fabsf(gyroOnly) - trueTilt); g.n++;
      }
  }
  printf("Mean absolute tilt error (deg), simulated thigh, 40 Hz, alpha %.2f\n", TF_ALPHA);
  printf("%-10s %16s %18s %12s\n", "segment", "complementary", "accelerometer only", "gyro only");
  bool ok = true;
  for (auto &g : seg) {
    printf("%-10s %16.2f %18.2f %12.2f\n", g.name, g.errC / g.n, g.errA / g.n, g.errG / g.n);
    if (g.errC / g.n > 5) ok = false;
  }
  printf("accelerometer samples used %lu, skipped as impacts %lu\n", cf.used, cf.skipped);
  float m, sd; float demo[4] = {80, 82, 84, 86}; tf_tilt_stats(demo, 4, &m, &sd);
  if (fabsf(m - 83) > 1e-4 || fabsf(sd - 2.2360680f) > 1e-4) ok = false;
  printf("tilt stats check (80,82,84,86 -> mean 83, std 2.236): %.3f %.3f\n", m, sd);
  printf("%s: complementary filter error <= 5 deg in every segment\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
