# Spike: modern-tyrian as the FTMS bike base (Phase 0)

Status: throwaway spike, nothing committed or pushed. Wall time about 1h20m of the 2h box.
Subject: `vittau/modern-tyrian` HEAD `677e5c0` (v0.7.0, 2026-10-09), SDL3, 4-core / 15 GB Linux VM.
Work tree: `$SCRATCH/mt-spike` (git worktree of the clone). Other files in this directory:

| File | What |
|---|---|
| `regress-bike.patch` | `git diff` of the spike (bike module, flag, hash, HUD, check script, trace). 435 lines, 9 files. |
| `modern-hud-bike-on.png`, `modern-hud-bike-off.png` | Modern HUD at 16:9, frame 330, 3x nearest-neighbour. "on" shows `190W 88RPM` above `GEN Advanced M.` |
| `ride1.csv` | The scripted trace used (tick,watts,rpm). |
| `ride1-bikelog-excerpt.txt` | Per-tick `tick watts rpm add power` excerpt from `--regress-bike-log`. |
| `check_bike.out` | Output of `tools/check_bike.sh` (4 PASS). |
| `regress-quick-unmodified.out`, `regress-full-flag-off.out` | Trimmed regress logs. |

## 1. Build (Linux, headless)

No SDL3 on the box, so I used the repo's own route (`make_linux.sh` builds pinned static SDL3 3.4.16 + SDL3_net 3.2.0 from tarballs, sha256-verified).

```sh
cd mt-spike; ln -s $SCRATCH/data/tyrian21 data
./make_linux.sh --deps-only        # failed at once, see friction 1
apt-get install -y libxtst-dev libpulse-dev libdecor-0-dev libgles2-mesa-dev libpipewire-0.3-dev wayland-protocols libusb-1.0-0-dev   # 5 s
./make_linux.sh --deps-only        # real 1m51s (SDL3 + SDL3_net, -j4)
./make_linux.sh data/              # real 14.6s (82 .c files, 0 warnings), links static, ldd shows no libSDL3
```

Timings: SDL3 + SDL3_net 1m51s once (cacheable, CI caches `build/sdl`), game 15 s, incremental rebuild after my patch about 5 s.

Friction points (all small):
1. SDL3 3.4.16 hard-fails without `libxtst-dev` (XTEST). The script's header comment says so; it is just not installed by default. Fix was one apt line (apt was not locked).
2. `make_linux.sh data` with `data` a symlink fails "no Tyrian data": `find -maxdepth 1` does not follow the symlink. Pass `data/` (mine, not theirs).
3. `make regress*` does not set `PKG_CONFIG_PATH`; with a source-built SDL3 you must `export PKG_CONFIG_PATH=$PWD/build/sdl/lib/pkgconfig` (CI does this at job level) or the guard compiles (`game-rules`) fail with `SDL3/SDL_stdinc.h: No such file`.
4. `REGRESS_SKIP_BUILD=1` is needed to reuse the make_linux.sh binary. A plain `make` against the static prefix also links on glibc 2.39 without `--static` libs (verified in a second worktree).
5. Data: baselines expect Tyrian 2.1 in `./data` (or `TYRIAN_DATA`); `get_data.sh` fetches it. Our data dir worked unchanged (file names are lower-case). Tyrian 2000 data was not needed (`2000 quick: skipped (no data)`).

## 2. Regression, unmodified

```sh
export PKG_CONFIG_PATH=$PWD/build/sdl/lib/pkgconfig REGRESS_SKIP_BUILD=1 REGRESS_TIMINGS=1
make regress-quick   # 16 cases + guards: "All 16 regression cases passed in 28.2s" (real 36.7s)
make regress         # (later, with my flag-off patch) "All 207 regression cases passed in 299.0s", real 5m42s, 4 jobs
```

All PASS, no data surprises. Slowest quick cases: `modern-demo1-d4` 27.8 s, `modern-wide-demo1-d4` 23.4 s, `depth-tyrian-on-d4` 15.4 s. The one SKIP is a chmod test (running as root). Docs and AGENTS.md are accurate for this path.

## 3. How the harness works, and the `--regress-bike` spike

