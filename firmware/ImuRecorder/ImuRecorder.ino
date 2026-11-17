/*
  ImuRecorder.ino - records the watch's motion sensor to the SD card for training data.
  Waveshare ESP32-S3-Touch-AMOLED-2.06 (QMI8658 IMU, microSD slot)

  What it does
    Records accelerometer + gyroscope at 40 Hz for a fixed time (RECORD_S seconds, default 60),
    one activity label per recording, and saves a CSV file on the SD card:
        /W1_walking_001.csv   = wearer 1, walking, take 1   (take number goes up by itself)

  Buttons
    BOOT  1 tap       : next activity   (sedentary -> walking -> running)
    BOOT  2 quick taps: next wearer     (1 .. 9)
    BOOT  hold 1 s    : start (5 s countdown); hold again to stop early / cancel
    (the PWR button is not used. Serial Monitor also works: s = start/stop, a = activity, w = wearer)

  How to record well
    * Wear the watch the same way every time (WRIST below) and do not change it between wearers.
    * Hold BOOT for 1 s, then get into position during the countdown, and do the activity until it stops.
    * Walk/run at a natural pace. Do 2-5 takes per activity per wearer.
    * Trim the first and last ~5 s of each file when you analyse it.

  File format (CSV, lines starting with # are notes):
    # wearer=1 activity=walking take=1 rate_hz=40 ...
    t_ms,ax,ay,az,gx,gy,gz        (acceleration in g, rotation in degrees/second)
    ...
    # samples=2400 dropped=0 read_errors=0 duration_s=60.0 stopped_early=0

  Arduino IDE
    Board "ESP32S3 Dev Module", esp32 core 3.x, USB CDC On Boot: Enabled,
    Flash 32MB, PSRAM: OPI PSRAM, Partition scheme with >= 3MB APP.
    Library: "GFX Library for Arduino" (moononournation). No sensor library needed:
    the QMI8658 motion sensor is read directly over I2C in this file.
    SD card: FAT32, inserted BEFORE power-up.
*/
#include <Arduino.h>
#include <Wire.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Arduino_GFX_Library.h>
#include "esp_timer.h"

// ---------------- Pins (Waveshare pin_config.h) ---------------------------
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 11
#define LCD_CS 12
#define LCD_RESET 8
#define LCD_WIDTH 410
#define LCD_HEIGHT 502
#define LCD_COL_OFFSET 22
#define IIC_SDA 15
#define IIC_SCL 14
#define SD_CLK 2
#define SD_CMD 1
#define SD_D0 3
#define BTN_BOOT 0      // LOW  = pressed
#define BTN_PWR 10      // not used

// ---------------- Settings (edit these) -----------------------------------
const int RATE_HZ = 40;                 // sampling rate (same as the reference paper)
const int PERIOD_MS = 1000 / RATE_HZ;   // 25 ms between samples
const int RECORD_S = 60;                // length of one recording
const int COUNTDOWN_S = 5;              // time to get into position
const char *WRIST = "left";             // which wrist the watch is on - keep it the same!
const int MAX_WEARER = 9;

// ---------------- Types (must come before functions) ----------------------
struct Smp {
  uint32_t t_us;      // microseconds since the recording started
  float v[6];         // ax ay az (g)  gx gy gz (deg/s)
};

enum State { S_IDLE, S_COUNT, S_REC, S_DONE, S_ERROR };

struct Btn {
  int pin;
  bool activeLow;
  bool down;
  bool longFired;
  uint32_t since;
};

// ---------------- Globals --------------------------------------------------
const char *ACT[] = {"sedentary", "walking", "running"};
const char *ACT_SHOW[] = {"SEDENTARY", "WALKING", "RUNNING"};

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
    bus, LCD_RESET, 0 /*rotation*/, LCD_WIDTH, LCD_HEIGHT,
    LCD_COL_OFFSET, 0, 0, 0);
Arduino_Canvas *canvas = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);
Arduino_GFX *g = nullptr;
bool useCanvas = false;

