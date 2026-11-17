"""terminal_dashboard.py - the dashboard in a terminal window (no browser needed).

Reads the same summary as the web and Node-RED dashboards from the receiver (receiver.py or
mqtt_receiver.py, port 8000) and redraws every 3 s. Standard library only.

    python3 terminal_dashboard.py                    on the Pi itself
    python3 terminal_dashboard.py --url http://192.168.2.126:8000   from another computer
    python3 terminal_dashboard.py --once             draw once and exit
Ctrl+C to quit.
"""
import argparse
import datetime as dt
import json
import os
import shutil
import sys
import time
import urllib.request

NAMES = {"sedentary": "Sedentary", "walking": "Walking", "running": "Running"}
ORDER = ["sedentary", "walking", "running"]
COL = {"sedentary": 105, "walking": 36, "running": 208}      # 256-colour codes: indigo, green, orange
RESET, BOLD, DIM = "\033[0m", "\033[1m", "\033[2m"


def fg(c): return f"\033[38;5;{c}m"
def bg(c): return f"\033[48;5;{c}m"
def hhmm(ts): return dt.datetime.fromtimestamp(ts).strftime("%H:%M")
def hhmmss(ts): return dt.datetime.fromtimestamp(ts).strftime("%H:%M:%S")
def mins(m): return f"{m:.1f}" if m < 10 else f"{m:.0f}"


