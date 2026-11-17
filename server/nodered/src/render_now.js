// ---- "Right now": current activity, link, session ribbon, figures ----
const s = msg.payload || {};
const now = s.server_time || Date.now() / 1000;
const sess = s.session;
const ws = s.watch_status;
let linkCls = "off", linkTxt = "No data from the watch yet";
if (ws && ws.status === "offline") { linkCls = "bad"; linkTxt = "Watch offline since " + hhmm(ws.since) + " (Last Will)"; }
else if (sess && s.link_ok) { linkCls = "ok"; linkTxt = "Watch connected"; }
else if (sess) { linkCls = "warn"; linkTxt = "No signal from the watch for " + duration(s.last_heard_s || 0); }
const link = '<div class="at-link"><i class="at-dot at-' + linkCls + '"></i>' + esc(linkTxt) + "</div>";

if (!sess) {
  msg.payload = link + '<p class="at-word">No sessions yet</p>' +
    '<p class="at-sub">Turn on the watch. Its first activity appears here a few seconds after its link turns green.</p>';
  return msg;
}
const act = s.current_activity;
let sub;
if (s.in_progress && s.current_since) sub = "For <b>" + esc(duration(now - s.current_since)) + "</b>, in a session that started at " + esc(dayTime(sess.start, now)) + ".";
else if (s.in_progress) sub = "Session started at " + esc(dayTime(sess.start, now)) + ".";
else sub = "No session in progress. This is the last activity of the session that ended at <b>" + esc(dayTime(sess.end, now)) + "</b>.";

const total = Math.max(1, sess.end - sess.start);
const ribbon = (s.timeline || []).map(function (seg) {
  const w = Math.max(seg[1] - seg[0], total / 400);
  return '<span style="flex:' + w.toFixed(1) + ";background:" + COLOR[seg[2]] + '" title="' +
    esc(NAMES[seg[2]] + ", " + hhmm(seg[0]) + "-" + hhmm(seg[1]) + " (" + duration(seg[1] - seg[0]) + ")") + '"></span>';
}).join("");

let note = "First session", noteCls = "";
if (s.prev_active_min !== null && s.prev_active_min !== undefined) {
  const diff = s.active_min - s.prev_active_min;
  note = (diff >= 0 ? "+" : "-") + Math.abs(diff).toFixed(1) + " min compared with last session";
  noteCls = diff > 0.05 ? " at-up" : diff < -0.05 ? " at-down" : "";
}
const split = s.by_activity_min || {};
const splitBar = ORDER.map((k) => '<span style="flex:' + (split[k] || 0) + ";background:" + COLOR[k] + '"></span>').join("");
const legend = ORDER.map((k) => '<span><i style="background:' + COLOR[k] + '"></i>' + NAMES[k] + " " + mins(split[k] || 0) + " min</span>").join("");

msg.payload = link +
  '<p class="at-word" style="color:' + (COLOR[act] || "#1c2433") + '">' + esc(NAMES[act] || "-") + "</p>" +
  '<p class="at-sub">' + sub + "</p>" +
  '<div class="at-ribbon">' + ribbon + "</div>" +
  '<div class="at-ends"><span>' + esc(hhmm(sess.start)) + "</span><span>" + (s.in_progress ? "now" : esc(hhmm(sess.end))) + "</span></div>" +
  '<div class="at-figs">' +
    '<div><div class="at-label">Active minutes this session</div><div class="at-value">' + esc(mins(s.active_min)) + "<small>min</small></div>" +
      '<div class="at-note' + noteCls + '">' + esc(note) + "</div></div>" +
    '<div><div class="at-label">Session length</div><div class="at-value">' + esc(mins(sess.length_min)) + "<small>min</small></div>" +
      '<div class="at-note">' + (s.in_progress ? "Still going" : "Ended " + esc(dayTime(sess.end, now))) + "</div></div>" +
    '<div><div class="at-label">Time in each activity</div><div class="at-split">' + splitBar + '</div><div class="at-legend">' + legend + "</div></div>" +
  "</div>";
return msg;