// ---------------- QMI8658 motion sensor (direct I2C, no library) ----------
// Register numbers and settings taken from the Waveshare/SensorLib QMI8658 driver.
const uint8_t IMU_ADDR = 0x6B;
const float ACC_SCALE = 8.0f / 32768.0f;       // +-8 g range     -> g per count
const float GYR_SCALE = 1024.0f / 32768.0f;    // +-1024 dps range -> deg/s per count

bool imuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool imuRead(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(IMU_ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool imuBegin() {
  Wire.begin(IIC_SDA, IIC_SCL);
  Wire.setClock(400000);
  imuWrite(0x60, 0xB0);                           // soft reset
  bool ok = false;
  for (int i = 0; i < 50 && !ok; i++) {           // reset done when register 0x4D reads 0x80
    delay(10);
    uint8_t r = 0;
    ok = imuRead(0x4D, &r, 1) && r == 0x80;
  }
  if (!ok) return false;
  uint8_t id = 0;
  if (!imuRead(0x00, &id, 1) || id != 0x05) return false;   // WHO_AM_I must be 0x05
  uint8_t c1 = 0;
  imuRead(0x02, &c1, 1);
  imuWrite(0x02, c1 | 0x40);                      // auto-increment register address
  imuWrite(0x06, 0x77);                           // low-pass filter on, mode 3 (about 13% of rate)
  imuWrite(0x03, 0x26);                           // accelerometer: +-8 g, 125 Hz
  imuWrite(0x04, 0x66);                           // gyroscope: +-1024 dps, 112 Hz
  imuWrite(0x08, 0x03);                           // enable accelerometer + gyroscope
  return true;
}

// Reads accel (g) and gyro (deg/s) in one burst: v = ax ay az gx gy gz
bool imuReadAll(float *v) {
  uint8_t b[12];
  if (!imuRead(0x35, b, 12)) return false;
  for (int i = 0; i < 6; i++) {
    int16_t raw = (int16_t)((b[2 * i + 1] << 8) | b[2 * i]);
    v[i] = raw * (i < 3 ? ACC_SCALE : GYR_SCALE);
  }
  return true;
}

const int RING = 1024;                  // 25 s of samples: survives slow SD writes
Smp ring[RING];
volatile uint32_t rHead = 0, rTail = 0;
volatile uint32_t dropped = 0, readErr = 0;
volatile bool recOn = false;
volatile int64_t t0us = 0;

State state = S_IDLE;
int wearer = 1;
int actIdx = 0;
int take = 1;
uint32_t stateSince = 0;
uint32_t samplesWritten = 0;
bool dirty = true;
char msg[64] = "";
char curName[40] = "";
File outFile;
char wbuf[4096];
size_t wlen = 0;
int cardMB = 0, freeMB = 0;
bool lastOk = true;

Btn bBoot = {BTN_BOOT, true, false, false, 0};

#define C_BG RGB565(8, 12, 20)
#define C_CARD RGB565(22, 29, 44)
#define C_TXT RGB565(240, 244, 250)
#define C_DIM RGB565(130, 142, 160)
#define C_GOOD RGB565(46, 204, 113)
#define C_WARN RGB565(255, 176, 32)
#define C_BAD RGB565(240, 84, 84)
#define C_LINE RGB565(52, 62, 80)

const int SX = 30;            // text margin (away from the rounded screen corners)
const int X0 = 24;            // card left edge
const int CARD_W = 362;
const int CARD_H = 62;

uint16_t actColor(int a) {
  if (a == 0) return RGB565(120, 140, 200);
  if (a == 1) return C_GOOD;
  return RGB565(255, 140, 60);
}

// ---------------- Sampler task (runs on its own core) ---------------------
// Every 25 ms: read the latest accelerometer + gyroscope values and put them in the ring.
// The sensor itself runs at 125 Hz with its low-pass filter at about 16 Hz, so the
// 40 Hz samples are already band-limited (no aliasing).
void samplerTask(void *) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(PERIOD_MS));
    if (!recOn) continue;
    float v6[6];
    if (imuReadAll(v6)) {
      uint32_t h = rHead;
      if (h - rTail >= (uint32_t)RING) {
        dropped++;
        continue;
      }
      Smp &s = ring[h % RING];
      s.t_us = (uint32_t)(esp_timer_get_time() - t0us);
      for (int i = 0; i < 6; i++) s.v[i] = v6[i];
      __sync_synchronize();
      rHead = h + 1;
    } else {
      readErr++;
    }
  }
}

