C1 - correct label shown live in a scripted sit/walk/run session (REQ-AC-01)
Test: Nov 15, 2026, 19:11:45-19:17:45, one wearer (Kenneth), watch on the wrist,
model 0xED69B0BC, smoothing 3 windows, watch -> Pi over MQTT.
Plan (written before the test): sit 2 min, walk 2 min, run 2 min.
Pass criterion (written before the test): each activity shows the correct label >= 90 % of
the time, first 10 s after each change ignored.

Result (score_session.py, from the Pi's database):
  sedentary  correct 100.0 %   right label after 0.0 s
  walking    correct 100.0 %   right label after 7.3 s
  running    correct 100.0 %   right label after 5.8 s
  Minute by minute: 6 of 6 minutes match. Active minutes reported 3.9 of 4 planned.
  -> PASS.

Files
  score_session_output_2026-11-15_1911.png   the script's printout
  dashboard_after_C1_test_2026-11-15_1917.png dashboard: Sedentary 2.1, Walking 2.0, Running 2.0 min;
                                              changes 19:11 Sedentary, 19:13 Walking, 19:15 Running,
                                              19:17 Sedentary
  session_score_2026-11-15_1911.csv           (copy from the Pi with WinSCP and put it here)

Limits: one person, one session, one place; the reference is the written plan and a phone
timer, not a video. It shows C1 works; it does not show how often it works for other people.
