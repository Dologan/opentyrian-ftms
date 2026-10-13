# FTMS-driven OpenTyrian: Phase 0 viability report

Date: 2026-10-13. Status: Phase 0 complete. **Verdict: GO.**

The question was whether OpenTyrian can be "fused" with the Flywheel project, so that live
exercise-bike data drives gameplay over the BLE FTMS protocol. Watts would feed Tyrian's ship
generator, which is the counterpart of Flywheel's energy meter. The question is mostly about the
infrastructure, and secondarily about game design.

Short answer: the fit is good at every level we tested. The gameplay hook is a handful of lines
at one call site. The data rate (about 1 Hz) is trivial for a 35 fps single-threaded loop. Four
transport routes were built far enough to show they work: bridge client, native BLE, sidecar and
browser. The real choices are now product choices, listed in section 7, not feasibility
questions.

All supporting work is under [`phase0-spikes/`](phase0-spikes/). Each spike has a `RESULTS.md`
with commands, outputs and caveats. Spike code is throwaway and is not part of the main build.

---

## 1. Method

- **Exploration and research** (Haiku agents): code surveys of both repos and of the modern-tyrian
  fork, plus web research on BLE libraries, sidecar freezing, Web Bluetooth and existing ports.
- **Hands-on spikes** (Sonnet agents): native build, bridge client, modern-tyrian harness, C FTMS
  parser and BLE probes, Emscripten build, and generator calibration.
- **Fact verification**: a dedicated agent re-checked every claim the decision depends on against
  the source, and the main session re-read the critical lines.

No real bike or BLE adapter was available in this environment. Every live-data result uses
Flywheel's simulator or replays of the two recorded Q200B rides.

## 2. Verified facts that the design depends on

All references are to this repo's `src/` (upstream OpenTyrian at `624e2f2`). modern-tyrian has
the same logic at slightly different lines.

| Fact | Where |
|---|---|
| Generator refill is `power += powerAdd`, capped at 900. Power is pinned to 900 when `twoPlayerMode \|\| onePlayerAction`. | `tyrian2.c:1112-1120` |
| `powerAdd = powerSys[generator].power` is the only write. It refreshes at level start, on the item screen and on cheat ship swaps. | `varz.c:322`; callers `tyrian2.c:751`, `game_menu.c:245,571`, `mainint.c:2951,2994` |
| Every weapon shot draws `poweruse`. If power is short the shot is refused and retried next frame, so fire rate degrades gracefully. | `shots.c:321-323` |
| Shield regen costs `tpwr*20` every 15 frames. | `tyrian2.c:825,1072-1086` |
| `power = 0` at every level start. The upgrade screen simulates the same refill. | `tyrian2.c:747`; `game_menu.c:2248,3225` |
| Gameplay runs at 34.78 fps: 2 ticks of 14.375 ms (`0x4300` divisor). Other speed settings give 48.6, 23.2 and 14.0 fps, so per-frame scaling must use the actual frame time. | `nortsong.c:46-58`, `config.c:596-599` |
| The game is single-threaded (only the SDL audio callback has its own thread). Every loop passes through `handleSdlEvents()`, which is a natural non-blocking poll point. | `keyboard.c:176-215, 414-469` |
| There is no "in gameplay" flag. Pause, menus and shop are separate blocking loops, so gating needs a new flag set at level start and cleared on `endLevel`. | verification report |
| Network play serialises input deltas, and demos record keys. Bike input must stay local and be off in network and demo modes. | `mainint.c:3298-3301, 3439, 3567-3594` |
| `powerSys[].speed` and `ships[].spd` are loaded but never read. | `episodes.c:157,169` |
| CLI options use the `Options[]` table, and the next free long-only id is 258. Config sections round-trip through `config_find_section`, following the `joystick.c:377-427` pattern. | `params.c:45-68`, `config.c:248-355` |

Flywheel side (`/home/user/flywheel`):
- The bridge serves `ws://127.0.0.1:8787/ws` with JSON `v:1` telemetry (`shared/protocol/telemetry.schema.json`).
  There is no auth or handshake. It accepts `sim`, `replay` and `control` client messages.
- The real Q200B notifies at about 0.91 Hz. Its power is probably estimated from cadence and resistance.
- Stops are detected 5–18 s late (`docs/hardware-results.md`).

