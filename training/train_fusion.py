"""train_fusion.py - wrist-only vs thigh-only vs fused, from your OWN recordings (C9), and a thigh
model trained on your data for the watch's sensor fusion (activity_model_thigh.h).

Data: the watch records every 1.5 s window to its SD card while the thigh unit is on:
/fusion/S0001.csv, S0002.csv ... (a new file at power-on and at each BOOT hold). Each session
follows a written plan, exactly like score_session.py, e.g. sit 10, walk 8, run 4, sit 8 min.

1. Copy the CSV files from the SD card into a folder, e.g. training/fusion_data/.
2. Write training/fusion_data/sessions.csv, one line per session (plan = activity:minutes,...):
       file,plan
       S0001.csv,"sit:10,walk:8,run:4,sit:8"
       S0002.csv,"walk:5,sit:5,run:3,sit:5"
   The plan starts at the file's first window (start the session with a BOOT hold, then follow
   the plan from that moment).
3. python train_fusion.py fusion_data
   Prints the error of each model for each left-out session (leave one session out: tested on a
   session it was not trained on), writes C9_wrist_thigh_fused.csv, and writes
   activity_model_thigh.h trained on your recordings. Copying it into firmware/ActivityClassifier/
   replaces the public-data thigh model (same interface); the watch keeps averaging the votes of
   the wrist and thigh models.

Fair comparison: all three models use the SAME windows (only windows where thigh packets were
fresh, minus the first --grace seconds after each change of activity), the same settings as the
wrist model (random forest, 10 trees, depth 6, balanced classes) and the same test sessions.
Needs: numpy, pandas, scikit-learn.
"""
import argparse
import os
import sys

import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from thigh_model_public import THG_IDX, export_thigh_to_c   # noqa: E402  same format as the watch expects

ALIASES = {"sit": "sedentary", "sitting": "sedentary", "stand": "sedentary", "sedentary": "sedentary",
           "walk": "walking", "walking": "walking", "run": "running", "running": "running"}
CLASSES = ["running", "sedentary", "walking"]          # same order as the watch's model
WRIST = ["w_acc_mean", "w_acc_std", "w_acc_min", "w_acc_max", "w_acc_jerk",
         "w_gyro_mean", "w_gyro_std", "w_gyro_max", "w_cadence_hz", "w_cadence_power"]
THIGH = ["t_acc_mean", "t_acc_std", "t_acc_min", "t_acc_max", "t_acc_jerk",
         "t_gyro_mean", "t_gyro_std", "t_gyro_max", "t_cadence_hz", "t_cadence_power",
         "t_tilt_mean", "t_tilt_std", "t_tilt_acc_mean", "t_tilt_acc_std"]   # thigh_packet.h order
THG = [THIGH[i] for i in THG_IDX]          # the 9 the watch's thigh model uses
WIN_S = 1.5


def make_model():
    return RandomForestClassifier(n_estimators=10, max_depth=6, class_weight="balanced", random_state=0)


def parse_plan(text):
    plan = []
    for part in text.split(","):
        name, _, minutes = part.strip().partition(":")
        if name.strip().lower() not in ALIASES or not minutes:
            sys.exit(f"bad plan part {part!r}: use activity:minutes, e.g. walk:8")
        plan.append((ALIASES[name.strip().lower()], float(minutes) * 60))
    return plan


def label_session(df, plan, grace):
    """Planned activity for each window (by its middle), None near a change or after the plan."""
    t = (df["ms"].to_numpy() - df["ms"].iloc[0]) / 1000.0 + WIN_S - WIN_S / 2   # window middle
    labels, start = [None] * len(df), 0.0
    for act, dur in plan:
        for i, ti in enumerate(t):
            if start + grace <= ti < start + dur:
                labels[i] = act
        start += dur
    return labels


