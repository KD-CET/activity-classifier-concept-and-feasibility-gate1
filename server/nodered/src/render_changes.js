// ---- "Recent changes": newest first ----
const s = msg.payload || {};
const ch = s.recent_changes || [];
msg.payload = ch.length
  ? '<ul class="at-changes">' + ch.map((c) => "<li><time>" + esc(hhmm(c.ts)) + '</time><i style="background:' +
      COLOR[c.activity] + '"></i>' + esc(NAMES[c.activity]) + '<span class="at-src">' + esc(c.sensors || "") + "</span></li>").join("") + "</ul>"
  : '<p class="at-empty">No changes yet in this session.</p>';
return msg;
