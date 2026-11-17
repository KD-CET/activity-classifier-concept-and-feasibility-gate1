// ---- shared helpers (copied into each function node; function nodes cannot share code) ----
const NAMES = { sedentary: "Sedentary", walking: "Walking", running: "Running" };
const ORDER = ["sedentary", "walking", "running"];
const COLOR = { sedentary: "#6d78e8", walking: "#1fa97a", running: "#f08a3c" };
const esc = (v) => String(v === undefined || v === null ? "" : v)
  .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
const pad = (n) => (n < 10 ? "0" : "") + n;
// times use the Pi's time zone (the setup script sets it)
const hhmm = (ts) => { const d = new Date(ts * 1000); return pad(d.getHours()) + ":" + pad(d.getMinutes()); };
const hhmmss = (ts) => { const d = new Date(ts * 1000); return hhmm(ts) + ":" + pad(d.getSeconds()); };
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
const sameDay = (a, b) => new Date(a * 1000).toDateString() === new Date(b * 1000).toDateString();
const dayLabel = (ts, now) => sameDay(ts, now) ? hhmm(ts)
  : MONTHS[new Date(ts * 1000).getMonth()] + " " + new Date(ts * 1000).getDate();
const dayTime = (ts, now) => sameDay(ts, now) ? hhmm(ts) : dayLabel(ts, now) + ", " + hhmm(ts);
const mins = (m) => (m < 10 ? m.toFixed(1) : String(Math.round(m)));
const duration = (s) => {
  if (s < 60) return Math.max(0, Math.round(s)) + " s";
  const m = Math.floor(s / 60);
  return m < 60 ? m + " min" : Math.floor(m / 60) + " h " + (m % 60) + " min";
};