def load(folder, grace):
    sess = pd.read_csv(os.path.join(folder, "sessions.csv"))
    frames = []
    for _, row in sess.iterrows():
        df = pd.read_csv(os.path.join(folder, row["file"]))
        df["label"] = label_session(df, parse_plan(row["plan"]), grace)
        df["session"] = row["file"]
        frames.append(df)
    data = pd.concat(frames, ignore_index=True)
    n_all = len(data)
    data = data[data["label"].notna()]
    n_lab = len(data)
    data = data[(data["thigh"] == 1) & data[THIGH].notna().all(axis=1)]
    print(f"{n_all} windows recorded, {n_lab} inside the plans (minus {grace:g} s after each change), "
          f"{len(data)} of them with fresh thigh data: used for every model.")
    return data


def main(argv=None):
    p = argparse.ArgumentParser(description="Wrist vs thigh vs fused, leave one session out (C9)")
    p.add_argument("folder", help="folder with the S*.csv files and sessions.csv")
    p.add_argument("--grace", type=float, default=10, help="seconds ignored after each change of activity")
    p.add_argument("--out", default="activity_model_thigh.h")
    a = p.parse_args(argv)

    data = load(a.folder, a.grace)
    missing = [c for c in CLASSES if c not in set(data["label"])]
    if missing:
        sys.exit(f"no windows for {missing}: every class must appear in the recordings")
    sets = {"wrist only": WRIST, "thigh only (watch's 9)": THG, "thigh only (all 14)": THIGH,
            "fused, one model": WRIST + THIGH}
    sessions = sorted(data["session"].unique())
    rows = []
    if len(sessions) < 2:
        print("Only one session: leave-one-session-out needs at least two. Record another session.")
    else:
        for s in sessions:
            tr, te = data["session"] != s, data["session"] == s
            if len(set(data.loc[tr, "label"])) < len(CLASSES):
                print(f"  skipping {s}: the other sessions do not contain all three activities")
                continue
            row, proba = {"left-out session": s, "windows": int(te.sum())}, {}
            y_te = data.loc[te, "label"].to_numpy()
            for name, cols in sets.items():
                m = make_model().fit(data.loc[tr, cols].to_numpy(), data.loc[tr, "label"])
                proba[name] = m.predict_proba(data.loc[te, cols].to_numpy())
                row[name] = round(100 * np.mean(m.predict(data.loc[te, cols].to_numpy()) != y_te), 1)
            # the watch's method: wrist and thigh models trained separately, votes averaged
            avg = (proba["wrist only"] + proba["thigh only (watch's 9)"]) / 2
            row["late fusion (as on the watch)"] = round(100 * np.mean(np.array(CLASSES)[avg.argmax(1)] != y_te), 1)
            # what the watch itself decided live (PAMAP2 wrist + public thigh model at the time)
            row["watch live decision"] = round(100 * np.mean(data.loc[te, "label_raw"] != y_te), 1)
            rows.append(row)
    if rows:
        table = pd.DataFrame(rows).set_index("left-out session")
        w = table["windows"]
        mean = {c: round(float(np.average(table[c], weights=w)), 1) for c in table.columns if c != "windows"}
        table.loc["MEAN (weighted by windows)"] = {"windows": int(w.sum()), **mean}
        print("\nError rate (%) on the left-out session, same windows for every model (C9, REQ-AC-09):")
        print(table.to_string())
        out_csv = os.path.join(a.folder, "C9_wrist_thigh_fused.csv")
        table.to_csv(out_csv)
        print(f"Saved: {out_csv}")

    final = make_model().fit(data[THG].to_numpy(), data["label"])
    if list(final.classes_) != CLASSES:
        sys.exit(f"class order {list(final.classes_)} differs from the watch {CLASSES}")
    mid = export_thigh_to_c(final, a.out, f"Trained on own recordings ({len(data)} windows, {len(sessions)} sessions).")
    nodes = sum(t.tree_.node_count for t in final.estimators_)
    print(f"\nWrote {a.out}: thigh model on all {len(data)} windows, {nodes} nodes, id 0x{mid:08X}. "
          "Copy it into firmware/ActivityClassifier/ (replaces the public-data thigh model) and upload.")


if __name__ == "__main__":
    main()
