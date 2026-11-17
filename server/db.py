"""db.py - the filing cabinet. SQLite storage for events from the watch."""
import os
import sqlite3
import time

DB_PATH = os.environ.get("DB_PATH", "dashboard.db")

SCHEMA = """
CREATE TABLE IF NOT EXISTS events (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT    NOT NULL,
    seq       INTEGER NOT NULL,      -- watch's message counter (same seq = same message)
    type      TEXT    NOT NULL,      -- session_start | session_end | heartbeat | activity_change
    activity  TEXT,                  -- sedentary | walking | running (or NULL)
    sensors   TEXT,                  -- e.g. "wrist+thigh" or "wrist"
    ts        REAL    NOT NULL,      -- when it happened (watch clock, epoch seconds)
    recv_ts   REAL    NOT NULL,      -- when the receiver got it (computer clock)
    retries   INTEGER NOT NULL DEFAULT 0,
    UNIQUE (device_id, seq)          -- a resent message cannot be stored twice
);
"""


def connect(path=None):
    con = sqlite3.connect(path or DB_PATH, timeout=5)
    con.row_factory = sqlite3.Row
    con.execute("PRAGMA journal_mode=WAL")   # readers do not block the writer
    con.execute(SCHEMA)
    return con


def insert_event(ev, path=None):
    """Store one event. Returns True if new, False if it was a duplicate."""
    con = connect(path)
    try:
        cur = con.execute(
            "INSERT OR IGNORE INTO events "
            "(device_id, seq, type, activity, sensors, ts, recv_ts, retries) "
            "VALUES (?,?,?,?,?,?,?,?)",
            (ev["device_id"], ev["seq"], ev["type"], ev.get("activity"),
             ev.get("sensors"), ev["ts"], ev.get("recv_ts", time.time()),
             ev.get("retries", 0)))
        con.commit()
        return cur.rowcount == 1
    finally:
        con.close()


def fetch_events(since_id=0, path=None):
    con = connect(path)
    try:
        rows = con.execute("SELECT * FROM events WHERE id > ? ORDER BY id",
                           (since_id,)).fetchall()
        return [dict(r) for r in rows]
    finally:
        con.close()