## 3. Spike results

### 0.2: Native build baseline ([route-a/RESULTS.md](phase0-spikes/route-a/RESULTS.md))
- `make -j4` takes 18 s and `make debug` (with `-Werror`) passes.
- Zero warnings; the binary is 635 KB.
- It boots headless with the dummy SDL drivers. No blockers.

### 0.3: Route A, OpenTyrian as a client of the existing Flywheel bridge ([route-a/](phase0-spikes/route-a/))
- **Client:** a thread-free C99 WebSocket client on SDL2_net, 320 lines of code. It handles the
  upgrade handshake, masked frames, ping/pong and a field scanner, and polls with
  `SDLNet_CheckSockets(set, 0)`.
- **Bridge simulator:** cadence 0/60/90/110 rpm gave about 0/100/186/245 W. Messages arrived at
  0.91 Hz with 5.6 ms mean latency on loopback. Replay of a Q200B fixture also worked.
- **Gameplay, headless on the attract demo:**
  - At 0 W the power bar stayed at 0 and shots were refused.
  - At 189 W power refilled at 1302 units/s, and above about 100 W no shots were refused.
- **Bridge stop and restart:** if the bridge is killed, power drains after 3 s. The game reconnects
  1–2 s after the bridge restarts, and the frame rate is unaffected.
- **Hazard:** SDL2_net `connect()` blocks. It returns instantly on refused localhost, but froze the
  game for 134 s against a blackholed address. Production needs an asynchronous connect.
- **Patch size:** +557/−1 over 9 files, and the hooks in existing files total about 40 lines.
- **Estimate:** 2–4 days to make it production-quality.

### 0.0: modern-tyrian as a base ([modern-tyrian/RESULTS.md](phase0-spikes/modern-tyrian/RESULTS.md))
- **Build:** static SDL3 and SDL3_net via `make_linux.sh` took 1m51s once. The game builds in 15 s
  with zero warnings. The only friction was one missing apt package.
- **Regression suite:**
  - `make regress-quick` passed 16/16 in 28 s.
  - Full `make regress` passed 207/207, both unmodified and with the bike patch present but off.
- **Prototype feed:** a throwaway `--regress-bike=<csv>` feed (`src/bike.[ch]` plus a hook at the
  refill) was tested with `tools/check_bike.sh`:
  - With the flag off, baselines are byte-identical.
  - A constant 150 W produces frames identical to stock.
  - The same ride trace run twice gives identical frames, state hashes and logs.
  - The ride differs from baseline in 499 of 600 frame lines.
  - This shows that bike-driven gameplay can be **regression-tested deterministically**.
- **HUD:** a 19-line `hud_draw_bike()` shows `190W 88RPM` next to the existing PWR bar
  ([screenshot](phase0-spikes/modern-tyrian/modern-hud-bike-on.png)).
- **Porting our fork's 9 commits:**
  - 1 applies cleanly, 3 need small hand ports, 2 README commits can be skipped, and 2 need
    porting by intent.
  - The SWAR background speedup is a real merge, about 0.5–1 day.
  - Several of these commits fix bugs that are still present in modern-tyrian.
- **Fork activity:** 283 commits since the fork point, in bursts of 35–113 a day. There is one
  maintainer and the work is heavily AI-assisted. Releases are tagged v0.1.0 to v0.7.0.

### 0.4: Route B, native BLE in the game ([route-b/RESULTS.md](phase0-spikes/route-b/RESULTS.md))
- **Parser:** a C99, dependency-free FTMS parser (`ftms_parse.[ch]`) covers Indoor Bike Data,
  the Cycling Power fallback, control point and feature.
  - It passes **all 338 checks** from Flywheel's `ftms-vectors.json`. These were re-run by the main
    session.
  - It decodes all 527 packets of the two raw Q200B rides exactly as the Python bridge does.
- **Libraries:** there is no suitable third-party BLE library.
  - SimpleBLE: MIT until late 2023, then GPLv3 or commercial, then BUSL-1.1 from v0.9. BUSL is
    incompatible with shipping inside a GPL-2.0+ binary. It is also C++ and pulls in libdbus at
    runtime.
  - btleplug: Rust only, with no C API, and it also needs libdbus.
  - gattlib: Linux only and stale.
  - SDL2 and SDL3 have no BLE GATT API.
