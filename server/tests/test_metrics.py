"""Unit tests for core/metrics.py.   Run from the dashboard folder:  python -m unittest -v"""
import unittest
from core.metrics import compute

T = 1_700_000_000.0


def ev(kind, t, activity=None, seq=0, recv=None, retries=0, sensors="wrist+thigh"):
    return {"device_id": "watch1", "seq": seq, "type": kind, "activity": activity, "sensors": sensors,
            "ts": T + t, "recv_ts": T + (t if recv is None else recv), "retries": retries}


class TestMetrics(unittest.TestCase):
    def test_empty(self):
        m = compute([], T, 1800)
        self.assertIsNone(m["shown"])
        self.assertEqual(m["total_events"], 0)
        self.assertFalse(m["link_ok"])

    def test_time_per_activity_open_session(self):
        es = [ev("session_start", 0, "sedentary", 1), ev("activity_change", 60, "walking", 2),
              ev("activity_change", 180, "running", 3)]
        m = compute(es, T + 240, 1800)                     # 60 s more of running, until now
        self.assertTrue(m["in_progress"])
        self.assertEqual(m["current_activity"], "running")
        self.assertAlmostEqual(m["by_activity_min"]["sedentary"], 1.0)
        self.assertAlmostEqual(m["by_activity_min"]["walking"], 2.0)
        self.assertAlmostEqual(m["by_activity_min"]["running"], 1.0)
        self.assertAlmostEqual(m["active_min"], 3.0)

    def test_session_end_and_previous(self):
        es = [ev("session_start", 0, "walking", 1), ev("session_end", 120, None, 2),
              ev("session_start", 1000, "running", 3), ev("heartbeat", 1030, None, 4)]
        m = compute(es, T + 1060, 1800)
        self.assertEqual(len(m["sessions"]), 2)
        self.assertAlmostEqual(m["prev_active_min"], 2.0)   # 120 s walking
        self.assertAlmostEqual(m["active_min"], 1.0)        # 60 s running so far
        self.assertEqual(m["per_session"][0][1], 2.0)

    def test_timeout_closes_session(self):
        es = [ev("session_start", 0, "walking", 1), ev("heartbeat", 60, None, 2)]
        m = compute(es, T + 5000, 1800)                    # silent for > 30 min
        self.assertFalse(m["in_progress"])
        self.assertAlmostEqual(m["active_min"], 1.0)        # counted only until the last event
        self.assertFalse(m["link_ok"])

    def test_silence_starts_new_session(self):
        es = [ev("session_start", 0, "walking", 1), ev("activity_change", 4000, "running", 2)]
        m = compute(es, T + 4030, 1800)
        self.assertEqual(len(m["sessions"]), 2)

    def test_health_fields(self):
        es = [ev("session_start", 0, "sedentary", 1, recv=0.4),
              ev("activity_change", 10, "walking", 2, recv=13.0, retries=2, sensors="wrist")]
        m = compute(es, T + 20, 1800)
        self.assertAlmostEqual(m["last_delay_s"], 3.0)
        self.assertEqual(m["retried_msgs"], 1)
        self.assertEqual(m["sensors"], "wrist")
        self.assertTrue(m["link_ok"])
        self.assertEqual(m["log"][0]["seq"], 2)            # newest first


if __name__ == "__main__":
    unittest.main()
