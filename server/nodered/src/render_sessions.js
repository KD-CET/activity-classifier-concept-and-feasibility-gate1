// ---- "Active minutes per session": bars for the last 10 sessions ----
const s = msg.payload || {};
const now = s.server_time || Date.now() / 1000;
const ss = s.sessions || [];
if (!ss.length) { msg.payload = '<p class="at-empty">No sessions yet.</p>'; return msg; }
const top = Math.max(1, ...ss.map((x) => x.active_min));
const bars = ss.map(function (x, i) {
  const cur = i === ss.length - 1;
  return '<div class="at-bar' + (cur ? " at-cur" : "") + '"><b>' + esc(mins(x.active_min)) + '</b><span style="height:' +
    Math.max(2, Math.round(140 * x.active_min / top)) + 'px" title="' + esc(dayTime(x.start, now) + ": " + mins(x.active_min) +
    " active min of " + mins(x.length_min)) + '"></span></div>';
}).join("");
const names = ss.map((x) => "<span>" + esc(dayLabel(x.start, now)) + "</span>").join("");
msg.payload = '<p class="at-hint">Walking and running, last 10 sessions. The darkest bar is the session shown above.</p>' +
  '<div class="at-bars">' + bars + '</div><div class="at-names">' + names + "</div>";
return msg;
