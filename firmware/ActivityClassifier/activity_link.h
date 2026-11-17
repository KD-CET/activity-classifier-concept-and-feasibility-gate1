// activity_link.h - sends the watch's activity reports to the receiver (MQTT, or HTTP as backup).
//
// How it works
//   * The main loop calls link_post() when the shown (smoothed) activity starts, changes or ends.
//   * A separate task (core 1, low priority) owns Wi-Fi and the connection, so a slow network
//     never delays the classifier. Reports wait in an outbox and go out one at a time, in order.
//   * Each report is resent with the SAME seq until the receiver acks it (the receiver drops
//     duplicates). A "nack" means the receiver rejected it: it is dropped, not resent.
//   * During an outage reports stay in the outbox and go out when the link is back (claim C5).
//     Their time stamps are when they happened, not when they were finally sent.
//   * MQTT: Last Will = "offline" on activity/<device>/status, so the broker announces a drop.
//   * Needs the clock set from the internet (NTP) before sending, because the dashboard uses
//     real time. Until then reports wait in the outbox.
// Library (MQTT only): "PubSubClient" by Nick O'Leary, from the Arduino Library Manager.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "link_config.h"

enum { EV_START = 0, EV_END = 1, EV_HB = 2, EV_CHANGE = 3 };
static const char *LINK_TYPE_NAMES[4] = {"session_start", "session_end", "heartbeat", "activity_change"};

struct LinkEvent {
  uint8_t type;
  int8_t activity;      // class index from activity_model.h, -1 = none
  uint32_t ms;          // millis() when it happened
};

// ---------------- Pure helpers (no hardware; tested on the PC) ------------------------------
struct OutboxItem {
  LinkEvent ev;
  uint32_t seq;         // 0 = not sent yet (seq is given at the first send, after the clock is set)
  uint8_t retries;
};

struct Outbox {
  OutboxItem item[OUTBOX_SIZE];
  int head = 0, count = 0;
  uint32_t dropped = 0;

  OutboxItem *front() { return count ? &item[head] : nullptr; }
  void pop() { if (count) { head = (head + 1) % OUTBOX_SIZE; count--; } }

  void push(const LinkEvent &e) {
    if (count == OUTBOX_SIZE) {               // full: drop the oldest heartbeat, else the oldest
      int victim = 0;
      for (int i = 0; i < count; i++)
        if (item[(head + i) % OUTBOX_SIZE].ev.type == EV_HB) { victim = i; break; }
      for (int i = victim; i < count - 1; i++)
        item[(head + i) % OUTBOX_SIZE] = item[(head + i + 1) % OUTBOX_SIZE];
      count--;
      dropped++;
    }
    OutboxItem &it = item[(head + count) % OUTBOX_SIZE];
    it.ev = e; it.seq = 0; it.retries = 0;
    count++;
  }
};

// JSON body, same fields as the HTTP version. Returns its length.
static int link_build_json(char *buf, int size, const char *device, uint32_t seq, uint8_t type,
                           const char *activity, double ts, const char *sensors, int retries) {
  char act[40] = "";
  if (activity) snprintf(act, sizeof(act), "\"activity\":\"%s\",", activity);
  return snprintf(buf, size,
                  "{\"device_id\":\"%s\",\"seq\":%lu,\"type\":\"%s\",%s\"ts\":%.3f,"
                  "\"sensors\":\"%s\",\"retries\":%d}",
                  device, (unsigned long)seq, LINK_TYPE_NAMES[type], act, ts, sensors, retries);
}

// Reads {"ack": 12, ...} or {"nack": 12, ...}. Returns 1 = ack, 2 = nack, 0 = not understood.
static int link_parse_reply(const char *s, int len, uint32_t *seq) {
  char tmp[160];
  int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
  memcpy(tmp, s, n); tmp[n] = 0;
  const char *k = strstr(tmp, "\"nack\"");
  int kind = 2;
  if (!k) { k = strstr(tmp, "\"ack\""); kind = 1; }
  if (!k) return 0;
  k = strchr(k, ':');
  if (!k) return 0;
  k++;
  while (*k == ' ') k++;
  if (*k < '0' || *k > '9') return 0;        // e.g. "nack": null
  *seq = (uint32_t)strtoul(k, nullptr, 10);
  return kind;
}