// ---------------- SD writing ----------------------------------------------
void flushWrite() {
  if (wlen && outFile) outFile.write((const uint8_t *)wbuf, wlen);
  wlen = 0;
}

void putText(const char *s) {
  size_t n = strlen(s);
  if (wlen + n >= sizeof(wbuf)) flushWrite();
  memcpy(wbuf + wlen, s, n);
  wlen += n;
}

void drainRing() {
  char line[96];
  while (rTail != rHead) {
    const Smp &s = ring[rTail % RING];
    snprintf(line, sizeof(line), "%lu.%03lu,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f\n",
             (unsigned long)(s.t_us / 1000), (unsigned long)(s.t_us % 1000),
             s.v[0], s.v[1], s.v[2], s.v[3], s.v[4], s.v[5]);
    putText(line);
    samplesWritten++;
    __sync_synchronize();
    rTail = rTail + 1;
  }
}

int nextTake(int w, int a) {
  char p[48];
  for (int t = 1; t < 1000; t++) {
    snprintf(p, sizeof(p), "/W%d_%s_%03d.csv", w, ACT[a], t);
    if (!SD_MMC.exists(p)) return t;
  }
  return 999;
}

void refreshTake() { take = nextTake(wearer, actIdx); }

void refreshCard() {
  cardMB = (int)(SD_MMC.cardSize() / (1024ULL * 1024ULL));
  freeMB = (int)((SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024ULL * 1024ULL));
}

void setState(State s) {
  state = s;
  stateSince = millis();
  dirty = true;
}

void startCountdown() {
  refreshTake();
  setState(S_COUNT);
  Serial.printf("countdown: wearer %d, %s, take %d\n", wearer, ACT[actIdx], take);
}

void startRecording() {
  snprintf(curName, sizeof(curName), "W%d_%s_%03d.csv", wearer, ACT[actIdx], take);
  char path[48];
  snprintf(path, sizeof(path), "/%s", curName);
  outFile = SD_MMC.open(path, FILE_WRITE);
  if (!outFile) {
    snprintf(msg, sizeof(msg), "Cannot create file");
    lastOk = false;
    setState(S_DONE);
    return;
  }
  wlen = 0;
  char hdr[200];
  snprintf(hdr, sizeof(hdr),
           "# wearer=%d activity=%s take=%d rate_hz=%d wrist=%s acc_unit=g acc_range_g=8 "
           "gyro_unit=dps gyro_range_dps=1024 imu=QMI8658\nt_ms,ax,ay,az,gx,gy,gz\n",
           wearer, ACT[actIdx], take, RATE_HZ, WRIST);
  putText(hdr);
  samplesWritten = 0;
  dropped = 0;
  readErr = 0;
  rTail = rHead;
  t0us = esp_timer_get_time();
  recOn = true;
  setState(S_REC);
  Serial.printf("recording %s\n", curName);
}

void stopRecording(bool early) {
  recOn = false;
  delay(PERIOD_MS + 10);              // let the sampler finish its last sample
  drainRing();
  float dur = (esp_timer_get_time() - t0us) / 1e6f;
  char foot[120];
  snprintf(foot, sizeof(foot), "# samples=%lu dropped=%lu read_errors=%lu duration_s=%.1f stopped_early=%d\n",
           (unsigned long)samplesWritten, (unsigned long)dropped, (unsigned long)readErr, dur, early ? 1 : 0);
  putText(foot);
  flushWrite();
  outFile.close();
  refreshCard();
  snprintf(msg, sizeof(msg), "%lu samples, %lu lost", (unsigned long)samplesWritten, (unsigned long)dropped);
  lastOk = true;
  Serial.printf("saved %s: %s (%.1f s)\n", curName, msg, dur);
  setState(S_DONE);
}

