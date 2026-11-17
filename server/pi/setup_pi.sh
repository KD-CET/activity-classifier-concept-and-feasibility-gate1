#!/bin/bash
# setup_pi.sh - one command to install and start the Activity Tracker on a Raspberry Pi.
#
#   cd ~/server
#   bash pi/setup_pi.sh
#
# The receiver also serves the dashboard website, using only the Python that comes with
# Raspberry Pi OS: nothing to download, works on the 32-bit and 64-bit OS. Safe to run again.
# It then starts by itself at every boot and restarts if it ever crashes.
set -e
DIR="$(cd "$(dirname "$0")/.." && pwd)"
USER_NAME="$(whoami)"
case "$DIR" in
  *" "*) echo "!! The folder path has a space in it: $DIR"
         echo "!! Move the folder somewhere without spaces (e.g. ~/server) and run again."; exit 1;;
esac
echo "== Activity Tracker setup in $DIR (user $USER_NAME)"
command -v python3 >/dev/null || { sudo apt update; sudo apt install -y python3; }
[ -f "$DIR/web/index.html" ] || { echo "!! web/index.html is missing - copy the whole server folder."; exit 1; }

# remove the older Streamlit service if an earlier version of this script created it
if systemctl list-unit-files 2>/dev/null | grep -q '^activity-dashboard.service'; then
  sudo systemctl disable --now activity-dashboard.service || true
  sudo rm -f /etc/systemd/system/activity-dashboard.service
fi

sudo tee /etc/systemd/system/activity-receiver.service >/dev/null <<UNIT
[Unit]
Description=Activity Tracker receiver + dashboard (port 8000)
After=network-online.target
Wants=network-online.target

[Service]
User=$USER_NAME
WorkingDirectory=$DIR
Environment=DB_PATH=$DIR/dashboard.db
Environment=PYTHONUNBUFFERED=1
ExecStart=/usr/bin/python3 $DIR/receiver.py
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
UNIT

sudo systemctl daemon-reload
sudo systemctl enable activity-receiver.service
sudo systemctl restart activity-receiver.service

sleep 2
IP="$(hostname -I | awk '{print $1}')"
echo
if curl -s "http://localhost:8000/health" | grep -q '"ok"'; then
  echo "== Running."
else
  echo "!! The receiver did not answer yet. Check:  journalctl -u activity-receiver -n 30"
fi
echo "   Dashboard (any browser on the same Wi-Fi):  http://$IP:8000"
echo "   Watch link_config.h:  LINK_USE_MQTT 0, LINK_HOST \"$IP\", HTTP_PORT 8000"
echo "   Live log of reports:  journalctl -u activity-receiver -f"