#ifdef ARDUINO
// ---------------- On the watch ------------------------------------------------------------------
#include <WiFi.h>
#include <sys/time.h>
#include <time.h>
#if LINK_USE_MQTT
#include <PubSubClient.h>
#else
#include <HTTPClient.h>
#endif

enum { LS_NO_WIFI = 0, LS_NO_CLOCK, LS_NO_SERVER, LS_OK };
volatile int linkState = LS_NO_WIFI;
volatile int linkQueued = 0;
volatile uint32_t linkSent = 0, linkResends = 0, linkRejected = 0, linkDropped = 0;
static QueueHandle_t linkQ;
static Outbox outbox;

static bool clockSet() { return time(nullptr) > 1700000000; }   // after Nov 2023 = set by NTP

static double nowEpoch() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + tv.tv_usec / 1e6;
}

// called from the main loop
void link_post(uint8_t type, int activity) {
  LinkEvent e = {type, (int8_t)activity, millis()};
  if (linkQ) xQueueSend(linkQ, &e, 0);
}

static int buildFor(OutboxItem *it, char *buf, int size) {
  double ts = nowEpoch() - (millis() - it->ev.ms) / 1000.0;      // when it HAPPENED
  const char *act = (it->ev.activity >= 0 && it->ev.type != EV_HB && it->ev.type != EV_END)
                        ? ACT_CLASS_NAMES[it->ev.activity] : nullptr;
  return link_build_json(buf, size, DEVICE_ID, it->seq, it->ev.type, act, ts, SENSORS_USED, it->retries);
}

static uint32_t nextSeq = 0;
static void giveSeq(OutboxItem *it) {
  if (it->seq == 0) it->seq = nextSeq++;
  else { it->retries++; linkResends++; }
}

static void handleReply(int kind, uint32_t seq) {
  OutboxItem *f = outbox.front();
  if (!f || f->seq != seq) return;           // a late ack for something already done
  if (kind == 1) linkSent++;
  else { linkRejected++; Serial.printf("[link] receiver rejected seq=%lu\n", (unsigned long)seq); }
  outbox.pop();
}

#if LINK_USE_MQTT
static WiFiClient netClient;
static PubSubClient mqtt(netClient);
static char tReport[64], tAck[64], tStatus[64];
static volatile int replyKind = 0;
static volatile uint32_t replySeq = 0;

static void onMqtt(char *topic, byte *payload, unsigned int len) {
  uint32_t s;
  int k = link_parse_reply((const char *)payload, (int)len, &s);
  if (k) { replySeq = s; replyKind = k; }
}

static bool mqttConnect() {
  char id[48];
  snprintf(id, sizeof(id), "%s-%08lx", DEVICE_ID, (unsigned long)(ESP.getEfuseMac() & 0xffffffff));
  bool ok = strlen(MQTT_USER)
      ? mqtt.connect(id, MQTT_USER, MQTT_PASS, tStatus, 1, true, "offline")
      : mqtt.connect(id, tStatus, 1, true, "offline");               // Last Will
  if (ok) {
    mqtt.subscribe(tAck, 1);
    mqtt.publish(tStatus, "online", true);
    Serial.printf("[link] MQTT connected to %s:%d\n", LINK_HOST, MQTT_PORT);
  }
  return ok;
}
#endif