Harness facts (src/regress.c, src/joystick.c):
- Headless: `SDL_VIDEO_DRIVER=dummy`, `SDL_AUDIO_DRIVER=dummy`; a virtual clock (`regress_clock_ticks10bit`, `regress_clock_advance_to`) replaces sleeping, so a 600-frame run takes 0.46 s.
- `--regress-out=F` writes `frame hash` per presented frame (FNV of the 8-bit framebuffer; Modern hashes the XRGB canvas). `--regress-state-out=F` writes `frame statehash` where `regress_state_hash()` folds the RNG state, both players, all enemies/shots/explosions arrays, event/level position. Pointers are skipped, scalars are fixed-width LE so LP64/LLP64 agree.
- `--regress-stick=X,Y` calls `joystick_inject_stick()`, which installs a fake `Joystick` (`injected=true`, `handle=NULL`) so the real analog movement path runs headless; `--regress-stick-log` writes a per-tick trajectory. That is the exact pattern a bike feed wants: inject at the lowest layer, keep everything above it real.
- **`power` and `powerAdd` are NOT in the state hash.** They influence frames only through the power bar pixels (Classic bar at x=269..276; Modern HUD) and, via `shots.c:348` (`power < poweruse` blocks a shot; the shot costs `poweruse`) and shield regen (`tyrian2.c` ~1157), through shot arrays and shield, which are hashed. So a watts change is visible in frame hashes immediately and in state hashes once it gates firing, but the reserve itself is invisible.

Implementation (patch `regress-bike.patch`):
- `src/bike.[ch]` (new): `bike_load("const:W[,RPM]" | file.csv)`, `bike_power_add(base_add)`, `bike_watts()`, `bike_cadence_rpm()`, `bike_level_start()`. CSV `tick,watts[,rpm]`, piecewise constant, `#` comments. Pure integer, a function of the level tick only.
- Scaling: `acc += watts * powerAdd; add = acc / 150; acc %= 150;`. So 150 W is exactly the equipped generator's stock `powerAdd` (Advanced MR-12 = 14 per tick in the regress loadout, so the bar fills in 64 ticks), and the sub-unit remainder is carried exactly, no drift or float. (Design note: because it scales the *equipped* generator, a generator upgrade multiplies bike gain; use a constant reference if we want generators to mean nothing, or keep it as a progression hook.)
- Hook: `tyrian2.c` "Power Bar": `if (bike_active()) { add = bike_power_add(powerAdd); power += add; clamp 900; log } else { power += powerAdd; ... }`; `bike_level_start()` next to `power = 0`. Shop screen refill (`game_menu.c:3649`) and `twoPlayerMode || onePlayerAction` (power pinned to 900) are untouched: **the bike has no effect in arcade/2-player modes** by the engine's own rules.
- Flags (ids 440/441, free range): `--regress-bike=FILE|const:W[,RPM]`, `--regress-bike-log=FILE` (`tick watts rpm add power_after`).
- State hash: `power` and `lastPower` are folded in **only when a bike feed is active**. Always hashing them would change every one of the 207 baselines (needs a justification per AGENTS.md); gating keeps flag-off byte-identical.
- Guard `tools/check_bike.sh` (3.4 s), output in `check_bike.out`:

```
PASS bike-off: demo3-d4 matches committed baseline
PASS bike-150W: frames identical to flag-off (neutral rate)
PASS bike-determinism: frames, state hashes and per-tick log identical across runs
PASS bike-observable: ride frames differ from flag-off (first diff line 102,600c102,600)
```

Evidence, case `--regress-script=1:3 --regress-seed=32402394 --regress-detail=2 --regress-frames=600` (`$SCRATCH/out`):
- (a) **Flag OFF unchanged:** `make regress-quick` 16/16 and full `make regress` 207/207 pass with the patch applied; and a pristine HEAD build produces identical frame AND state files for the case above (`cmp` clean).
- 150 W constant: frames identical to flag-off (the neutral-rate identity); state hash differs only because `power` is then hashed (expected, by design).
- (b) **Deterministic:** `ride1.csv` run twice: frames, state hashes and bike log byte-identical (frame digest 9f61de19...). **Differs from baseline:** 499 of 600 frame lines differ from flag-off, starting at line 102 (the first frames before that are identical; I did not trace why, likely level intro before the HUD bar moves); every state line differs because `power` is hashed.
- Trace sanity (`ride1-bikelog-excerpt.txt`): 100 W warm-up adds 9/tick (bar 333 at tick 100), ramp to 235 W adds 22/tick, saturates at 900; at 0 W the reserve drains (900 to 780 over 50 ticks, consumed by firing and shield regen).

Limits of the spike: tick = level tick only (pause behaviour not examined), no live source yet, Tyrian 2000 untested, no `visualc/opentyrian.vcxproj` entry for `bike.c` (AGENTS.md requires it; CRLF file) and no `gcc-16 -std=iso9899:1999 -pedantic -Werror -fsyntax-only` audit (the normal build with their flags, `-pedantic -Wall -Wextra`, was warning-free).

## 4. Modern HUD readout