// ---------------- Buttons --------------------------------------------------
// returns 0 = nothing, 1 = short press (on release), 2 = long press (after 1 s)
int readBtn(Btn &b) {
  bool pressed = (digitalRead(b.pin) == LOW) == b.activeLow;
  int ev = 0;
  if (pressed && !b.down) {
    b.down = true; b.longFired = false; b.since = millis();
  } else if (pressed && b.down && !b.longFired && millis() - b.since > 1000) {
    b.longFired = true; ev = 2;
  } else if (!pressed && b.down) {
    b.down = false;
    if (!b.longFired && millis() - b.since > 40) ev = 1;
  }
  return ev;
}

void nextActivity() { actIdx = (actIdx + 1) % 3; refreshTake(); dirty = true; }
void nextWearer() { wearer = wearer % MAX_WEARER + 1; refreshTake(); dirty = true; }

void toggleStart() {
  if (state == S_IDLE || state == S_DONE) startCountdown();
  else if (state == S_COUNT) { setState(S_IDLE); }
  else if (state == S_REC) stopRecording(true);
}

// ---------------- Drawing --------------------------------------------------
void drawText(int x, int y, int size, uint16_t col, const char *s) {
  g->setTextSize(size);
  g->setTextColor(col);
  g->setCursor(x, y);
  g->print(s);
}

// ---------------- Drawing helpers -----------------------------------------
int textW(const char *s, int size) { return (int)strlen(s) * 6 * size; }

void drawCentered(int y, int size, uint16_t col, const char *s) {
  drawText((LCD_WIDTH - textW(s, size)) / 2, y, size, col, s);
}

uint16_t actTint(int a) {                       // dark background tint for the selected card
  if (a == 0) return RGB565(34, 42, 76);
  if (a == 1) return RGB565(14, 58, 40);
  return RGB565(72, 44, 12);
}

void drawHeader(uint16_t dotColor) {
  char buf[48];
  drawText(SX, 30, 3, C_TXT, "IMU Recorder");
  g->fillCircle(360, 42, 6, dotColor);
  snprintf(buf, sizeof(buf), "Wearer %d | %d Hz | %s", wearer, RATE_HZ, WRIST);
  drawText(SX, 62, 2, C_DIM, buf);
  g->fillRect(SX, 86, LCD_WIDTH - 2 * SX, 2, C_LINE);
}

void drawActCard(int y, int a, bool selected, const char *right) {
  uint16_t c = actColor(a);
  g->fillRoundRect(X0, y, CARD_W, CARD_H, 12, selected ? actTint(a) : C_CARD);
  if (selected) g->drawRoundRect(X0, y, CARD_W, CARD_H, 12, c);
  g->fillRect(X0, y + 12, 4, CARD_H - 24, selected ? c : C_LINE);
  drawText(X0 + 18, y + 19, 3, selected ? C_TXT : C_DIM, ACT_SHOW[a]);
  if (right && selected) drawText(X0 + CARD_W - 14 - textW(right, 2), y + 23, 2, c, right);
}

void drawIdle() {
  char buf[48];
  drawHeader(C_GOOD);
  for (int a = 0; a < 3; a++) {
    snprintf(buf, sizeof(buf), "take %d", take);
    drawActCard(100 + a * (CARD_H + 8), a, a == actIdx, buf);
  }
  g->fillRoundRect(X0, 316, CARD_W, 70, 12, C_CARD);
  drawText(X0 + 16, 328, 2, C_DIM, "Next file");
  drawText(X0 + CARD_W - 16 - textW("READY", 2), 328, 2, C_GOOD, "READY");
  snprintf(buf, sizeof(buf), "W%d_%s_%03d.csv", wearer, ACT[actIdx], take);
  drawText(X0 + 16, 352, 2, C_TXT, buf);
  drawCentered(404, 1, C_DIM, "BOOT tap: activity   2 taps: wearer");
  drawCentered(420, 1, C_DIM, "hold BOOT 1 s: start recording");
  snprintf(buf, sizeof(buf), "SD card: %d MB free of %d MB", freeMB, cardMB);
  drawCentered(440, 1, C_DIM, buf);
}

