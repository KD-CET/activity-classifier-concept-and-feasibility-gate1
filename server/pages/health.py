"""Page 2: System health (technical)."""
import time
import datetime as dt

import pandas as pd
import streamlit as st

from client import API_URL, SESSION_TIMEOUT_MIN, fetch_events
from core.metrics import compute

st.set_page_config(page_title="System health", layout="wide")
st.title("System health")


@st.fragment(run_every=2)
def health():
    try:
        events = fetch_events()
    except Exception as e:
        st.error(f"Cannot reach the receiver at {API_URL}  ({e})")
        return
    m = compute(events, time.time(), SESSION_TIMEOUT_MIN * 60)
    c1, c2, c3, c4 = st.columns(4)
    c1.metric("Sensors used", m["sensors"] or "-")
    c2.metric("Last delivery delay",
              "-" if m["last_delay_s"] is None else f"{m['last_delay_s']:.1f} s", "target 10 s",
              delta_color="off")
    c3.metric("Link", "OK" if m["link_ok"] else "No signal",
              None if m["last_heard_s"] is None else f"heard {m['last_heard_s']:.0f} s ago",
              delta_color="off")
    c4.metric("Messages that needed a retry", m["retried_msgs"], f"of {m['total_events']}",
              delta_color="off")
    st.subheader("Delivery log (newest first)")
    rows = [{"time": dt.datetime.fromtimestamp(e["ts"]).strftime("%H:%M:%S"),
             "type": e["type"], "activity": e["activity"], "sensors": e["sensors"],
             "delay (s)": round(max(0, e["recv_ts"] - e["ts"]), 2), "retries": e["retries"]}
            for e in m["log"]]
    st.dataframe(pd.DataFrame(rows), hide_index=True, use_container_width=True)


health()