`src/modern_hud.c`: `hud_draw_bike()` prints `"%uW %uRPM"` with `hud_text()` in the value bank, right under the vitals block and above `GEN <name>`, in `hud_draw_status_panel()` only for roomy panels and only when `bike_active()`; the vitals block's free-space calculation reserves 8 rows so nothing overlaps. 19 lines. It reads `bike_watts()`/`bike_cadence_rpm()` (set by the level loop for this tick), so it is presentation-only and obeys the "Modern layer never writes game state, never calls mt_rand" rule. Screenshot via `--regress-snapshot=330:file.bmp` (the in-engine mechanism; `tools/dump_screens.py` only dumps static pictures, not gameplay), converted to PNG with PIL. Not done: compact 2-player panel (power is pinned there anyway), Classic (non-Modern) HUD, and a power-bar-adjacent numeric style (the bar already prints its own 0..900 value; a "W" label could replace the generator line instead).

## 5. Windows path and CI

- modern-tyrian: MSYS2 UCRT64 (`msys2/setup-msys2@v2`, `update: true`, packages `mingw-w64-ucrt-x86_64-{cc,pkgconf,sdl3,sdl3-net}` + make/python/zip), `-Werror` debug compile, `make regress`, then T2000 fetch and `regress-2000`, package exe + DLLs. x86_64 only (arm64 job dropped 2026-09). Needs `python` and `cygpath` care; AGENTS.md lists MSYS pitfalls. `visualc/` still has a solution/vcxproj but the maintainers' documented Windows route is MSYS2, and the vcxproj must be kept in sync by hand when adding `.c` files (CRLF).
- Upstream (`ot-upstream` checkout): same shape (MSYS2, UCRT64 plus CLANGARM64, SDL2 packages) plus a `visualc/` solution. So the Windows story is equivalent in kind; the practical gap is that modern-tyrian's pipeline also *runs the whole regress suite* on Windows, and the MSYS2 `sdl3-net` package lagged the base image (they needed `update: true`).
- Would CI run in our fork? Yes in principle: triggers are `push`/`pull_request`/`workflow_dispatch`, runners are GitHub-hosted (`ubuntu-22.04`, `ubuntu-22.04-arm`, `windows-latest`, `macos-14`), no secrets used, `GITHUB_TOKEN` only (`contents:read`, `actions:read`; the release job needs `contents:write`). Concerns: (1) every run downloads Tyrian 2000 from camanis.net and **fails the job** if that is unreachable, plus `tools/fetch_t2000_data.sh` behaviour on a fork should be reviewed (no cache by design); we may want to drop the T2000 steps; (2) `decide.cjs` skips docs-only pushes and reuses green SHAs, which is fine but means a docs-only push yields no package; (3) macOS and ARM runners cost minutes; (4) they cannot be validated without pushing, which I was told not to do, so this is a read-only assessment. `regress-full.yml` is manual-only.
- Their README badge points at `vittau/modern-tyrian`; trivial to retarget.

## 6. Port cost: our fork's commits beyond `5a9d8da`

Finding: `ours/claude/focused-ritchie-ahh2sn` is just 9 **upstream** (Carl Reinke) commits, 2026-09-26..10-09, on top of `5a9d8da`; there are no FTMS commits on it yet (our FTMS work to port would be new code). `5a9d8da` is an ancestor of modern-tyrian HEAD, which has 283 commits since. Each pick applied alone onto `677e5c0` in a throwaway worktree (aborted/reset afterwards, the clone is untouched):

| Commit | Result | Effort |
|---|---|---|
| `a0ed9fd` Use UTF-8 ACP on Windows | applies cleanly (visualc manifest/rc/vcxproj only) | 0 (check windres/MinGW path) |
| `216c771` Include math.h where it is used | conflicts in joystick.c, varz.c (include lines; varz.c was split by their schema work) | 10 min, re-add `#include <math.h>` by hand |
| `49af573` toupper UB | 1 hunk in tyrian2.c (they use `arcade->engage_code` tables) | 10 min, still needed |
| `dda8863` Fix typo | conflicts in keyboard.c, opentyr.c (SDL3 API rewrites) | 15 min, mostly obsolete |
| `ba9c399`, `f3d4560` README | README.md conflicts (they rewrote it) | skip |
| `52140ae` Skip sound load if audio init fails | conflicts in loudness.c (they refactored `init_audio`/`configure_audio`) | 20 min, port by intent |
| `a923d8b` Format fatal log once | conflicts in logging.c (own log-file implementation) | 20 min, port by intent |
| `624e2f2` SWAR background and smoothies speedup | conflicts: backgrnd.c 13 hunks, tyrian2.c 1; touches the same blit paths their drawlist recorder hooks (`drawlist_record_bg_row`) | 0.5 to 1 day; upside: 207 byte-exact cases verify it |

