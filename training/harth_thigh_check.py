"""harth_thigh_check.py - how well does a THIGH sensor alone classify sedentary / walking / running,
across many people? Uses the public HARTH dataset (22 people, accelerometer on the right front
thigh, CC BY 4.0, https://archive.ics.uci.edu/dataset/779/harth).

Same method as the wrist model: 40 Hz, 1.5 s windows (60 samples), random forest (10 trees,
depth 6, balanced classes), leave one person out.

HARTH has NO gyroscope, so only the thigh unit's accelerometer features can be computed:
  acc_mean, acc_std, acc_min, acc_max, acc_jerk, cadence_hz, cadence_power   (7, as on the watch)
  tilt_mean, tilt_std   angle from the standing pose. With no gyro the complementary filter
                        reduces to its accelerometer path, so tilt here = angle between the
                        window's mean acceleration direction and the person's mean standing
                        direction (the thigh unit's BOOT-button calibration does the same).
Labels: sedentary = standing, sitting, lying; walking; running. Shuffling, stairs and cycling are
left out (like the test-only activities in PAMAP2).

    python harth_thigh_check.py path/to/harth            (folder with S006.csv ... S029.csv)
Writes harth_thigh_results.csv and prints the tables.
"""
import glob
import os
import sys

import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import confusion_matrix

FS, WIN = 40.0, 60
LABELS = {6: "sedentary", 7: "sedentary", 8: "sedentary", 1: "walking", 2: "running"}
ACT = {6: "standing", 7: "sitting", 8: "lying", 1: "walking", 2: "running"}
CLASSES = ["running", "sedentary", "walking"]
ACC = ["acc_mean", "acc_std", "acc_min", "acc_max", "acc_jerk", "cadence_hz", "cadence_power"]
TILT = ["tilt_mean", "tilt_std"]


def acc_features(a):
    am = np.linalg.norm(a, axis=1)
    spec = np.abs(np.fft.rfft(am - am.mean(), n=256)) ** 2
    freqs = np.fft.rfftfreq(256, d=1 / FS)
    band = (freqs >= 0.5) & (freqs <= 4.0)
    k = np.argmax(spec[band])
    return [am.mean(), am.std(), am.min(), am.max(), np.abs(np.diff(am)).mean(),
            freqs[band][k], spec[band][k] / (spec.sum() + 1e-9)]


def tilt_features(a, ref):
    u = a / np.maximum(np.linalg.norm(a, axis=1, keepdims=True), 1e-6)
    ang = np.degrees(np.arccos(np.clip(u @ ref, -1, 1)))
    return [ang.mean(), ang.std()]


def subject_windows(path):
    d = pd.read_csv(path, usecols=["timestamp", "thigh_x", "thigh_y", "thigh_z", "label"])
    ts = pd.to_datetime(d["timestamp"])
    t = (ts - ts.iloc[0]).dt.total_seconds().to_numpy()          # seconds from the start
    a = d[["thigh_x", "thigh_y", "thigh_z"]].to_numpy(float)
    lab = d["label"].to_numpy()
    st = a[lab == 6]
    ref = st.mean(axis=0) / np.linalg.norm(st.mean(axis=0))      # "calibration": standing direction
    rows = []
    # unbroken runs of one label, no time gaps > 0.1 s, resampled to 40 Hz
    brk = np.r_[0, np.flatnonzero((np.diff(lab) != 0) | (np.diff(t) > 0.1)) + 1, len(lab)]
    for s, e in zip(brk[:-1], brk[1:]):
        if lab[s] not in LABELS or e - s < 10:
            continue
        tt = np.arange(t[s], t[e - 1], 1 / FS)
        x = np.column_stack([np.interp(tt, t[s:e], a[s:e, i]) for i in range(3)])
        for i in range(0, len(x) - WIN + 1, WIN):
            w = x[i:i + WIN]
            rows.append([LABELS[lab[s]], ACT[lab[s]]] + acc_features(w) + tilt_features(w, ref))
    df = pd.DataFrame(rows, columns=["label", "activity"] + ACC + TILT)
    df["subject"] = os.path.basename(path)[:4]
    return df


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "harth"
    files = sorted(glob.glob(os.path.join(folder, "S*.csv")))
    if not files:
        sys.exit(f"no S*.csv in {folder}")
    data = pd.concat([subject_windows(f) for f in files], ignore_index=True)
    print(f"{len(data)} windows from {data.subject.nunique()} people:",
          data["activity"].value_counts().to_dict())

    sets = {"thigh accel features (7)": ACC, "thigh accel + tilt (9)": ACC + TILT}
    res, cms = {}, {k: np.zeros((3, 3), int) for k in sets}
    for s in sorted(data.subject.unique()):
        tr, te = data.subject != s, data.subject == s
        row = {}
        for name, cols in sets.items():
            m = RandomForestClassifier(n_estimators=10, max_depth=6, class_weight="balanced",
                                       random_state=0).fit(data.loc[tr, cols], data.loc[tr, "label"])
            pred = m.predict(data.loc[te, cols])
            row[name] = round(100 * np.mean(pred != data.loc[te, "label"]), 1)
            cms[name] += confusion_matrix(data.loc[te, "label"], pred, labels=CLASSES)
        res[s] = row
    table = pd.DataFrame(res).T
    table.index.name = "left-out person"
    table.loc["MEAN"] = table.mean().round(1)
    table.loc["WORST"] = table.drop("MEAN").max()
    print("\nError rate (%) on the left-out person (leave one person out). Wrist model on PAMAP2: 3.3 %")
    print(table.to_string())
    for name, cm in cms.items():
        print(f"\nConfusion, {name} (rows = true, columns = predicted, order {CLASSES}):")
        print(pd.DataFrame(cm, index=CLASSES, columns=CLASSES).to_string())

    print("\nThigh tilt from the standing pose (deg), middle 80 % of windows:")
    for act in ["standing", "sitting", "lying", "walking", "running"]:
        v = data.loc[data.activity == act, "tilt_mean"]
        print(f"  {act:<9} {np.percentile(v, 10):6.1f} - {np.percentile(v, 90):6.1f}   (median {v.median():.1f}, {len(v)} windows)")
    table.to_csv("harth_thigh_results.csv")
    print("\nSaved: harth_thigh_results.csv")


if __name__ == "__main__":
    main()
