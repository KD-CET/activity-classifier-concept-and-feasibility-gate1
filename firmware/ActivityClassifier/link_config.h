// link_config.h - EDIT THIS FILE before uploading: your Wi-Fi and where the receiver runs.
#pragma once

// ---- Wi-Fi (2.4 GHz; the ESP32 cannot join 5 GHz networks) ----
#define WIFI_SSID "your-hotspot-name"
#define WIFI_PASS "your-hotspot-password"

// ---- How to send: 1 = MQTT (Mosquitto broker), 0 = HTTP backup (POST to the receiver) ----
#define LINK_USE_MQTT 1

// ---- Where the broker / receiver runs: your laptop now, the Pi later (its IP address) ----
#define LINK_HOST "192.168.2.126"
#define MQTT_PORT 1883              // Mosquitto
#define HTTP_PORT 8000              // mqtt_receiver.py or receiver.py
#define MQTT_USER ""                // leave "" unless you turned on a Mosquitto password
#define MQTT_PASS ""

// ---- Identity and timing ----
#define DEVICE_ID "watch1"          // must match the topic the receiver expects
#define TOPIC_ROOT "activity"       // topics: activity/watch1/report, /ack, /status
#define SENSORS_USED "wrist"        // becomes "wrist+thigh" once the board is added
#define HEARTBEAT_MS 10000          // "still here" message every 10 s while the link is up
#define ACK_TIMEOUT_MS 2000         // no ack within this -> resend the same message
#define OUTBOX_SIZE 64              // reports kept while the link is down (a 60 s outage needs ~10)

// ---- Clock: reports carry real time, so the watch sets its clock from the internet ----
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
