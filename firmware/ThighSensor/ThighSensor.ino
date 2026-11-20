/*
  ThighSensor.ino - the thigh unit: ESP32-WROOM dev board + MPU-6050 (GY-521), in the 3D-printed
  box strapped to the front of the thigh. It sends one feature packet per 1.5 s window to the
  watch over ESP-NOW (no router needed between them). The watch fuses them with its own wrist
  features and reports "wrist+thigh"; without packets it falls back to wrist only (REQ-AC-10).

  Every 25 ms (40 Hz, same as the watch):
    read the MPU-6050 -> complementary filter (thigh tilt) -> store the sample
  Every 60 samples (1.5 s):
    10 motion features (same code as the watch, activity_features.h) + tilt mean and spread,
    from the complementary filter and from the accelerometer alone (14 features)
    -> TP_FEATURES packet to the watch; one CSV line on the Serial Monitor

  Files in this folder (keep them together):
    ThighSensor.ino, thigh_fusion.h (complementary filter), thigh_packet.h (radio messages,
    same file as in ActivityClassifier), activity_features.h (same file as in ActivityClassifier)

  Wiring (GY-521 -> ESP32): VCC -> 3V3, GND -> GND, SCL -> GPIO22, SDA -> GPIO21, AD0 -> GND (0x68)
  Power: see firmware/ThighSensor/README.txt (battery wiring and the regulator note).

  Arduino IDE: board "ESP32 Dev Module", Upload Speed 921600, everything else default.
  Libraries: none (Wire, WiFi, ESP-NOW and Preferences come with the ESP32 board package).

  Buttons and light
    BOOT button, hold 2 s while STANDING STILL: records the standing pose (tilt 0). Saved, so
                 it survives power-off. Do it again whenever the box is re-strapped.
    LED (GPIO2): slow blink = searching for the watch; short blink every 1.5 s = linked and
                 sending; fast blink = sensor error; 3 quick flashes = calibration saved.
*/
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <esp_idf_version.h>
#include "activity_features.h"
#include "thigh_fusion.h"
#include "thigh_packet.h"

// ---------------- Settings ------------------------------------------------------------------
#define PIN_SDA 21
#define PIN_SCL 22
#define PIN_LED 2                    // on-board LED on most ESP32-WROOM dev boards
#define PIN_BTN 0                    // BOOT button (free to use after start-up)
#define MPU_ADDR 0x68
#define I2C_HZ 100000                // 100 kHz: slower but more tolerant of loose jumper wires
#define TX_POWER WIFI_POWER_8_5dBm   // lower radio power: shorter current peaks on battery;
                                     // plenty for watch <-> thigh (under 2 m)
#define SEARCH_DWELL_MS 250          // searching: time on each channel
#define HELLO_EVERY_MS 80            // searching: HELLO interval
#define LOST_AFTER_FAILS 4           // linked: this many failed sends in a row -> search again
#define LOST_AFTER_MS 10000          // linked: no ack from the watch for this long -> search again

// MPU-6050 scales for the ranges set in mpuBegin(): +-8 g, +-1000 deg/s
#define ACC_LSB_PER_G 4096.0f
#define GYR_LSB_PER_DPS 32.8f

// ---------------- Sensor --------------------------------------------------------------------
static volatile uint32_t i2cErrTotal = 0;
static volatile bool imuOk = false;

static bool mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool mpuBegin() {
  Wire.begin(PIN_SDA, PIN_SCL, I2C_HZ);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75);                                   // WHO_AM_I
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1) != 1) return false;
  uint8_t who = Wire.read();
  Serial.printf("[imu] WHO_AM_I 0x%02X%s\n", who, who == 0x68 ? "" : " (clone chip, usually fine)");
  bool ok = mpuWrite(0x6B, 0x01)                      // wake up, clock from the X gyro
         && mpuWrite(0x1A, 0x04)                      // low-pass ~21 Hz (no aliasing at 40 Hz)
         && mpuWrite(0x19, 0x04)                      // internal sample rate 200 Hz
         && mpuWrite(0x1B, 0x10)                      // gyro +-1000 deg/s
         && mpuWrite(0x1C, 0x10);                     // accel +-8 g (running jolts stay in range)
  delay(50);
  return ok;
}

