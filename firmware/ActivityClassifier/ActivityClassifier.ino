/*
  ActivityClassifier - live sedentary / walking / running on the watch, plus SD replay test
  Kenneth Dialogo, 247-519-VA Project I
  Board: Waveshare ESP32-S3-Touch-AMOLED-2.06

  LIVE screen
    * Samples the QMI8658 motion sensor at 40 Hz.
    * Every 1.5 s (60 samples) computes the 10 features and runs the random forest ON THE WATCH.
    * Smoothing: the shown activity changes only after 3 windows in a row agree (4.5 s), so a
      brief arm movement while sitting is not shown or counted as walking.
    * Shows the activity inside a ring (how many trees agree), the last 60 s as a colour strip,
      and the minutes spent in each activity.
    * Serial Monitor: one line per window (shown label, raw label, vote share, the 10 features).

  REPLAY screen (needs replay.bin from the notebook, Part 4, in the root of the SD card)
    * Runs every PAMAP2 window in the file through the on-watch model.
    * Shows agreement with Colab (should be 100 %), accuracy, recall per activity and speed.
    * Saves /replay_results.csv on the SD card.

  BOOT button
    tap        : switch between LIVE and REPLAY
    hold 1 s   : LIVE -> reset the minute totals,  REPLAY -> start / stop the replay

  Reporting (Nov 13): every change of the shown activity is sent to the receiver over MQTT
  (or HTTP as backup), with a heartbeat every 10 s. EDIT link_config.h FIRST (Wi-Fi, address).
  Needs the "PubSubClient" library (Nick O'Leary) for MQTT. See MQTT_GUIDE.txt in server.

  Files in this folder (all six are needed)
    ActivityClassifier.ino, activity_features.h, activity_model.h, activity_replay.h,
    activity_link.h, link_config.h

  Arduino IDE (same settings as ImuRecorder)
    Board "ESP32S3 Dev Module", esp32 core 3.x, USB CDC On Boot: Enabled,
    Flash 32MB, PSRAM: OPI PSRAM. Library: "GFX Library for Arduino" (moononournation).
    SD card: FAT32, inserted BEFORE power-up.
*/
#include <Arduino.h>
#include <Wire.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Arduino_GFX_Library.h>
#include <math.h>
#include "activity_features.h"
#include "activity_model.h"
#include "activity_replay.h"
#include "activity_link.h"          // sends reports to the receiver (settings in link_config.h)

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
#define BTN_BOOT 0      // LOW = pressed

const int RATE_HZ = 40;                         // must match the training data
const int PERIOD_MS = 1000 / RATE_HZ;           // 25 ms
const float WIN_S = ACT_WIN / (float)RATE_HZ;   // 1.5 s
const char *REPLAY_PATH = "/replay.bin";
const char *RESULTS_PATH = "/replay_results.csv";

// ---------------- Display ---------------------------------------------------
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
    bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, LCD_COL_OFFSET, 0, 0, 0);
Arduino_Canvas *canvas = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);
Arduino_GFX *g = nullptr;
bool useCanvas = false;

// Colours: true black background (the AMOLED pixels are off, which also saves battery)
#define C_BG     RGB565(0, 0, 0)
#define C_CARD   RGB565(20, 22, 30)
#define C_TRACK  RGB565(34, 38, 50)
#define C_TXT    RGB565(244, 246, 250)
#define C_DIM    RGB565(128, 136, 152)
#define C_FAINT  RGB565(70, 76, 92)
#define C_GOOD   RGB565(52, 211, 153)
#define C_WARN   RGB565(251, 191, 36)
#define C_BAD    RGB565(248, 113, 113)
#define C_BLUE   RGB565(96, 165, 250)
#define C_SED    RGB565(129, 140, 248)   // indigo
#define C_WALK   RGB565(52, 211, 153)    // green
#define C_RUN    RGB565(251, 146, 60)    // orange

