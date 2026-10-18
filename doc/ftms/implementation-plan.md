# Bike-powered Tyrian: implementation plan (Phase 1 onward)

## Context

Phase 0 ([`doc/ftms/phase0-viability.md`](phase0-viability.md)) returned GO.

User decisions (2026-10-14):
1. The code base becomes **modern-tyrian**, pinned at `v0.7.0` (`edd39d1`).
2. Standalone transports (native BLE, sidecar, browser) are **parked**.
3. The live transport is the **Flywheel bridge client**.
4. Platforms are **Windows and Linux**. No macOS work.
5. The generator uses **mapping C**: `units/frame = max(F, G × (0.5 + effortNorm(W̄/FTP)))`.
   - It will need calibration later, so its constants must be tunable.
   - **FTP is entered manually in an in-game menu.** *(Extended: FTP can now come from the Flywheel bridge's rider, measured by its calibration test; the typed value remains, see design.md 3a.)*
   - **Surge options are user-configurable in the menu.**
6. **Flywheel becomes GPL** so its code can be ported. *(Superseded: Flywheel is MIT licensed, which is compatible with this GPL fork.)*
7. Start/Resume (0x07) is not a concern. The bridge already does the FTMS handshake.
8. The Tyrian 2.1 data stays **bundled**, as modern-tyrian already does.

The outcome is a modern-tyrian-based OpenTyrian in which pedalling through the Flywheel bridge drives the ship's generator. It has an in-game Bike menu, a HUD readout, configurable surge bonuses, and deterministic regression tests. With the feature off, it is byte-identical to stock.

### Answer to "should I re-fork?"

**No re-fork is needed.**
- `Dologan/opentyrian-ftms` is a GitHub fork of `opentyrian/opentyrian`. modern-tyrian shares that history up to `5a9d8da`, so we can bring its history straight into our repo with git.
- GitHub lets an account hold only one fork per fork network. If modern-tyrian sits in the same network, a second fork would be refused unless this repo were deleted first. I could not check modern-tyrian's fork status from this session.
- The only things a re-fork changes are cosmetic: the "forked from" label and the default target for PRs.

**Recommendation:** keep this repo and rebuild the branch on modern-tyrian `v0.7.0`. Later, when you approve it, point `master` at the new history. If you ever want the label, or easy PRs to vittau, you can re-fork from vittau/modern-tyrian then, after renaming or archiving this repo.

### Rule: every change can be switched off (user requirement, 2026-10-14)

Every change in this plan is a separate setting in the in-game menu. With all of them off, the game plays exactly as stock modern-tyrian.

- **Settings:**

  | Group | Settings |
  |---|---|
  | **Bike** | Bike power (generator), and each surge effect |
  | **Controls** | Auto-fire, air-mouse mode, touch controls, remote button mapping, pointer sensitivity |
  | **HUD** | The bike readout |

- **A master "Bike & Controls" switch** sits above them and overrides them all. Turning it off restores stock behaviour without losing the individual choices.
- **Everything defaults to off**, so a fresh config or a regress run behaves exactly like stock.
- **Settings can change mid-game** through the in-game options, and take effect from the next frame.
- **Testing:** a single regress guard runs every existing baseline with all settings explicitly off. Each setting also gets its own on-case.

### Ground rules inherited from modern-tyrian (AGENTS.md)

- **Classic 2.1 stays byte-exact.** The Modern layer is presentation-only. So the bike feature is **opt-in and off by default**, and every existing baseline must stay unchanged.
- Bike-driven state is allowed to change gameplay only while enabled. Its regress cases use their own case set.
- Bookkeeping for new code:
  - New `.c` and `.h` files must be added to `visualc/opentyrian.vcxproj` and `.filters` (CRLF).
  - New CLI options take a new id range, **440–459**, recorded in the AGENTS table.
  - Long option names must not be prefixes of other options (getopt prefix trap).
  - Run the gcc `-pedantic -Werror` syntax audit per touched file.
  - Use `%lu`, not `%zu`.
  - Every change gets entries in `docs/MODERNIZATION.md` and `docs/CHANGELOG.md`.
- Port fork logic by understanding it, never by pasting files, and cite the fork commit.

## Model allocation

| Role | Model | Used for |
|---|---|---|
| Orchestrator | Opus (main session) | Sequencing, integration, verification, commits and pushes |
| **Advisor** | Opus subagent | Core API design review (gate G1), the SWAR merge if it gets messy, a code review at the end of each phase (`/code-review`), balance sanity check |
| Implementer | Sonnet | Anything touching game state, C networking, menus, the regress harness |
| Helper | Haiku | Licence files, data-conversion scripts, docs, CI tweaks, read-only surveys |

Each agent works in its own git worktree under the scratchpad, on a task branch. The orchestrator merges into `claude/focused-ritchie-ahh2sn` after verification. Each task's prompt names its files, its tests and its "done" criteria.

## Work breakdown

### Phase 1: Foundation (rebase and licence)

**T1.1 Rebuild the branch on modern-tyrian v0.7.0** (Sonnet; Opus advises if the SWAR merge fights back)
- Create the branch from `v0.7.0`.
- Port our 9 upstream commits by intent, citing each original commit:
  - the visualc UTF-8 manifest (applies cleanly)
  - logFatal va_list reuse
  - toupper UB
  - the audio-init guard
  - the math.h include
  - typo fixes
  - SWAR background drawing (the real merge, 0.5–1 day)
  - README items: skip
- Re-apply the 7 Phase 0 doc commits (`doc/ftms/**`) unchanged.
- Force-push the designated branch. This is allowed because we created it; the old history stays reachable through the existing upstream-based commits.
- Done when: `make debug` passes; full `make regress` passes 207/207; the 2000 regress passes if the data fetch works. SWAR is perf-only, so the baselines must not move.

**T1.2 Make CI work in our fork** (Haiku)
- Make sure the workflows run on our branch.
- Make the Tyrian 2000 fetch non-fatal if camanis.net is down, or keep it if it's reliable.
- Skip release jobs on forks.
- Done when: the GitHub Actions runs on the pushed branch are green, checked through the GitHub MCP tools.

**T1.3 Licence Flywheel as GPL-2.0-or-later** (Haiku, in `Dologan/flywheel` on branch `claude/focused-ritchie-ahh2sn`)
- Why 2.0-or-later: it matches OpenTyrian, so ported code combines cleanly. GPL-3.0-only would not.
- Add `LICENSE` (GPLv2 text) and a licence section in the README.
- Add SPDX `license` fields to `bridge/pyproject.toml` and `game/package.json`.

### Phase 2: Bike core (transport-independent)

**G1, design gate** (Opus advisor). The advisor writes `doc/ftms/design.md`, about one page, before T2.x starts.
- Module split:
  - `src/bike.[ch]` — the sample mailbox and source selection: off, scripted CSV, bridge.
  - `src/bike_effort.[ch]` — pure C99 with no SDL. Holds the EMA, effortNorm, staleness and surge meter.
  - `src/bike_rules.[ch]` — the generator mapping, gating and surge effects.
  - `src/bike_ws.[ch]` — the bridge transport.
- A settings struct, persisted and editable in the menu.
- The tick contract: one call per gameplay frame, with `dt` taken from the actual frame period (`frameCountMax` ticks), never a hard-coded 35.
- Gating predicate: enabled AND in a level AND NOT any of twoPlayerMode, onePlayerAction, isNetworkGame, playDemo, recordDemo, galagaMode.
- Determinism contract: under regress, inputs are a pure function of the tick.

**T2.1 Port Flywheel's effort engine to C** (Sonnet)
- Source files: `game/src/core/effort/effort-engine.ts` and `filters.ts`.
- Port the 3 s EMA, the piecewise `effortNorm` (0→0.10, 0.40→0.15, 0.75→0.50, 1.10→1.00), the staleness timeout (3 s), the moving/stop detector, and the surge meter / overdrive-ready event.
- Golden fixtures:
  - Write a small Node script (node 22 is available) that runs the real TS engine over Flywheel's Q200B raw rides and synthetic traces, and emits expected outputs.
  - The C unit test, `test/unit/bike_effort_test.c`, must match within a tolerance.
  - Wire it the way `tools/check_game_rules.sh` is wired.

**T2.2 Generator hook and gating** (Sonnet)
- Replace the refill at `src/tyrian2.c:~1196-1206` with `bike_power_add()` when gated on, using an exact remainder accumulator. When off, the code path is untouched.
- Mapping C uses tunable constants: floor F (default 8), base offset (0.5), cap.
- Link loss: blend back to the stock `powerAdd` over about 2 s, never starving the rider.
- The weapon simulator in the upgrade screen (`src/game_menu.c:3649`) keeps stock behaviour, as AGENTS warns.
- Use the spike's `regress-bike.patch` as the starting point.

**T2.3 Regress harness for the bike** (Sonnet)
- Productionise `--regress-bike=<csv>`, using ids from 440 and an exact-name check.
- Fold `power` and the bike state into the state hash only while a feed is active.
- Add a `tools/check_bike.sh` guard and a `make regress-bike` target. Add one case to the quick tier.
- Cases:
  - flag off still matches the baseline
  - constant 150 W
  - ramp
  - dropout (staleness)
  - stop
  - FTP edge values
  - each surge option
  - two rides converted from Flywheel's Q200B `rides/*.raw.jsonl`, using a Haiku-written converter run against the Phase 0 C parser (T2.3a, Haiku)

**T2.4 Settings, config and CLI** (Sonnet)
- Add an `[ftms]` section to `opentyrian.cfg` via `config_find_or_add_section`, following the Depth pattern at `config.c:406-412`. Keys:
  - enabled
  - bridge host
  - bridge port
  - FTP
  - floor / strength
  - surge-option toggles
  - HUD readout on/off
- CLI overrides: `--ftms-bridge=HOST[:PORT]`, `--ftms-ftp=W`, `--ftms-off`.
- Regress pins all of this off regardless of the file, as `regress.c:1356` does for Depth.

**T2.5 In-game Bike menu** (Sonnet; Opus reviews the UX)
- *Superseded:* the row list below is the original plan. The shipped menu is a flat, scrolling list of 19 rows (18 settings + Done), including FTP Source; see `design.md` §3 and `src/bike_menu.c`.
- Add a new `SETUP_MENU_BIKE` submenu, using the e994e9a Depth row as the template (`src/opentyr.c` menu tables). The Graphics menu is full.
- Rows:
  - Bike: On/Off
  - **FTP: a new left/right stepper**, 50–500 W in steps of 5, hold to accelerate
  - Bridge: a host:port text box, reusing the save-name text entry at `mainint.c:~2176` with a longer limit
  - Surge options: one row each
  - A master "Bike & Controls" switch, plus the Bike power toggle. The Controls toggles from Phase 3b sit in the same submenu.
  - Difficulty floor / strength: a stepper
  - Status: read-only, showing Connected / Connecting / Off, plus the last watts and cadence
- If cheap, also add a Bike entry in the in-game options table (`game_schema.h:114`, `options_rows()`), so FTP and surge can be changed mid-campaign.
- Verify with `--regress-snapshot` PNGs.

**T2.6 HUD** (Sonnet)
- Modern HUD: harden the spike's 19-line `hud_draw_bike()` into watts, cadence, a link indicator and a surge meter.
- Classic HUD: a minimal link and watts indicator drawn only when the bike is on, so Classic baselines with the bike off are unaffected.

### Phase 3: Configurable surge effects

**T3.1** (Sonnet; Opus advisor sanity-checks balance)
- Each effect is its own menu toggle, and all are fed by the T2.1 surge/overdrive event:
  - **Superbomb** — +1 `superbombs` on overdrive-ready
  - **Sidekick charge** — faster `charge_ticks` while surging
  - **Special recharge** — shorter `specialWait` while surging
  - **Shield regen** — a faster regen interval that scales with effort
- Each effect gets a deterministic regress case.
- Defaults: superbomb on, the rest off, pending calibration.

### Phase 3b: Controls for riding (air mouse and touchscreen)

The rider's hands are on the handlebars, in front of a Surface touchscreen, possibly holding the G60S Pro air mouse.

**What the survey found:**
- **The remote** (per Flywheel's `docs/hardware-results.md:70-87` and `bindings.ts`) sends:
  - D-pad as arrow keys
  - **OK as a left click**
  - **Back and Home both as a right click**
  - Menu as the `ContextMenu` key
  - the media keys
  - gyro motion as relative mouse movement
- **Steering by pointer already exists** (`keyboard.c:362` → `mainint.c:3732-3736`). It is disabled for player 1 because `inputDevice` defaults to `{1,2}` (`config.c:223`). So by default OK does not fire and Back does nothing during play.
- **Touch** has no handling at all. SDL3 turns taps into left clicks, so taps already work in menus. Menus act on the row under the pointer, but the air mouse has no visible pointer.
- **There is no auto-fire option.** `--constant` is a cheat, not an option.
- **Typing a name** (save names, high scores) needs a keyboard, but Enter on a blank name is accepted.

**Tasks** (all behind the switches above):
- **T3b.0 Input probe** (Haiku builds it, you run it on the Surface):
  - A `--input-probe` diagnostic screen that logs every SDL3 event: keys with scancodes, mouse buttons and motion rate, finger events, gamepad.
  - It answers what Windows/SDL3 actually delivers from the G60S Pro (are media keys delivered as scancodes?) and from the touchscreen.
  - Results go into `doc/ftms/controls-probe.md`. The tasks below make conservative assumptions until then.
- **T3b.1 Auto-fire as a toggle you switch during play** (Sonnet; the Opus advisor checks the energy interplay):
  - **Menu setting:** "Auto-fire: Off / Toggle" decides whether the feature is available at all. Off means stock behaviour.
  - **During play:** a single press switches auto-fire on or off at any moment, with no menu trip. The key choice waits for the probe, but candidates are the remote's OK button, a media key, a gamepad button, or a touch tap zone. A Modern-HUD indicator shows `AUTO` and whether firing is currently held back.
  - **Energy:** every shot draws generator power (`shots.c` `poweruse`). Constant fire therefore keeps the pool near empty, which starves shield regen (`power > shieldT` is required). An optional setting, **"Auto-fire reserve"** (Off / 25% / 50%), holds auto-fire back while `power` is below the reserve, so shields can still recharge. Manual fire always ignores the reserve.
  - **Hook:** latch `button[0]` in `mainint.c` after the input blocks (`~3737-3768`), before network packing, and only while the bike gate is open, so network and demo play are never affected.
    - Never via a new `keySettings` slot, because the demo bit layout depends on the existing slots.
    - The in-play toggle key is a separate constant or a binding in our `[ftms]` config.
  - **Regress cases:** toggle on and off mid-level, and the reserve behaviour while generator income is low.
- **T3b.2 Air-mouse mode** (Sonnet):
  - A Modern-only player-1 device setting that enables the pointer steering path, with a sensitivity scale at `mainint.c:3735-3736`. Keep the ±30 clamp.
  - Optional smoothing, porting Flywheel's `one-euro.ts`, which is written but unused there.
  - **Button translation, only while air-mouse mode is on** (`keyboard.c:379-399`):
    - In menus: OK → Enter (act on the focused row, not pointer position) and right click → Esc.
    - In play: OK → fire (or toggle auto-fire), right click → pause / in-game menu, ContextMenu → superbomb or "release surge".
  - Media keys follow the probe results: next/previous → change rear mode or the sidekick, play/pause → pause.
- **T3b.3 Touch controls** (Sonnet; Opus advisor on the interaction design):
  - Handle `SDL_EVENT_FINGER_*` in `handleSdlEvents` (`keyboard.c:~204`) and map positions with `mapWindowPointToScreen`.
  - **Relative drag-to-steer** anywhere on the playfield feeds `mouseXC/YC`. Keep SDL's tap-as-click so menu taps still work.
  - **Tap zones on the Modern side panels**, with no virtual stick: pause/menu, superbomb, change fire, sidekicks.
  - Auto-fire is the expected companion.
  - A two-finger tap opens the in-game menu.
- **T3b.4 Remote-only navigation pass** (Haiku audit, then Sonnet fixes):
  - Walk every screen with only the remote's key set: title, setup, shop, data cube, save/load, in-game menu, high-score/save-name entry.
  - Fix dead ends, for example: OK = Enter on the name prompt accepts a default name; Back = Esc.
  - Verify with `--regress-snapshot`.
- **Tests:** these input paths change game state only when switched on. Each gets a regress case with injected events (following `--regress-stick` / `joystick_inject_stick()`), plus an all-off case to show stock is unchanged.

### Phase 4: Bridge transport

**T4.1 Bridge WebSocket client on SDL3_net** (Sonnet)
- Port `doc/ftms/phase0-spikes/route-a/bike_ws.c` from SDL2_net to SDL3_net. SDL3_net is fully asynchronous:
  - `NET_ResolveHostname` + `NET_GetAddressStatus`
  - `NET_CreateClient` + `NET_GetConnectionStatus`
  - `NET_ReadFromStreamSocket` / `NET_WriteToStreamSocket`
  - This removes the Phase 0 blocking-connect hazard.
- Add a small SHA-1/base64-free handshake. A fixed key is fine, but verify `Sec-WebSocket-Accept` loosely.
- Add a reconnect backoff.
- Harden the field scanner against reordering, nulls and `v != 1`.
- Poll from the per-frame bike tick, not from the event loop. Under regress it is never opened.

**T4.2 Bridge integration check** (Sonnet)
- Write `tools/check_bike_bridge.sh` (local only, not CI, because it needs Python and uv).
- Steps:
  1. Start `flywheel-bridge run --source replay <Q200B ride>`.
  2. Run the game headless with `--ftms-bridge`.
  3. Assert that the power refill tracks the watts in the bike log.
  4. Kill the bridge and assert the blend back to stock.
  5. Restart the bridge and assert it reconnects.

**T4.3 Launch helpers** (Haiku)
- (Historical; the Windows `.cmd` was replaced by `Play with Flywheel.cmd`.) `tools/ftms/play-with-bike.cmd` and `.sh` took a Flywheel checkout path. They start the bridge (`uv run flywheel-bridge run`), wait for `/healthz`, launch the game with `--ftms-bridge=127.0.0.1:8787`, and stop the bridge on exit. Model them on `flywheel/launcher/flywheel.sh`.
- Write a README "Playing with an exercise bike" section, and add a `docs/MODERNIZATION.md` entry and a `docs/CHANGELOG.md` entry.

### Phase 5: Review and calibration readiness

- **T5.1** (Opus advisor): run `/code-review` on the whole diff against v0.7.0, then fix the findings (Sonnet).
- **T5.2** (Sonnet): a calibration aid. `--ftms-log=FILE` writes a per-second CSV of watts, effort, refill, power and refused shots. Add a hardware playtest checklist to `doc/ftms/` for your Surface + Q200B session.

## Sequencing and parallelism

```
T1.1 ──┬─ T1.2 ─────────────────────────────────────────────┐
       │                                                     │
T1.3 (parallel, flywheel repo)                               │
G1 ────┼─ T2.1 (pure C, parallel with T1.1) ─┐               │
       └─ T2.2 → T2.3 (+T2.3a Haiku) ─────────┼─ T3.1 ── T5.1 ┴─ push
            T2.4 → T2.5 → T2.6 ───────────────┤
            T4.1 → T4.2, T4.3 ────────────────┤
            T3b.0 (probe; user runs on Surface) → T3b.1–T3b.4 ┘
```

The controls work (Phase 3b) needs T2.4 (settings) and T2.5 (menu framework) first. T3b.1 (auto-fire) does not depend on the probe results.

- **Can start immediately:** T1.1, T1.3, G1 and T2.1, which depends only on G1.
- **After T1.1:** T2.2 and T2.4 can run in parallel, because they touch different files. Merge conflicts are expected only in `params.c`, `config.c` and `opentyr.c`, and the orchestrator merges those in order.
- **Pushes:** each task gets one validated push, made after the orchestrator has run `make debug`, `make regress-quick`, `make regress-bike` and the unit tests. The full `make regress` runs at each phase end.

## Verification (end to end)

- **Bike off:** full `make regress` passes 207/207 and `regress-2000` is green. Classic stays byte-exact.
- **Bike on, deterministic:** `make regress-bike` passes, and every case is reproducible across two runs.
- **Effort engine:** `bike_effort_test` matches the TS golden output from the real Flywheel engine on the Q200B rides.
- **Live bridge:** `tools/check_bike_bridge.sh` passes against `flywheel-bridge --source replay`. This covers power tracking watts, the blend to stock on link loss, and reconnect.
- **Visual:** `--regress-snapshot` PNGs of the Bike menu, the Modern HUD readout and the Classic indicator, reviewed by the orchestrator.
- **CI:** GitHub Actions green on Linux and Windows (MSYS2) for the branch.
- **Your hardware session** (follow-up, using the T5.2 checklist): Surface + Q200B via Flywheel's bridge.
- **All toggles off:** the full regress suite matches stock baselines with every setting explicitly off.
- **Each toggle on:** each toggle has a deterministic on-case, and switching it mid-game changes behaviour from the next frame.
- **Controls:** injected-event regress cases for auto-fire, air-mouse steering and button translation, and touch drag and tap zones.
- **Probe:** `doc/ftms/controls-probe.md` records what the G60S Pro and the touchscreen really send on Windows. You run the probe on the Surface.

## Parked (not in this plan)

- Native BLE (route B), the sidecar (route C) and the browser build (route D).
- Pointing `master` at the new history: this needs your explicit OK, since it rewrites the default branch.
- Opening a PR: only if you ask.

## Status (2026-10-14, end of session)

Every task in this plan is done, merged and pushed.
- CI is green on Linux, macOS and Windows at `5c8249c`.
- Locally, the full regress suite passes 207/207, `tools/check_bike.sh` passes (73 digests: 64 cases + 9 snapshots, after the FTP-source work), and `tools/check_bike_bridge.sh` passes against the Flywheel replay bridge.

What needs the Surface + Q200B:
- **Input probe:** run `--input-probe` and record the results in `doc/ftms/controls-probe.md`.
- **Playtest:** follow `doc/ftms/playtest-checklist.md` with `--ftms-log`.
- **Tuning:** the mapping C constants, the surge frequency (this is Flywheel's own tuning), the touch drag gain, and the auto-fire toggle keys.

Open, low priority:
- Regress-only flags are accepted in normal play.
- The launcher ignores the touch exclusion.
- There is no letter picker for name entry.
- There is no confirmation on the in-game Quit.
- Pointer smoothing (one-euro) was not ported.