void drawCountdown(uint32_t el) {
  char buf[16];
  drawHeader(C_WARN);
  snprintf(buf, sizeof(buf), "take %d", take);
  drawActCard(100, actIdx, true, buf);
  int left = COUNTDOWN_S - (int)el;
  if (left < 0) left = 0;
  snprintf(buf, sizeof(buf), "%d", left);
  drawCentered(200, 14, C_WARN, buf);
  drawCentered(330, 2, C_TXT, "get into position");
  drawCentered(380, 1, C_DIM, "hold BOOT: cancel");
}

void drawRecording(uint32_t el) {
  char buf[48];
  bool blink = (millis() / 500) % 2 == 0;
  drawHeader(blink ? C_BAD : C_LINE);
  snprintf(buf, sizeof(buf), "take %d", take);
  drawActCard(100, actIdx, true, buf);
  uint32_t shown = el > (uint32_t)RECORD_S ? (uint32_t)RECORD_S : el;
  snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)(shown / 60), (unsigned long)(shown % 60));
  drawCentered(190, 9, C_TXT, buf);
  snprintf(buf, sizeof(buf), "of %d:%02d", RECORD_S / 60, RECORD_S % 60);
  drawCentered(272, 2, C_DIM, buf);
  int trackW = LCD_WIDTH - 2 * SX;
  int w = trackW * (int)shown / RECORD_S;
  g->fillRoundRect(SX, 308, trackW, 16, 8, C_LINE);
  if (w >= 16) g->fillRoundRect(SX, 308, w, 16, 8, C_BAD);
  else if (w > 0) g->fillRect(SX, 308, w, 16, C_BAD);
  g->fillRoundRect(X0, 342, CARD_W, 62, 12, C_CARD);
  snprintf(buf, sizeof(buf), "samples %lu", (unsigned long)samplesWritten);
  drawText(X0 + 16, 364, 2, C_TXT, buf);
  snprintf(buf, sizeof(buf), "lost %lu", (unsigned long)dropped);
  drawText(X0 + CARD_W - 16 - textW(buf, 2), 364, 2, dropped ? C_BAD : C_GOOD, buf);
  drawCentered(430, 1, C_DIM, "hold BOOT: stop early");
}

void drawDone() {
  char buf[48];
  drawHeader(C_GOOD);
  if (!lastOk) {                                   // could not save
    g->fillRoundRect(X0, 130, CARD_W, 130, 12, RGB565(60, 20, 24));
    g->drawRoundRect(X0, 130, CARD_W, 130, 12, C_BAD);
    drawText(X0 + 18, 148, 3, C_BAD, "NOT SAVED");
    drawText(X0 + 18, 192, 2, C_TXT, msg);
    return;
  }
  g->fillCircle(205, 170, 50, C_GOOD);             // green circle with a check mark
  for (int d = 0; d < 4; d++) {
    g->drawLine(182, 170 + d, 198, 186 + d, C_BG);
    g->drawLine(198, 186 + d, 230, 152 + d, C_BG);
  }
  drawCentered(245, 4, C_GOOD, "SAVED");
  drawCentered(292, 2, C_TXT, curName);
  uint16_t c = (dropped == 0 && samplesWritten > 0) ? C_GOOD : C_WARN;
  drawCentered(324, 2, c, msg);
  drawCentered(400, 1, C_DIM, "returning to ready...");
}

void drawError() {
  drawHeader(C_BAD);
  g->fillRoundRect(X0, 130, CARD_W, 130, 12, RGB565(60, 20, 24));
  g->drawRoundRect(X0, 130, CARD_W, 130, 12, C_BAD);
  drawText(X0 + 18, 148, 3, C_BAD, "ERROR");
  drawText(X0 + 18, 192, 2, C_TXT, msg);
  drawText(X0 + 18, 222, 2, C_DIM, "BOOT: retry");
}

