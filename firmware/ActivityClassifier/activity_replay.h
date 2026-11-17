// activity_replay.h - replays PAMAP2 windows from replay.bin through the on-watch model.
// replay.bin is written by Part 4 of wrist_classifier_pamap2.ipynb.
//
// File layout (little-endian)
//   header, 32 bytes : "ACTR", version, windows, window length, classes, model id,
//                      Colab correct count, spare
//   each window      : subject (1-8), true label, Colab prediction, spare (4 bytes)
//                      then 60 x 6 float32 (ax ay az in g, gx gy gz in deg/s)
#pragma once
#include <stdint.h>
#include <string.h>
#include "activity_features.h"
#include "activity_model.h"

#define RP_REC_BYTES (4 + ACT_WIN * 6 * 4)
#define RP_MAX_SUBJ 16

typedef struct {
  char magic[4];
  uint32_t version, windows, win_len, n_classes, model_id, colab_correct, spare;
} RpHeader;

typedef struct {
  uint32_t total;                       // windows in the file
  uint32_t done, correct, agree;        // processed, matches true label, matches Colab
  uint32_t trueN[ACT_N_CLASSES], hitN[ACT_N_CLASSES];
  uint32_t subjN[RP_MAX_SUBJ], subjOk[RP_MAX_SUBJ];
  uint64_t busyUs;                      // time spent in features + model
  uint32_t colabCorrect;
} RpStats;

// 0 = ok, otherwise a short reason
static const char *rp_check_header(const RpHeader *h) {
  if (memcmp(h->magic, "ACTR", 4) != 0 || h->version != 1) return "not a replay file";
  if (h->win_len != ACT_WIN || h->n_classes != ACT_N_CLASSES) return "window/class mismatch";
  if (h->model_id != ACT_MODEL_ID) return "made for another model";
  if (h->windows == 0) return "file is empty";
  return 0;
}

static void rp_reset(RpStats *s, const RpHeader *h) {
  memset(s, 0, sizeof(*s));
  s->total = h->windows;
  s->colabCorrect = h->colab_correct;
}

// Runs one window. usNow() should return microseconds (micros() on the watch).
static void rp_process(const uint8_t *rec, RpStats *s, uint32_t (*usNow)(void)) {
  static float w[ACT_WIN][6];
  memcpy(w, rec + 4, sizeof(w));        // copy: the floats in the file are not aligned
  uint8_t subj = rec[0], truth = rec[1], colab = rec[2];
  float x[ACT_N_FEATURES];
  uint32_t t0 = usNow();
  act_compute_features(w, x);
  int pred = act_predict(x, 0);
  s->busyUs += (uint32_t)(usNow() - t0);
  s->done++;
  if (pred == colab) s->agree++;
  if (truth < ACT_N_CLASSES) {
    s->trueN[truth]++;
    if (pred == truth) { s->correct++; s->hitN[truth]++; }
  }
  if (subj < RP_MAX_SUBJ) {
    s->subjN[subj]++;
    if (pred == truth) s->subjOk[subj]++;
  }
}