// ---------------- QMI8658 motion sensor (direct I2C, from ImuRecorder) ----
const uint8_t IMU_ADDR = 0x6B;
const float ACC_SCALE = 8.0f / 32768.0f;       // +-8 g      -> g per count
const float GYR_SCALE = 1024.0f / 32768.0f;    // +-1024 dps -> deg/s per count

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
  for (int i = 0; i < 50 && !ok; i++) {
    delay(10);
    uint8_t r = 0;
    ok = imuRead(0x4D, &r, 1) && r == 0x80;
  }
  if (!ok) return false;
  uint8_t id = 0;
  if (!imuRead(0x00, &id, 1) || id != 0x05) return false;
  uint8_t c1 = 0;
  imuRead(0x02, &c1, 1);
  imuWrite(0x02, c1 | 0x40);                      // auto-increment
  imuWrite(0x06, 0x77);                           // low-pass filters on
  imuWrite(0x03, 0x26);                           // accel +-8 g, 125 Hz
  imuWrite(0x04, 0x66);                           // gyro +-1024 dps, 112 Hz
  imuWrite(0x08, 0x03);                           // enable accel + gyro
  return true;
}

bool imuReadAll(float *v) {
  uint8_t b[12];
  if (!imuRead(0x35, b, 12)) return false;
  for (int i = 0; i < 6; i++) {
    int16_t raw = (int16_t)((b[2 * i + 1] << 8) | b[2 * i]);
    v[i] = raw * (i < 3 ? ACC_SCALE : GYR_SCALE);
  }
  return true;
}

// ---------------- Sampler task: 40 Hz into a ring buffer (core 0) ---------
const int RING = 256;
float ring[RING][6];
volatile uint32_t rHead = 0, rTail = 0;
volatile uint32_t dropped = 0, readErr = 0;

void samplerTask(void *) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(PERIOD_MS));
    float v[6];
    if (!imuReadAll(v)) { readErr++; continue; }
    uint32_t h = rHead;
    if (h - rTail >= (uint32_t)RING) { dropped++; continue; }
    for (int i = 0; i < 6; i++) ring[h % RING][i] = v[i];
    __sync_synchronize();
    rHead = h + 1;
  }
}

// ---------------- State -----------------------------------------------------
enum Screen { SCR_LIVE, SCR_REPLAY };
enum RpState { RP_NOSD, RP_NOFILE, RP_BAD, RP_READY, RP_RUN, RP_DONE };

Screen screen = SCR_LIVE;
bool imuOk = false, sdOk = false;
bool dirty = true;

// live
float win[ACT_WIN][6];
int winFill = 0;
uint32_t windows = 0;
int current = -1;                       // shown (smoothed) class index from activity_model.h
int rawLast = -1, rawRun = 0;           // latest raw prediction and how many windows in a row
const int STABLE_N = 3;                 // label changes only after 3 agreeing windows (4.5 s)
float prob[ACT_N_CLASSES];
float secs[ACT_N_CLASSES];
uint32_t lastInferUs = 0;
const int HIST = 40;                    // 40 windows x 1.5 s = last 60 s
int8_t hist[HIST];
int histPos = 0;

// replay
RpState rpState = RP_NOSD;
const char *rpWhy = "";
RpHeader rpHdr;
RpStats rp;
File rpFile;
bool rpSaved = false;
uint8_t rpBuf[RP_REC_BYTES * 8];

// ---------------- Helpers -----------------------------------------------------
int classByName(const char *n) {
  for (int k = 0; k < ACT_N_CLASSES; k++)
    if (!strcmp(ACT_CLASS_NAMES[k], n)) return k;
  return -1;
}

uint16_t classColor(int k) {
  if (k < 0) return C_FAINT;
  const char *n = ACT_CLASS_NAMES[k];
  if (!strcmp(n, "sedentary")) return C_SED;
  if (!strcmp(n, "walking")) return C_WALK;
  return C_RUN;
}

const char *ORDER[3] = {"sedentary", "walking", "running"};   // display order
const char *SHORT[3] = {"SED", "WALK", "RUN"};

uint32_t usNow() { return micros(); }

// ---------------- Drawing primitives ------------------------------------------
int textW(const char *s, int size) { return (int)strlen(s) * 6 * size - size; }

