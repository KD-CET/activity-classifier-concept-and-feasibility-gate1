"""build_flow.py - builds activity_dashboard_flow.json (import it into Node-RED) from src/.
Run after editing anything in src/:   python build_flow.py"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "src")
read = lambda name: open(os.path.join(SRC, name), encoding="utf-8").read()

helpers, css = read("helpers.js"), read("style.css")
TEMPLATE = ('<template>\n  <div class="at" v-html="msg?.payload || \'\'"></div>\n</template>\n'
            "<style>\n" + css + "</style>\n")
BREAKPOINTS = [{"name": "Default", "px": "0", "cols": "3"}, {"name": "Tablet", "px": "576", "cols": "6"},
               {"name": "Small Desktop", "px": "768", "cols": "9"}, {"name": "Desktop", "px": "1024", "cols": "12"}]

flow = [
    {"id": "at_tab", "type": "tab", "label": "Activity Tracker", "disabled": False,
     "info": "Shows the watch's activity. mqtt_receiver.py (on the same Pi) publishes everything the "
             "dashboard needs on the MQTT topic activity/summary; this flow only displays it.\n"
             "Dashboard: http://<pi-address>:1880/dashboard"},
    {"id": "at_note", "type": "comment", "z": "at_tab", "name": "activity/summary  ->  4 sections (Overview + System health)",
     "info": "The function nodes turn the summary into HTML; each ui-template shows one section.\n"
             "Edit the source in server/nodered/src and rebuild with build_flow.py.",
     "x": 290, "y": 60, "wires": []},
    # ---- MQTT ----
    {"id": "at_broker", "type": "mqtt-broker", "name": "Mosquitto on this Pi", "broker": "localhost", "port": "1883",
     "clientid": "", "autoConnect": True, "usetls": False, "protocolVersion": "4", "keepalive": "60",
     "cleansession": True, "autoUnsubscribe": True, "birthTopic": "", "birthQos": "0", "birthPayload": "",
     "birthMsg": {}, "closeTopic": "", "closeQos": "0", "closePayload": "", "closeMsg": {}, "willTopic": "",
     "willQos": "0", "willPayload": "", "willMsg": {}, "userProps": "", "sessionExpiry": ""},
    {"id": "at_in", "type": "mqtt in", "z": "at_tab", "name": "activity/summary", "topic": "activity/summary",
     "qos": "0", "datatype": "json", "broker": "at_broker", "nl": False, "rap": True, "rh": 0, "inputs": 0,
     "x": 130, "y": 180, "wires": [["at_f_now", "at_f_sessions", "at_f_changes", "at_f_health"]]},
    # ---- Dashboard 2.0 config ----
    {"id": "at_ui", "type": "ui-base", "name": "Activity Tracker", "path": "/dashboard", "includeClientData": True,
     "acceptsClientConfig": ["ui-notification", "ui-control"], "showPathInSidebar": False, "showPageTitle": True,
     "navigationStyle": "default", "titleBarStyle": "default"},
    {"id": "at_theme", "type": "ui-theme", "name": "Activity Tracker",
     "colors": {"surface": "#1c2433", "primary": "#6d78e8", "bgPage": "#eef1f6", "groupBg": "#ffffff", "groupOutline": "#d6dce6"},
     "sizes": {"density": "default", "pagePadding": "12px", "groupGap": "12px", "groupBorderRadius": "14px", "widgetGap": "12px"}},
    {"id": "at_p_over", "type": "ui-page", "name": "Overview", "ui": "at_ui", "path": "/overview", "icon": "run",
     "layout": "grid", "theme": "at_theme", "breakpoints": BREAKPOINTS, "order": 1, "className": "",
     "visible": "true", "disabled": "false"},
    {"id": "at_p_health", "type": "ui-page", "name": "System health", "ui": "at_ui", "path": "/health", "icon": "heart-pulse",
     "layout": "grid", "theme": "at_theme", "breakpoints": BREAKPOINTS, "order": 2, "className": "",
     "visible": "true", "disabled": "false"},
]
sections = [  # id, title, page, group width, order, source file
    ("now", "Right now", "at_p_over", "12", 1, "render_now.js"),
    ("sessions", "Active minutes per session", "at_p_over", "6", 2, "render_sessions.js"),
    ("changes", "Recent changes", "at_p_over", "6", 3, "render_changes.js"),
    ("health", "Link and delivery", "at_p_health", "12", 1, "render_health.js"),
]
for i, (key, title, page, width, order, src) in enumerate(sections):
    y = 120 + i * 60
    flow.append({"id": f"at_g_{key}", "type": "ui-group", "name": title, "page": page, "width": width, "height": "1",
                 "order": order, "showTitle": True, "className": "", "visible": "true", "disabled": "false",
                 "groupType": "default"})
    flow.append({"id": f"at_f_{key}", "type": "function", "z": "at_tab", "name": title,
                 "func": helpers + "\n" + read(src), "outputs": 1, "timeout": 0, "noerr": 0,
                 "initialize": "", "finalize": "", "libs": [], "x": 400, "y": y, "wires": [[f"at_t_{key}"]]})
    flow.append({"id": f"at_t_{key}", "type": "ui-template", "z": "at_tab", "group": f"at_g_{key}", "page": "", "ui": "",
                 "name": title, "order": 1, "width": 0, "height": 0, "head": "", "format": TEMPLATE,
                 "storeOutMessages": True, "passthru": False, "resendOnRefresh": True, "templateScope": "local",
                 "className": "", "x": 650, "y": y, "wires": [[]]})

out = os.path.join(HERE, "activity_dashboard_flow.json")
with open(out, "w", encoding="utf-8") as f:
    json.dump(flow, f, indent=2)
print(f"wrote {out} ({len(flow)} nodes)")
