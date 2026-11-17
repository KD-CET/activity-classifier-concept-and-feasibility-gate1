"""simulator.py - a fake watch, so the dashboard can be built before the real one works.

Examples:
  python simulator.py                         # session, changes every 20 s, heartbeat every 10 s
  python simulator.py --loss 0.3 --ackloss 0.2   # flaky link: lost messages + lost ACKs (retries, duplicates)
  python simulator.py --changes 6 --end       # stop after 6 changes and send session_end
Time is real time, so use small numbers while testing.
"""
import argparse
import json
import random
import time
import urllib.request

p = argparse.ArgumentParser()
p.add_argument("--url", default="http://localhost:8000/report")
p.add_argument("--change-every", type=float, default=20)
p.add_argument("--heartbeat", type=float, default=10)
p.add_argument("--changes", type=int, default=0, help="0 = run forever")
p.add_argument("--loss", type=float, default=0.0, help="chance a send never arrives")
p.add_argument("--ackloss", type=float, default=0.0, help="chance it arrives but the ACK is lost")
p.add_argument("--end", action="store_true", help="send session_end when done")
p.add_argument("--sensors", default="wrist+thigh")
args = p.parse_args()

seq = int(time.time()) % 1000000     # new run -> new numbers
activity = "sedentary"


def post(msg):
    req = urllib.request.Request(args.url, json.dumps(msg).encode(),
                                 {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=3) as r:
        return json.load(r)


def send(kind, **extra):
    """Like the watch: keep the same seq and resend until an ACK arrives."""
    global seq
    seq += 1
    msg = {"device_id": "watch1", "seq": seq, "type": kind, "ts": time.time(),
           "sensors": args.sensors, "retries": 0, **extra}
    for attempt in range(8):
        msg["retries"] = attempt
        r = random.random()
        try:
            if r < args.loss:
                raise OSError("simulated loss (never arrived)")
            ack = post(msg)
            if r < args.loss + args.ackloss:
                raise OSError("simulated lost ACK (arrived, watch does not know)")
            print(f"sent {kind:<15} seq={seq} retries={attempt} ack={ack}")
            return
        except OSError as e:
            print(f"  attempt {attempt} failed: {e}")
            time.sleep(1)
    print(f"  GAVE UP on seq={seq}")


send("session_start", activity=activity)
last_hb = last_change = time.time()
n = 0
try:
    while not args.changes or n < args.changes:
        time.sleep(0.5)
        now = time.time()
        if now - last_change >= args.change_every:
            activity = random.choice([a for a in ("sedentary", "walking", "running") if a != activity])
            send("activity_change", activity=activity)
            last_change, n = now, n + 1
        elif now - last_hb >= args.heartbeat:
            send("heartbeat")
            last_hb = now
except KeyboardInterrupt:
    pass
if args.end:
    send("session_end")
