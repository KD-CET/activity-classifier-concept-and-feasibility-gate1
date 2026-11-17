"""app.py - Page 1: Overview (for stakeholders).   Run: streamlit run app.py"""
import time
import datetime as dt

import pandas as pd
import streamlit as st

from client import API_URL, SESSION_TIMEOUT_MIN, fetch_events
from core.metrics import compute

st.set_page_config(page_title="Activity Tracker", layout="wide")
st.title("Activity Tracker")


def hhmm(ts):
    return dt.datetime.fromtimestamp(ts).strftime("%H:%M:%S")


@st.fragment(run_every=2)          # re-runs only this block every 2 s (no full-page flicker)
def overview():
    try:
        events = fetch_events()
    except Exception as e:
        st.error(f"Cannot reach the receiver at {API_URL}  ({e})")
        return
    m = compute(events, time.time(), SESSION_TIMEOUT_MIN * 60)
    s = m["shown"]
    if s is None:
        st.info("Waiting for the first session from the watch...")
        return
    if not m["in_progress"]:
        st.warning("No session in progress - showing the last session.")

    c1, c2, c3 = st.columns(3)
    c1.metric("Right now", (m["current_activity"] or "-").capitalize())
    delta = None
    if m["prev_active_min"] is not None:
        delta = f"{m['active_min'] - m['prev_active_min']:+.1f} min vs last session"
    c2.metric("Active minutes (this session)", f"{m['active_min']:.1f}", delta)
    c3.metric("Session length", f"{(s.end - s.start) / 60:.1f} min")

    left, right = st.columns(2)
    with left:
        st.subheader("Time by activity (minutes)")
        st.bar_chart(pd.Series(m["by_activity_min"]).rename("minutes"))
    with right:
        st.subheader("Active minutes per session")
        df = pd.DataFrame(m["per_session"], columns=["session", "active min", "open"])
        st.bar_chart(df.set_index("session")["active min"])

    st.subheader("Recent changes")
    rows = [{"time": hhmm(e["ts"]), "activity": e["activity"], "sensors": e["sensors"] or ""}
            for e in m["recent_changes"]]
    st.dataframe(pd.DataFrame(rows), hide_index=True, use_container_width=True)
    st.caption(f"Updated {hhmm(time.time())}")


overview()
