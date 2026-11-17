"""score_session.py - scores a scripted session (claims C1 and C11) from the receiver's database.

You follow a written plan (e.g. 10 min sitting, 8 walking, 4 running, 8 sitting) while wearing the
watch. The receiver stored every label the watch SHOWED, with its time. This script compares
them with your plan:
  * minute by minute (C11): a minute counts as correct only if the planned activity's label was
    shown for MORE than half of that minute (a 50/50 minute counts as wrong)
  * per activity (C1): share of time the right label was shown, ignoring the first few seconds
    of each activity (the watch needs ~4.5 s to confirm a change)
and saves the table as a CSV next to the database (evidence for the logbook).

Start the test by HOLDING BOOT for 1 s on the watch's LIVE screen: that starts a new session,
and the script uses its start as the start of the plan (no clock reading needed).

  python score_session.py --plan "sedentary:10,walking:8,running:4,sedentary:8"
  python score_session.py --plan "sedentary:2,walking:2,running:2" --need 90
  python score_session.py --db dashboard.db --plan ... --start "2026-11-15 19:10:00"
Activities: sedentary, walking, running (or sit, walk, run). Minutes may have decimals.
Standard library only (runs on the Pi or the laptop).
"""
import argparse
import csv
import datetime as dt
import os
import sqlite3
import sys

ALIASES = {"sit": "sedentary", "sitting": "sedentary", "sedentary": "sedentary", "stand": "sedentary",
           "walk": "walking", "walking": "walking", "run": "running", "running": "running"}
ACTIVE = ("walking", "running")


def parse_plan(text):
    plan = []
    for part in text.split(","):
        name, _, minutes = part.strip().partition(":")
        if name.strip().lower() not in ALIASES or not minutes:
            sys.exit(f"bad plan part {part!r}: use activity:minutes, e.g. walking:8")
        plan.append((ALIASES[name.strip().lower()], float(minutes)))
    return plan


def load_events(db_path, device):
    if not os.path.exists(db_path):
        sys.exit(f"database not found: {db_path} (copy dashboard.db from the Pi, or give --db)")
    con = sqlite3.connect(db_path)
    rows = con.execute("SELECT type, activity, ts FROM events WHERE device_id = ? ORDER BY ts, seq",
                       (device,)).fetchall()
    con.close()
    return rows


def label_at(changes, t):
    """The label the watch showed at time t (last change at or before t)."""
    lab = None
    for ts, a in changes:
        if ts > t:
            break
        lab = a
    return lab


