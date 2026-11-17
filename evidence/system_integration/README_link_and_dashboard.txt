Wireless link and dashboard - evidence
======================================

Data path (as running on November 14, 2026)
  Watch (classifies on the watch, 3-window smoothing)
     --MQTT over Wi-Fi-->  Raspberry Pi Zero 2 W:
        Mosquitto broker
        mqtt_receiver.py   acks each report, rejects duplicates, stores it (SQLite),
                           publishes the dashboard summary on activity/summary every 3 s
        Node-RED Dashboard 2.0   shows the summary: http://<pi>:1880/dashboard
        (also a built-in web dashboard on port 8000, and HTTP reports as a backup path)
  MQTT topics: activity/watch1/report (watch->receiver, resent with the same seq until acked),
  activity/watch1/ack, activity/watch1/status ("online", or "offline" from the broker's Last Will),
  activity/summary (receiver->dashboards).

Files
  live_dashboard_watch_to_pi_2026-11-14_1922.png
      FIRST LIVE RUN, Nov 14 2026 ~19:22: real watch -> Pi over MQTT -> Node-RED dashboard.
      Shows "Watch connected", labels Sedentary -> Running -> Sedentary from the watch.
      Informal demonstration; the scripted test is in ../C1_live_classification/.
  watch_link_pc_simulation.txt
      The watch's own link code (activity_link.h) run on a PC against a simulated lossy link.
  unit_tests_output.txt
      19 automated tests: session/minute logic, MQTT receiver (valid, rejected, duplicate,
      lossy link, Last Will, summary topic), web dashboard and summary.
  end_to_end_receiver_test.txt     HTTP receiver + simulator under loss (Nov 11).
  dashboard_overview_test_data.png, nodered_sections_test_data.png
      The dashboards rendered with test data (layout and content check).

Measured on the real link (Nov 15-16, 2026): see ../C3_C4_C5_link_tests/ and claims_register.csv.
