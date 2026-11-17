# Evidence

Evidence behind each feasibility claim (C1–C16) of the Stage 1 Concept and Feasibility
Report. Each claim tests one requirement (REQ-AC-xx) or one assumption (A1–A5) from the
proposal. **`claims_register.csv`** lists every claim with its evidence class (Measured,
Calculated, Assumed), its files, its status and the next step.

Pass criteria were fixed before each test. Failed and invalid attempts are kept.

## Status (November 16, 2026)

| Claim | Tests | Result | Status |
|---|---|---|---|
| C1 | REQ-AC-01 | Scripted 2/2/2 min sit/walk/run: correct label 100 % of the time per activity (criterion ≥ 90 %) | Supported |
| C2 | REQ-AC-02 | Leave-one-wearer-out error 3.3 % on 9 PAMAP2 wearers (target ≤ 15 %); 1.5 % with smoothing | Partly supported |
| C3 | REQ-AC-03 | Healthy link: 40/40 reports within 10 s, max 0.14 s (6 of the 30 min the method requires) | Partly supported |
| C4 | REQ-AC-04 | 500 ms + 20 % loss: 22/24 within 30 s (max 53.1 s); none lost | Unsupported |
| C5 | REQ-AC-05 | 60 s broker outage: all buffered reports delivered with their real times; none lost | Supported |
| C6 | REQ-AC-06 | Sketch incl. model 1,075,030 B (3 % of the 32 MB scheme), RAM 22 % | Supported |
| C7 | REQ-AC-07 | Board not built | Open |
| C8 | REQ-AC-08 | One command deploys the server side on a fresh Pi; watch upload manual | Partly supported |
| C9 | REQ-AC-09 | Needs board recordings | Open |
| C10 | REQ-AC-10 | Needs the board | Open |
| C11 | REQ-AC-11 | 30-min scripted session planned | Open |
| C12 | REQ-AC-12 | Dashboard shows current-session active minutes beside the last 10 sessions | Supported |
| C13 | A1 | Watch reaches the Pi in one room (no loss in C1/C5); board part not built | Partly supported |
| C14 | A3 | Own watch: 8 of 10 features separate all three activities; cadence does not | Supported |
| C15 | A4 | Watch figures taken from the vendor wiki | Assumed (Open) |
| C16 | A5 | Needs the board | Open |

## Folders

| Folder | Contents |
|---|---|
| `C1_live_classification/` | Scored scripted session (printout), dashboard after the test; also the C12 screenshot |
| `C2_classifier_error/` | Leave-one-wearer-out error per wearer (3 models), confusion matrices, effect of the sedentary-class definition and of smoothing |
| `C3_C4_C5_link_tests/` | `link_check.py` printouts: healthy link (C3), degraded link attempts 1 and 2 (C4), broker outage (C5) |
| `C6_memory_footprint/` | Compile results (both partition schemes), exported model, C-vs-Python equivalence check (8,994/8,994 windows) |
| `C14_features/` | Feature separation on PAMAP2 and on the watch (Serial log, figure, numbers) |
| `data_provenance/` | Dataset source, inclusion choices, minutes per activity and wearer |
| `system_integration/` | First end-to-end run (Nov 14), link-code simulation under loss and outage, 19 automated tests, dashboard renders |
| `unseen_activities/` | Model output on activities it was never trained on |

## Known limits

* Training data is public (PAMAP2, wrist only). The placement matches the watch; the sensor
  does not, so live results on several wearers of this watch are still needed (C2).
* Three of the nine wearers have no running data.
* The fusion half of the proof-of-concept question (C7, C9, C10, C16) needs the thigh board.
* C4's degradation was applied at the Pi in both directions, which is harsher than
  watch-to-computer only. C5's outage was a broker outage, not a radio outage.