- **Viable design:** write thin native backends.
  - **Linux:** BlueZ over a `dlopen`'d libdbus. The probe is 17 KB, links only libc, keeps the
    self-contained binary and degrades gracefully when D-Bus is absent.
  - **Windows:** a small C++/WinRT file behind a C header. MSYS2 ships `cppwinrt`, but compiling
    it with GCC is **untested**. This is the main risk.
  - **macOS:** an ObjC CoreBluetooth shim, optional.
- **Architecture:** a mutex-guarded latest-sample mailbox that merges FTMS fragments, and a
  connection state machine mirroring Flywheel's `link.py` (scan 0x1826, connect, subscribe 0x2AD2,
  Request Control/Start, silence watchdog, backoff).
- **Estimate:** about 3.2–4k lines. That is roughly 1.5 weeks for Windows and Linux, or about
  3 weeks with macOS.

### 0.5: Route C, bundled bridge sidecar ([research/ble-and-sidecar.md](phase0-spikes/research/ble-and-sidecar.md))
- **Linux build:** a PyInstaller one-file build of the real bridge is 14.3 MB. `/healthz`
  answered within about 1 s and it exits cleanly on SIGTERM.
- **Windows:** the freeze is **untested**. It needs the `winrt-*` packages collected, and the bleak
  COM-apartment caveat applies.
- **Licences:** bleak, aiohttp and PyInstaller are all compatible (MIT, Apache, and GPL with the
  bootloader exception).
- **Costs:**
  - SmartScreen blocks unsigned executables, so code signing (about $10–300 a year) becomes a
    real consideration.
  - SDL2 has no process API, while SDL3 has `SDL_CreateProcess`.
  - The child process needs Job Object handling on Windows, and exit-on-stdin-EOF.

### 0.6: Route D, browser build ([route-d/RESULTS.md](phase0-spikes/route-d/RESULTS.md), [research/web-route.md](phase0-spikes/research/web-route.md))
- **Build:** unmodified upstream sources build with emcc + `-sASYNCIFY` and need **no source
  changes**. The only essential extra is `-sSTACK_SIZE=1MB`, because `JE_loadMap` keeps about
  64 KB on the stack.
- **In headless Chromium:**
  - Menus and level 1 play at about 34.8 fps.
  - Saves persist via IDBFS.
  - A JS-to-C bike sample path works with synthetic FTMS packets.
  - The real Web Bluetooth connect code is written but has never run against a device.
- **Size:** wasm 432 KB gzip (ASYNCIFY adds about 30%), data 4.75 MB gzip.
- **Prior art:** midzer/opentyrian has a live ASYNCIFY build.
- **Web Bluetooth support:**
  - Chrome and Edge on Windows, macOS, ChromeOS and Android.
  - Linux only behind a flag.
  - No Firefox, Safari or iOS.
  - `requestDevice` needs a user gesture.
  - Reconnecting without the device chooser (`getDevices()`) is unconfirmed.
- **Estimate:** 3–6 days for a production-quality web build.
- **Embedding in Flywheel:** it could also run inside Flywheel's browser shell, which already has
  the telemetry and would push samples directly.
- **On modern-tyrian:** an equivalent web build has untested blockers: the installer's
  process/curl use, and SDL3_net.

### 0.7: Gameplay calibration ([calibration/ANALYSIS.md](phase0-spikes/calibration/ANALYSIS.md))
- **Generator output** (power units per frame, decoded from `tyrian.hdt`):

  | Generator | Units/frame |
  |---|---|
  | Standard MR-9 | 10 |
  | Advanced MR-12 | 14 |
  | Gencore Custom MR-12 | 19 |
  | Standard MicroFusion | 25 |
  | Advanced MicroFusion | 30 |
  | Gravitron Pulse-Wave | 50 |

  That is 348–1739 units/s.
- **Loadout economy:** typical loadouts can sustain continuous fire 78–86% of the time on the stock
  generator for their stage.
