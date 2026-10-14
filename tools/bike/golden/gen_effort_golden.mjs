// Golden-fixture generator for the C effort engine (src/bike_effort.c).
//
// Bundles Flywheel's real TypeScript EffortEngine and steps it over the feed CSVs in
// test/bike/feeds/, writing one row per gameplay tick to test/bike/golden/<case>.csv
// plus test/bike/golden/manifest.txt ("name feed ftp_w ticks").
//
// usage: FLYWHEEL_DIR=/path/to/flywheel node gen_effort_golden.mjs
// Needs Node >= 22 and flywheel's game/node_modules (rolldown is used as the bundler).
// Flywheel is only read, never modified.
//
// Semantics shared with the C test (test/unit/bike_effort_test.c):
//  * dt = 28750 us per tick; clock = tick * dt.
//  * each connected=1 row creates a NEW sample object (isNewSample), arrival = that tick;
//    connected=0 sets linkUp=false until the next connected row; before the first row linkUp=false.
//  * sampleAgeS = (tick - arrivalTick) * dt; after the last row the feed stays silent.
//  * a case runs for (last row tick + TAIL_TICKS) ticks.
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../../..');
const feedsDir = join(root, 'test/bike/feeds');
const goldenDir = join(root, 'test/bike/golden');
const flywheel = process.env.FLYWHEEL_DIR;
if (!flywheel) {
  console.error('set FLYWHEEL_DIR to the flywheel checkout');
  process.exit(2);
}
const DT_US = 28750;
const TAIL_TICKS = 400;

// name, feed file, FTP
const CASES = [
  ['const150', 'const150.csv', 200],
  ['ramp0_300', 'ramp0_300.csv', 200],
  ['ramp0_300_ftp50', 'ramp0_300.csv', 50],
  ['ramp0_300_ftp500', 'ramp0_300.csv', 500],
  ['surge', 'surge.csv', 200],
  ['surge_ftp50', 'surge.csv', 50],
  ['surge_ftp500', 'surge.csv', 500],
  ['dropout', 'dropout.csv', 200],
  ['stop', 'stop.csv', 200],
  ['stop_hold', 'stop_hold.csv', 200],
  ['silent', 'silent.csv', 200],
  ['nopower', 'nopower.csv', 200],
  ['q200b-ride1', 'q200b-ride1.csv', 200],
  ['q200b-ride2', 'q200b-ride2.csv', 200],
];

function parseFeed(path) {
  const rows = [];
  for (const raw of readFileSync(path, 'utf8').split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith('#') || !/^[0-9+\-.]/.test(line)) continue;
    const f = line.split(',').map((x) => x.trim());
    const num = (s) => (s === undefined || s === '' ? null : Number(s));
    rows.push({
      tick: Number(f[0]),
      powerW: num(f[1]),
      cadenceRpm: num(f[2]),
      connected: f[3] === undefined || f[3] === '' ? true : Number(f[3]) !== 0,
    });
  }
  return rows;
}

// Bundle the real engine.
const req = createRequire(join(flywheel, 'game/package.json'));
const { rolldown } = req('rolldown');
const tmp = mkdtempSync(join(tmpdir(), 'effort-golden-'));
try {
  const bundle = await rolldown({
    input: join(flywheel, 'game/src/core/effort/effort-engine.ts'),
    platform: 'node',
    logLevel: 'silent',
  });
  await bundle.write({ file: join(tmp, 'engine.mjs'), format: 'esm' });
  const { EffortEngine } = await import(pathToFileURL(join(tmp, 'engine.mjs')).href);

  mkdirSync(goldenDir, { recursive: true });
  const manifest = [];
  for (const [name, feedFile, ftpW] of CASES) {
    const rows = parseFeed(join(feedsDir, feedFile));
    const ticks = rows[rows.length - 1].tick + TAIL_TICKS;
    const eng = new EffortEngine({ ftpW });
    let sample = null;
    let arrival = 0;
    let linkUp = false;
    let next = 0;
    const out = ['tick,effort_w,effort_norm,moving,stale,surging,surge_meter,events,baseline_w,surge'];
    for (let t = 0; t < ticks; t++) {
      while (next < rows.length && rows[next].tick <= t) {
        const r = rows[next++];
        if (r.connected) {
          linkUp = true;
          sample = { cadenceRpm: r.cadenceRpm, powerW: r.powerW, resistance: null }; // new object per row
          arrival = r.tick;
        } else {
          linkUp = false;
        }
      }
      const s = eng.step(DT_US / 1e6, {
        sample,
        sampleAgeS: ((t - arrival) * DT_US) / 1e6,
        linkUp,
        paused: false,
      });
      let ev = 0;
      for (const e of eng.events()) {
        if (e.type === 'movingStart') ev |= 1;
        else if (e.type === 'movingStop') ev |= 2;
        else if (e.type === 'surgeStart') ev |= 4;
        else if (e.type === 'surgeEnd') ev |= 8;
        else if (e.type === 'overdriveReady') ev |= 16;
      }
      out.push(
        [
          t,
          (s.effortPct * ftpW).toFixed(5),
          s.effortNorm.toFixed(6),
          s.moving ? 1 : 0,
          s.stale ? 1 : 0,
          s.surging ? 1 : 0,
          s.surgeMeter.toFixed(6),
          ev,
          s.baselineW.toFixed(5),
          s.surge.toFixed(6),
        ].join(','),
      );
    }
    writeFileSync(join(goldenDir, `${name}.csv`), out.join('\n') + '\n');
    manifest.push(`${name} ${feedFile} ${ftpW} ${ticks}`);
  }
  writeFileSync(join(goldenDir, 'manifest.txt'), manifest.join('\n') + '\n');
  console.log(`wrote ${manifest.length} golden cases to ${goldenDir}`);
} finally {
  rmSync(tmp, { recursive: true, force: true });
}
