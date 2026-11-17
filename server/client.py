"""client.py - how the dashboard gets events: HTTP from the receiver.
Because of this, Streamlit can run on a different machine than the receiver."""
import json
import os
import urllib.request

API_URL = os.environ.get("API_URL", "http://localhost:8000")
SESSION_TIMEOUT_MIN = float(os.environ.get("SESSION_TIMEOUT_MIN", 30))


def fetch_events(base=API_URL):
    with urllib.request.urlopen(base + "/events", timeout=3) as r:
        return json.load(r)
