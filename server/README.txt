Dashboard (computer side). One wearer.

MAIN WAY (Raspberry Pi or laptop): one program, nothing to install
  python receiver.py          then open  http://<this computer's address>:8000  in a browser
  receiver.py receives the watch's reports and serves the dashboard website (web/index.html).
  On the Pi: see pi/PI_GUIDE.txt (one-command setup that starts at boot).

Files
  receiver.py      Receives reports (HTTP POST /report), stores them, ACKs; serves the dashboard
                   at / and its data at /api/summary. Standard library only.
  web/index.html   The dashboard website: Overview and System health tabs. No outside libraries,
                   so it loads from the Pi without internet. The browser draws everything.
  core/summary.py  Builds the small summary the website shows; keeps the last 7 days in memory
                   and reads only new rows from the database.
  core/metrics.py  Turns events into sessions and minutes (rebuilt Nov 11, 2026).
  db.py            SQLite storage; a resent message (same device + seq) is never stored twice.
  terminal_dashboard.py   The dashboard in a terminal window, no browser needed (light on the Pi):
                   python3 terminal_dashboard.py   (add --url http://<pi>:8000 from another computer)
  link_check.py     Delivery delay, resends and lost reports for a time window (C3, C4, C5):
                   python3 link_check.py --last 10
  score_session.py  Scores a scripted session (C1, C11) from the database, minute by minute:
                   python3 score_session.py --plan "sit:10,walk:8,run:4,sit:8"
  simulator.py     A fake watch for testing, with optional message loss and lost ACKs:
                   python simulator.py --change-every 10
  pi/              setup_pi.sh (one-command install on the Pi) and PI_GUIDE.txt (step by step).
  tests/           Unit tests:  python -m unittest -v

Optional / alternatives (not needed on the Pi)
  app.py, pages/health.py, client.py   The earlier Streamlit version of the same two pages,
                   for a laptop: pip install -r requirements.txt, then python -m streamlit run app.py
                   (heavy; downloads every event on each refresh).
MQTT WAY (Nov 13): Mosquitto + mqtt_receiver.py + Node-RED Dashboard
  mqtt_receiver.py, mqtt_simulator.py, mosquitto/, MQTT_GUIDE.txt   The MQTT path.
                   mqtt_receiver.py acks and stores the watch's reports, serves the same web
                   dashboard on port 8000, and publishes the dashboard summary on the MQTT
                   topic activity/summary every 3 s.
  nodered/         The Node-RED dashboard: activity_dashboard_flow.json (import it), src/ (its
                   code), build_flow.py, NODERED_GUIDE.txt (laptop first, then the Pi).
  pi/setup_pi_mqtt.sh   One command on the Pi: Mosquitto + mqtt_receiver.py + Node-RED + Dashboard.