static void linkTask(void *) {
  char body[256];
  uint32_t lastWifiTry = 0, lastServerTry = 0, lastHb = 0, sentAt = 0;
  bool inflight = false, wifiWasUp = false;
#if LINK_USE_MQTT
  snprintf(tReport, sizeof(tReport), "%s/%s/report", TOPIC_ROOT, DEVICE_ID);
  snprintf(tAck, sizeof(tAck), "%s/%s/ack", TOPIC_ROOT, DEVICE_ID);
  snprintf(tStatus, sizeof(tStatus), "%s/%s/status", TOPIC_ROOT, DEVICE_ID);
  mqtt.setServer(LINK_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(2);
#endif
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastWifiTry = millis();

  for (;;) {
    LinkEvent e;
    while (xQueueReceive(linkQ, &e, 0) == pdTRUE) outbox.push(e);
    linkDropped = outbox.dropped;
    linkQueued = outbox.count;

    // 1. Wi-Fi
    if (WiFi.status() != WL_CONNECTED) {
      linkState = LS_NO_WIFI;
      inflight = false;
      if (wifiWasUp) { Serial.println("[link] Wi-Fi lost"); wifiWasUp = false; }
      if (millis() - lastWifiTry > 15000) { WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASS); lastWifiTry = millis(); }
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!wifiWasUp) {
      wifiWasUp = true;
      Serial.printf("[link] Wi-Fi up, IP %s\n", WiFi.localIP().toString().c_str());
      configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
    }

    // 2. Clock (needed for real time stamps and for seq numbers that never repeat)
    if (!clockSet()) { linkState = LS_NO_CLOCK; vTaskDelay(pdMS_TO_TICKS(200)); continue; }
    if (nextSeq == 0) nextSeq = (uint32_t)time(nullptr);   // new numbers after every restart

    // 3. Server connection
#if LINK_USE_MQTT
    if (!mqtt.connected()) {
      linkState = LS_NO_SERVER;
      inflight = false;
      if (millis() - lastServerTry > 3000) { lastServerTry = millis(); mqttConnect(); }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    mqtt.loop();
    linkState = LS_OK;
    if (replyKind) { int k = replyKind; replyKind = 0; handleReply(k, replySeq); inflight = false; }

    // 4. Send the oldest report; resend it if no ack arrives in time
    OutboxItem *f = outbox.front();
    if (f && (!inflight || millis() - sentAt > ACK_TIMEOUT_MS)) {
      giveSeq(f);
      int n = buildFor(f, body, sizeof(body));
      if (mqtt.publish(tReport, (const uint8_t *)body, n, false)) { inflight = true; sentAt = millis(); }
    }
#else
    linkState = LS_OK;
    OutboxItem *f = outbox.front();
    if (f) {
      giveSeq(f);
      int n = buildFor(f, body, sizeof(body));
      HTTPClient http;
      char url[96];
      snprintf(url, sizeof(url), "http://%s:%d/report", LINK_HOST, HTTP_PORT);
      http.begin(url);
      http.setTimeout(3000);
      http.addHeader("Content-Type", "application/json");
      int code = http.POST((uint8_t *)body, n);
      String r = code > 0 ? http.getString() : String();
      http.end();
      uint32_t s;
      int k = (code == 200 || code == 400) ? link_parse_reply(r.c_str(), r.length(), &s) : 0;
      if (code == 400 && !k) { k = 2; s = f->seq; }            // rejected without a seq
      if (k) handleReply(k, s);
      else { linkState = LS_NO_SERVER; vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
#endif
    // 5. Heartbeat while the link is up and nothing is waiting
    if (linkState == LS_OK && outbox.count == 0 && millis() - lastHb > HEARTBEAT_MS) {
      lastHb = millis();
      link_post(EV_HB, -1);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void link_begin() {
  linkQ = xQueueCreate(32, sizeof(LinkEvent));
  xTaskCreatePinnedToCore(linkTask, "link", 8192, nullptr, 1, nullptr, 1);
}

const char *link_status_text() {
  static const char *names[] = {"no Wi-Fi", "setting clock", "no server", "link ok"};
  return names[linkState];
}
#endif  // ARDUINO