def main(argv=None):
    p = argparse.ArgumentParser(description="Score a scripted session against its plan")
    p.add_argument("--db", default=os.environ.get("DB_PATH", "dashboard.db"))
    p.add_argument("--device", default="watch1")
    p.add_argument("--plan", required=True, help='e.g. "sedentary:10,walking:8,running:4,sedentary:8"')
    p.add_argument("--start", help='plan start "YYYY-MM-DD HH:MM:SS" (default: start of the last session)')
    p.add_argument("--grace", type=float, default=10, help="seconds ignored after each change of activity (per-activity score)")
    p.add_argument("--need", type=float, default=85, help="pass mark in percent for the minute-by-minute score")
    a = p.parse_args(argv)

    plan = parse_plan(a.plan)
    rows = load_events(a.db, a.device)
    if a.start:
        start = dt.datetime.strptime(a.start, "%Y-%m-%d %H:%M:%S").timestamp()
    else:
        starts = [ts for typ, _, ts in rows if typ == "session_start"]
        if not starts:
            sys.exit("no session_start in the database: hold BOOT on the watch before the test, or give --start")
        start = starts[-1]
    total = sum(m for _, m in plan) * 60
    end = start + total
    changes = [(ts, act) for typ, act, ts in rows if typ in ("session_start", "activity_change") and act]
    last_ts = max((ts for _, _, ts in rows), default=0)
    if last_ts < end - 30:
        print(f"WARNING: the database ends {dt.datetime.fromtimestamp(last_ts):%H:%M:%S}, before the plan ends "
              f"{dt.datetime.fromtimestamp(end):%H:%M:%S}. Copy the database again after the test finishes.\n")

    # reference: planned activity at time t
    bounds, t = [], start
    for act, m in plan:
        bounds.append((t, t + m * 60, act))
        t += m * 60

    def planned(t):
        for s, e, act in bounds:
            if s <= t < e:
                return act
        return None

    step = 0.5
    # ---- minute by minute (C11) ----
    minutes = []
    n_min = int(total // 60)
    for i in range(n_min):
        s0 = start + i * 60
        shown, ref = {}, {}
        k = 0.0
        while k < 60:
            t = s0 + k
            lab = label_at(changes, t) or "none"
            shown[lab] = shown.get(lab, 0) + step
            r = planned(t)
            ref[r] = ref.get(r, 0) + step
            k += step
        shown_major = max(shown, key=shown.get)
        ref_major = max(ref, key=ref.get)
        # strict rule: a minute counts only if the planned label was shown for MORE than half of it
        right_share = shown.get(ref_major, 0) / 60
        minutes.append({"minute": i + 1, "from": dt.datetime.fromtimestamp(s0).strftime("%H:%M:%S"),
                        "planned": ref_major, "shown_most": shown_major,
                        "right_label_pct": round(100 * right_share),
                        "match": "yes" if right_share > 0.5 else "NO"})
    ok = sum(m["match"] == "yes" for m in minutes)

    # ---- per planned activity (C1) ----
    segs = []
    for s, e, act in bounds:
        t, good, n = s + a.grace, 0, 0
        while t < e:
            n += 1
            good += label_at(changes, t) == act
            t += step
        first_right = next((ts for ts, x in changes if s <= ts < e and x == act), None)
        if label_at(changes, s) == act:
            first_right = s
        segs.append({"activity": act, "from": dt.datetime.fromtimestamp(s).strftime("%H:%M:%S"),
                     "minutes": round((e - s) / 60, 2), "correct_pct": round(100 * good / max(n, 1), 1),
                     "label_after_s": None if first_right is None else round(first_right - s, 1)})

    # ---- active minutes ----
    shown_active = sum(step for k in range(int(total / step)) if label_at(changes, start + k * step) in ACTIVE) / 60
    plan_active = sum(m for act, m in plan if act in ACTIVE)

    print(f"Session scored from {dt.datetime.fromtimestamp(start):%Y-%m-%d %H:%M:%S}, plan {total/60:g} min: {a.plan}\n")
    print("Per activity (C1): share of time the right label was shown, first "
          f"{a.grace:g} s of each activity ignored")
    for sg in segs:
        lag = "never" if sg["label_after_s"] is None else f"{sg['label_after_s']:.1f} s"
        print(f"  {sg['from']}  {sg['activity']:<9} {sg['minutes']:>5g} min   correct {sg['correct_pct']:5.1f} %   "
              f"right label appeared after {lag}")
    print("\nMinute by minute (C11): label shown for most of each minute vs plan")
    for m in minutes:
        print(f"  {m['minute']:>2}  {m['from']}  planned {m['planned']:<9}  shown most {m['shown_most']:<9} "
              f"right label {m['right_label_pct']:>3} % of the minute  {m['match']}")
    pct = 100 * ok / max(len(minutes), 1)
    print(f"\nRESULT: {ok} of {len(minutes)} one-minute intervals match ({pct:.1f} %). "
          f"Pass mark {a.need:g} %: {'PASS' if pct >= a.need else 'FAIL'}")
    print(f"Active minutes: watch reported {shown_active:.1f} min, plan had {plan_active:g} min")

    out = os.path.join(os.path.dirname(os.path.abspath(a.db)),
                       f"session_score_{dt.datetime.fromtimestamp(start):%Y-%m-%d_%H%M}.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow([f"# plan {a.plan}; start {dt.datetime.fromtimestamp(start):%Y-%m-%d %H:%M:%S}; "
                    f"{ok}/{len(minutes)} minutes match ({pct:.1f} %); active shown {shown_active:.1f} min, planned {plan_active:g} min"])
        w.writerow(list(minutes[0].keys()) if minutes else ["minute"])
        for m in minutes:
            w.writerow(list(m.values()))
        w.writerow([])
        w.writerow(["activity", "from", "minutes", "correct_pct", "label_after_s"])
        for sg in segs:
            w.writerow(list(sg.values()))
    print(f"\nSaved: {out}")
    return pct


if __name__ == "__main__":
    main()
