"""mqtt_receiver.py - MQTT front door for the watch's reports.

Use this INSTEAD of receiver.py when the watch sends over MQTT. It also serves the same HTTP
API as receiver.py, so HTTP stays available as a backup:
    GET  /events   all stored events (the dashboard reads this)
    GET  /health   {"ok": true, "mqtt_connected": ..., "watch_status": {...}}
    POST /report   the old HTTP way still works

MQTT topics (TOPIC_ROOT = "activity", device = e.g. "watch1")
    activity/<device>/report   watch -> receiver   one JSON report, same fields as the HTTP body
    activity/<device>/ack      receiver -> watch   {"ack": seq, "duplicate": bool}  (stored)
                                                   {"nack": seq, "error": "..."}    (rejected, don't resend)
    activity/<device>/status   watch / broker      "online", or "offline" sent by the broker's
                                                   Last Will when the watch drops off
    activity/summary           receiver -> dashboards   everything a dashboard shows (same JSON as
                                                   GET /api/summary + watch_status), retained,
                                                   every 3 s and right after each new report.
                                                   The Node-RED dashboard (nodered/) reads this.

Run (the broker, Mosquitto, must already be running - see MQTT_GUIDE.txt)
    python mqtt_receiver.py
Settings (environment variables): MQTT_HOST (default localhost), MQTT_PORT (1883),
MQTT_USER / MQTT_PASS (optional), TOPIC_ROOT (activity), PORT (8000, for the HTTP API), DB_PATH.
"""
import json
import os
import threading
import time
from http.server import ThreadingHTTPServer

import paho.mqtt.client as mqtt

import db
import receiver                      # reuse validate() and the HTTP handler

MQTT_HOST = os.environ.get("MQTT_HOST", "localhost")
MQTT_PORT = int(os.environ.get("MQTT_PORT", 1883))
MQTT_USER = os.environ.get("MQTT_USER")
MQTT_PASS = os.environ.get("MQTT_PASS")
TOPIC_ROOT = os.environ.get("TOPIC_ROOT", "activity")
HTTP_PORT = int(os.environ.get("PORT", 8000))
SUMMARY_TOPIC = f"{TOPIC_ROOT}/summary"
SUMMARY_EVERY_S = float(os.environ.get("SUMMARY_EVERY_S", 3))

state = {"mqtt_connected": False, "watch_status": {}}    # watch_status: device -> (text, time)
publish_now = threading.Event()                          # set after a new report: publish at once


# ---------------- message handling (no network code, so it can be unit tested) -----------
def handle_report(topic, payload):
    """Returns (reply_topic, reply_dict, log_line). Never raises on bad input."""
    parts = topic.split("/")
    device = parts[1] if len(parts) == 3 else "unknown"
    reply_topic = f"{TOPIC_ROOT}/{device}/ack"
    try:
        msg = json.loads(payload)
    except (ValueError, UnicodeDecodeError):
        return reply_topic, {"nack": None, "error": "invalid JSON"}, f"REJECTED from {device}: invalid JSON"
    seq = msg.get("seq") if isinstance(msg, dict) else None
    if isinstance(msg, dict) and "device_id" in msg and str(msg["device_id"]) != device:
        return reply_topic, {"nack": seq, "error": "device_id does not match topic"}, \
            f"REJECTED from {device}: device_id {msg['device_id']!r} does not match topic"
    ev, err = receiver.validate(msg)
    if err:
        return reply_topic, {"nack": seq, "error": err}, f"REJECTED from {device} seq={seq}: {err}"
    new = db.insert_event(ev)
    line = (f"{time.strftime('%H:%M:%S')} {ev['type']:<15} {ev['activity'] or '-':<9} "
            f"seq={ev['seq']} retries={ev['retries']} via MQTT{'' if new else ' DUPLICATE'}")
    # ACK duplicates too, otherwise the watch would keep resending
    return reply_topic, {"ack": ev["seq"], "duplicate": not new}, line


