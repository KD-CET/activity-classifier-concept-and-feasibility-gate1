"""receiver.py - front door AND web dashboard. Standard library only (runs on a Pi Zero 2 W
with nothing installed, 32-bit or 64-bit OS).

POST /report       watch sends one JSON message -> validated, stored, ACKed
GET  /             the dashboard (web/index.html): open http://<pi-address>:8000 in a browser
GET  /api/summary  everything the dashboard shows, computed once on the Pi (small JSON)
GET  /events       all stored events as JSON (heavy; for debugging)
GET  /health       {"ok": true}

Run:  python receiver.py          (PORT=8000 by default)
"""
import gzip
import json
import os
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import db
from core.summary import SummaryService

PORT = int(os.environ.get("PORT", 8000))
SESSION_TIMEOUT_MIN = float(os.environ.get("SESSION_TIMEOUT_MIN", 30))
WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")
summary_service = SummaryService(timeout_s=SESSION_TIMEOUT_MIN * 60)
TYPES = {"session_start", "session_end", "heartbeat", "activity_change"}
ACTIVITIES = {"sedentary", "walking", "running"}


def validate(m):
    """Return (clean_event, None) or (None, error_text)."""
    if not isinstance(m, dict):
        return None, "body must be a JSON object"
    for k in ("device_id", "seq", "type", "ts"):
        if k not in m:
            return None, f"missing field: {k}"
    if m["type"] not in TYPES:
        return None, f"type must be one of {sorted(TYPES)}"
    if not isinstance(m["seq"], int) or not isinstance(m["ts"], (int, float)):
        return None, "seq must be an integer and ts a number"
    if m["type"] == "activity_change" and m.get("activity") not in ACTIVITIES:
        return None, f"activity must be one of {sorted(ACTIVITIES)}"
    if m.get("activity") is not None and m["activity"] not in ACTIVITIES:
        return None, "unknown activity"
    return {
        "device_id": str(m["device_id"])[:32], "seq": m["seq"], "type": m["type"],
        "activity": m.get("activity"), "sensors": str(m.get("sensors", ""))[:32] or None,
        "ts": float(m["ts"]), "recv_ts": time.time(),
        "retries": int(m.get("retries", 0)),
    }, None


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, obj):
        self._send_bytes(code, json.dumps(obj, separators=(",", ":")).encode(), "application/json")

    def _send_bytes(self, code, body, ctype, cache="no-store"):
        # compress bigger replies when the browser accepts it (less Wi-Fi time on the Pi)
        zipped = len(body) > 1400 and "gzip" in self.headers.get("Accept-Encoding", "")
        if zipped:
            body = gzip.compress(body, compresslevel=5)
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", cache)
        if zipped:
            self.send_header("Content-Encoding", "gzip")
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/report":
            return self._send(404, {"error": "not found"})
        n = int(self.headers.get("Content-Length", 0))
        if n <= 0 or n > 4096:
            return self._send(400, {"error": "bad body size"})
        try:
            msg = json.loads(self.rfile.read(n))
        except ValueError:
            return self._send(400, {"error": "invalid JSON"})
        ev, err = validate(msg)
        if err:
            return self._send(400, {"error": err})
        new = db.insert_event(ev)
        # ACK even for duplicates, otherwise the watch would keep resending.
        self._send(200, {"ack": ev["seq"], "duplicate": not new})
        print(f"{time.strftime('%H:%M:%S')} {ev['type']:<15} {ev['activity'] or '-':<9} "
              f"seq={ev['seq']} retries={ev['retries']}{' DUPLICATE' if not new else ''}")

    def do_GET(self):
        path = self.path.split("?")[0]
        if path in ("/", "/index.html"):
            try:
                with open(os.path.join(WEB_DIR, "index.html"), "rb") as f:
                    return self._send_bytes(200, f.read(), "text/html; charset=utf-8", "no-cache")
            except OSError:
                return self._send(500, {"error": "web/index.html is missing"})
        if path == "/api/summary":
            return self._send(200, summary_service.get())
        if path == "/health":
            return self._send(200, {"ok": True})
        if path.startswith("/events"):
            return self._send(200, db.fetch_events())
        self._send(404, {"error": "not found"})

    def log_message(self, *a):       # silence the default per-request log
        pass


if __name__ == "__main__":
    db.connect().close()
    print(f"receiver listening on 0.0.0.0:{PORT}  (db: {db.DB_PATH})")
    print(f"dashboard: open http://<this computer's address>:{PORT} in a browser")
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