// One reading: a = g, w = deg/s. False on an I2C error.
static bool mpuRead(float a[3], float w[3]) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) != 14) return false;
  uint8_t b[14];
  for (int i = 0; i < 14; i++) b[i] = Wire.read();
  for (int i = 0; i < 3; i++) {
    a[i] = (int16_t)((b[2 * i] << 8) | b[2 * i + 1]) / ACC_LSB_PER_G;
    w[i] = (int16_t)((b[8 + 2 * i] << 8) | b[9 + 2 * i]) / GYR_LSB_PER_DPS;
  }
  return true;
}

// ---------------- Calibration (standing pose) -------------------------------------------------
static Preferences prefs;
static float refG[3] = {0, 0, 1};
static bool calibrated = false;

static void loadCalibration() {
  prefs.begin("thigh", true);
  calibrated = prefs.getBool("cal", false);
  if (calibrated) {
    refG[0] = prefs.getFloat("gx", 0); refG[1] = prefs.getFloat("gy", 0); refG[2] = prefs.getFloat("gz", 1);
  }
  prefs.end();
  Serial.printf("[cal] %s: %.3f %.3f %.3f\n", calibrated ? "standing pose loaded" : "NOT calibrated yet",
                refG[0], refG[1], refG[2]);
}

static void saveCalibration(const float g[3]) {
  for (int i = 0; i < 3; i++) refG[i] = g[i];
  calibrated = true;
  prefs.begin("thigh", false);
  prefs.putBool("cal", true);
  prefs.putFloat("gx", g[0]); prefs.putFloat("gy", g[1]); prefs.putFloat("gz", g[2]);
  prefs.end();
  Serial.printf("[cal] standing pose saved: %.3f %.3f %.3f\n", g[0], g[1], g[2]);
}

// ---------------- Radio (ESP-NOW) -------------------------------------------------------------
enum { L_SEARCH = 0, L_LINKED };
static volatile int linkMode = L_SEARCH;
static uint8_t channel = 1;
static uint8_t watchMac[6];
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static volatile bool ackSeen = false;               // set by the receive callback
static volatile uint8_t ackChannel = 0;
static volatile uint32_t lastAckMs = 0, ackReceived = 0;
static uint8_t ackMac[6];
static volatile int sendFails = 0;
static uint32_t sentOk = 0, sentFail = 0;

static void setChannel(uint8_t ch) {
  channel = ch;
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

static void addPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0;                                     // 0 = whatever channel the radio is on
  p.ifidx = WIFI_IF_STA;
  p.encrypt = false;
  esp_now_add_peer(&p);
}

static void handleRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (tp_check(data, len) != TP_ACK) return;
  TpAck a;
  memcpy(&a, data, sizeof(a));
  memcpy(ackMac, mac, 6);
  ackChannel = a.h.channel;
  ackReceived = a.received;
  lastAckMs = millis();
  ackSeen = true;
}

static void handleSent(esp_now_send_status_t st) {
  if (st == ESP_NOW_SEND_SUCCESS) { sendFails = 0; sentOk++; }
  else { sendFails = sendFails + 1; sentFail++; }
}

// The callback signatures changed between ESP32 board package versions
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) { handleRecv(info->src_addr, data, len); }
#else
static void onRecv(const uint8_t *mac, const uint8_t *data, int len) { handleRecv(mac, data, len); }
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void onSent(const wifi_tx_info_t *, esp_now_send_status_t st) { handleSent(st); }
#else
static void onSent(const uint8_t *, esp_now_send_status_t st) { handleSent(st); }
#endif

static void radioBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();                                 // ESP-NOW only, no router
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setTxPower(TX_POWER);
  if (esp_now_init() != ESP_OK) { Serial.println("[radio] ESP-NOW init FAILED"); return; }
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  addPeer(BCAST);
  setChannel(1);
  Serial.printf("[radio] thigh unit MAC %s, searching for the watch\n", WiFi.macAddress().c_str());
}

