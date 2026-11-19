SERVER - runs on the Raspberry Pi (Mosquitto + receiver + Node-RED dashboard)

  watch --MQTT--> Mosquitto --> mqtt_receiver.py --> SQLite (dashboard.db)
                                      |
                                      +--> activity/summary --> Node-RED dashboard
  HTTP backup: the watch can also POST to mqtt_receiver.py on port 8000.

Install on a fresh Pi (one command; everything then starts at every boot):
  bash pi/setup_pi_mqtt.sh
  Dashboard:         http://<pi-address>:1880/dashboard
  Backup dashboard:  http://<pi-address>:8000

Files
  mqtt_receiver.py   Receives the watch's reports over MQTT (and HTTP), checks them, stores them,
                     acks the watch; publishes the dashboard summary on activity/summary every
                     3 s; serves the backup dashboard and /api/summary on port 8000.
                     Runs as the service activity-mqtt-receiver.
  receiver.py        HTTP part (validation, HTTP handler, backup dashboard); used by
                     mqtt_receiver.py, can also run alone as an HTTP-only receiver.
  db.py              SQLite storage; a resent report (same device + seq) is never stored twice.
  core/metrics.py    Turns stored events into sessions, minutes and active minutes.
  core/summary.py    Builds the summary the dashboards show (last 7 days kept in memory).
  web/index.html     Backup web dashboard (no outside libraries).
  nodered/           Node-RED Dashboard 2.0 flow (activity_dashboard_flow.json), its source (src/),
                     build_flow.py, NODERED_GUIDE.txt.
  mosquitto/         Broker settings for the Pi (activity.conf) and for laptop testing.
  pi/setup_pi_mqtt.sh  One-command install on the Pi.
  mqtt_simulator.py  A fake watch for testing without hardware (loss, lost acks, dropouts).
  terminal_dashboard.py  The dashboard in a terminal:  python3 terminal_dashboard.py

Test tools (read dashboard.db; run in this folder)
  score_session.py   Label shown vs a scripted plan, per activity and minute by minute (C1, C11):
                     python3 score_session.py --plan "sit:10,walk:8,run:4,sit:8"
  link_check.py      Delivery delay, resends and lost reports for a time window (C3, C4, C5, C13):
                     python3 link_check.py --last 10
  c4_bad_link.sh     500 ms delay + 20 % loss on the Pi's Wi-Fi for 5 min (C4):
                     sudo bash c4_bad_link.sh
  tests/             Automated tests:  python3 -m unittest discover -s tests -v

Guides: MQTT_GUIDE.txt (MQTT on a laptop, topics), nodered/NODERED_GUIDE.txt.
