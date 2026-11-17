"""link_check.py - checks the watch link from the receiver's database (claims C3, C4, C5).

For every report stored in a time window it uses:
  delay = when the receiver got it (Pi clock) - when it happened (watch clock)
and the watch's message counter (seq), which goes up by 1 for every report it sends.
  * C3 (healthy link): all reports arrive within --target seconds (default 10)
  * C4 (degraded link): same, with --target 30
  * C5 (outage): no seq number is missing (nothing lost), and the reports made during the
    outage arrived afterwards with their real times
Both clocks come from the internet (NTP), so the delay is only as good as the two clocks.

  python3 link_check.py --last 10                    the last 10 minutes
  python3 link_check.py --from "2026-11-16 19:00:00" --to "2026-11-16 19:10:00"
  python3 link_check.py --last 10 --target 30        degraded-link test (C4)
Standard library only (runs on the Pi or the laptop).
"""
import argparse
import csv
import datetime as dt
import os
import sqlite3
import statistics
import sys
import time


def when(ts):
    return dt.datetime.fromtimestamp(ts).strftime("%H:%M:%S")


def main(argv=None):
    p = argparse.ArgumentParser(description="Check delivery delay and losses on the watch link")
    p.add_argument("--db", default=os.environ.get("DB_PATH", "dashboard.db"))
    p.add_argument("--device", default="watch1")
    p.add_argument("--last", type=float, help="check the last N minutes")
    p.add_argument("--from", dest="t_from", help='"YYYY-MM-DD HH:MM:SS"')
    p.add_argument("--to", dest="t_to", help='"YYYY-MM-DD HH:MM:SS" (default: now)')
    p.add_argument("--target", type=float, default=10, help="delay target in seconds (C3: 10, C4: 30)")
    a = p.parse_args(argv)

    if not os.path.exists(a.db):
        sys.exit(f"database not found: {a.db}")
    fmt = "%Y-%m-%d %H:%M:%S"
    t_to = dt.datetime.strptime(a.t_to, fmt).timestamp() if a.t_to else time.time()
    if a.last:
        t_from = t_to - a.last * 60
    elif a.t_from:
        t_from = dt.datetime.strptime(a.t_from, fmt).timestamp()
    else:
        sys.exit("give --last N (minutes) or --from \"YYYY-MM-DD HH:MM:SS\"")

    con = sqlite3.connect(a.db)
    rows = con.execute("SELECT seq, type, activity, ts, recv_ts, retries FROM events "
                       "WHERE device_id = ? AND ts >= ? AND ts <= ? ORDER BY seq",
                       (a.device, t_from, t_to)).fetchall()
    con.close()
    if not rows:
        sys.exit(f"no reports from {a.device} between {when(t_from)} and {when(t_to)}")

    delays = [r[4] - r[3] for r in rows]
    late = [r for r in rows if r[4] - r[3] > a.target]
    retried = [r for r in rows if r[5] > 0]
    # missing seq numbers (seq restarts when the watch reboots: a jump of more than 1000 is a reboot)
    gaps, reboots = [], 0
    for prev, cur in zip(rows, rows[1:]):
        d = cur[0] - prev[0]
        if 1 < d <= 1000:
            gaps.append((prev[0] + 1, cur[0] - 1))
        elif d > 1000 or d < 0:
            reboots += 1
    missing = sum(b - a_ + 1 for a_, b in gaps)
    # longest time with nothing arriving at the receiver (shows the outage)
    recv = sorted(r[4] for r in rows)
    silence = max(((b - a_, a_, b) for a_, b in zip(recv, recv[1:])), default=(0, 0, 0))

    print(f"Reports from {a.device} made between {when(t_from)} and {when(t_to)}: {len(rows)} "
          f"({sum(r[1] == 'activity_change' for r in rows)} activity changes, "
          f"{sum(r[1] == 'heartbeat' for r in rows)} heartbeats)\n")
    print(f"Delivery delay (receiver clock - watch clock): min {min(delays):.2f} s, "
          f"median {statistics.median(delays):.2f} s, max {max(delays):.2f} s")
    print(f"Over the {a.target:g} s target: {len(late)} of {len(rows)}")
    for r in late[:20]:
        print(f"   seq {r[0]}  {r[1]:<15} {r[2] or '':<9} made {when(r[3])}  arrived {when(r[4])}  "
              f"delay {r[4] - r[3]:.1f} s  resent {r[5]}x")
    if len(late) > 20:
        print(f"   ... and {len(late) - 20} more (all in the CSV)")
    print(f"Resent at least once: {len(retried)} of {len(rows)}")
    print(f"Missing seq numbers (lost reports): {missing}" + (f"  {gaps}" if gaps else "") +
          (f"   (watch restarted {reboots}x in this window)" if reboots else ""))
    print(f"Longest time with nothing arriving: {silence[0]:.0f} s"
          + (f" ({when(silence[1])} to {when(silence[2])})" if silence[0] else ""))
    ok_delay = not late
    print(f"\nRESULT: {'all' if ok_delay else 'NOT all'} reports within {a.target:g} s; "
          f"{'none' if not missing else missing} lost.")

    out = os.path.join(os.path.dirname(os.path.abspath(a.db)),
                       f"link_check_{dt.datetime.fromtimestamp(t_from):%Y-%m-%d_%H%M}.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow([f"# {a.device} {when(t_from)}-{when(t_to)}; {len(rows)} reports; "
                    f"delay median {statistics.median(delays):.2f} s max {max(delays):.2f} s; "
                    f"over {a.target:g} s: {len(late)}; missing seq: {missing}; resent: {len(retried)}"])
        w.writerow(["seq", "type", "activity", "made", "arrived", "delay_s", "resent"])
        for r in rows:
            w.writerow([r[0], r[1], r[2] or "", when(r[3]), when(r[4]), round(r[4] - r[3], 3), r[5]])
    print(f"\nSaved: {out}")


if __name__ == "__main__":
    main()