static uint8_t flags() { return (imuOk ? TP_FLAG_IMU_OK : 0) | (calibrated ? TP_FLAG_CALIBRATED : 0); }

// Called often from loop(): channel hopping while searching, link-loss detection while linked.
static void radioLoop() {
  static uint32_t lastHop = 0, lastHello = 0;
  if (ackSeen) {                                     // a watch answered
    ackSeen = false;
    uint8_t ch = ackChannel;
    if (linkMode == L_SEARCH || ch != channel || memcmp(ackMac, watchMac, 6) != 0) {
      memcpy(watchMac, ackMac, 6);
      addPeer(watchMac);
      if (ch >= 1 && ch <= 13 && ch != channel) setChannel(ch);
      if (linkMode == L_SEARCH)
        Serial.printf("[radio] linked to watch %02X:%02X:%02X:%02X:%02X:%02X on channel %d\n",
                      watchMac[0], watchMac[1], watchMac[2], watchMac[3], watchMac[4], watchMac[5], channel);
      linkMode = L_LINKED;
      sendFails = 0;
    }
  }
  uint32_t now = millis();
  if (linkMode == L_LINKED && (sendFails >= LOST_AFTER_FAILS || now - lastAckMs > LOST_AFTER_MS)) {
    Serial.printf("[radio] lost the watch (%d failed sends, last ack %lu ms ago): searching\n",
                  (int)sendFails, (unsigned long)(now - lastAckMs));
    linkMode = L_SEARCH;
  }
  if (linkMode == L_SEARCH) {
    if (now - lastHop >= SEARCH_DWELL_MS) { setChannel(channel % 13 + 1); lastHop = now; }
    if (now - lastHello >= HELLO_EVERY_MS) {
      TpHello h;
      tp_header(&h.h, TP_HELLO, channel, flags());
      h.boot_ms = now;
      esp_now_send(BCAST, (const uint8_t *)&h, sizeof(h));
      lastHello = now;
    }
  }
}

static void sendFeatures(const TpFeatures &p) {
  if (linkMode != L_LINKED) return;                  // nobody to send to; the window is lost
  esp_now_send(watchMac, (const uint8_t *)&p, sizeof(p));
}

// ---------------- Sampling (own task, exact 40 Hz) --------------------------------------------
#define RING 256
static float ring[RING][8];                          // ax ay az gx gy gz tilt tilt_acc
static volatile uint32_t rHead = 0;
static CompFilter cf;
static portMUX_TYPE cfMux = portMUX_INITIALIZER_UNLOCKED;

static void samplerTask(void *) {
  TickType_t next = xTaskGetTickCount();
  float a[3], w[3];
  int consecutive = 0;
  for (;;) {
    vTaskDelayUntil(&next, pdMS_TO_TICKS(25));
    if (!mpuRead(a, w)) {
      i2cErrTotal = i2cErrTotal + 1;
      if (++consecutive >= 5) { imuOk = false; Wire.end(); mpuBegin(); consecutive = 0; }   // try to recover
      continue;
    }
    consecutive = 0;
    imuOk = true;
    portENTER_CRITICAL(&cfMux);
    tf_update(&cf, a, w, 0.025f);
    float tilt = tf_tilt_deg(&cf, refG);
    float tiltAcc = tf_tilt_acc_deg(a, refG);
    portEXIT_CRITICAL(&cfMux);
    float *s = ring[rHead % RING];
    s[0] = a[0]; s[1] = a[1]; s[2] = a[2]; s[3] = w[0]; s[4] = w[1]; s[5] = w[2]; s[6] = tilt; s[7] = tiltAcc;
    __sync_synchronize();
    rHead = rHead + 1;
  }
}