- **Recommended mapping** (Flywheel's "effort buys advantages, never punishes" rule), using `n` =
  Flywheel's `effortNorm` of the 3 s average watts divided by FTP:

  `units/frame = max(8, G × (0.5 + n))`, where `G` is the owned generator's stock output.

  - At 75% of FTP this gives exactly stock output, at 110% of FTP or above it gives 1.5×, and at
    zero effort it gives 0.6×.
  - Generator upgrades still matter and FTP normalises across riders.
  - Reference values for a 14-unit generator at 150 W: 19 at FTP 150, 11.9 at FTP 250.
- **Sparse data:**
  - Use a 3 s EMA and treat samples older than 3 s as "not pedalling".
  - The 900-unit tank already smooths 1 Hz input.
  - On link loss, blend back to the stock generator and never starve the rider.
- **Porting Flywheel's effort logic to C:** about 150 lines for the minimum (EMA, `effortNorm`,
  staleness), plus about 80 for stop detection and about 70 for surge.
- **Other hooks:** surge or overdrive could award a superbomb, speed sidekick charge and special
  recharge, and decouple shield regen. Do not touch scroll speed, because level events are keyed to
  position.
- **Disable for:** two-player, `onePlayerAction` (Arcade, Super Arcade, Super Tyrian), network,
  demo playback or recording, and Galaga mode.

## 4. Route comparison

Scores run from 1 (poor) to 5 (good). "Effort" means production-quality, from the spike estimates. Route C's figure is my own estimate, because no spike sized it.

| | A: bridge client | B: native BLE | C: sidecar | D: browser |
|---|---|---|---|---|
| Standalone (no Flywheel install) | 1 | **5** | 4 (two processes) | 5 (needs Chrome/Edge) |
| Effort | **5** (2–4 d) | 2 (≈1.5 wk Win+Linux) | 3 (≈1 wk incl. Windows freeze and signing) | 4 (3–6 d) |
| Technical risk | **5** (proven end to end) | 3 (WinRT toolchain untested) | 3 (Windows freeze and SmartScreen untested) | 3 (Web Bluetooth not yet run on a device; Linux flag) |
| Windows / Surface | 5 | 4 | 4 | 5 |
| Linux | 5 | 4 | 4 | 2 (flag) |
| Licence | 5 | 5 (own code) | 5 | 5 |
| Testable without hardware | 5 (bridge sim/replay) | 4 (parser and replay backend) | 5 | 3 |
| Reuses Flywheel code | 5 (whole bridge) | 3 (C ports of the parser and `link.py` logic) | 5 | 3 |
| Fits a modern-tyrian base | 5 (SDL3_net version) | 5 | 5 (`SDL_CreateProcess`) | 2 (untested blockers) |

**Key observation.** Routes are not exclusive. A transport-independent `bike` module (sample
mailbox, effort engine, generator mapping, gating, HUD) can be fed by any of them. The modern-tyrian
spike already has this shape: scripted CSV under regress, a live per-tick snapshot in play.

## 5. Code base decision: upstream vs modern-tyrian

| | Upstream (our fork as is) | modern-tyrian (pinned release tag) |
|---|---|---|
| Deterministic regression harness | none | **207 cases, per-tick state hashes, proven with a bike feed** |
| HUD for watts/cadence | Classic HUD only | Modern HUD with PWR bar, 19-line readout shown |
| Input | SDL2 joystick | SDL3 gamepad, analog movement, Steam Deck tested |
| Content | Tyrian 2.1 | Tyrian 2.1 + Tyrian 2000 (separate install) |
| Churn / maintenance | slow, stable | very fast, single maintainer, AI-generated |
| Windows build | `visualc/` + CI | MSYS2 UCRT64 CI (`visualc/` secondary) |
| Browser build (route D) | **works unmodified** | larger port (installer, SDL3_net) |
| Our 9 upstream fixes | present | about 1–1.5 days to port, several fix live bugs there |

**Recommendation: adopt modern-tyrian, pinned to a release tag** (v0.7.0 today) for the native
product.
- Keep our work in new files plus a few small hooks. Merge forward only at release tags.
- Offer our upstream bug fixes back to modern-tyrian.
- The harness is the decisive reason, because this feature's correctness is literally "same
  trace → same hashes". The Modern HUD and gamepad/Steam Deck support are bonuses.

Choose upstream instead **only if** the browser route becomes the primary product.

