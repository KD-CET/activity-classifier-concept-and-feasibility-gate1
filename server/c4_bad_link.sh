#!/bin/bash
# c4_bad_link.sh - makes the Pi's Wi-Fi bad in BOTH directions (claim C4), then undoes it by itself.
#   sudo bash c4_bad_link.sh          500 ms delay + 20 % loss each way, for 5 minutes
#   sudo bash c4_bad_link.sh undo     turn it off now
# A reboot also turns it off.
DEV=${DEV:-wlan0}; SECS=${SECS:-300}; DELAY=${DELAY:-500ms}; LOSS=${LOSS:-20%}

undo() {
  tc qdisc del dev "$DEV" root 2>/dev/null
  tc qdisc del dev "$DEV" ingress 2>/dev/null
  ip link del ifb0 2>/dev/null
}

[ "$(id -u)" = 0 ] || { echo "Run it with sudo:  sudo bash $0"; exit 1; }
if [ "$1" = "undo" ]; then undo; echo "Bad link OFF at $(date +%T)"; exit 0; fi

undo
# schedule the automatic undo first, so it happens even if this terminal freezes or closes
if ! systemd-run --on-active="$SECS" /bin/bash "$(readlink -f "$0")" undo >/dev/null 2>&1; then
  echo "WARNING: could not schedule the automatic undo. Turn it off yourself after 5 min:"
  echo "         sudo bash $0 undo"
fi

# 1) what the Pi sends (acks to the watch)
if ! tc qdisc add dev "$DEV" root netem delay "$DELAY" loss "$LOSS"; then
  echo "netem is not available on this Pi, so C4 cannot be tested here."; undo; exit 1
fi
# 2) what the Pi receives (the watch's reports), through a helper interface ifb0
both=1
{ modprobe ifb && ip link add ifb0 type ifb && ip link set ifb0 up &&
  tc qdisc add dev "$DEV" handle ffff: ingress &&
  tc filter add dev "$DEV" parent ffff: protocol ip u32 match u32 0 0 action mirred egress redirect dev ifb0 &&
  tc qdisc add dev ifb0 root netem delay "$DELAY" loss "$LOSS"; } 2>/dev/null || both=0
if [ "$both" = 0 ]; then
  tc qdisc del dev "$DEV" ingress 2>/dev/null; ip link del ifb0 2>/dev/null
  echo "NOTE: only the Pi's OUTGOING traffic is degraded (ifb not available). Write this in the entry."
fi

echo "Bad link ON at $(date +%T): $DELAY delay + $LOSS loss, $( [ $both = 1 ] && echo 'both directions' || echo 'outgoing only' ), for $SECS s"
echo "Check that it is really on (ping to the router, 10 tries):"
GW=$(ip route | awk '/default/ {print $3; exit}')
ping -c 10 -q "$GW" | tail -2
echo "Expected with both directions: about 1000 ms and about 30-40 % packet loss."
echo "Now change activity a few times. It turns off by itself at about $(date -d "+$SECS sec" +%T)."