Net: 1 of 9 clean, 3 small hand ports, 2 skippable doc commits, 2 port-by-intent, 1 real merge. Continuing to track upstream is therefore not a mechanical rebase: expect hours per upstream batch and a bigger event whenever upstream touches the renderer. The reverse is also true: anything we add on modern-tyrian that touches `tyrian2.c`, `backgrnd.c`, `joystick.c` will keep colliding with a maintainer who commits in bursts (35, 113, 57, 26 commits on 2026-09-27..30, 17 on 10-09; `git diff 5a9d8da HEAD -- src`: 140 files, +45k/-15k lines, 62k LOC vs 47k upstream).

## 7. Assessment: adopt modern-tyrian as our base?

Pros
- **Harness is real and works first time.** 207 per-tick frame+state cases, headless, virtual clock, 5 min on 4 cores, 28 s quick tier. It let me prove flag-off-unchanged, 150 W identity, determinism and observability in under an hour. Classic mode is documented byte-exact with upstream, so we keep an upstream-equivalence oracle.
- The `--regress-stick` pattern (inject at the device layer, log per tick) is a ready template for a bike source, and trace replay is exactly how we should test an FTMS decoder and the watts-to-power mapping without a bike.
- Modern HUD is semantic and presentation-only; a watts/cadence readout was 19 lines and clean. 16:9/ultrawide layouts exist (a bike on a TV is the likely display).
- Tyrian 2000, Steam Deck/controller work, launcher, CRT filter, pause/menus are done for us; CI builds and tests on three OSes.
- AGENTS.md is thorough and accurate; hard rules (no state writes from presentation, deterministic RNG) match what we need.

Cons
- **Churn and bus factor.** One main author driving AI-worker sessions, bursts of 100+ commits/day, docs in Portuguese, heavy architectural change (schema/variant tables, drawlist, interp). Tracking upstream is manual per section 6; we would effectively fork a fork.
- **Hash-baseline tax.** Any change that alters frames/state needs baseline updates with a stated reason; each new feature wants guard scripts, vcxproj entries, option-id ranges and a `-pedantic -Werror` audit. Good discipline, but more process per bike feature.
- **SDL3 build friction:** modest (1m51s once, one missing `-dev` package, PKG_CONFIG_PATH for make targets), and upstream SDL2 is trivially available from apt. On Windows MSYS2 SDL3 packages exist but lagged (`update: true` needed).
- CI in a fork downloads T2000 and fails hard if unreachable; costs macOS/ARM minutes.
- Size: 62k LOC (+32%), including a 16 MiB lazily-built NTSC table, an installer with threads/processes, a launcher; more to port/disable for web.

Emscripten/browser: SDL3 has first-class Emscripten support and the engine is software-rendered (CPU-composed canvas, no GL requirement), which is good. Blockers I can see by reading, not tested: `installer.c` uses `SDL_CreateProcessWithProperties`/`SDL_CreateThread` and the system `curl` (must be compiled out for web, `WITH_NETWORK=false` covers only SDL3_net), the launcher/user-path code assumes a filesystem, SDL3_net has no web port, pthread/asyncify decisions for the main loop. Upstream SDL2 has a simpler known Emscripten path; a web build of modern-tyrian is likely a larger first-time port but keeps the harness (which is headless and could run under node).

Native BLE: independent of the SDL version in both cases. Either way BLE (BlueZ/D-Bus, WinRT, CoreBluetooth, or a library such as SimpleBLE) lives behind a small C interface that publishes the latest (watts, rpm) into an atomic snapshot read once per game tick. `bike.c` here is that seam: scripted CSV in regress, live snapshot in play, same deterministic quantisation. SDL3 threads/atomics/properties are slightly nicer; modern-tyrian's own `SDL_Process`/thread usage shows precedent for background workers. On web, Web Bluetooth would feed the same seam from JS.

Recommendation: modern-tyrian is the stronger base **if** we accept being a downstream of its cadence (pin to a release tag such as v0.7.0, rebase on tags only, keep our changes in new files plus a few small hooks: the `bike.c` module, the 3-line `tyrian2.c` refill hook, HUD function). The harness alone is worth it for a feature whose correctness is "same trace, same hashes." Choose upstream SDL2 instead only if keeping a near-zero diff against upstream matters more than testability, or if the browser build is the first deliverable. Open items for Phase 1: decide generator-scale semantics (equipped vs constant), what happens in arcade/2-player (power pinned to 900), pause/disconnect behaviour of the feed, and whether the state hash should always include `power` (one deliberate baseline update).
