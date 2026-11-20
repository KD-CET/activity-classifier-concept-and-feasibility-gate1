// activity_board.h - receives the thigh unit's feature packets over ESP-NOW (claims C7, C10).
//
// * The thigh unit searches the Wi-Fi channels with HELLO messages; the watch answers each one
//   with an ACK that carries the watch's channel (the channel of its Wi-Fi network). The thigh
//   unit then stays on that channel. Nothing to pair or type in.
// * Every TP_FEATURES packet (one per 1.5 s window) is kept with the time it arrived.
//   board_latest() hands the newest one to the classifier if it is FRESH (arrived within
//   BOARD_FRESH_MS); otherwise the watch classifies on the wrist alone (REQ-AC-10).
// * Counts for the evidence: packets received, packets missing (gaps in the window counter),
//   and the thigh unit's own sensor-read errors (C16).
// Uses the radio that activity_link.h has already started in station mode: ESP-NOW works on the
// same channel as the watch's Wi-Fi connection, so MQTT keeps working.
#pragma once
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include "thigh_packet.h"

#define BOARD_FRESH_MS 3500          // a packet every 1.5 s; one may be missed

static portMUX_TYPE boardMux = portMUX_INITIALIZER_UNLOCKED;
static TpFeatures boardLast;
static volatile uint32_t boardLastMs = 0;           // millis() when the last packet arrived
static volatile uint32_t boardRx = 0, boardMissing = 0, boardHellos = 0, boardRestarts = 0;
static uint32_t boardLastSeq = 0;
static bool boardHaveSeq = false;
static uint8_t boardMac[6];
static QueueHandle_t boardAckQ;                     // callback -> board task: who to answer

struct BoardAckJob { uint8_t mac[6]; uint32_t seq; };

static void boardHandleRecv(const uint8_t *mac, const uint8_t *data, int len) {
  int type = tp_check(data, len);
  if (type == TP_HELLO) {
    boardHellos = boardHellos + 1;
  } else if (type == TP_FEATURES) {
    TpFeatures p;
    memcpy(&p, data, sizeof(p));
    portENTER_CRITICAL_ISR(&boardMux);
    // The thigh unit restarted (switched off and on): its clock and window counter start again
    // from 0, so its packets look "older" than the last one. Detect that by its clock going back.
    bool restarted = boardHaveSeq && p.board_ms < boardLast.board_ms;
    if (restarted) boardRestarts = boardRestarts + 1;
    else if (boardHaveSeq && p.seq > boardLastSeq + 1) boardMissing = boardMissing + (p.seq - boardLastSeq - 1);
    if (!boardHaveSeq || restarted || p.seq > boardLastSeq) {   // newer, or after a restart
      boardLast = p;
      boardLastSeq = p.seq;
      boardHaveSeq = true;
      boardLastMs = millis();
      memcpy(boardMac, mac, 6);
    }
    boardRx = boardRx + 1;
    portEXIT_CRITICAL_ISR(&boardMux);
  } else {
    return;
  }
  BoardAckJob j;
  memcpy(j.mac, mac, 6);
  j.seq = type == TP_FEATURES ? boardLastSeq : 0;
  xQueueSend(boardAckQ, &j, 0);                       // answered from boardTask, not from here
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void boardOnRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) { boardHandleRecv(info->src_addr, data, len); }
#else
static void boardOnRecv(const uint8_t *mac, const uint8_t *data, int len) { boardHandleRecv(mac, data, len); }
#endif

static void boardTask(void *) {
  BoardAckJob j;
  for (;;) {
    if (xQueueReceive(boardAckQ, &j, portMAX_DELAY) != pdTRUE) continue;
    if (!esp_now_is_peer_exist(j.mac)) {
      esp_now_peer_info_t p = {};
      memcpy(p.peer_addr, j.mac, 6);
      p.channel = 0;                                  // the channel the watch is on
      p.ifidx = WIFI_IF_STA;
      esp_now_add_peer(&p);
    }
    uint8_t primary = 1;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    TpAck a;
    tp_header(&a.h, TP_ACK, primary, 0);
    a.seq = j.seq;
    a.received = boardRx;
    esp_now_send(j.mac, (const uint8_t *)&a, sizeof(a));
  }
}

// Called by the link task right after it puts Wi-Fi in station mode.
static void board_begin() {
  boardAckQ = xQueueCreate(8, sizeof(BoardAckJob));
  WiFi.setSleep(false);                               // radio always listening: no missed packets
  if (esp_now_init() != ESP_OK) { Serial.println("[board] ESP-NOW init failed"); return; }
  esp_now_register_recv_cb(boardOnRecv);
  xTaskCreatePinnedToCore(boardTask, "board", 3072, nullptr, 3, nullptr, 1);
  Serial.printf("[board] listening for the thigh unit (watch MAC %s)\n", WiFi.macAddress().c_str());
}

// Newest thigh packet if it is fresh. age_ms (optional) = time since it arrived.
static bool board_latest(TpFeatures *out, uint32_t *age_ms) {
  uint32_t age = millis() - boardLastMs;
  if (age_ms) *age_ms = age;
  if (!boardHaveSeq || age > BOARD_FRESH_MS) return false;
  portENTER_CRITICAL(&boardMux);
  *out = boardLast;
  portEXIT_CRITICAL(&boardMux);
  return true;
}

static bool board_fresh() { return boardHaveSeq && millis() - boardLastMs <= BOARD_FRESH_MS; }

// Set by the classifier each window: true when the thigh unit's data was actually used (fresh,
// sensor ok, calibrated). Reports are tagged "wrist+thigh" only then (claim C10).
static volatile bool boardUsed = false;
static bool board_in_use() { return boardUsed && board_fresh(); }
