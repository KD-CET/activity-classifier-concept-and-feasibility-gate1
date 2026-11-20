# Evidence

Evidence behind each feasibility claim (C1–C16) of the Stage 1 Concept and Feasibility
Report. Each claim tests one requirement (REQ-AC-xx) or one assumption (A1–A5) from the
proposal. **`claims_register.csv`** lists every claim with its evidence class (Measured,
Calculated, Assumed), its files, its status and the next step.

Pass criteria were written in the logbook before each test (C2 and C3 use the requirement's
own threshold; C12 is an observation). Failed and invalid attempts are kept.

## Status (November 20, 2026)

| Claim | Tests | Result | Status |
|---|---|---|---|
| C1 | REQ-AC-01 | Scripted 2/2/2 min sit/walk/run: correct label 100 % of the time per activity (criterion ≥ 90 %) | Supported |
| C2 | REQ-AC-02 | Leave-one-wearer-out error 3.3 % on 9 PAMAP2 wearers (target ≤ 15 %); 1.5 % with smoothing | Partly supported |
| C3 | REQ-AC-03 | Healthy link: 40/40 reports within 10 s, max 0.14 s (6 of the 30 min the method requires) | Partly supported |
| C4 | REQ-AC-04 | 500 ms + 20 % loss: 22/24 (phone hotspot) and 33/35 (home router) within 30 s; none lost | Unsupported |
| C5 | REQ-AC-05 | 60 s broker outage: all buffered reports delivered with their real times; none lost | Supported |
| C6 | REQ-AC-06 | Sketch incl. model 1,075,030 B (3 % of the 32 MB scheme), RAM 22 % | Supported |
| C7 | REQ-AC-07 | 284 of 284 thigh packets reached the watch (7 min, criterion ≥ 95 %) | Supported |
| C8 | REQ-AC-08 | One command deploys the server side on a fresh Pi; watch upload manual | Partly supported |
| C9 | REQ-AC-09 | selfBACK 9 people: fused 2.9 % vs wrist 5.0 % error; own hardware, one wearer: 95.8 % vs 89.2 % right | Partly supported |
| C10 | REQ-AC-10 | Thigh unit off: tagged `wrist` within 10 s; back on: `wrist+thigh` without restarting the watch | Supported |
| C11 | REQ-AC-11 | 30-min scripted session wearing both units, planned by Nov 23 | Open |
| C12 | REQ-AC-12 | Dashboard shows current-session active minutes beside the last 10 sessions | Supported |
| C13 | A1 | Thigh unit to watch 284/284; watch to Pi at ~5 m 22/23 within 10 s, none lost | Partly supported |
| C14 | A3 | Own watch: 8 of 10 features separate all three activities; cadence does not | Supported |
| C15 | A4 | Watch figures taken from the vendor wiki | Assumed (Open) |
| C16 | A5 | 0 I2C read errors in 284 windows (~17,000 reads) while walking and jogging in place | Supported |

## Folders

| Folder | Contents |
|---|---|
| `C1_live_classification/` | Scored scripted session (printout), dashboard after the test; also the C12 screenshot |
| `C2_classifier_error/` | Leave-one-wearer-out error per wearer (3 models), confusion matrices, effect of the sedentary-class definition and of smoothing |
| `C3_C4_C5_link_tests/` | `link_check.py` printouts: healthy link (C3), degraded link attempts 1–3 (C4), broker outage (C5); C13 photos |
| `C6_memory_footprint/` | Compile results (both partition schemes), exported model, C-vs-Python equivalence check (8,994/8,994 windows) |
| `C9_fusion/` | Fusion methods compared on selfBACK (9 and 33 people); own-hardware session S0012 and its results |
| `C14_features/` | Feature separation on PAMAP2 and on the watch (Serial log, figure, numbers) |
| `data_provenance/` | Dataset source, inclusion choices, minutes per activity and wearer |
| `system_integration/` | First end-to-end run (Nov 14), link-code simulation under loss and outage, 19 automated tests, dashboard renders |
| `thigh_unit/` | C7/C16 session S0011 and results, C10 screenshots, HARTH thigh results, thigh model results, filter simulation |
| `unseen_activities/` | Model output on activities it was never trained on |

## Known limits

* Training data is public (PAMAP2, wrist only). The placement matches the watch; the sensor
  does not, so live results on several wearers of this watch are still needed (C2).
* Three of the nine wearers have no running data.
* Fusion on own hardware is tested on one wearer, walking and jogging in place only (C9); the
  thigh model, trained on public HARTH data, is weak on jogging in place.
* The thigh unit was powered from a USB power bank in all tests, not its battery.
* C4's degradation was applied at the Pi in both directions, which is harsher than
  watch-to-computer only. C5's outage was a broker outage, not a radio outage.
