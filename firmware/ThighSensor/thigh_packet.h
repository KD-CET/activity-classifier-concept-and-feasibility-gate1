// thigh_packet.h - the radio messages between the thigh unit and the watch (ESP-NOW).
// The SAME file is in firmware/ThighSensor/ and firmware/ActivityClassifier/: if you change one,
// copy it to the other, or the two devices will not understand each other.
//
//   thigh unit -> watch   TP_HELLO     "is a watch on this channel?" (sent while searching)
//   thigh unit -> watch   TP_FEATURES  one 1.5 s window of thigh features (every 1.5 s)
//   watch -> thigh unit   TP_ACK       "got it", with the watch's Wi-Fi channel
//
// Pairing needs no MAC addresses typed in: the thigh unit hops channels sending TP_HELLO until a
// watch answers with TP_ACK, then stays on the channel the watch reports (the channel of the
// watch's Wi-Fi network, because the watch stays connected to it for MQTT).
#pragma once
#include <stdint.h>
#include <string.h>

#define TP_MAGIC   0x4341444Bu       // "KDAC" in memory: ignore any other ESP-NOW traffic
#define TP_VERSION 2                 // 2: 14 features (accelerometer-only tilt added)

enum { TP_HELLO = 1, TP_FEATURES = 2, TP_ACK = 3 };
enum { TP_FLAG_IMU_OK = 0x01, TP_FLAG_CALIBRATED = 0x02 };

// Thigh features, in this order:
//   0-9   the watch's 10 motion features (activity_features.h), on the thigh sensor
//   10-11 tilt from the COMPLEMENTARY FILTER (gyro + accelerometer): mean and spread
//   12-13 tilt from the accelerometer alone: mean and spread. The thigh model on the watch was
//         trained on public thigh data that has no gyroscope, so it uses these two, computed
//         exactly as in its training data; 10-11 are for display and for models trained on
//         your own recordings.
#define TP_N_FEAT 14
static const char *TP_FEAT_NAMES[TP_N_FEAT] = {
    "t_acc_mean", "t_acc_std", "t_acc_min", "t_acc_max", "t_acc_jerk",
    "t_gyro_mean", "t_gyro_std", "t_gyro_max", "t_cadence_hz", "t_cadence_power",
    "t_tilt_mean", "t_tilt_std", "t_tilt_acc_mean", "t_tilt_acc_std"};

struct __attribute__((packed)) TpHeader {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint8_t channel;                   // sender's current Wi-Fi channel
  uint8_t flags;                     // TP_FLAG_*
};

struct __attribute__((packed)) TpHello {
  TpHeader h;
  uint32_t boot_ms;
};

struct __attribute__((packed)) TpFeatures {
  TpHeader h;
  uint32_t seq;                      // window counter, +1 per window (gaps = lost packets, C7)
  uint32_t board_ms;                 // thigh unit's millis() at the end of the window
  uint16_t samples;                  // good sensor readings in the window (60 = none missed)
  uint16_t i2c_errors_window;        // failed sensor reads in this window (C16)
  uint32_t i2c_errors_total;         // since power-on
  float f[TP_N_FEAT];
};

struct __attribute__((packed)) TpAck {
  TpHeader h;
  uint32_t seq;                      // last TP_FEATURES seq received (0 for a HELLO)
  uint32_t received;                 // TP_FEATURES packets received since the watch started
};

static inline void tp_header(TpHeader *h, uint8_t type, uint8_t channel, uint8_t flags) {
  h->magic = TP_MAGIC; h->version = TP_VERSION; h->type = type; h->channel = channel; h->flags = flags;
}

// Returns the message type if data is a valid message of a known type and length, else 0.
static inline int tp_check(const uint8_t *data, int len) {
  if (len < (int)sizeof(TpHeader)) return 0;
  TpHeader h;
  memcpy(&h, data, sizeof(h));
  if (h.magic != TP_MAGIC || h.version != TP_VERSION) return 0;
  if (h.type == TP_HELLO && len == (int)sizeof(TpHello)) return TP_HELLO;
  if (h.type == TP_FEATURES && len == (int)sizeof(TpFeatures)) return TP_FEATURES;
  if (h.type == TP_ACK && len == (int)sizeof(TpAck)) return TP_ACK;
  return 0;
}
