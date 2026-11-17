"""mqtt_simulator.py - a fake watch that talks MQTT, so the MQTT path can be tested without the watch.

It behaves like the real watch should:
  * Last Will: if it drops off without saying goodbye, the broker publishes "offline" for it.
  * Publishes "online" (retained) when it connects.
  * Sends each report on activity/<device>/report with QoS 1, then waits for the receiver's
    ack on activity/<device>/ack. No ack in time -> resend the SAME seq (duplicates are rejected).

Examples
  python mqtt_simulator.py                                   # normal session, runs until Ctrl+C
  python mqtt_simulator.py --changes 6 --end                 # 6 activity changes, then session_end
  python mqtt_simulator.py --loss 0.3 --ackloss 0.2          # flaky link: lost sends + lost acks
  python mqtt_simulator.py --drop-after 30                   # vanish after 30 s -> Last Will "offline"
  python mqtt_simulator.py --host 192.168.1.50               # broker on another machine (the Pi)
"""
import argparse
import json
import os
import random
import threading
import time

import paho.mqtt.client as mqtt


def make_client(client_id):
    try:                                             # paho-mqtt 2.x
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    except AttributeError:                           # paho-mqtt 1.x
        return mqtt.Client(client_id=client_id)


def parse(argv=None):
    p = argparse.ArgumentParser(description="Fake watch over MQTT")
    p.add_argument("--host", default=os.environ.get("MQTT_HOST", "localhost"))
    p.add_argument("--port", type=int, default=int(os.environ.get("MQTT_PORT", 1883)))
    p.add_argument("--user", default=os.environ.get("MQTT_USER"))
    p.add_argument("--password", default=os.environ.get("MQTT_PASS"))
    p.add_argument("--root", default=os.environ.get("TOPIC_ROOT", "activity"))
    p.add_argument("--device", default="watch1")
    p.add_argument("--sensors", default="wrist")
    p.add_argument("--change-every", type=float, default=20, help="seconds between activity changes")
    p.add_argument("--heartbeat", type=float, default=10, help="seconds between heartbeats")
    p.add_argument("--changes", type=int, default=0, help="stop after this many changes (0 = forever)")
    p.add_argument("--end", action="store_true", help="send session_end when done")
    p.add_argument("--loss", type=float, default=0.0, help="chance a send is never published")
    p.add_argument("--ackloss", type=float, default=0.0, help="chance an ack is ignored (as if lost)")
    p.add_argument("--ack-timeout", type=float, default=2.0, help="seconds to wait for an ack")
    p.add_argument("--tries", type=int, default=8, help="sends per message before giving up")
    p.add_argument("--drop-after", type=float, default=0, help="vanish after N s (tests Last Will)")
    p.add_argument("--seed", type=int, default=None)
    return p.parse_args(argv)


def main(argv=None):
    a = parse(argv)
    if a.seed is not None:
        random.seed(a.seed)
    t_report, t_ack, t_status = (f"{a.root}/{a.device}/report", f"{a.root}/{a.device}/ack",
                                 f"{a.root}/{a.device}/status")
    pending, replies = {}, {}
    connected = threading.Event()
    stats = {"sent": 0, "resends": 0, "gave_up": 0, "duplicates": 0}

    client = make_client(f"{a.device}-sim-{os.getpid()}")
    if a.user:
        client.username_pw_set(a.user, a.password)
    client.will_set(t_status, "offline", qos=1, retain=True)       # the broker sends this if we vanish

    def on_connect(c, userdata, flags, rc, properties=None):
        c.subscribe(t_ack, qos=1)
        c.publish(t_status, "online", qos=1, retain=True)
        connected.set()
        print(f"connected to {a.host}:{a.port} as {a.device}")

    def on_message(c, userdata, msg):
        try:
            r = json.loads(msg.payload)
        except ValueError:
            return
        seq = r.get("ack", r.get("nack"))
        if seq not in pending:
            return
        if random.random() < a.ackloss:
            print(f"  (simulated lost ack for seq={seq})")
            return
        replies[seq] = r
        pending[seq].set()

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect_async(a.host, a.port, keepalive=15)
    client.loop_start()
    if not connected.wait(10):
        print(f"could not reach the broker at {a.host}:{a.port} - is Mosquitto running?")
        client.loop_stop()
        return stats

    seq = [int(time.time()) % 1_000_000]            # new run -> new numbers

    def send(kind, **extra):
        seq[0] += 1
        s = seq[0]
        msg = {"device_id": a.device, "seq": s, "type": kind, "ts": time.time(),
               "sensors": a.sensors, "retries": 0, **extra}
        pending[s] = threading.Event()
        for attempt in range(a.tries):
            msg["retries"] = attempt
            if attempt:
                stats["resends"] += 1
            if random.random() < a.loss:
                print(f"  attempt {attempt}: simulated loss (never published)")
            else:
                client.publish(t_report, json.dumps(msg), qos=1)
            if pending[s].wait(a.ack_timeout):
                r = replies.pop(s)
                pending.pop(s)
                if "nack" in r:
                    print(f"REJECTED {kind} seq={s}: {r.get('error')}")
                    return False
                stats["sent"] += 1
                stats["duplicates"] += int(bool(r.get("duplicate")))
                print(f"sent {kind:<15} {extra.get('activity', ''):<9} seq={s} retries={attempt}"
                      f"{' (receiver already had it)' if r.get('duplicate') else ''}")
                return True
        pending.pop(s, None)
        stats["gave_up"] += 1
        print(f"  GAVE UP on {kind} seq={s}")
        return False

    activity = "sedentary"
    start = time.time()
    send("session_start", activity=activity)
    last_hb = last_change = time.time()
    n = 0
    try:
        while not a.changes or n < a.changes:
            time.sleep(0.2)
            now = time.time()
            if a.drop_after and now - start >= a.drop_after:
                print("dropping off without goodbye -> the broker should publish 'offline' (Last Will)")
                os._exit(0)                          # no DISCONNECT is sent, like a dead battery
            if now - last_change >= a.change_every:
                activity = random.choice([x for x in ("sedentary", "walking", "running") if x != activity])
                send("activity_change", activity=activity)
                last_change, n = now, n + 1
            elif now - last_hb >= a.heartbeat:
                send("heartbeat")
                last_hb = now
    except KeyboardInterrupt:
        pass
    if a.end:
        send("session_end")
    client.publish(t_status, "offline", qos=1, retain=True)       # clean goodbye
    time.sleep(0.3)
    client.disconnect()
    client.loop_stop()
    print(f"done: {stats['sent']} delivered, {stats['resends']} resends, "
          f"{stats['duplicates']} duplicates rejected, {stats['gave_up']} gave up")
    return stats


if __name__ == "__main__":
    main()
