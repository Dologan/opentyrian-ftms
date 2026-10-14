// Generates the synthetic bike feed CSVs in test/bike/feeds/ (deterministic).
// usage: node gen_feeds.mjs [OUT_DIR]   (default: <repo>/test/bike/feeds)
// One row per 32 ticks (~0.92 s at 28.75 ms/tick), like the Q200B.
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const out = resolve(process.argv[2] ?? join(here, '../../../test/bike/feeds'));
mkdirSync(out, { recursive: true });
const STEP = 32;
const secToTick = (s) => Math.round((s * 1e6) / 28750);

/** rows: [tick, watts|null, cadence|null, connected] */
function write(name, comment, rows) {
  const lines = [`# ${comment}`, 'tick,watts,cadence,connected'];
  for (const [t, w, c, k] of rows) lines.push(`${t},${w ?? ''},${c ?? ''},${k}`);
  writeFileSync(join(out, name), lines.join('\n') + '\n');
}

/** Sample at every STEP ticks for `sec` seconds starting at tick t0 using f(sec_into_segment). */
function seg(t0, sec, f) {
  const rows = [];
  const n = Math.floor(secToTick(sec) / STEP);
  for (let i = 0; i < n; i++) {
    const [w, c] = f((i * STEP * 28750) / 1e6);
    rows.push([t0 + i * STEP, w, c, 1]);
  }
  return rows;
}
const end = (rows) => rows[rows.length - 1][0] + STEP;

write('const150.csv', 'constant 150 W at 80 rpm for 60 s', seg(0, 60, () => [150, 80]));

write('ramp0_300.csv', 'ramp 0 -> 300 W over 180 s, cadence 40 -> 95', seg(0, 180, (s) => {
  const f = s / 180;
  return [Math.round(300 * f), Math.round(40 + 55 * f)];
}));

{ // interval sprints: 60 s warm-up at 150 W, then 3 x (20 s at 320 W, 40 s at 140 W)
  let rows = seg(0, 60, () => [150, 85]);
  for (let i = 0; i < 3; i++) {
    rows = rows.concat(seg(end(rows), 20, () => [320, 105]));
    rows = rows.concat(seg(end(rows), 40, () => [140, 80]));
  }
  write('surge.csv', 'interval sprints: 60 s at 150 W, then 3 x (20 s 320 W, 40 s 140 W)', rows);
}

{ // dropout: link down for 2 s then 5 s
  let rows = seg(0, 20, () => [150, 85]);
  rows.push([end(rows), null, null, 0]);
  let t = end(rows) - STEP + secToTick(2);
  rows = rows.concat(seg(t, 20, () => [150, 85]));
  rows.push([end(rows), null, null, 0]);
  t = end(rows) - STEP + secToTick(5);
  rows = rows.concat(seg(t, 20, () => [150, 85]));
  write('dropout.csv', 'ride 150 W; link down 2 s; ride; link down 5 s; ride', rows);
}

{ // stop: ride, gradual coast-down, stopped, restart
  let rows = seg(0, 30, () => [150, 85]);
  const coast = [[120, 70], [80, 62], [40, 44], [15, 30], [5, 18], [0, 8], [0, 0]];
  let t = end(rows);
  for (const [w, c] of coast) { rows.push([t, w, c, 1]); t += STEP; }
  rows = rows.concat(seg(t, 20, () => [0, 0]));
  rows = rows.concat(seg(end(rows), 30, () => [150, 90]));
  write('stop.csv', 'ride 30 s, gradual coast-down, 20 s stopped, restart at 90 rpm', rows);
}

{ // stop_hold: Q200B style - holds the last cadence ~3 s after pedals stop, then 0
  let rows = seg(0, 30, () => [150, 85]);
  rows = rows.concat(seg(end(rows), 3, () => [150, 85]));
  rows = rows.concat(seg(end(rows), 20, () => [0, 0]));
  rows = rows.concat(seg(end(rows), 20, () => [140, 75]));
  write('stop_hold.csv', 'ride, cadence held 3 s after stopping, then 0, restart', rows);
}

{ // silent: the bike stops notifying while the link stays up
  let rows = seg(0, 20, () => [150, 85]);
  const t = end(rows) - STEP + secToTick(12);
  rows = rows.concat(seg(t, 20, () => [150, 85]));
  write('silent.csv', 'ride 20 s, 12 s of no notifications (link up), ride', rows);
}

write('nopower.csv', 'cadence only (power empty): exercises the cadence fallback curve',
  seg(0, 90, (s) => [null, Math.round(50 + 50 * Math.min(1, s / 60))]));
