"""selfback_fusion_check.py - wrist-only vs thigh-only vs fused, on people who wore BOTH sensors at
the same time (claim C9, REQ-AC-09). Public selfBACK dataset (UCI, CC BY 4.0,
https://archive.ics.uci.edu/dataset/521/selfback): Axivity AX3 accelerometers on the dominant
wrist and the thigh, 100 Hz, recorded together.

Same method as the wrist model on PAMAP2: 9 people, 40 Hz, 1.5 s windows (60 samples), random
forest (10 trees, depth 6, balanced classes), leave one person out. All models use the SAME
windows and the SAME test person, so the errors can be compared directly.

selfBACK has NO gyroscope, so only accelerometer features exist:
  wrist: acc_mean, acc_std, acc_min, acc_max, acc_jerk, cadence_hz, cadence_power        (7)
  thigh: the same 7 + tilt_mean, tilt_std (angle from the person's standing direction,
         like the thigh unit's calibration; with no gyro the complementary filter reduces to
         its accelerometer path)                                                         (9)
Models compared:
  wrist only (7) | thigh only (9) | fused, one model on wrist + thigh features (16)
  | late fusion: wrist model and thigh model trained separately, votes averaged
Labels: sedentary = sitting, standing, lying; walking = slow, moderate, fast; running = jogging.
Stairs are left out (test-only activities, as with PAMAP2).

    python selfback_fusion_check.py path/to/selfBACK [--people 9]
Uses the wt/ folder (6 columns: wrist x y z, thigh x y z). People are taken in ID order.
"""
import argparse
import os
import sys

import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import confusion_matrix

FS_IN, FS, WIN = 100.0, 40.0, 60
ACTS = {"sitting": "sedentary", "standing": "sedentary", "lying": "sedentary",
        "walkslow": "walking", "walkmod": "walking", "walkfast": "walking", "jogging": "running"}
CLASSES = ["running", "sedentary", "walking"]
ACC = ["acc_mean", "acc_std", "acc_min", "acc_max", "acc_jerk", "cadence_hz", "cadence_power"]
W = ["w_" + f for f in ACC]
T = ["t_" + f for f in ACC] + ["t_tilt_mean", "t_tilt_std"]


def make_model():
    return RandomForestClassifier(n_estimators=10, max_depth=6, class_weight="balanced", random_state=0)


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


def resample(x):
    t_in = np.arange(len(x)) / FS_IN
    t = np.arange(0, t_in[-1], 1 / FS)
    return np.column_stack([np.interp(t, t_in, x[:, i]) for i in range(x.shape[1])])


def person_windows(folder, pid):
    st = np.loadtxt(os.path.join(folder, f"{pid}_standing"), delimiter=",")
    ref = st[:, 3:6].mean(axis=0)
    ref /= np.linalg.norm(ref)                         # "calibration": thigh direction when standing
    rows = []
    for act, lab in ACTS.items():
        x = resample(np.loadtxt(os.path.join(folder, f"{pid}_{act}"), delimiter=","))
        for i in range(0, len(x) - WIN + 1, WIN):
            w = x[i:i + WIN]
            rows.append([pid, lab, act] + acc_features(w[:, :3]) + acc_features(w[:, 3:6])
                        + tilt_features(w[:, 3:6], ref))
    return pd.DataFrame(rows, columns=["person", "label", "activity"] + W + T)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("folder", help="selfBACK folder (contains wt/)")
    p.add_argument("--people", type=int, default=9)
    p.add_argument("--out", default="selfback_fusion_results.csv")
    a = p.parse_args()
    wt = os.path.join(a.folder, "wt")
    ids = sorted({f.split("_")[0] for f in os.listdir(wt) if not f.startswith(".")})
    use = ids[:a.people]
    print(f"{len(ids)} people in the dataset, using the first {len(use)} by ID: {', '.join(use)}")
    data = pd.concat([person_windows(wt, pid) for pid in use], ignore_index=True)
    print(f"{len(data)} windows:", data["label"].value_counts().to_dict())

    sets = {"wrist only (7)": W, "thigh only (9)": T, "fused, one model (16)": W + T}
    res, cms = {}, {k: np.zeros((3, 3), int) for k in list(sets) + ["late fusion (votes averaged)"]}
    for pid in use:
        tr, te = data.person != pid, data.person == pid
        row, proba = {}, {}
        for name, cols in sets.items():
            m = make_model().fit(data.loc[tr, cols], data.loc[tr, "label"])
            pred = m.predict(data.loc[te, cols])
            proba[name] = m.predict_proba(data.loc[te, cols])
            row[name] = round(100 * np.mean(pred != data.loc[te, "label"]), 1)
            cms[name] += confusion_matrix(data.loc[te, "label"], pred, labels=CLASSES)
        avg = (proba["wrist only (7)"] + proba["thigh only (9)"]) / 2      # classes_ are sorted alike
        pred = np.array(CLASSES)[avg.argmax(axis=1)]
        row["late fusion (votes averaged)"] = round(100 * np.mean(pred != data.loc[te, "label"]), 1)
        cms["late fusion (votes averaged)"] += confusion_matrix(data.loc[te, "label"], pred, labels=CLASSES)
        res[pid] = row
    table = pd.DataFrame(res).T
    table.index.name = "left-out person"
    table.loc["MEAN"] = table.mean().round(1)
    table.loc["WORST"] = table.drop("MEAN").max()
    print("\nError rate (%) on the left-out person, same windows for every model (leave one person out):")
    print(table.to_string())
    for name, cm in cms.items():
        print(f"\nConfusion, {name} (rows = true, columns = predicted, order {CLASSES}):")
        print(pd.DataFrame(cm, index=CLASSES, columns=CLASSES).to_string())
    table.to_csv(a.out)
    print(f"\nSaved: {a.out}")


if __name__ == "__main__":
    main()