void drawText(int x, int y, int size, uint16_t col, const char *s) {
  g->setTextSize(size);
  g->setTextColor(col);
  g->setCursor(x, y);
  g->print(s);
}

void drawCentered(int cx, int y, int size, uint16_t col, const char *s) {
  drawText(cx - textW(s, size) / 2, y, size, col, s);
}

void drawRight(int xr, int y, int size, uint16_t col, const char *s) {
  drawText(xr - textW(s, size), y, size, col, s);
}

// Part of a ring from angle a0 to a1 (degrees, 0 = top, clockwise)
void arcBand(int cx, int cy, int rOut, int rIn, float a0, float a1, uint16_t col) {
  const float step = 3.0f;
  for (float a = a0; a < a1; a += step) {
    float b = a + step > a1 ? a1 : a + step;
    float ra = a * 0.0174533f, rb = b * 0.0174533f;
    int x0 = cx + rOut * sinf(ra), y0 = cy - rOut * cosf(ra);
    int x1 = cx + rOut * sinf(rb), y1 = cy - rOut * cosf(rb);
    int x2 = cx + rIn * sinf(rb), y2 = cy - rIn * cosf(rb);
    int x3 = cx + rIn * sinf(ra), y3 = cy - rIn * cosf(ra);
    g->fillTriangle(x0, y0, x1, y1, x2, y2, col);
    g->fillTriangle(x0, y0, x2, y2, x3, y3, col);
  }
}

// Track plus a filled part (0..1) with rounded ends
void gauge(int cx, int cy, int rOut, int rIn, float frac, uint16_t col) {
  arcBand(cx, cy, rOut, rIn, 0, 360, C_TRACK);
  if (frac <= 0) return;
  if (frac > 1) frac = 1;
  float end = 360 * frac;
  arcBand(cx, cy, rOut, rIn, 0, end, col);
  int rm = (rOut + rIn) / 2, cap = (rOut - rIn) / 2;
  g->fillCircle(cx, cy - rm, cap, col);
  float re = end * 0.0174533f;
  g->fillCircle(cx + rm * sinf(re), cy - rm * cosf(re), cap, col);
}

void header(const char *title, const char *pill, uint16_t pillCol) {
  drawText(32, 34, 3, C_TXT, title);
  int pw = textW(pill, 2) + 40;
  int px = LCD_WIDTH - 32 - pw;
  g->fillRoundRect(px, 28, pw, 34, 17, C_CARD);
  g->fillCircle(px + 18, 45, 6, pillCol);
  drawText(px + 30, 38, 2, pillCol, pill);
}

void hbar(int x, int y, int w, int h, float frac, uint16_t col) {
  g->fillRoundRect(x, y, w, h, h / 2, C_TRACK);
  int f = (int)(w * (frac < 0 ? 0 : frac > 1 ? 1 : frac));
  if (f >= h) g->fillRoundRect(x, y, f, h, h / 2, col);
  else if (f > 0) g->fillCircle(x + h / 2, y + h / 2, h / 2, col);
}

// ---------------- LIVE screen ---------------------------------------------------
const int CX = LCD_WIDTH / 2, CY = 210, R_OUT = 124, R_IN = 104;

