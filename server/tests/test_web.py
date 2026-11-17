"""Tests for the lightweight web dashboard: summary contents and the receiver's web routes.
Run from the dashboard folder:  python -m unittest -v"""
import json
import os
import tempfile
import threading
import time
import unittest
import urllib.request
from http.server import ThreadingHTTPServer

import db
from core.summary import build_summary, EventCache

T = time.time()


def ev(i, kind, t, activity=None, recv=None, retries=0):
    return {"id": i, "device_id": "watch1", "seq": i, "type": kind, "activity": activity, "sensors": "wrist",
            "ts": T + t, "recv_ts": T + (t if recv is None else recv), "retries": retries}


class TestSummary(unittest.TestCase):
    def test_timeline_and_figures(self):
        es = [ev(1, "session_start", -600, "sedentary"), ev(2, "activity_change", -300, "walking"),
              ev(3, "activity_change", -240, "walking"),            # same activity again: one stretch
              ev(4, "activity_change", -120, "running"), ev(5, "heartbeat", -10)]
        s = build_summary(es, T, 1800)
        self.assertEqual([seg[2] for seg in s["timeline"]], ["sedentary", "walking", "running"])
        self.assertAlmostEqual(s["timeline"][-1][1] - s["timeline"][0][0], 600, delta=0.2)
        self.assertEqual(s["current_activity"], "running")
        self.assertAlmostEqual(s["active_min"], 5.0, delta=0.01)    # 3 min walking + 2 min running
        self.assertTrue(s["in_progress"] and s["link_ok"])
        self.assertEqual(s["current_since"], round(T - 120, 1))
        json.dumps(s)                                                # must be JSON-safe

    def test_empty(self):
        s = build_summary([], T, 1800)
        self.assertIsNone(s["session"])
        self.assertEqual(s["timeline"], [])


class TestReceiverWeb(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        db.DB_PATH = os.path.join(tempfile.mkdtemp(), "web.db")
        import receiver
        receiver.summary_service.cache = EventCache()
        receiver.summary_service.max_age = 0
        cls.srv = ThreadingHTTPServer(("127.0.0.1", 0), receiver.Handler)
        cls.base = f"http://127.0.0.1:{cls.srv.server_address[1]}"
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()

    def post(self, m):
        req = urllib.request.Request(self.base + "/report", json.dumps(m).encode(), {"Content-Type": "application/json"})
        return json.load(urllib.request.urlopen(req))

    def test_page_and_summary(self):
        page = urllib.request.urlopen(self.base + "/").read().decode()
        self.assertIn("<title>Activity Tracker</title>", page)
        self.assertEqual(self.post({"device_id": "watch1", "seq": 1, "type": "session_start",
                                    "activity": "walking", "ts": time.time() - 60})["ack"], 1)
        self.assertTrue(self.post({"device_id": "watch1", "seq": 1, "type": "session_start",
                                   "activity": "walking", "ts": time.time() - 60})["duplicate"])
        s = json.load(urllib.request.urlopen(self.base + "/api/summary"))
        self.assertEqual(s["current_activity"], "walking")
        self.assertEqual(s["total_events"], 1)
        self.post({"device_id": "watch1", "seq": 2, "type": "activity_change", "activity": "running", "ts": time.time()})
        s = json.load(urllib.request.urlopen(self.base + "/api/summary"))
        self.assertEqual(s["current_activity"], "running")            # cache picked up the new row
        self.assertEqual(len(s["timeline"]), 2)

    def test_gzip(self):
        req = urllib.request.Request(self.base + "/", headers={"Accept-Encoding": "gzip"})
        r = urllib.request.urlopen(req)
        self.assertEqual(r.headers.get("Content-Encoding"), "gzip")


if __name__ == "__main__":
    unittest.main()