def handle_status(topic, payload):
    device = topic.split("/")[1] if topic.count("/") == 2 else "unknown"
    text = payload.decode(errors="replace").strip() if isinstance(payload, bytes) else str(payload)
    state["watch_status"][device] = (text, time.time())
    return f"{time.strftime('%H:%M:%S')} {device} is {text.upper()}"


def dashboard_payload(force=False):
    """The summary every dashboard shows, plus the broker's view of the watch (Last Will)."""
    s = dict(receiver.summary_service.get(force=force))
    latest = max(state["watch_status"].items(), key=lambda kv: kv[1][1], default=None)
    s["watch_status"] = {"device": latest[0], "status": latest[1][0], "since": latest[1][1]} if latest else None
    s["mqtt_connected"] = state["mqtt_connected"]
    return s


def publisher_loop(client):
    """Publishes the summary every SUMMARY_EVERY_S seconds, and straight after each new report."""
    while True:
        woke = publish_now.wait(SUMMARY_EVERY_S)
        publish_now.clear()
        if state["mqtt_connected"]:
            try:
                client.publish(SUMMARY_TOPIC, json.dumps(dashboard_payload(force=woke), separators=(",", ":")),
                               qos=0, retain=True)
            except Exception as e:
                print(f"could not publish the summary: {e}")


# ---------------- MQTT client ---------------------------------------------------------------
def make_client(client_id):
    try:                                             # paho-mqtt 2.x
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    except AttributeError:                           # paho-mqtt 1.x
        return mqtt.Client(client_id=client_id)


def failed(rc):
    return rc.is_failure if hasattr(rc, "is_failure") else rc != 0


def on_connect(client, userdata, flags, rc, properties=None):
    if failed(rc):
        print(f"MQTT connection refused: {rc}")
        return
    state["mqtt_connected"] = True
    client.subscribe([(f"{TOPIC_ROOT}/+/report", 1), (f"{TOPIC_ROOT}/+/status", 1)])
    print(f"MQTT connected to {MQTT_HOST}:{MQTT_PORT}, listening on {TOPIC_ROOT}/+/report and /status")


def on_disconnect(client, userdata, *args):
    state["mqtt_connected"] = False
    print("MQTT disconnected - reconnecting automatically...")


def on_message(client, userdata, msg):
    try:
        if msg.topic.endswith("/report"):
            reply_topic, reply, line = handle_report(msg.topic, msg.payload)
            client.publish(reply_topic, json.dumps(reply), qos=1)
            print(line)
            if "ack" in reply and not reply.get("duplicate"):
                publish_now.set()                       # dashboards see the change at once
        elif msg.topic.endswith("/status"):
            print(handle_status(msg.topic, msg.payload))
            publish_now.set()
    except Exception as e:                          # keep the receiver alive no matter what
        print(f"error handling {msg.topic}: {e}")


# ---------------- HTTP API (same as receiver.py, plus MQTT state in /health) ----------------
class Handler(receiver.Handler):
    def do_GET(self):
        if self.path == "/health":
            status = {d: {"status": s, "since": t} for d, (s, t) in state["watch_status"].items()}
            return self._send(200, {"ok": True, "mqtt_connected": state["mqtt_connected"],
                                    "watch_status": status})
        return super().do_GET()


def start_http():
    srv = ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print(f"HTTP API on 0.0.0.0:{HTTP_PORT}  (dashboard reads /events; POST /report still works)")
    return srv


def main():
    db.connect().close()
    print(f"database: {db.DB_PATH}")
    start_http()
    client = make_client(f"activity-receiver-{os.getpid()}")
    if MQTT_USER:
        client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    threading.Thread(target=publisher_loop, args=(client,), daemon=True).start()
    print(f"dashboard summary on MQTT topic {SUMMARY_TOPIC} every {SUMMARY_EVERY_S:g} s")
    print(f"connecting to MQTT broker {MQTT_HOST}:{MQTT_PORT} ... (keeps retrying if it is not up yet)")
    client.connect_async(MQTT_HOST, MQTT_PORT, keepalive=30)
    client.loop_forever(retry_first_connection=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nstopped")