## 6. Overall recommendation

1. **Build the transport-independent `bike` module first** on modern-tyrian. It holds the effort
   engine port, mapping C, gating, `[ftms]` config and `--ftms-*` options, and the HUD readout.
   It is tested with scripted traces and the Q200B raw rides through the regress harness.
2. **First live transport: bridge client (A).** It is cheap and proven, and it gives real-bike
   playtesting with the existing Flywheel install within days. This is where gameplay gets tuned.
   It remains useful afterwards as a development and kiosk option.
3. **Standalone transport: native BLE (B).**
   - Gate it on a **one-day Windows spike**: compile a `BluetoothLEAdvertisementWatcher` scan with
     MSYS2 `cppwinrt`.
   - If that spike passes, build the Windows backend, then Linux BlueZ via dlopen. macOS is
     optional.
   - If it fails, try clang/MSVC for that one file. **Fall back to sidecar (C)**, which reuses
     the bleak stack we already trust on Windows.
4. **Browser (D): keep it as an optional later track.** It is viable and cheap on upstream and
   could embed in Flywheel's shell. It does not fit the modern-tyrian base without extra work, and
   Linux needs a browser flag.

## 7. Open decisions for the user

1. **Base:** adopt modern-tyrian (recommended) or stay on upstream?
2. **Standalone path:** native BLE (B) with sidecar (C) as fallback, as recommended, or prioritise
   the zero-install browser build (D), which would favour upstream?
3. **Bridge client first?** Accept the bridge client as the first live milestone, even though it
   needs Flywheel installed?
4. **Platforms:** Windows (Surface) and Linux confirmed. Is macOS in or out?
5. **Gameplay:**
   - Is mapping C acceptable (generator × FTP-relative effort, with a floor)?
   - Where does FTP come from: a config value, an in-game test, or import from a Flywheel profile?
   - Should surge award a superbomb?
6. **Licensing:** may the code derived from Flywheel (FTMS parser, effort engine) be licensed
   GPL-2.0-or-later in this fork? Flywheel currently has no licence file.
7. **Hardware checks needed** (Surface + Q200B):
   - Does the Q200B require FTMS Start/Resume (0x07) before it notifies?
   - Does WinRT reconnect a known device without a scan?
   - For route D: does `getDevices()` persist in kiosk mode?
8. **Tyrian data distribution:** the freeware licence says "play it all you want and share it with
   friends". That is fine for bundling the 2.1 data the way upstream and modern-tyrian do. Public
   web hosting deserves a second look.

## 8. Draft plan for Phase 1 onward (to refine once section 7 is answered)

| Phase | Content | Exit criteria |
|---|---|---|
| **1. Foundation** | Fork onto the modern-tyrian tag and port our 9 fixes. CI green in our fork, dropping the Tyrian 2000 download step if needed. Add `src/bike.[ch]` with scripted/replay sources, gating flag, `[ftms]` config and `--ftms-*` options, and the HUD readout. Add regress cases. | All baselines unchanged with the feature off. Bike cases are deterministic. |
| **2. Gameplay** | Port the effort engine (EMA, `effortNorm`, staleness, stop detection). Implement mapping C, link-loss blend to stock, the upgrade-screen simulation and FTP config. Tune against Q200B replays. Optionally add the surge → superbomb hook. | Tuned feel on replays. Regress cases cover stop, dropout and FTP edge cases. |
| **3. Transport A** | Port the bridge WebSocket client to SDL3_net with asynchronous connect, link-status UX and parser hardening. | Real-bike playtest on the Surface with Flywheel's bridge. |
| **4. Transport B** | Day-1 Windows `cppwinrt` spike. Then the Windows backend, the Linux BlueZ/dlopen backend, a device picker and a remembered device. The C parser from Phase 0 becomes production code with unit tests. Sidecar fallback if the spike fails. | Standalone exe connects to the Q200B on the Surface. Linux self-containment CI still passes. |
| **5. Validation & release** | Hardware sessions, tuning, packaging, and a README section on bike setup. | Tagged release. |
| *(opt.) D* | Browser build (upstream-based, or ported to SDL3), with Web Bluetooth or as an embedded Flywheel mode. | Plays in Chrome/Edge with a real bike. |