// ---------------- Window -> features -> packet -------------------------------------------------
static float win[ACT_WIN][6];
static float winTilt[ACT_WIN], winTiltAcc[ACT_WIN];
static int winFill = 0;
static uint32_t rTail = 0, windowSeq = 0;
static uint32_t errAtLastWindow = 0;
static uint32_t lastPacketMs = 0;

static void finishWindow() {
  TpFeatures p;
  tp_header(&p.h, TP_FEATURES, channel, flags());
  p.seq = ++windowSeq;
  p.board_ms = millis();
  p.samples = ACT_WIN;                               // good readings (failed ones are skipped)
  uint32_t e = i2cErrTotal;
  p.i2c_errors_window = (uint16_t)(e - errAtLastWindow);
  errAtLastWindow = e;
  p.i2c_errors_total = e;
  float f[TP_N_FEAT];                                // computed here, then copied (the packet is packed)
  act_compute_features(win, f);                      // f[0..9], same as the wrist
  tf_tilt_stats(winTilt, ACT_WIN, &f[10], &f[11]);   // complementary-filter tilt
  tf_tilt_stats(winTiltAcc, ACT_WIN, &f[12], &f[13]); // accelerometer-only tilt (for the thigh model)
  memcpy(p.f, f, sizeof(f));
  sendFeatures(p);
  lastPacketMs = millis();

  Serial.printf("%lu,%s,%d,%u,%lu", (unsigned long)p.seq, linkMode == L_LINKED ? "linked" : "search",
                channel, p.i2c_errors_window, (unsigned long)p.i2c_errors_total);
  for (int i = 0; i < TP_N_FEAT; i++) Serial.printf(",%.4f", p.f[i]);
  Serial.println();
}

// ---------------- LED and button --------------------------------------------------------------
static uint32_t flashUntil = 0;

static void ledLoop() {
  uint32_t t = millis();
  bool on;
  if (flashUntil > t) on = ((flashUntil - t) / 100) % 2;          // calibration: quick flashes
  else if (!imuOk) on = (t / 100) % 2;                            // sensor error: fast blink
  else if (linkMode == L_SEARCH) on = (t / 500) % 2;              // searching: slow blink
  else on = t - lastPacketMs < 60;                                // linked: blink per packet
  digitalWrite(PIN_LED, on);
}

static void buttonLoop() {
  static uint32_t downAt = 0;
  static bool done = false;
  bool down = digitalRead(PIN_BTN) == LOW;
  if (down && !downAt) { downAt = millis(); done = false; }
  if (!down) downAt = 0;
  if (down && !done && millis() - downAt > 2000) {   // held 2 s: standing pose
    float g[3];
    portENTER_CRITICAL(&cfMux);
    g[0] = cf.g[0]; g[1] = cf.g[1]; g[2] = cf.g[2];
    portEXIT_CRITICAL(&cfMux);
    saveCalibration(g);
    flashUntil = millis() + 600;
    done = true;
  }
}

// ---------------- Setup / loop ----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[boot] ThighSensor (complementary filter, ESP-NOW to the watch)");
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  tf_reset(&cf);
  loadCalibration();
  imuOk = mpuBegin();
  Serial.println(imuOk ? "[boot] MPU-6050 ok" : "[boot] ERROR: MPU-6050 not found (check SDA 21, SCL 22, 3V3, GND)");
  radioBegin();
  xTaskCreatePinnedToCore(samplerTask, "imu", 4096, nullptr, 5, nullptr, 1);
  Serial.print("window,link,channel,i2c_err_window,i2c_err_total");
  for (int i = 0; i < TP_N_FEAT; i++) Serial.printf(",%s", TP_FEAT_NAMES[i]);
  Serial.println();
}

void loop() {
  while (rTail != rHead) {
    const float *s = ring[rTail % RING];
    for (int i = 0; i < 6; i++) win[winFill][i] = s[i];
    winTilt[winFill] = s[6];
    winTiltAcc[winFill] = s[7];
    rTail++;
    if (++winFill == ACT_WIN) { finishWindow(); winFill = 0; }
  }
  radioLoop();
  buttonLoop();
  ledLoop();
  delay(2);
}