void drawLive() {
  char buf[48];
  header("Activity", imuOk ? "LIVE" : "NO IMU", imuOk ? C_GOOD : C_BAD);

  if (!imuOk) {
    gauge(CX, CY, R_OUT, R_IN, 0, C_BAD);
    drawCentered(CX, CY - 22, 3, C_BAD, "sensor");
    drawCentered(CX, CY + 10, 3, C_BAD, "not found");
    drawCentered(CX, 380, 2, C_DIM, "hold BOOT: restart");
    return;
  }

  // ring: share of trees that voted for the current activity
  uint16_t col = classColor(current);
  gauge(CX, CY, R_OUT, R_IN, current < 0 ? 0 : prob[current], col);
  if (current < 0) {
    drawCentered(CX, CY - 12, 3, C_DIM, "starting");
    snprintf(buf, sizeof(buf), "result in %.1f s", WIN_S);
    drawCentered(CX, CY + 24, 2, C_FAINT, buf);
  } else {
    char name[16];
    strncpy(name, ACT_CLASS_NAMES[current], sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    for (char *p = name; *p; p++) *p = toupper(*p);
    int size = textW(name, 4) <= 2 * R_IN - 24 ? 4 : 3;
    drawCentered(CX, CY - 4 * size - 6, size, col, name);
    snprintf(buf, sizeof(buf), "%d%% sure", (int)(prob[current] * 100 + 0.5f));
    drawCentered(CX, CY + 18, 2, C_DIM, buf);
  }

  // timeline: last 60 s, oldest on the left
  const int TX = 34, TW = LCD_WIDTH - 68, TY = 364, TH = 18;
  drawText(TX, 344, 2, C_DIM, "LAST 60 S");
  drawRight(TX + TW, 344, 2, C_FAINT, "NOW");
  float slot = TW / (float)HIST;
  for (int i = 0; i < HIST; i++) {
    int k = hist[(histPos + i) % HIST];
    int x = TX + (int)(i * slot);
    int w = (int)((i + 1) * slot) - (int)(i * slot) - 2;
    g->fillRoundRect(x, TY, w, TH, 2, k < 0 ? C_CARD : classColor(k));
  }

  // tiles: minutes in each activity
  const int TILE_Y = 394, TILE_H = 64, GAP = 10, TILE_W = (LCD_WIDTH - 68 - 2 * GAP) / 3;
  for (int r = 0; r < 3; r++) {
    int k = classByName(ORDER[r]);
    int x = 34 + r * (TILE_W + GAP);
    bool on = (k == current);
    g->fillRoundRect(x, TILE_Y, TILE_W, TILE_H, 12, C_CARD);
    if (on) g->drawRoundRect(x, TILE_Y, TILE_W, TILE_H, 12, classColor(k));
    g->fillCircle(x + 18, TILE_Y + 17, 5, classColor(k));
    drawText(x + 30, TILE_Y + 10, 2, on ? C_TXT : C_DIM, SHORT[r]);
    float m = k < 0 ? 0 : secs[k] / 60.0f;
    snprintf(buf, sizeof(buf), m < 10 ? "%.1f" : "%.0f", m);
    drawText(x + 12, TILE_Y + 30, 3, C_TXT, buf);
    drawText(x + 14 + textW(buf, 3), TILE_Y + 37, 2, C_DIM, "m");
  }

  // link status: green when reports are getting through, amber while they wait in the outbox
  snprintf(buf, sizeof(buf), "%s %s | queued %d", LINK_USE_MQTT ? "MQTT" : "HTTP",
           link_status_text(), (int)linkQueued);
  drawCentered(CX, 470, 2, linkState == LS_OK ? C_GOOD : C_WARN, buf);
}

// ---------------- REPLAY screen -------------------------------------------------
void drawReplay() {
  char buf[64];
  uint16_t pillCol = rpState == RP_RUN ? C_BLUE : rpState == RP_DONE ? C_GOOD
                   : rpState == RP_READY ? C_DIM : C_WARN;
  header("Replay", rpState == RP_RUN ? "RUNNING" : rpState == RP_DONE ? "DONE" : "SD", pillCol);

  if (rpState == RP_NOSD || rpState == RP_NOFILE || rpState == RP_BAD) {
    gauge(CX, CY, R_OUT, R_IN, 0, C_WARN);
    const char *big = rpState == RP_NOSD ? "no SD card" : rpState == RP_NOFILE ? "no file" : "bad file";
    drawCentered(CX, CY - 12, 3, C_WARN, big);
    const char *l1 = rpState == RP_NOSD ? "insert a FAT32 card"
                   : rpState == RP_NOFILE ? "copy replay.bin"
                   : rpWhy;
    const char *l2 = rpState == RP_NOSD ? "then restart"
                   : rpState == RP_NOFILE ? "to the SD card root"
                   : "re-export both files";
    drawCentered(CX, 362, 2, C_TXT, l1);
    drawCentered(CX, 388, 2, C_DIM, l2);
    drawCentered(CX, 462, 2, C_FAINT, "tap BOOT: live");
    return;
  }

  if (rpState == RP_READY) {
    gauge(CX, CY, R_OUT, R_IN, 0, C_BLUE);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)rp.total);
    drawCentered(CX, CY - 30, 5, C_TXT, buf);
    drawCentered(CX, CY + 16, 2, C_DIM, "windows");
    snprintf(buf, sizeof(buf), "model 0x%08lX", (unsigned long)rpHdr.model_id);
    drawCentered(CX, 352, 2, C_FAINT, buf);
    g->fillRoundRect(74, 384, LCD_WIDTH - 148, 50, 25, C_BLUE);
    drawCentered(CX, 401, 2, C_BG, "hold BOOT: start");
    drawCentered(CX, 462, 2, C_FAINT, "tap BOOT: live");
    return;
  }

  // RUN or DONE
  bool done = rpState == RP_DONE;
  float agreeF = rp.done ? rp.agree / (float)rp.done : 0;
  if (!done) {
    gauge(CX, CY, R_OUT, R_IN, rp.total ? rp.done / (float)rp.total : 0, C_BLUE);
    snprintf(buf, sizeof(buf), "%d%%", (int)(100.0f * rp.done / rp.total));
    drawCentered(CX, CY - 30, 6, C_TXT, buf);
    snprintf(buf, sizeof(buf), "%lu / %lu", (unsigned long)rp.done, (unsigned long)rp.total);
    drawCentered(CX, CY + 24, 2, C_DIM, buf);
  } else {
    uint16_t c = rp.agree == rp.done ? C_GOOD : C_BAD;
    gauge(CX, CY, R_OUT, R_IN, agreeF, c);
    snprintf(buf, sizeof(buf), "%.1f%%", 100.0f * agreeF);
    drawCentered(CX, CY - 34, 5, c, buf);
    drawCentered(CX, CY + 10, 2, C_DIM, "same as Colab");
    snprintf(buf, sizeof(buf), "%lu / %lu", (unsigned long)rp.agree, (unsigned long)rp.done);
    drawCentered(CX, CY + 38, 2, C_FAINT, buf);
  }

  // recall per activity
  for (int r = 0; r < 3; r++) {
    int k = classByName(ORDER[r]);
    int y = 344 + r * 24;
    float f = (k >= 0 && rp.trueN[k]) ? rp.hitN[k] / (float)rp.trueN[k] : 0;
    drawText(34, y, 2, C_DIM, SHORT[r]);
    hbar(104, y + 2, 200, 12, f, classColor(k));
    snprintf(buf, sizeof(buf), "%.1f%%", 100 * f);
    drawRight(LCD_WIDTH - 34, y, 2, C_TXT, buf);
  }

  float acc = rp.done ? 100.0f * rp.correct / rp.done : 0;
  float colab = rp.total ? 100.0f * rp.colabCorrect / rp.total : 0;
  snprintf(buf, sizeof(buf), "acc %.1f%% | Colab %.1f%%", acc, colab);
  drawCentered(CX, 420, 2, C_TXT, buf);
  snprintf(buf, sizeof(buf), "%lu us/win%s", (unsigned long)(rp.done ? rp.busyUs / rp.done : 0),
           done ? (rpSaved ? " | CSV saved" : " | not saved") : "");
  drawCentered(CX, 444, 2, C_DIM, buf);
  drawCentered(CX, 468, 2, C_FAINT, done ? "hold: again  tap: live" : "hold BOOT: stop");
}

