#!/bin/bash
# setup_pi_mqtt.sh - the MQTT version on a Raspberry Pi (Zero 2 W, 64-bit Raspberry Pi OS Lite):
#   Mosquitto (broker) + mqtt_receiver.py (acks, storage) + Node-RED with Dashboard 2.0.
#
#   cd ~/server
#   bash pi/setup_pi_mqtt.sh
#
# Needs internet on the Pi (it downloads Mosquitto and Node-RED). Takes 20-40 minutes on a
# Pi Zero 2 W, mostly Node-RED. Safe to run again: finished steps are skipped.
# Time zone for the dashboard's clock times: TZ_NAME=America/Toronto by default, e.g.
#   TZ_NAME=America/Vancouver bash pi/setup_pi_mqtt.sh
set -e
DIR="$(cd "$(dirname "$0")/.." && pwd)"
USER_NAME="$(whoami)"
TZ_NAME="${TZ_NAME:-America/Toronto}"
case "$DIR" in
  *" "*) echo "!! The folder path has a space in it: $DIR"
         echo "!! Move the folder somewhere without spaces (e.g. ~/server) and run again."; exit 1;;
esac
[ -f "$DIR/nodered/activity_dashboard_flow.json" ] || { echo "!! nodered/activity_dashboard_flow.json missing - copy the whole folder."; exit 1; }
[ "$(uname -m)" = "aarch64" ] || echo "(note: this is a 32-bit OS; newer Node.js has no 32-bit builds. If Node-RED fails, use the 64-bit OS.)"

echo "== 1/5 Time zone ($TZ_NAME) and packages"
sudo timedatectl set-timezone "$TZ_NAME" || true
sudo apt update
sudo apt install -y mosquitto mosquitto-clients python3-paho-mqtt curl build-essential git

echo "== 2/5 Mosquitto (broker)"
sudo cp "$DIR/mosquitto/activity.conf" /etc/mosquitto/conf.d/activity.conf
sudo systemctl enable mosquitto
sudo systemctl restart mosquitto

echo "== 3/5 MQTT receiver (acks the watch, stores reports, publishes activity/summary)"
# the HTTP-only receiver uses the same port 8000; mqtt_receiver.py serves HTTP too, so stop the old one
if systemctl list-unit-files 2>/dev/null | grep -q '^activity-receiver.service'; then
  sudo systemctl disable --now activity-receiver.service || true
fi
sudo tee /etc/systemd/system/activity-mqtt-receiver.service >/dev/null <<UNIT
[Unit]
Description=Activity Tracker MQTT receiver (also HTTP + web dashboard on port 8000)
After=network-online.target mosquitto.service
Wants=network-online.target mosquitto.service

[Service]
User=$USER_NAME
WorkingDirectory=$DIR
Environment=DB_PATH=$DIR/dashboard.db
Environment=PYTHONUNBUFFERED=1
ExecStart=/usr/bin/python3 $DIR/mqtt_receiver.py
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
UNIT
sudo systemctl daemon-reload
sudo systemctl enable activity-mqtt-receiver.service
sudo systemctl restart activity-mqtt-receiver.service

echo "== 4/5 Node-RED"
if ! command -v node-red >/dev/null; then
  echo "   Installing Node-RED with its official installer. Answer 'y' to its questions."
  bash <(curl -sL https://github.com/node-red/linux-installers/releases/latest/download/install-update-nodered-deb)
fi
# keep Node.js's memory use small on a 512 MB Pi
sudo mkdir -p /etc/systemd/system/nodered.service.d
printf '[Service]\nEnvironment="NODE_OPTIONS=--max-old-space-size=256"\n' | sudo tee /etc/systemd/system/nodered.service.d/memory.conf >/dev/null
sudo systemctl daemon-reload
sudo systemctl enable nodered.service

echo "== 5/5 Dashboard 2.0 and the Activity Tracker flow"
mkdir -p "$HOME/.node-red"
if [ ! -d "$HOME/.node-red/node_modules/@flowfuse/node-red-dashboard" ]; then
  (cd "$HOME/.node-red" && npm install --no-audit --no-fund @flowfuse/node-red-dashboard)
fi
FLOWS="$HOME/.node-red/flows.json"
if [ ! -s "$FLOWS" ] || [ "$(tr -d ' \n' < "$FLOWS")" = "[]" ]; then
  cp "$DIR/nodered/activity_dashboard_flow.json" "$FLOWS"
  FLOW_MSG="The Activity Tracker flow was loaded for you."
else
  FLOW_MSG="Node-RED already had flows, so they were left alone: import nodered/activity_dashboard_flow.json (see the guide)."
fi
sudo systemctl restart nodered.service

sleep 3
IP="$(hostname -I | awk '{print $1}')"
echo
echo "== Done."
echo "   $FLOW_MSG"
echo "   Node-RED dashboard:   http://$IP:1880/dashboard   (Node-RED can take ~1 min to start)"
echo "   Node-RED editor:      http://$IP:1880"
echo "   Web dashboard (also): http://$IP:8000"
echo "   Watch link_config.h:  LINK_USE_MQTT 1, LINK_HOST \"$IP\", MQTT_PORT 1883"
echo "   Receiver log:         journalctl -u activity-mqtt-receiver -f"
