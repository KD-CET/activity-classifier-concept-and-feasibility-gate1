"""core/summary.py - what the web dashboard needs, computed ONCE on the Pi and sent as a small JSON.

Why: the old Streamlit pages downloaded every stored event every 2 s and worked everything out
in a heavy framework. Here the receiver keeps recent events in memory (EventCache), adds only
the new ones from the database, and builds one small summary that every open browser shares.
Standard library only, so it runs on a Pi Zero 2 W with nothing installed.
"""
import threading
import time

import db
from core.metrics import compute, ACTIVITIES

HISTORY_DAYS = 7           # events older than this stay in the database but are not loaded (Pi memory)
LOG_ROWS = 50              # rows in the delivery log table
CHANGE_ROWS = 10           # rows in "recent changes"


class EventCache:
    """Recent events in memory. Each refresh reads only rows added since the last one."""

    def __init__(self, history_days=HISTORY_DAYS):
        self.history_s = history_days * 86400
        self.events = []
        self.last_id = 0
        self.lock = threading.Lock()

    def refresh(self, now=None):
        now = time.time() if now is None else now
        with self.lock:
            new = db.fetch_events(since_id=self.last_id)
            if new:
                self.events.extend(new)
                self.last_id = new[-1]["id"]
            cutoff = now - self.history_s
            if self.events and self.events[0]["ts"] < cutoff:
                self.events = [e for e in self.events if e["ts"] >= cutoff]
            return list(self.events)


def _session_dict(s):
    return {"start": s.start, "end": s.end, "open": s.open,
            "length_min": round((s.end - s.start) / 60.0, 1),
            "active_min": round(s.active_min, 2)}


def _timeline(s, max_segments=400):
    """The shown session as [start, end, activity] stretches (for the session ribbon)."""
    segs = []
    for e in s.events:
        if e["type"] in ("session_start", "activity_change") and e.get("activity") in ACTIVITIES:
            if segs and segs[-1][2] == e["activity"]:
                continue                                  # same activity again: keep going
            if segs:
                segs[-1][1] = e["ts"]
            segs.append([e["ts"], s.end, e["activity"]])
    if segs:
        segs[-1][1] = s.end
    if len(segs) > max_segments:                          # very long session: keep the latest part
        segs = segs[-max_segments:]
    return [[round(a, 1), round(b, 1), act] for a, b, act in segs]


def build_summary(events, now, timeout_s):
    m = compute(events, now, timeout_s)
    shown = m["shown"]
    timeline = _timeline(shown) if shown else []
    return {
        "timeline": timeline,
        "current_since": timeline[-1][0] if timeline else None,
        "server_time": now,
        "history_days": HISTORY_DAYS,
        "session": _session_dict(shown) if shown else None,
        "in_progress": m["in_progress"],
        "current_activity": m["current_activity"],
        "active_min": round(m["active_min"], 2),
        "prev_active_min": None if m["prev_active_min"] is None else round(m["prev_active_min"], 2),
        "by_activity_min": {a: m["by_activity_min"].get(a, 0.0) for a in ACTIVITIES} if shown else {},
        "sessions": [_session_dict(s) for s in m["sessions"][-10:]],
        "recent_changes": [{"ts": e["ts"], "activity": e["activity"], "sensors": e.get("sensors")}
                           for e in m["recent_changes"][:CHANGE_ROWS]],
        "sensors": m["sensors"],
        "last_delay_s": None if m["last_delay_s"] is None else round(m["last_delay_s"], 2),
        "last_heard_s": None if m["last_heard_s"] is None else round(m["last_heard_s"], 1),
        "link_ok": m["link_ok"],
        "retried_msgs": m["retried_msgs"],
        "total_events": m["total_events"],
        "log": [{"ts": e["ts"], "type": e["type"], "activity": e.get("activity"),
                 "sensors": e.get("sensors"), "retries": e.get("retries", 0),
                 "delay_s": round(max(0.0, e["recv_ts"] - e["ts"]), 2)}
                for e in m["log"][:LOG_ROWS]],
    }


class SummaryService:
    """Shares one summary between all viewers for `max_age` seconds, so 3 open browsers
    cost the Pi the same as one."""

    def __init__(self, timeout_s, max_age=1.5):
        self.cache = EventCache()
        self.timeout_s = timeout_s
        self.max_age = max_age
        self._value, self._made, self._lock = None, 0.0, threading.Lock()

    def get(self, force=False):
        with self._lock:
            now = time.time()
            if force or self._value is None or now - self._made > self.max_age:
                self._value = build_summary(self.cache.refresh(now), now, self.timeout_s)
                self._made = now
            return self._value
