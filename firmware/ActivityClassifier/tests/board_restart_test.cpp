// Build on a PC with stand-in ESP32 headers (Arduino.h, WiFi.h, esp_now.h, esp_wifi.h).
// Replays the C10 case: thigh unit runs 40 windows, is switched off, restarts from window 1.
#include "Arduino.h"
#include "../activity_board.h"
#include <cstdio>
static uint32_t fakeMs = 0;
static void send(uint32_t seq, uint32_t board_ms) {
  TpFeatures p = {}; tp_header(&p.h, TP_FEATURES, 6, TP_FLAG_IMU_OK | TP_FLAG_CALIBRATED);
  p.seq = seq; p.board_ms = board_ms; p.samples = 60;
  uint8_t mac[6] = {1,2,3,4,5,6};
  boardHandleRecv(mac, (const uint8_t*)&p, sizeof(p));
}
int main() {
  for (uint32_t s = 1; s <= 40; s++) send(s, 2000 + s * 1500);          // first run, 40 windows
  send(43, 2000 + 43 * 1500);                                           // 2 packets lost
  uint32_t before = boardLastSeq;
  for (uint32_t s = 1; s <= 5; s++) send(s, 1800 + s * 1500);           // after power-off/on
  printf("last seq before restart %u, after restart %u (expected 5)\n", before, boardLastSeq);
  printf("restarts %u (expected 1), missing %u (expected 2), received %u (expected 46)\n",
         boardRestarts, boardMissing, boardRx);
  bool ok = boardLastSeq == 5 && boardRestarts == 1 && boardMissing == 2 && boardRx == 46;
  printf("%s\n", ok ? "PASS: packets after a thigh unit restart are accepted" : "FAIL");
  return ok ? 0 : 1;
}
