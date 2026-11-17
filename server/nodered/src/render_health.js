// ---- "System health": link figures + delivery log ----
const s = msg.payload || {};
const ws = s.watch_status;
const delay = s.last_delay_s;
const delayCol = delay === null || delay === undefined ? "#1fa97a" : delay > 10 ? "#d64545" : delay > 5 ? "#c98a12" : "#1fa97a";
const brokerTxt = ws ? (ws.status === "online" ? "Online" : "Offline") : "Unknown";
const brokerNote = ws ? "since " + hhmm(ws.since) + (ws.status === "offline" ? " (Last Will from the broker)" : "") : "No status message yet";
const figs = '<div class="at-figs at-four">' +
  '<div><div class="at-label">Sensors used</div><div class="at-value">' + esc(s.sensors || "-") + '</div><div class="at-note">Fewer sensors means a fallback is in use</div></div>' +
  '<div><div class="at-label">Last delivery delay</div><div class="at-value">' + (delay === null || delay === undefined ? "-" : esc(delay.toFixed(1)) + "<small>s</small>") +
    '</div><div class="at-meter"><span style="width:' + Math.min(100, 10 * (delay || 0)) + "%;background:" + delayCol + '"></span></div><div class="at-note">Target: 10 s</div></div>' +
  '<div><div class="at-label">Watch (broker)</div><div class="at-value" style="color:' + (ws && ws.status === "online" ? "#1fa97a" : "#d64545") + '">' + esc(brokerTxt) +
    '</div><div class="at-note">' + esc(brokerNote) + "</div></div>" +
  '<div><div class="at-label">Messages that needed a retry</div><div class="at-value">' + esc(s.retried_msgs || 0) +
    '</div><div class="at-note">of ' + esc(s.total_events || 0) + " in the last " + esc(s.history_days || 7) + " days</div></div></div>";
const rows = (s.log || []).slice(0, 20).map((e) => "<tr><td>" + esc(hhmmss(e.ts)) + "</td><td>" + esc(String(e.type).replace("_", " ")) + "</td><td>" +
  esc(NAMES[e.activity] || "") + "</td><td>" + esc(e.sensors || "") + '</td><td class="at-num' + (e.delay_s > 10 ? " at-late" : "") + '">' +
  esc(e.delay_s.toFixed(2)) + ' s</td><td class="at-num">' + esc(e.retries) + "</td></tr>").join("");
msg.payload = figs + '<p class="at-hint" style="margin-top:22px">Delivery log, newest 20 messages. Delay is the time from the watch to the Pi; red means over the 10 s target.</p>' +
  '<div class="at-tablewrap"><table class="at-table"><thead><tr><th>Time</th><th>Message</th><th>Activity</th><th>Sensors</th><th class="at-num">Delay</th><th class="at-num">Retries</th></tr></thead><tbody>' +
  (rows || '<tr><td colspan="6" class="at-empty">No messages yet.</td></tr>') + "</tbody></table></div>";
return msg;
