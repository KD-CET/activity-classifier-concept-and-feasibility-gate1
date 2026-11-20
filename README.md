# Wearable Lifestyle Activity Classifier with Wireless Reporting

**Stage 1 — Concept and Feasibility** · Kenneth Dialogo · Vanier College, 247-519-VA

A wrist-worn device that classifies the wearer's activity as **sedentary, walking or
running** on the device itself, and reports each change over Wi-Fi to a small server that
logs it and shows progress across sessions. The proof of concept asks two questions:

1. Does fusing wrist, chest and/or thigh motion data improve classification over the wrist alone?
2. Does the link deliver each result on time when degraded, with nothing lost after an outage?

This repository holds the firmware, the training pipeline, the server, the test tools and the
evidence for every feasibility claim at Gate 1.

## System

```
 Wrist unit (ESP32-S3 watch)                          Server (Raspberry Pi Zero 2 W)
 IMU 40 Hz -> 1.5 s windows -> 10 features           Mosquitto broker
   -> random forest (10 trees) -> 3-window smoothing  -> receiver: ack, de-duplicate, SQLite
   -> on-screen label                                 -> summary topic -> Node-RED dashboard
   -> report on change, buffer + retry --- MQTT ---->
```

* **Classification on the device.** Random forest, 10 trees, depth 6, exported to C
  (`firmware/ActivityClassifier/activity_model.h`, model id `0xED69B0BC`). Ten
  orientation-free features: acceleration mean, spread, minimum, maximum, jerk; rotation mean,
  spread, maximum; step rhythm and its strength.
* **Reporting.** One message per change of the shown activity plus a 10 s heartbeat. Each
  report keeps its sequence number until the receiver acknowledges it, so resends are stored
  once. Up to 64 reports are buffered during an outage. Broker Last Will marks the device
  offline.
* **Server.** Mosquitto, a Python receiver (SQLite, unique per device and sequence number), and
  a Node-RED Dashboard 2.0 view. HTTP on port 8000 is a backup path.

## Results at Gate 1

| | Result |
|---|---|
| Classification error, leave one wearer out (9 PAMAP2 wearers) | 3.3 % mean, 6.7 % worst (target ≤ 15 %); 1.5 % with smoothing |
| Live classification on the watch, scripted sit/walk/run | correct label 100 % of the time for each activity |
| Memory | 1,075,030 B program (3 %), 72,888 B RAM (22 %) |
| Healthy link | 40/40 reports delivered, max 0.14 s |
| 60 s outage | all buffered reports delivered, none lost |
| 500 ms delay + 20 % loss | 22/24 within 30 s, none lost — **target not met** |
| Fusion with the thigh sensor | **open** — thigh unit firmware written (Nov 19), hardware tests pending |

Full register: [`evidence/claims_register.csv`](evidence/claims_register.csv) ·
summary: [`evidence/README.md`](evidence/README.md) ·
report: [`reports/stage1/`](reports/stage1/)

## Repository layout

| Folder | Contents |
|---|---|
| `firmware/` | `ActivityClassifier` (watch program), `ThighSensor` (thigh unit: MPU-6050, complementary filter, ESP-NOW to the watch; the watch averages its wrist model's and a thigh model's votes), `ImuRecorder` (raw IMU to SD), `sd_card/replay.bin` (replay test data) |
| `training/` | Training notebook (Colab), the dataset build script, `thigh_model_public.py` (thigh model on public HARTH data), and the fusion checks `selfback_fusion_check.py` / `train_fusion.py` (C9) |
| `data/` | PAMAP2 wrist data, 9 wearers (CC BY 4.0) |
| `server/` | Receiver, database, dashboards, Pi installer, test and scoring tools, automated tests |
| `evidence/` | Results, logs and screenshots for claims C1–C16 |
| `reports/` | Stage 1 report (LaTeX) and the proposal source |
| `docs/` | Setup and operation guide; test procedures |

## Rebuild

1. **Server:** copy `server/` to a Raspberry Pi (Raspberry Pi OS) and run
   `bash pi/setup_pi_mqtt.sh`. It installs and starts the broker, receiver and dashboard.
   Dashboard: `http://<pi-address>:1880/dashboard`.
2. **Watch:** open `firmware/ActivityClassifier/ActivityClassifier.ino` in the Arduino IDE.
   In `link_config.h`, set the Wi-Fi credentials and `LINK_HOST` (the Pi's address).
   Board *ESP32S3 Dev Module*; USB CDC On Boot *Enabled*; Flash *32MB*; a *32M* partition
   scheme; PSRAM *OPI PSRAM*. Libraries: *GFX Library for Arduino*, *PubSubClient*. Upload.
3. **Model (optional):** run `training/wrist_classifier_pamap2.ipynb` in Colab with the nine
   `data/pamap2_wrist_csv/*.csv.gz` files; it regenerates `activity_model.h` and `replay.bin`.

Details: [`docs/setup_and_operation.txt`](docs/setup_and_operation.txt). Test procedures:
[`docs/test_procedures.md`](docs/test_procedures.md).

> `link_config.h` is committed with placeholder credentials. Keep real Wi-Fi credentials out
> of the repository.

## Test tools (`server/`)

```
python3 score_session.py --plan "sit:10,walk:8,run:4,sit:8"   # label shown vs scripted plan (C1, C11)
python3 link_check.py --last 10                               # delay, resends, lost reports (C3-C5)
sudo bash c4_bad_link.sh                                      # 500 ms + 20 % loss for 5 min (C4)
python3 -m unittest discover -s tests                         # automated tests (19)
```

## Data and acknowledgements

PAMAP2 Physical Activity Monitoring dataset, A. Reiss and D. Stricker, UCI Machine Learning
Repository, CC BY 4.0. Hand IMU only; see [`data/README.md`](data/README.md).

An AI assistant was used to draft parts of the code (link layer, server, test tools) and of the
documentation. Each part was verified by automated tests and on the hardware; the logbook
records each AI-assisted step and its check.