void redraw() {
  g->fillScreen(C_BG);
  if (screen == SCR_LIVE) drawLive();
  else drawReplay();
  if (useCanvas) canvas->flush();
}

// ---------------- Live classification --------------------------------------------
void classifyLive() {
  float x[ACT_N_FEATURES];
  uint32_t t0 = micros();
  act_compute_features(win, x);
  int raw = act_predict(x, prob);
  lastInferUs = micros() - t0;
  // smoothing: a brief arm movement (1-2 windows) does not change the shown activity
  rawRun = (raw == rawLast) ? rawRun + 1 : 1;
  rawLast = raw;
  int before = current;
  if (current < 0 || rawRun >= STABLE_N) current = raw;
  if (current != before) link_post(before < 0 ? EV_START : EV_CHANGE, current);   // report it
  secs[current] += WIN_S;
  hist[histPos] = current;
  histPos = (histPos + 1) % HIST;
  windows++;
  if (screen == SCR_LIVE) dirty = true;
  Serial.printf("%lu,%s,%s,%.2f", (unsigned long)windows, ACT_CLASS_NAMES[current],
                ACT_CLASS_NAMES[raw], prob[raw]);
  for (int i = 0; i < ACT_N_FEATURES; i++) Serial.printf(",%.4f", x[i]);
  Serial.println();
}