def duration(s):
    s = max(0, s)
    if s < 60:
        return f"{s:.0f} s"
    m = int(s // 60)
    return f"{m} min" if m < 60 else f"{m // 60} h {m % 60} min"


# big letters for the current activity (5 rows high)
FONT = {
    "S": ["█████", "█    ", "█████", "    █", "█████"], "E": ["█████", "█    ", "████ ", "█    ", "█████"],
    "D": ["████ ", "█   █", "█   █", "█   █", "████ "], "N": ["█   █", "██  █", "█ █ █", "█  ██", "█   █"],
    "T": ["█████", "  █  ", "  █  ", "  █  ", "  █  "], "A": [" ███ ", "█   █", "█████", "█   █", "█   █"],
    "R": ["████ ", "█   █", "████ ", "█  █ ", "█   █"], "Y": ["█   █", " █ █ ", "  █  ", "  █  ", "  █  "],
    "W": ["█   █", "█   █", "█ █ █", "██ ██", "█   █"], "L": ["█    ", "█    ", "█    ", "█    ", "█████"],
    "K": ["█   █", "█  █ ", "███  ", "█  █ ", "█   █"], "I": ["█████", "  █  ", "  █  ", "  █  ", "█████"],
    "G": [" ████", "█    ", "█  ██", "█   █", " ████"], "U": ["█   █", "█   █", "█   █", "█   █", " ███ "],
}


def big(word, colour, width):
    word = word.upper()
    if len(word) * 6 > width:                       # too narrow: plain bold text instead
        return [fg(colour) + BOLD + word + RESET]
    return [fg(colour) + " ".join(FONT.get(ch, ["     "] * 5)[r] for ch in word) + RESET for r in range(5)]


def draw(s, width):
    out = []
    now = s.get("server_time", time.time())
    sess = s.get("session")
    ws = s.get("watch_status")
    if ws and ws.get("status") == "offline":
        link = fg(160) + f"● Watch offline since {hhmm(ws['since'])} (Last Will)" + RESET
    elif sess and s.get("link_ok"):
        link = fg(36) + "● Watch connected" + RESET
    elif sess:
        link = fg(178) + f"● No signal from the watch for {duration(s.get('last_heard_s') or 0)}" + RESET
    else:
        link = DIM + "● No data from the watch yet" + RESET
    out.append(BOLD + "Activity Tracker" + RESET + "   " + link)
    out.append("")
    if not sess:
        out += big("waiting", 245, width)
        out.append("")
        out.append("Turn on the watch. Its first activity appears here a few seconds after its link turns green.")
        return out

    act = s.get("current_activity")
    out += big(NAMES.get(act, "-"), COL.get(act, 250), width)
    out.append("")
    if s.get("in_progress") and s.get("current_since"):
        out.append(f"For {BOLD}{duration(now - s['current_since'])}{RESET}, in a session that started at {hhmm(sess['start'])}.")
    elif s.get("in_progress"):
        out.append(f"Session started at {hhmm(sess['start'])}.")
    else:
        out.append(f"{fg(178)}No session in progress.{RESET} Last session ended at {hhmm(sess['end'])}.")

    # session ribbon: the whole session, each stretch as wide as it lasted
    w = max(10, width - 2)
    total = max(1.0, sess["end"] - sess["start"])
    cells = []
    for a, b, x in s.get("timeline", []):
        cells.append((max(1, round(w * (b - a) / total)), x))
    used = sum(n for n, _ in cells)
    if cells and used != w:                          # make it exactly fit
        n, x = cells[-1]
        cells[-1] = (max(1, n + (w - used)), x)
    out.append("")
    out.append("".join(bg(COL[x]) + " " * n for n, x in cells) + RESET)
    end = "now" if s.get("in_progress") else hhmm(sess["end"])
    out.append(DIM + hhmm(sess["start"]).ljust(w - len(end)) + end + RESET)
    out.append("")

    prev = s.get("prev_active_min")
    if prev is None:
        cmp_txt = "first session"
    else:
        d = s["active_min"] - prev
        cmp_txt = (fg(36) if d > 0.05 else fg(160) if d < -0.05 else "") + f"{d:+.1f} min vs last session" + RESET
    out.append(f"Active minutes  {BOLD}{mins(s['active_min'])} min{RESET}  ({cmp_txt})")
    out.append(f"Session length  {BOLD}{mins(sess['length_min'])} min{RESET}")
    split = s.get("by_activity_min", {})
    out.append("Time per activity  " + "   ".join(fg(COL[k]) + "■ " + RESET + f"{NAMES[k]} {mins(split.get(k, 0))} min" for k in ORDER))
    out.append("")

    out.append(BOLD + "Recent changes" + RESET)
    for c in s.get("recent_changes", [])[:5]:
        out.append(f"  {DIM}{hhmm(c['ts'])}{RESET}  {fg(COL[c['activity']])}●{RESET} {NAMES[c['activity']]}")
    out.append("")
    delay = s.get("last_delay_s")
    delay_txt = "-" if delay is None else (fg(160) if delay > 10 else "") + f"{delay:.1f} s" + RESET
    out.append(BOLD + "Link" + RESET + f"  sensors {s.get('sensors') or '-'}  |  last delay {delay_txt} (target 10 s)"
               f"  |  retries {s.get('retried_msgs', 0)} of {s.get('total_events', 0)}")
    return out


def main():
    p = argparse.ArgumentParser(description="Activity Tracker dashboard in the terminal")
    p.add_argument("--url", default=os.environ.get("API_URL", "http://localhost:8000"))
    p.add_argument("--every", type=float, default=3.0, help="seconds between refreshes")
    p.add_argument("--once", action="store_true", help="draw once and exit")
    a = p.parse_args()
    if os.name == "nt":
        os.system("")                                 # turn on colours in the Windows terminal
    try:
        while True:
            width = min(110, shutil.get_terminal_size((90, 30)).columns)
            try:
                with urllib.request.urlopen(a.url.rstrip("/") + "/api/summary", timeout=4) as r:
                    lines = draw(json.load(r), width)
            except Exception as e:
                lines = [BOLD + "Activity Tracker" + RESET, "",
                         fg(160) + f"Can't reach the receiver at {a.url} ({e})." + RESET,
                         "Is mqtt_receiver.py (or receiver.py) running? Trying again..."]
            screen = "\n".join(lines) + "\n\n" + DIM + f"Updated {time.strftime('%H:%M:%S')}  |  Ctrl+C to quit" + RESET
            if a.once:
                print(screen)
                return
            sys.stdout.write("\033[H\033[2J" + screen + "\n")
            sys.stdout.flush()
            time.sleep(a.every)
    except KeyboardInterrupt:
        print(RESET)


if __name__ == "__main__":
    main()
