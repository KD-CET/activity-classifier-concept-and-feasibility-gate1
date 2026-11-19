"""core/metrics.py - turns the raw event list into what the two dashboard pages show.

REBUILT on Nov 11, 2026: the original file was lost with a deleted chat. It returns exactly the
fields the dashboard summary uses. Pure Python, so it can be unit tested.

Sessions
  * A session starts at a session_start event, or at the first event after a silence longer
    than the session timeout.
  * It ends at session_end, or when the watch has been silent for longer than the timeout.
  * It is "open" (in progress) if it has not ended and the watch was heard within the timeout.
Time per activity
  * The activity carried by session_start / activity_change lasts until the next change.
  * The last activity of an open session runs until now; of a closed one, until its last event.
  * Active minutes = walking + running.
Delivery delay = receiver clock (recv_ts) minus watch clock (ts). This is only meaningful when
the watch's clock is synchronised (e.g. NTP over Wi-Fi), otherwise the delay includes clock offset.
"""
import datetime as dt
from dataclasses import dataclass, field

ACTIVITIES = ("sedentary", "walking", "running")
ACTIVE = ("walking", "running")
LINK_TIMEOUT_S = 30          # no message for this long -> "No signal" (heartbeats come every 10 s)
MARKERS = ("session_start", "activity_change")


@dataclass
class Session:
    index: int
    start: float
    end: float = 0.0
    open: bool = False
    events: list = field(default_factory=list)
    seconds: dict = field(default_factory=lambda: {a: 0.0 for a in ACTIVITIES})
    current: str = None

    @property
    def active_min(self):
        return sum(self.seconds[a] for a in ACTIVE) / 60.0


def _split_sessions(events, now, timeout_s):
    sessions, cur = [], None
    for e in sorted(events, key=lambda e: (e["ts"], e.get("seq", 0))):
        gap = cur is not None and cur.events and e["ts"] - cur.events[-1]["ts"] > timeout_s
        if cur is None or e["type"] == "session_start" or gap:
            if cur is not None:
                sessions.append(cur)
            cur = Session(index=len(sessions) + 1, start=e["ts"])
        cur.events.append(e)
        if e["type"] == "session_end":
            sessions.append(cur)
            cur = None
    if cur is not None:
        sessions.append(cur)

    for s in sessions:
        last_ts, current = s.start, None
        for e in s.events:
            if current is not None:
                s.seconds[current] += max(0.0, e["ts"] - last_ts)
            last_ts = e["ts"]
            if e["type"] in MARKERS and e.get("activity") in ACTIVITIES:
                current = e["activity"]
        ended = s.events[-1]["type"] == "session_end"
        s.open = (not ended) and (now - last_ts) <= timeout_s
        s.end = now if s.open else last_ts
        if current is not None:
            s.seconds[current] += max(0.0, s.end - last_ts)
        s.current = current
    return sessions


def compute(events, now, timeout_s):
    events = list(events or [])
    sessions = _split_sessions(events, now, timeout_s)
    shown = sessions[-1] if sessions else None
    prev = sessions[-2] if len(sessions) >= 2 else None

    by_recv = sorted(events, key=lambda e: e.get("recv_ts", e["ts"]), reverse=True)
    last = by_recv[0] if by_recv else None
    last_heard_s = (now - last["recv_ts"]) if last else None
    sensors = next((e["sensors"] for e in by_recv if e.get("sensors")), None)

    recent = []
    if shown:
        recent = [e for e in reversed(shown.events) if e["type"] in MARKERS and e.get("activity")][:10]

    per_session = [
        (f"#{s.index} {dt.datetime.fromtimestamp(s.start).strftime('%m-%d %H:%M')}",
         round(s.active_min, 1), s.open)
        for s in sessions[-10:]
    ]

    return {
        # Overview page
        "shown": shown,
        "in_progress": bool(shown and shown.open),
        "current_activity": shown.current if shown else None,
        "active_min": shown.active_min if shown else 0.0,
        "prev_active_min": prev.active_min if prev else None,
        "by_activity_min": {a: round(shown.seconds[a] / 60.0, 2) for a in ACTIVITIES} if shown else {},
        "per_session": per_session,
        "recent_changes": recent,
        "sessions": sessions,
        # System health page
        "sensors": sensors,
        "last_delay_s": max(0.0, last["recv_ts"] - last["ts"]) if last else None,
        "last_heard_s": last_heard_s,
        "link_ok": last_heard_s is not None and last_heard_s <= LINK_TIMEOUT_S,
        "retried_msgs": sum(1 for e in events if e.get("retries", 0) > 0),
        "total_events": len(events),
        "log": by_recv[:100],
    }
