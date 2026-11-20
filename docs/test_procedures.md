# Test procedures

How each measured claim was tested, so that the tests can be repeated. Scripts run on the Pi
in the `server` folder and read the receiver's database (`dashboard.db`). The pass criterion
of each test is written in the logbook before the test starts, and every attempt is recorded,
including failed and invalid ones.

Common set-up: Pi powered (broker, receiver and dashboard start automatically), watch running
`ActivityClassifier` with its link status showing **MQTT link ok**, dashboard at
`http://<pi-address>:1880/dashboard`.

## C1 – Live classification (REQ-AC-01)

* **Criterion:** for each activity, the correct label is shown ≥ 90 % of the time, ignoring the
  first 10 s after each change.
* **Procedure:** on the watch's live screen hold BOOT for 1 s (starts a new session; the script
  uses it as t = 0) and start a timer. Sit 2 min, walk 2 min, run 2 min. Then:
  `python3 score_session.py --plan "sit:2,walk:2,run:2"`
* **Output:** per-activity share of time correct, time until the right label appeared, CSV.

## C11 – 30-minute session (REQ-AC-11)

* **Criterion:** ≥ 85 % of the 30 one-minute intervals correct. A minute counts as correct only
  if the planned label was shown for more than half of it.
* **Procedure:** as C1, with the plan written in the logbook first, e.g.
  `python3 score_session.py --plan "sit:10,walk:8,run:4,sit:8"`
* **Output:** minute-by-minute table, pass/fail, active minutes reported vs planned, CSV.
  The same session also serves C3: `python3 link_check.py --from "<start>" --to "<end>"`.

## C3 – Healthy link (REQ-AC-03)

* **Criterion:** every report arrives within 10 s; none lost.
* **Procedure:** `python3 link_check.py --from "YYYY-MM-DD HH:MM:SS" --to "..."` over a session.
* **Measure:** delay = receiver time − watch time (both NTP-synchronised); lost reports =
  missing sequence numbers.

## C4 – Degraded link (REQ-AC-04)

* **Criterion:** every report made during the test arrives within 30 s; none lost.
* **Procedure:** `sudo bash c4_bad_link.sh` applies 500 ms delay and 20 % loss to the Pi's Wi-Fi
  in both directions (outgoing with netem, incoming through an ifb interface), checks it with a
  ping to the router, and removes it automatically after 5 min. During the 5 min alternate
  sitting and walking each minute. Then `python3 link_check.py --last 6 --target 30`.
* **Note:** attempt 1 degraded the outgoing direction only and is recorded as invalid.

## C5 – Outage (REQ-AC-05)

* **Criterion:** every activity change made during the outage is stored afterwards with its real
  time; no sequence number missing.
* **Procedure:**
  `sudo systemctl stop mosquitto; sleep 60; sudo systemctl start mosquitto`
  During the outage walk ~30 s and sit again (two changes). One minute after the restart:
  `python3 link_check.py --last 4`

## C6 – Memory footprint (REQ-AC-06)

* **Criterion:** program storage and dynamic memory use stay below the limit set in the logbook.
* **Procedure:** Arduino IDE → Verify; record the "Sketch uses …" and "Global variables use …"
  lines. Board settings as in `setup_and_operation.txt`.

## C13 – Range in one room (A1)

* **Criterion:** from the far side of the room, every report within 10 s, none lost over 3 min.
* **Procedure:** record the distance to the Pi, stay there 3 min (some movement), then
  `python3 link_check.py --last 3`.

## C14 – Feature separation (A3)

* **Criterion:** in at least one feature, the middle 80 % (10th–90th percentile) of each
  activity does not overlap the other two.
* **Procedure:** watch connected by USB, Serial Monitor at 115200 baud (one line per 1.5 s
  window with its 10 features). Sit, walk and run 1 min each; save the log; compare the
  feature ranges per activity.

## C12 – Session comparison (REQ-AC-12)

* **Procedure:** after at least three logged sessions, open the dashboard Overview and record
  the current-session active minutes, the comparison with the previous session and the
  per-session chart.

## Thigh unit (C7, C9, C10, C16)

Set-up: thigh unit strapped to the front of the thigh and calibrated standing (hold its BOOT
button 2 s); watch running the firmware with `activity_board.h`; pill shows WRIST+THIGH.
Details: `firmware/ThighSensor/README.txt`.

**C7 – board packets reach the watch.** Criterion (write first), e.g. "over 5 minutes of
walking and sitting, at least 95 % of thigh packets arrive". Count: thigh unit windows sent
(its Serial Monitor) vs watch rows with `thigh = 1` in `/fusion/S000n.csv`; missing
`thigh_seq` numbers are lost packets.

**C10 – fallback when the board is unplugged.** Switch the thigh unit off for 60 s while
walking, then on. Expected: pill WRIST within ~3.5 s, reports tagged `wrist`; back to
WRIST+THIGH after it is switched on. Check the `sensors` field in the database
(`dashboard.db`, column `sensors`) or the dashboard's System health page.

**C16 – no I2C errors in motion.** Walk 5 min and run 2 min with the thigh unit; criterion
e.g. "0 failed sensor reads". Read `thigh_i2c_err` in the SD recording (or the thigh unit's
Serial Monitor `i2c_err_total`).

**C9 – wrist vs thigh vs fused.** Record at least 3 scripted sessions (BOOT hold on the watch
starts each one, plan written first). Copy `/fusion/S*.csv` from the watch's SD card, write
`sessions.csv` (file, plan) and run `python training/train_fusion.py <folder>`. It reports
the leave-one-session-out error of wrist-only, thigh-only, one fused model and the watch's
method (votes averaged) on the same windows, and writes `activity_model_thigh.h` trained on
your data. Public-data result (selfBACK, 9 people): `evidence/C9_fusion/`.
