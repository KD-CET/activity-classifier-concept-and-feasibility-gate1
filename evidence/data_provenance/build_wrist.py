"""Builds wrist-only CSVs (one per subject) from the raw PAMAP2 .dat files.
Uses the hand IMU (worn on the dominant wrist): the +-16 g accelerometer (recommended by the
readme) and the gyroscope. Protocol and optional sessions are both used; each row keeps its session.
"""
import glob, os, sys
import pandas as pd

RAW = sys.argv[1:] or glob.glob("/mnt/user-data/uploads/subject*.dat")
OUT = "/home/claude/pamap2_wrist/wrist_csv"
os.makedirs(OUT, exist_ok=True)

NAMES = {1: "lying", 2: "sitting", 3: "standing", 4: "walking", 5: "running", 6: "cycling",
         7: "nordic_walking", 9: "watching_tv", 10: "computer_work", 11: "car_driving",
         12: "ascending_stairs", 13: "descending_stairs", 16: "vacuum_cleaning", 17: "ironing",
         18: "folding_laundry", 19: "house_cleaning", 20: "playing_soccer", 24: "rope_jumping"}
# sedentary = sitting, lying or standing still (Nov 11: car driving added; ironing and laundry were
# tested as sedentary but made vacuuming and stairs look sedentary too, so they stay test-only)
SEDENTARY = {1, 2, 3, 9, 10, 11}   # lying, sitting, standing, watching TV, computer work, car driving
LABEL = {**{a: "sedentary" for a in SEDENTARY}, 4: "walking", 5: "running"}
TEST_ONLY = {12, 13, 16, 17, 18, 19}   # stairs, vacuuming, ironing, laundry, cleaning: tested only
WRIST = {108: "left"}                   # dominant hand from subjectInformation.pdf; others right

cols = {0: "timestamp_s", 1: "activity_id", 4: "acc_x", 5: "acc_y", 6: "acc_z",
        10: "gyro_x", 11: "gyro_y", 12: "gyro_z"}
per_subject = {}
for f in sorted(RAW):
    d = pd.read_csv(f, sep=" ", header=None, usecols=list(cols)).rename(columns=cols)
    sid = int(os.path.basename(f).split("subject")[1][:3])
    session = "protocol" if d["activity_id"].isin([1, 2, 3, 4]).any() else "optional"
    keep = d["activity_id"].isin(set(LABEL) | TEST_ONLY)
    d = d[keep].dropna()                # rows where the wrist sensor dropped out are removed
    if d.empty:
        print(f"{os.path.basename(f)}: nothing usable, skipped"); continue
    d.insert(0, "session", session)
    d.insert(0, "wrist_side", WRIST.get(sid, "right"))
    d.insert(0, "subject", sid)
    d["activity"] = d["activity_id"].map(NAMES)
    d["label"] = d["activity_id"].map(LABEL).fillna("test_only")
    per_subject.setdefault(sid, []).append(d)
    print(f"{os.path.basename(f):<18} subject {sid} {session:<8} {len(d):>7} rows")

for sid, parts in sorted(per_subject.items()):
    d = pd.concat(parts)
    d["activity_id"] = d["activity_id"].astype(int)
    path = f"{OUT}/subject{sid}.csv.gz"
    d.to_csv(path, index=False, float_format="%.6g")
    mins = (d.groupby("label").size() / 6000).round(1).to_dict()
    print(f"subject{sid}.csv.gz  {os.path.getsize(path)/1e6:5.1f} MB  minutes: {mins}")
