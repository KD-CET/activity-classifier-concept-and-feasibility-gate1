Watch: Waveshare ESP32-S3-Touch-AMOLED-2.06
Arduino IDE settings (both sketches): Board "ESP32S3 Dev Module", esp32 core 3.x,
USB CDC On Boot: Enabled, Flash 32MB, PSRAM: OPI PSRAM.
Libraries (Arduino Library Manager): "GFX Library for Arduino" (moononournation) and, for the
MQTT link, "PubSubClient" (Nick O'Leary). No sensor library needed.

ActivityClassifier/   Live classification ON THE WATCH + replay test + reporting to the receiver.
                      Keep all 8 files together: ActivityClassifier.ino, activity_features.h,
                      activity_model.h, activity_replay.h, activity_link.h, link_config.h,
                      activity_board.h, thigh_packet.h, activity_model_thigh.h
  Sensor fusion: thigh unit packets over ESP-NOW; the wrist model and the thigh model
  (activity_model_thigh.h, trained on public HARTH thigh data) each vote and the votes are
  averaged. Pill WRIST+THIGH while fused, CAL THIGH if the thigh unit is not calibrated, WRIST
  otherwise; reports tagged "wrist+thigh" / "wrist"; windows recorded to /fusion/S0001.csv.
  BEFORE UPLOADING edit link_config.h: Wi-Fi name and password, the laptop's (later the Pi's)
  IP address, and LINK_USE_MQTT (1 = MQTT, as tested; 0 = HTTP backup). HTTP to the Pi:
  docs/setup_and_operation.txt. MQTT: server/MQTT_GUIDE.txt.
  Bottom line of the live screen: "HTTP link ok | queued 0" (green) when reports get through;
  amber "no Wi-Fi" / "setting clock" / "no server" while they wait in the outbox.
  BOOT tap      : switch LIVE <-> REPLAY
  BOOT hold 1 s : LIVE = reset minute totals; REPLAY = start / stop
  Model id 0xED69B0BC (must match SD_CARD/replay.bin)
  Smoothing: the shown activity changes only after 3 windows in a row agree (4.5 s).

SD_CARD/replay.bin    Copy to the ROOT of the SD card (FAT32), insert before power-up.
                      Replay writes /replay_results.csv to the card when done.

ImuRecorder/          The Nov 9-10 recorder: saves labelled 40 Hz motion CSVs to the SD card
                      (W1_walking_001.csv ...) for recording your own volunteers.

ThighSensor/          The thigh unit (ESP32-WROOM + MPU-6050, Nov 19): complementary-filter thigh
                      tilt + 10 motion features (14 in all), sent to the watch over ESP-NOW
                      every 1.5 s.
                      Board "ESP32 Dev Module". Wiring, calibration and safety: ThighSensor/README.txt
