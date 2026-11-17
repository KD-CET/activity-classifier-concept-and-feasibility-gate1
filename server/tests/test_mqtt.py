"""Tests for the MQTT path, using an in-process stand-in broker (tests/fake_paho).
They check the logic: topics, validation, acks, resends, duplicate rejection, Last Will.
Real networking is tested by hand with Mosquitto (see MQTT_GUIDE.txt).
Run from the dashboard folder:  python -m unittest -v"""
import json
import os
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "fake_paho"))      # use the stand-in broker, not real paho
for name in [m for m in sys.modules if m == "paho" or m.startswith("paho.")]:
    del sys.modules[name]
os.environ["DB_PATH"] = os.path.join(tempfile.mkdtemp(), "test_mqtt.db")

import paho.mqtt.client as fake          # noqa: E402
import db                                # noqa: E402
import mqtt_receiver as R                # noqa: E402
import mqtt_simulator as S               # noqa: E402

T = time.time()


def report(seq, kind="activity_change", activity="walking", device="watch1"):
    return json.dumps({"device_id": device, "seq": seq, "type": kind, "activity": activity,
                       "ts": T, "sensors": "wrist", "retries": 0}).encode()


def start_receiver():
    c = R.make_client("receiver")
    c.on_connect, c.on_message = R.on_connect, R.on_message
    c.connect_async("localhost", 1883)
    c.loop_start()
    return c


class TestHandleReport(unittest.TestCase):
    def setUp(self):
        db.DB_PATH = os.path.join(tempfile.mkdtemp(), "t.db")

    def test_valid_then_duplicate(self):
        topic, reply, _ = R.handle_report("activity/watch1/report", report(1))
        self.assertEqual(topic, "activity/watch1/ack")
        self.assertEqual(reply, {"ack": 1, "duplicate": False})
        _, reply, line = R.handle_report("activity/watch1/report", report(1))
        self.assertEqual(reply, {"ack": 1, "duplicate": True})
        self.assertIn("DUPLICATE", line)
        self.assertEqual(len(db.fetch_events()), 1)

    def test_invalid_json(self):
        _, reply, _ = R.handle_report("activity/watch1/report", b"{not json")
        self.assertIn("nack", reply)

    def test_bad_activity(self):
        _, reply, _ = R.handle_report("activity/watch1/report", report(2, activity="dancing"))
        self.assertEqual(reply["nack"], 2)

    def test_topic_device_mismatch(self):
        _, reply, _ = R.handle_report("activity/watch1/report", report(3, device="watch2"))
        self.assertEqual(reply["nack"], 3)
        self.assertEqual(len(db.fetch_events()), 0)

    def test_status(self):
        line = R.handle_status("activity/watch1/status", b"offline")
        self.assertIn("OFFLINE", line)
        self.assertEqual(R.state["watch_status"]["watch1"][0], "offline")


class TestEndToEnd(unittest.TestCase):
    def setUp(self):
        fake.reset()
        db.DB_PATH = os.path.join(tempfile.mkdtemp(), "e2e.db")
        self.rx = start_receiver()

    def test_clean_session(self):
        st = S.main(["--change-every", "0.3", "--heartbeat", "0.15", "--changes", "3", "--end",
                     "--seed", "1"])
        ev = db.fetch_events()
        types = [e["type"] for e in ev]
        self.assertEqual(types[0], "session_start")
        self.assertEqual(types[-1], "session_end")
        self.assertEqual(types.count("activity_change"), 3)
        self.assertEqual(st["gave_up"], 0)
        self.assertEqual(len({e["seq"] for e in ev}), len(ev))

    def test_lossy_link_no_loss_no_double_count(self):
        st = S.main(["--change-every", "0.3", "--heartbeat", "0.15", "--changes", "4", "--end",
                     "--loss", "0.3", "--ackloss", "0.3", "--ack-timeout", "0.05", "--tries", "20",
                     "--seed", "7"])
        ev = db.fetch_events()
        self.assertGreater(st["resends"], 0)                 # the flaky link really forced resends
        self.assertGreater(st["duplicates"], 0)              # and some resends were duplicates
        self.assertEqual(st["gave_up"], 0)                   # nothing lost
        self.assertEqual(len(ev), st["sent"])                # stored exactly once each
        self.assertEqual(len({e["seq"] for e in ev}), len(ev))

    def test_last_will(self):
        watch = S.make_client("watch1-x")
        watch.will_set("activity/watch1/status", "offline", qos=1, retain=True)
        watch.connect_async("localhost")
        watch.loop_start()
        watch.publish("activity/watch1/status", "online", qos=1, retain=True)
        self.assertEqual(R.state["watch_status"]["watch1"][0], "online")
        watch.drop()                                         # vanishes, like a dead battery
        self.assertEqual(R.state["watch_status"]["watch1"][0], "offline")


class TestSummaryTopic(unittest.TestCase):
    """The receiver publishes the dashboard summary on activity/summary (read by Node-RED)."""

    def test_summary_published_after_report(self):
        import threading
        from core.summary import EventCache
        import receiver
        fake.reset()
        db.DB_PATH = os.path.join(tempfile.mkdtemp(), "sum.db")
        receiver.summary_service.cache = EventCache()
        rx = start_receiver()
        got = []
        dash = fake.Client(client_id="nodered")
        dash.on_message = lambda c, u, m: got.append(json.loads(m.payload))
        dash.connect_async("localhost")
        dash.loop_start()
        dash.subscribe("activity/summary", 1)
        R.SUMMARY_EVERY_S = 0.2
        threading.Thread(target=R.publisher_loop, args=(rx,), daemon=True).start()

        watch = fake.Client(client_id="watch1")
        watch.connect_async("localhost")
        watch.loop_start()
        watch.publish("activity/watch1/status", "online", qos=1, retain=True)
        watch.publish("activity/watch1/report", report(500, "session_start", "walking"), qos=1)
        deadline = time.time() + 3
        while time.time() < deadline and not any(g.get("current_activity") == "walking" for g in got):
            time.sleep(0.05)
        last = [g for g in got if g.get("current_activity") == "walking"]
        self.assertTrue(last, "no summary with the new activity arrived")
        s = last[-1]
        self.assertEqual(s["watch_status"]["status"], "online")
        self.assertTrue(s["mqtt_connected"])
        self.assertEqual(s["timeline"][-1][2], "walking")
        self.assertIn("log", s)


if __name__ == "__main__":
    unittest.main()