// ---------------- Replay ---------------------------------------------------------
void replayOpen() {                     // check the card and the file header
  if (rpFile) rpFile.close();
  if (!sdOk) { rpState = RP_NOSD; return; }
  rpFile = SD_MMC.open(REPLAY_PATH, FILE_READ);
  if (!rpFile) { rpState = RP_NOFILE; return; }
  if (rpFile.read((uint8_t *)&rpHdr, sizeof(rpHdr)) != sizeof(rpHdr)) {
    rpWhy = "file too short"; rpState = RP_BAD; rpFile.close(); return;
  }
  const char *why = rp_check_header(&rpHdr);
  if (why) { rpWhy = why; rpState = RP_BAD; rpFile.close(); return; }
  rp_reset(&rp, &rpHdr);
  rpState = RP_READY;
}

void replaySave() {
  if (rp.done == 0) { rpSaved = false; return; }
  File f = SD_MMC.open(RESULTS_PATH, FILE_WRITE);
  if (!f) { rpSaved = false; return; }
  f.printf("# model_id=0x%08lX windows=%lu agree_with_colab=%lu accuracy_pct=%.2f colab_accuracy_pct=%.2f us_per_window=%lu\n",
           (unsigned long)rpHdr.model_id, (unsigned long)rp.done, (unsigned long)rp.agree,
           100.0 * rp.correct / rp.done, 100.0 * rp.colabCorrect / rp.total,
           (unsigned long)(rp.busyUs / rp.done));
  f.println("group,windows,correct,accuracy_pct");
  for (int s = 0; s < RP_MAX_SUBJ; s++)
    if (rp.subjN[s]) f.printf("subject%d,%lu,%lu,%.2f\n", 100 + s, (unsigned long)rp.subjN[s],
                              (unsigned long)rp.subjOk[s], 100.0 * rp.subjOk[s] / rp.subjN[s]);
  for (int k = 0; k < ACT_N_CLASSES; k++)
    if (rp.trueN[k]) f.printf("%s,%lu,%lu,%.2f\n", ACT_CLASS_NAMES[k], (unsigned long)rp.trueN[k],
                              (unsigned long)rp.hitN[k], 100.0 * rp.hitN[k] / rp.trueN[k]);
  f.close();
  rpSaved = true;
}

void replayFinish() {
  rpState = RP_DONE;
  rpFile.close();
  replaySave();
  if (rp.done)
    Serial.printf("replay done: %lu windows, agree %lu, accuracy %.2f%%, %lu us/window\n",
                  (unsigned long)rp.done, (unsigned long)rp.agree, 100.0 * rp.correct / rp.done,
                  (unsigned long)(rp.busyUs / rp.done));
  dirty = true;
}

void replayStep() {                     // a few windows per loop, so the screen keeps updating
  int want = 8;
  uint32_t left = rp.total - rp.done;
  if ((uint32_t)want > left) want = left;
  if (want <= 0) { replayFinish(); return; }
  int got = rpFile.read(rpBuf, RP_REC_BYTES * want) / RP_REC_BYTES;
  for (int i = 0; i < got; i++) rp_process(rpBuf + i * RP_REC_BYTES, &rp, usNow);
  if (got < want || rp.done >= rp.total) replayFinish();
}

