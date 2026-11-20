// activity_features.h - the 10 features, computed exactly like features() in the notebook.
// If you change a feature in the notebook, change it here too, or the model gets wrong inputs.
//
// Input : one 1.5 s window, 60 samples x {ax, ay, az (g), gx, gy, gz (deg/s)} at 40 Hz
// Output: x[0..9] = acc_mean, acc_std, acc_min, acc_max, acc_jerk,
//                   gyro_mean, gyro_std, gyro_max, cadence_hz, cadence_power
#pragma once
#include <math.h>

#define ACT_WIN 60          // samples per window (1.5 s at 40 Hz)
#define ACT_FS 40.0f        // sampling rate (Hz)
#define ACT_NFFT 256        // spectrum length (window padded with zeros, like np.fft.rfft(n=256))

static float act_cos[ACT_NFFT], act_sin[ACT_NFFT];
static bool act_tables_ready = false;

static void act_make_tables() {
  for (int i = 0; i < ACT_NFFT; i++) {
    act_cos[i] = (float)cos(2.0 * 3.14159265358979323846 * i / ACT_NFFT);
    act_sin[i] = (float)sin(2.0 * 3.14159265358979323846 * i / ACT_NFFT);
  }
  act_tables_ready = true;
}

static void act_compute_features(const float w[ACT_WIN][6], float *x) {
  if (!act_tables_ready) act_make_tables();
  double am[ACT_WIN], gm[ACT_WIN];
  for (int i = 0; i < ACT_WIN; i++) {
    am[i] = sqrt((double)w[i][0] * w[i][0] + (double)w[i][1] * w[i][1] + (double)w[i][2] * w[i][2]);
    gm[i] = sqrt((double)w[i][3] * w[i][3] + (double)w[i][4] * w[i][4] + (double)w[i][5] * w[i][5]);
  }
  // mean, standard deviation (divide by N, like numpy), min, max
  double aS = 0, gS = 0, aMin = am[0], aMax = am[0], gMax = gm[0], jerk = 0;
  for (int i = 0; i < ACT_WIN; i++) {
    aS += am[i]; gS += gm[i];
    if (am[i] < aMin) aMin = am[i];
    if (am[i] > aMax) aMax = am[i];
    if (gm[i] > gMax) gMax = gm[i];
    if (i) jerk += fabs(am[i] - am[i - 1]);
  }
  double aMean = aS / ACT_WIN, gMean = gS / ACT_WIN, aV = 0, gV = 0;
  for (int i = 0; i < ACT_WIN; i++) {
    aV += (am[i] - aMean) * (am[i] - aMean);
    gV += (gm[i] - gMean) * (gm[i] - gMean);
  }
  // cadence: strongest frequency between 0.5 and 4 Hz in the acceleration magnitude
  float c[ACT_WIN];
  for (int i = 0; i < ACT_WIN; i++) c[i] = (float)(am[i] - aMean);
  double total = 0, best = -1;
  int bestK = 0;
  for (int k = 0; k <= ACT_NFFT / 2; k++) {
    float re = 0, im = 0;
    for (int n = 0; n < ACT_WIN; n++) {
      int idx = (k * n) % ACT_NFFT;
      re += c[n] * act_cos[idx];
      im -= c[n] * act_sin[idx];
    }
    double pw = (double)re * re + (double)im * im;
    total += pw;
    float f = k * ACT_FS / ACT_NFFT;
    if (f >= 0.5f && f <= 4.0f && pw > best) { best = pw; bestK = k; }
  }
  x[0] = (float)aMean;
  x[1] = (float)sqrt(aV / ACT_WIN);
  x[2] = (float)aMin;
  x[3] = (float)aMax;
  x[4] = (float)(jerk / (ACT_WIN - 1));
  x[5] = (float)gMean;
  x[6] = (float)sqrt(gV / ACT_WIN);
  x[7] = (float)gMax;
  x[8] = bestK * ACT_FS / ACT_NFFT;
  x[9] = (float)(best / (total + 1e-9));
}