void redraw() {
  g->fillScreen(C_BG);
  uint32_t el = (millis() - stateSince) / 1000;
  if (state == S_IDLE) drawIdle();
  else if (state == S_COUNT) drawCountdown(el);
  else if (state == S_REC) drawRecording(el);
  else if (state == S_DONE) drawDone();
  else drawError();
  if (useCanvas) canvas->flush();
}

// ---------------- Setup / loop --------------------------------------------
bool initHardware() {
  Serial.println("[boot] starting I2C + IMU");
  if (!imuBegin()) {
    snprintf(msg, sizeof(msg), "IMU not found");
    return false;
  }
  Serial.println("[boot] IMU ok");

  Serial.println("[boot] mounting SD card");
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if (!SD_MMC.begin("/sdcard", true)) {            // 1-bit mode, like the Waveshare example
    snprintf(msg, sizeof(msg), "SD card not found");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    snprintf(msg, sizeof(msg), "No SD card");
    return false;
  }
  refreshCard();
  Serial.println("[boot] SD ok");
  return true;
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);                        // never wait for the Serial Monitor
#endif
  delay(2500);                                     // lets the Serial Monitor reconnect after a reset
  Serial.println("\n[boot] ImuRecorder starting");
  pinMode(BTN_BOOT, INPUT_PULLUP);

  panel->begin();
  panel->setBrightness(150);
  Serial.println("[boot] display started");
  useCanvas = canvas->begin(GFX_SKIP_OUTPUT_BEGIN);   // needs OPI PSRAM
  g = useCanvas ? (Arduino_GFX *)canvas : (Arduino_GFX *)panel;
  Serial.printf("[boot] canvas %s (PSRAM %s)\n", useCanvas ? "ok" : "NOT available", psramFound() ? "found" : "NOT found");

  if (initHardware()) {
    xTaskCreatePinnedToCore(samplerTask, "imu", 8192, nullptr, 5, nullptr, 0);
    refreshTake();
    setState(S_IDLE);
    Serial.println("ready. s = start/stop, a = activity, w = wearer");
  } else {
    Serial.printf("ERROR: %s\n", msg);
    setState(S_ERROR);
  }
}

void loop() {
  int eb = readBtn(bBoot);
  int ch = Serial.available() ? Serial.read() : 0;

  if (state == S_ERROR) {
    if (eb == 1 || ch) ESP.restart();       // BOOT (or any serial key) retries
  } else {
    // BOOT: 1 tap = next activity, 2 quick taps = next wearer, hold 1 s = start / stop
    static uint32_t tapAt = 0;                   // time of a first tap still waiting for a second
    bool idleish = (state == S_IDLE || state == S_DONE);
    if (eb == 1) {
      if (tapAt && millis() - tapAt < 400) { tapAt = 0; if (idleish) nextWearer(); }
      else tapAt = millis();
    }
    if (tapAt && millis() - tapAt >= 400) { tapAt = 0; if (idleish) nextActivity(); }
    if (eb == 2) { tapAt = 0; toggleStart(); }
    if (ch == 'a' && idleish) nextActivity();
    if (ch == 'w' && idleish) nextWearer();
    if (ch == 's') toggleStart();
  }

  if (state == S_COUNT && millis() - stateSince >= (uint32_t)COUNTDOWN_S * 1000) startRecording();
  if (state == S_REC) {
    drainRing();
    if (millis() - stateSince >= (uint32_t)RECORD_S * 1000) stopRecording(false);
  }
  if (state == S_DONE && millis() - stateSince > 4000) setState(S_IDLE);

  static uint32_t lastDraw = 0;
  if (dirty || ((state == S_REC || state == S_COUNT) && millis() - lastDraw > 500)) {
    dirty = false;
    lastDraw = millis();
    redraw();
  }
  delay(2);
}