// ---------------- Button -----------------------------------------------------------
// returns 1 = tap, 2 = hold for 1 s (fires while still held)
int readBoot() {
  static uint32_t downAt = 0;
  static bool held = false;
  bool down = digitalRead(BTN_BOOT) == LOW;
  if (down) {
    if (!downAt) { downAt = millis(); held = false; }
    if (!held && millis() - downAt >= 1000) { held = true; return 2; }
    return 0;
  }
  int ev = (downAt && !held && millis() - downAt >= 30) ? 1 : 0;
  downAt = 0;
  return ev;
}

// ---------------- Setup / loop -----------------------------------------------------
void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  delay(2000);
  Serial.printf("\n[boot] ActivityClassifier, model 0x%08lX\n", (unsigned long)ACT_MODEL_ID);
  pinMode(BTN_BOOT, INPUT_PULLUP);
  for (int i = 0; i < HIST; i++) hist[i] = -1;
  panel->begin();
  panel->setBrightness(160);
  useCanvas = canvas->begin(GFX_SKIP_OUTPUT_BEGIN);
  g = useCanvas ? (Arduino_GFX *)canvas : (Arduino_GFX *)panel;

  imuOk = imuBegin();
  Serial.println(imuOk ? "[boot] IMU ok" : "[boot] ERROR: IMU not found");
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  sdOk = SD_MMC.begin("/sdcard", true) && SD_MMC.cardType() != CARD_NONE;
  Serial.println(sdOk ? "[boot] SD ok" : "[boot] no SD card (replay disabled)");

  if (imuOk) {
    xTaskCreatePinnedToCore(samplerTask, "imu", 4096, nullptr, 5, nullptr, 0);
    link_begin();                                  // Wi-Fi + MQTT/HTTP in its own task (core 1)
    Serial.println("window,label_shown,label_raw,vote_share_raw,acc_mean,acc_std,acc_min,acc_max,acc_jerk,"
                   "gyro_mean,gyro_std,gyro_max,cadence_hz,cadence_power");
  }
  redraw();
}

void loop() {
  int ev = readBoot();
  if (ev == 1 && rpState != RP_RUN) {                 // tap: switch screens
    screen = screen == SCR_LIVE ? SCR_REPLAY : SCR_LIVE;
    if (screen == SCR_REPLAY && rpState != RP_DONE) replayOpen();
    dirty = true;
  }
  if (ev == 2) {                                      // hold
    if (screen == SCR_LIVE) {
      if (!imuOk) ESP.restart();
      for (int k = 0; k < ACT_N_CLASSES; k++) secs[k] = 0;
      link_post(EV_END, -1);                       // close the session on the dashboard...
      if (current >= 0) link_post(EV_START, current);   // ...and start a new one
      for (int i = 0; i < HIST; i++) hist[i] = -1;
    } else if (rpState == RP_READY) {
      rpState = RP_RUN; rpSaved = false;
      Serial.println("replay started");
    } else if (rpState == RP_RUN) {
      replayFinish();                                 // stop early, keep what was done
    } else if (rpState == RP_DONE) {
      replayOpen();
      if (rpState == RP_READY) { rpState = RP_RUN; rpSaved = false; }
    } else {
      replayOpen();
    }
    dirty = true;
  }

  // live samples: classify, unless the replay is using the processor
  while (rTail != rHead) {
    bool use = rpState != RP_RUN;
    if (use) for (int i = 0; i < 6; i++) win[winFill][i] = ring[rTail % RING][i];
    __sync_synchronize();
    rTail = rTail + 1;
    if (!use) { winFill = 0; continue; }
    if (++winFill == ACT_WIN) { classifyLive(); winFill = 0; }
  }

  static uint32_t lastDraw = 0;
  if (rpState == RP_RUN) {
    replayStep();
    if (millis() - lastDraw > 250) dirty = true;
  }
  if (dirty) {
    dirty = false;
    lastDraw = millis();
    redraw();
  }
  delay(rpState == RP_RUN ? 0 : 5);
}
