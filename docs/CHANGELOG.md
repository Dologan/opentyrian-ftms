# Modern Tyrian — Changelog

Project history in chronological order, **newest to oldest**, combining the old journal of the general plan and the diary of the Tyrian 2000 track. The current state of the project is in [MODERNIZATION.md](MODERNIZATION.md); this file holds what changed, why, the commits, the user's decisions, the causes of bugs and the process changes.

Conventions:

- Entries that apply only to Tyrian 2000 carry the **[2000]** tag; entries without a tag apply to 2.1 or to both.
- "Worker" is the agent that implements a task; the coordinator (Claude) reviews, commits and pushes.
- Regression case numbers were those of the time; the current number is in [MODERNIZATION.md](MODERNIZATION.md) and in `AGENTS.md`.
- Orca run/task ids cited are from work sessions and are valid only as a historical reference.

---

## 2026-10-20

### Bike (FTMS): no power, no invented watts
- **Why:** the owner decided that a bike that reports no power must not get watts made up from its cadence. The old fallback (cadence times a curve times FTP) is removed from `bike_effort.c`.
- **Behaviour:** a fresh sample with `powerW` null leaves moving/stopped to the cadence and keeps `effort_w`, `effort_norm`, `baseline_w`, `surge`, `surge_meter` at 0; no surge starts and a running one ends (`BikeEffortSignals.no_power`). `bike.c` feeds the income blend the link as down for such samples (the existing link-loss path, no new one), so the refill is the stock one. The Modern HUD and the Classic readout show "no power" instead of watts; the gauge has no fill and no threshold marker.
- **Golden that changed:** only `nopower` (the one feed with a bike without power; effort, baseline, norm, surge, surge meter, surging and events columns now all 0 while connected; the tail after the silent stop is the unchanged stale path), plus its line in `c_digests.txt`. The other 13 goldens and digests are identical. New unit check `check_no_power` in `test/unit/bike_effort_test.c`.
- **Game digests:** no `test/bike/regress.txt` label feeds a null power (only the unit-level `nopower` feed does), so none is expected to move; to be confirmed with `make regress-bike` where SDL3 is available.

### Bike (FTMS): the surge rule is the fork's own; surge keys; 80 % default
- **Why:** effort and surge are game mechanics, so each game owns them. Flywheel moved its effort data into its web game (its `a34faa5`) and no longer shares it. This fork stops depending on Flywheel for effort data.
- **Test data:** the feeds and goldens moved from the vendored `test/bike/feeds` and `test/bike/golden` to the fork-owned `test/bike/effort/{feeds,golden}` (copied unchanged). `vendor.py` now vendors only the protocol fixtures (`test/bike/protocol/{tolerance,rider}`, 53 files); `trace_to_feed.py` was removed here (nothing uses it; it stays in Flywheel). `test/bike/golden/c_digests.txt` stayed there until it moved to `test/bike/effort/c_digests.txt` (same content, moved with `git mv`).
- **Generator:** `tools/bike/gen_effort_golden.sh` rebuilds the goldens from this repo's C engine (`--gen-golden`, `--dump-effort` in `test/unit/bike_effort_test.c`). On the unchanged engine it reproduced the copied TypeScript goldens within the tolerances on all 14 cases (worst effort_w 0.002 W, norm 0.00003, meter 0.00002, surge 0.00005, same transition and event ticks), so the goldens are now its output. Run it only after an intentional engine change.
- **Surge rule:** `surge_min_pct` default 60 % to 80 % of the FTP; `surge_ratio` stays 1.20. Both are now `[ftms]` keys (`surge_ratio` 1.05 to 2.00 step 0.05, `surge_min_pct` 0 to 100 step 5) with two Bike menu rows, "Surge Ratio" (`1.20x`) and "Surge Min Power" (`80 %`); the menu has 21 rows. The detector, the HUD threshold marker and the gauge scale all read the configured values.
- **Goldens that changed** (all surge cases): `ramp0_300`, `surge_ftp500`, `nopower`, `q200b-ride1`, `q200b-ride2` (the surging, surge meter, events and surge columns; the baseline column too, because it adapts slower while surging; effort and moving columns are unchanged).
- **Bike digests that changed** (rides that surge between 60 % and 80 % of the FTP, at FTP 200): `ramp`, `ftpm200`, `ftpman`, `ftpmid`, `ftpintman`, `ftpintmid`, `ride1`, `ride2`, `sgride`, `sgride0`. New: `sgmin80`, `sgmin60`, `sgr200`, `sgr105`, `snap-menu-surge`. No snapshot digest changed (menu, HUD) and no 2.1 baseline.

### Bike (FTMS): surge-start cue and power gauge
- **Cue:** 1.5 s of gameplay time of flashing watts and SURGE label after a surge starts (Modern), watts colour only (Classic). Presentation only, not in any digest.
- **Gauge:** Modern HUD shows the smoothed power with a marker at the surge threshold from `bike_surge_threshold_w()`; details in MODERNIZATION.md section 4.14. `BIKE_HUD_H` 24 to 29.
- **Digests:** only the Modern HUD snapshots changed (`snap-hud-modern`, `snap-hud-surge`, `snap-hud-touch`).

## 2026-10-14

### Bike (FTMS): code review fixes (T5.1)
- **Review:** findings and resolution in `doc/ftms/review-t51.md`.
- **vcxproj:** merge markers (`<<<<<<<`) in `visualc/opentyrian.vcxproj` broke the Visual Studio project (the Windows CI uses MSYS2 and did not catch it). Fixed; new guard `tools/check_no_conflict_markers.sh` (in `make regress` and in quick).
- **Run-only overrides:** `--ftms-bridge`, `--ftms-ftp` and `--ftms-test-level` are no longer written to `[ftms]` (`bike_override_set` keeps the saved value and `bike_config_save` writes it back). New case `bike-config-override` in `check_bike.sh`.
- **`--ftms-off`** now also turns off button translation, touch and the in-game menu row (`bike_master_on()`).
- **Remote control buttons** have their own key and menu row (`remote_buttons`, "Remote Buttons"), independent of the Air Mouse; `pointer_smoothing` (no effect) was removed, old configs still load.
- **Bridge reconnection:** SDL3_net stays initialized for the lifetime of the client (`bike_ws_shutdown` only on exit); changing the address or turning off the master abandons the pending resolution and no longer waits for `getaddrinfo`.
- **Sample age** saturates at 32 bits (it wrapped around at ~71.6 min and made an old sample look fresh).
- **Superbomb cooldown** persists across levels (and attempts) and resets only on a new game; the `sgride` digest changed because of this (the run restarts the level), the only new digest.
- **Stock mouse:** mouse movement with zero delta counts again as in stock when remote mode is off.
- **Auto-fire:** the toggle key (default A) is ignored if it is bound to an action in `keySettings`.
- Not done: `--regress-bike-feed/events` outside the regress and the touch exclusion in the launcher (see the review).

### Bike (FTMS): touch controls (T3b.3)
- **Drag to steer:** the finger on the playfield moves the ship by its displacement, added to the `mouseXC/YC` path (±30 clamp and stock inertia kept), with the same `pointer_sensitivity` as the air mouse. The mouse synthesized by SDL3 from the finger is discarded in-game (no double counting and no fire on click) and kept outside it, so touches on menus are still clicks; touch does not go through the air mouse's OK/Back translation.
- **Zones on the side panels:** left REAR, SK-L, SK-R; right BOMB, AUTO, MENU, PAUSE; two quick fingers open the in-game menu. Modern draws the zones (presentation only); in Classic and 4:3 the zones are the margin and the bar of the 320 frame. Auto-fire is the expected companion: with Touch on and Auto-fire Off, the menu warns and the AUTO label is dimmed.
- **Code:** new `src/bike_touch.[ch]` (also in `visualc/`), with one hook in `handleSdlEvents`, one in `mainint.c` and one in `bike_controls_frame`.
- **Tests:** `--regress-bike-events` gained `finger_down|finger_move|finger_up` and `equip,rear=N`; new cases in `check_bike.sh` and a capture of the HUD with the zones in `doc/ftms/screens/hud-touch.png`. The 207 baselines and the old digests of `test/bike/regress.txt` did not change. What to verify on the Surface: `doc/ftms/controls-probe.md`.

### Bike (FTMS): air mouse and remote control navigation (T3b.2, part of T3b.4)
- **Air mouse:** with master and `air_mouse` on, relative pointer movement steers player 1's ship in 1P (`inputDevice` forced to 0 in `JE_playerMovement`), scaled by `pointer_sensitivity`, keeping the ±30 clamp. OK keeps firing, right click opens the in-game menu, Play/Pause pauses, Next/Previous switch the rear shot and fire the sidekicks. One-Euro was not ported.
- **Menus:** OK becomes Enter and Back becomes Esc (on the focused row, ignoring the pointer); other buttons and pointer drift are ignored. Fixes Back in the shop, the data cube and the launcher, right click on Yes in the quit prompt, the title that exited on Back (now it asks) and blank names (`PILOT`). The quit prompt starts on No in this mode.
- **Selector bug** (`opentyr.c`): the click compared y with a limit in x. Fixed unconditionally; no baseline changed (the regress delivers no mouse).
- **Tests:** new `--regress-bike-events` actions (injected movement, buttons and media keys) and new cases in `check_bike.sh`; the 207 baselines and the old digests of `test/bike/regress.txt` did not change. Table of problems and status in `doc/ftms/remote-navigation-audit.md`. Still open: confirmation when quitting through the in-game menu and through the launcher's Esc, a letter selector for names, and real delivery of media keys on Windows (probe T3b.0).

### Bike (FTMS): menu and HUD (T2.5, T2.6)
- **Menu:** new `src/bike_menu.[ch]`, "Bike and Controls..." in Setup (`SETUP_MENU_BIKE`) and in the in-game menu. Scrollable list with all the toggles from `design.md` (master, Bike Power, Bridge host:port with a text box, FTP with step 5 and acceleration when held, Strength, Floor, four surges, Auto-fire, 25/50% reserve, air mouse, pointer speed, touch, HUD) and a live status line. Applies immediately and persists in `[ftms]`; `&` and `<` do not exist in the game's fonts, which is why the name is "Bike and Controls".
- **HUD:** Modern (1P status panel: link dot, watts, rpm, effort, surge meter, AUTO/HOLD/SURGE) and Classic (dot and watts). Draws only with master and HUD on. `bike_autofire_state()` is a stub (false) until the T3b.1 auto-fire.
- **Regress:** the 207 baselines did not change (the in-game menu row only appears with the master on). New `--regress-screen=bike-menu[-nav|-edit]` and `--regress-menu=bike`; `tools/check_bike.sh` keeps the hashes of the menu and HUD PNGs and proves that the HUD does not change the state hash. Captures in `doc/ftms/screens/`.

### Bike (FTMS): core in the game (T2.2 to T2.4)
- **New, optional and off by default:** `src/bike.[ch]`, `src/bike_rules.[ch]` (the `bike_effort`/`bike_feed` engine was already on the branch). Master switch + one toggle per feature, in `[ftms]` of `opentyrian.cfg`, on the CLI (`--ftms-bridge`, `--ftms-ftp`, `--ftms-off`, ids 450–452) and in the regress (`--regress-bike-feed`, `--regress-bike-log`, `--regress-bike-set`, ids 440–442).
- **Generator:** C mapping with an exact-remainder accumulator and a blend back to stock when the link drops (1.5 s delay, 1/2 per second). Without a source or sample the game uses the stock `powerAdd`. The upgrade screen simulator does not change.
- **State hash:** `power`, `lastPower` and the bike digest only enter with a feed or the master pinned on; none of the 207 baselines changed.
- **Tests:** `tools/check_bike.sh` and `make regress-bike` (flag off = baseline, everything explicitly off = baseline, constant 150 W, ramp, link drop, stop, FTP 50 and 500, two Q200B rides, determinism; digests in `test/bike/regress.txt`). A subset runs in quick.
- Pending: bridge transport (`bike_ws`), surge effects, auto-fire, HUD and menus (next tasks).

### Bike (FTMS): surge effects and toggle auto-fire (T3.1, T3b.1)
- **Surge:** four effects, each with its own toggle under the master (superbomb on by default, the others off): +1 superbomb per overdrive-ready (cap 10), 2x sidekick charge, special recharge with extra decrement, shield regen from 15 down to 8 frames keeping the pool cost. Only with the gate open.
- **Auto-fire:** `src/bike_controls.[ch]`. The option only enables; one press during play (`A` key or Menu, or right analog click) turns it on/off, with no menu and no new `keySettings` slot. A reserve of Off/25%/50% of 900 holds back automatic fire while `power` is below it, so the shield can regenerate; manual fire ignores the reserve. The latch survives between levels and starts off on each run. Accessors for the HUD: `bike_autofire_active()`, `bike_autofire_held_back()`.
- **Regress:** `--regress-bike-events` (id 443) injects toggles, manual fire, configuration changes and equipment; new cases in `check_bike.sh` with digests in `test/bike/regress.txt`. The old digests did not change (the cases with overdrive pin `surge_superbomb=0`); 207/207 game cases unchanged.

### Ports from upstream OpenTyrian (carlreinke, 216c771..624e2f2)
- Ported by intent, one commit each, citing the upstream hash. No baseline changed: 207/207 2.1 cases pass.
  - `216c771` `math.h` included where used (8 `.c` files) and `TreatSpecificWarningsAsErrors=4013` in the MSVC project.
  - `49af573` `toupper`/`tolower` without undefined behavior: the title screen secret key code and also the other uses (`joystick.c`, `params.c`, `sprite.c`, the shapes file name), which upstream did not touch.
  - `dda8863` mouse movement only counts as input when it is nonzero; extra spaces in `JE_showVGA`. The `SDL_FALSE` -> `SDL_DISABLE` swap is SDL2-only and does not apply.
  - `52140ae` the sounds file is only loaded if `init_audio()` succeeds; if the device does not open, the audio subsystem is shut down.
  - `a0ed9fd` UTF-8 code page on Windows: `visualc/opentyrian.manifest` embedded by `visualc/resources.rc`. The MSYS2 build already compiles that `.rc` with `windres`, so it gets the manifest with no extra change (the `Makefile` rule now depends on the manifest). Not tested on Windows.
  - `a923d8b` `logFatal` formats the message once (before, it reused the `va_list`); it goes through `SDL_LogMessage`, keeping `--log-file`.
  - `624e2f2` **partial**: `src/swar.h` + SWAR in the background tile rows, in `drawlist_apply_filter_screen` and in the iced/blur filters, with identical output by construction. Left out: the rewrite of lava/water (upstream itself says "slightly different"), `showHeadlight` (diverges from the original for positions outside the range and fails upstream's asserts at px=py=0 in an exhaustive comparison) and `showFlipped`/`showNormal` (`interp_blit_playfield` already does that copy).
- README-only upstream commits (`f3d4560`, `ba9c399`) ignored.

## 2026-10-09

### Documentation: plans become `MODERNIZATION.md` and `CHANGELOG.md`
- At the user's request, the two plan files in the root (general and Tyrian 2000 track) were removed. The current state of both variants moved to `docs/MODERNIZATION.md`, and the history to this file.

### Shot light visible, fixed and proportional
- **User test:** depth stage 3 did not show up ("I didn't notice any difference"). The mean difference between before and after was 1 to 6 levels out of 255, and shot light brightened the terrain only +4 levels, even on High. Workers: Claude Sonnet 5.5 (medium).
- **Preview:** tuning flags for regression only (`--regress-light-scale`, `--regress-light-radius`, `--regress-fog-strength`, ids 435–437) allow rendering light and fog levels without recompiling.
- **Quality (`5f12511`):** the user saw "JPEG-like" pixelation, reddish dots and the halo shifted up and to the left.
  - Every 2× resample read half a cell ahead (`d*128+64` instead of `-64`), which shifted the light and bloom by ~2–3 px.
  - The masks were kept at 8 bits before the gain, with only 1–3 levels per pixel. The roundings turned into blocks, and each channel rounded differently, hence the olive ring, the red and magenta blotches and the red band between shots.
  - Now all planes are 16-bit Q8, and the light center is within 0.07 px of the shot center.
  - A soft limiter (knee 150, asymptote 216) replaces the hard clip at the ceiling.
- **Big shots (`cce14a3`):** the light grew with the emitting area, and big shots reached 3–3.4× the Pulse-Cannon. Before the blur, the player-shot part of the mask is now scaled by `(S_ref/S)^0.75`, with the local energy measured over the blur support. Big shots ended up at ~1.4–2.1× the small shot. Explosions, enemy shots and bloom did not change.
- **Levels (user's choice):**
  - Fog on bg1: 40 → **30**.
  - Light gain: High first went to 3328 (4×) and the user found it strong. They asked for 60% of that, so it ended at **High 1997, Low 1198** (`5353944`). Low is still 0.6× High, and the option is still Off/Low/High.
  - Approved while playing ("Looks great").
- **Regression:** only Modern frame hashes with light or Depth on changed. Classic, state and Modern without light did not change.
- **Process:** the user prefers testing in the game to looking at screenshots. Only generate captures when they ask.

### Depth stage 3, and the erased INSERT COIN
- **Workers:** two Claude Sonnet 5.5 (medium), at the user's request, each in its own Orca worktree from `master`, in a single Run.
- **INSERT COIN (`e41c056`):**
  - **Bug:** with Lighting on, the demo text glowed like a shot. It was drawn without its own context and inherited the one from the last object, almost always `DL_OBJ_PLAYER_SHOT`: it got an emission tag, light color and the shot layer.
  - **Fix:** the text now uses `DL_OBJ_HUD`, and the previous context is restored afterwards (`drawlist_get_context`, new). The message window (`JE_drawTextWindow`) and the ship trails at the end of the level got the same treatment.
  - **Review:** the first version zeroed the context after the text and some baselines were justified only by "consistency". It went back to the worker, who replaced the zeroing with a restore and attributed each baseline to one of the three fixes by A/B.
  - **Scenarios also change:** the regression scenarios run with `playDemo`, so they also show INSERT COIN, and that is why the level cases changed along with it.
  - **Proof:** `--regress-demo-hud-check` gained a probe in `modern-light-demo1-d4`. The emitting pixels added by the text were 3,933,643 and are now 0.
  - **Baselines:** 11 from 2.1 and 12 from 2000 changed, Modern frame hashes only.
- **Stage 3 (`41660ff`), only with `Depth: On` (Off stays bit-identical):**
  - **Fog:** bg1 pixels blend 40/256 (later tuned to 30, see the entry above) toward a fog color computed per frame (the average of bg1 still without fog, slightly desaturated and ~35% lighter). The translucent bg2 row gets half, and space levels get no fog.
  - **No animated noise:** interpolated frames have no single bg1 scroll to anchor the noise to, and badly anchored noise would "swim" over the terrain.
  - **Pass order:** fog → shadows → light, so the shadow keeps its strength over the fogged terrain.
  - **Light per layer:** bloom and light are in separate planes, and the light is weighted by the layer that receives it (Q8): terrain and ground enemies 256, sky and top enemies 160, ship and sidekicks 144, bg3 24, shots/explosions/VFX 256. Bloom is not weighted. In space levels there is no per-layer light.
  - **Held screens:** pause, menu and help reuse both effects from the snapshot.
  - **Cost:** ~0.16 ms per presented frame, on top of stage 2.
  - **Regression:** new `Depth fog:` and `Depth light:` lines; the held lines gained fields; the `--regress-depth-check` fixture covers the fog color, the bg1-only rule and the weight table; new `depth-light-*` cases (4 in 2.1 and 3 in 2000); only `depth-*` baselines with Depth On changed and the space ones stayed identical. After the rebase onto INSERT COIN, the `depth-*` ones were regenerated from the combined binary; in the worker's full suites, 207 cases in 2.1 and 183 in 2000.

### Release v0.6.0, quick tests and CI without rework
- **Release v0.6.0** ("Modern Tyrian v0.6.0: depth shadows") at commit `2ecacc4`, at the user's request, after green CI on the three systems. The CI attached the four packages (Linux x86_64/arm64, Windows x86_64, macOS universal).
- **Quick regression (`61ef2e7`, `e3dff92`)**, at the user's request ("these suites are too long"). Worker: Codex `gpt-6.1-sol` (medium).
  - `make regress-quick` (~20–25 s) covers the 5 Classic demos, the water/spotlight/flip/blur scenarios, the Modern core (4:3, 16:9, CRT, Depth On on TYRIAN, held pause, the matrix pause row), the cheap guards and 4 cases from 2000 when the data exists. Without data, it prints `2000 quick: skipped (no data)`.
  - Filter by area: `REGRESS_ONLY=<regex>` or `--case=REGEX`, in both suites.
  - 2000 suite in parallel: from ~4.5 min to ~1 min 45 s, with the same 180 cases. The full targets and the CI did not change.
  - The first version failed only on Windows: on MSYS2 the `cp` of `./opentyrian` writes `opentyrian.exe`, and the bootstrap guard flagged the executable's own copy. Fixed in the same worker.
- **CI without rework (`6af6638`)**, after the user noticed repeated builds. Each workflow gained a decision job (`.github/ci/decide.cjs`):
  - a push of only Markdown, `docs/` or `.worker-reports/` skips the build and the regression and finishes green;
  - a push whose SHA already passed the same workflow on another branch is also skipped, so a fast-forward of `master` to a tested branch costs nothing;
  - the release downloads the packages from the green push of the same SHA and attaches the original bytes; it only rebuilds if there is no green push and fails loudly if the packages have expired;
  - `workflow_dispatch` always runs everything.

  The release path will only be truly exercised on the next release (or a throwaway pre-release).
- **Process:** merges by fast-forward when the branch already contains `master`; the coordinator runs only quick locally and uses the CI as the full gate; paused work is resumed with `codex resume <session>` plus `worker-start --task … --terminal …` after an Orca restart.

### Depth in Modern: layer buffer and cast shadows
- **What went in (branch `vittau/depth-layers`, merged into `master`):** shadows cast between layers, with a fixed light coming from the top left. This is Phase 3 "Layers and depth", stages 1 and 2 (user request, 2026-10-08). Workers: Claude Sonnet 5.5 (high) on the first three tasks and Codex GPT-6.1 Sol (medium) on the last, at the user's request.
- **Technical decision (coordinator):** instead of one color buffer per layer, a **per-pixel layer buffer**, stamped by the same primitives that already stamp the emission tag. Separate color buffers would force redoing the composition of blends and filters outside the engine, the "preserving the blend math" risk. The 8-bit frame stays untouched, so bg2 and the lava, water and blur filters did not need to be redone. Each pixel stores the layer that painted it last, and each tick stores the order in which the layers were drawn, which changes per level.
- **How the frame is assembled (`JE_main`, `tyrian2.c`):** everything is painted in order into a single 8-bit frame: bg1 → stars → bg2 (on top, blended or filtered) → ground enemies (slots 25–49 and 75–99) → bg2 on levels with `background2over == 1` → bg3 when `background3over == 2` → sky enemies (0–24) → bg3 → top enemies (50–74) → shots, ship, explosions. `draw_background_2_blend`, `JE_darkenBackground` and the filters read the framebuffer itself.
- **Stage 1 (`7847f6d`):** the layer buffer in `game_screen`, `VGAScreen2` and the interpolated replay copies, like the tag. Who stamps: background rows (identity 1/2/3 by context, with a "blended" mark), sprites by context (enemy by slot range, item, ship, sidekick, shots, explosions), stars and superpixels; fills and clears zero it; `darken` does not change the pixel's owner. Lava reproduces its own displacement in the layer, the 264×184 window is copied to `modern_depth.c` with the same flip as the tag, and VFX pixels get a "no shadow" mark. No pixel or baseline changed.
- **Stage 2 (`e994e9a`) and adjustment (`ccccf5b`):** each layer casts onto the layers drawn before it and lower ones, with an offset proportional to height (dx,dy): bg2 (4,6), sky enemies (8,12), ship and sidekicks (9,14), bg3 (10,15), top enemies (12,18); ground enemies (2,2). The translucent bg2 casts with 31% of the weight. Shots, explosions, stars, superpixels and text neither cast nor receive. Space levels (`starActive`) have no shadow. Computation: whole offset silhouette, separable fixed-point blur and darkening of 78/256, ~0.3 ms per presented frame.
- **User test:**
  - **Halo:** there was a light halo between the floating earth and the shadow, worst on HOLES (physical E1:L11, earth on bg3). The cause was that the silhouette was only marked where it fell on a valid receiver; the blur then lightened the strip next to the edge. Fixed in `ccccf5b`.
  - **Option:** the user found High good and asked for only a toggle, so the option became `Depth: Off/On`, default On (not Off/Low/High as planned), with High's blur and the strength rebalanced for the same total darkening. The old config (`low`/`high`) reads as On.
  - **Pause (`4dcf353`):** on pause, in the ESC menu and in help the effect disappeared, and so did bloom and light. Now those screens reuse the layer, tag and light of the last real frame (`modern_held.c`), and pixels that the overlay changed receive nothing.
- **Levels × layers:** the `--regress-level` index is physical, not the play order. TYRIAN is physical 1:9 (1:15 on Hard), with the earth on bg1 and the water on the translucent bg2; 1:1 is ASTEROID1.
- **Regression:** the regression pins `Depth` to Off, so no old baseline changed outside the Graphics screens, because of the new menu row. 20 `depth-*` cases went in for 2.1 and 16 for 2000 (Off × On, identical state/RNG), the `--regress-depth-check` fixture and the `--regress-layer-check` and `--regress-held-check` checks. The CI passed on the three systems.
- **Pending at the time:** the "INSERT COIN" lit like a shot (fixed on the same date, see above) and stage 3.
- **Process:** the user found the suites long. From here on the coordinator no longer runs the full suites locally: reviews, pushes the branch and uses the CI as the gate.

---

## 2026-10-07

### Release v0.5.0
- **Release v0.5.0** ("Modern Tyrian v0.5.0: PROGRESS bar") at commit `8d0e15f`. Brings the Modern HUD PROGRESS bar on top of v0.4.1. The CI passed on the three systems, on push and on release, and attached four packages: Linux x86_64/arm64, Windows x86_64 and macOS universal (no Windows arm64 since v0.4.0).

### PROGRESS in the Modern HUD
- **Decision:** continuous horizontal bar with SHIELD's palette/finish, frozen during a recognized boss; preserves the existing HP bars. An early or alternative exit may end at the current value, below 100%. Level progress information, which the original decision of 2026-09-27 left out, was authorized here with these limits.
- **Conservative implementation:** presentation-only observer, with no writes to gameplay/RNG. Milestone estimated by the largest positive action 11/36 time; advance by segment up to 99%, with no credit for jumps or repetition. The main objective requires an executed milestone and a natural exit with a survivor, with no earlier negative cause; one survivor is enough in 2P. A temporary absence of living players pauses without canceling or resetting; an early exit stays closed until reset, even if another milestone arrives in the warp. Returns, 38/75 replacements and maps without a denominator freeze or omit the estimate. There is no universal identification of the final boss; the authorial exception for the E4/L5 timer in common mode does not cover Timed Battle or other timers.
- **Layout:** 1P/arcade on the left (label y=142, bar y=150); 2P on the title row under the playfield, keeping the message row. Without panels/4:3 it is intentionally omitted so as not to cover information. The drawing reads the snapshot, without interpolating the new bar; the confirmed latch remains in the warp/fade until the reset of the next map.
- **Limits:** the inventory of 62/70 levels is static. Segment graph, certified profiles, bosses without a bar, transformations and coverage of all paths/modes remain as future evolution, with no promise of a precise universal percentage. Selecting a map does not prove execution of its recoils or final victory. Details in [progress-runtime.md](progress-runtime.md) and [progress-levels.md](progress-levels.md).
- **Final validation confirmed by the coordinator:** release/debug, GCC16 audit of the eight final C files, 271 assertions of the scalar fixture with ASan/UBSan (not the whole engine), protected data guard and portability/VS XML/CRLF. Release suites: 172 2.1 cases and 153 2000 cases, with existing and PROGRESS guards, OFF/ON state/RNG equality and Classic framebuffer. Real 2.1 captures at 16:9, 16:10 and 2P approved; no 2000 captures.
- **Representative runtime coverage:** final release without acceleration: E1/L6 ends at 52% in both variants; E1/L4 reaches 100% with 40 samples held; E4/L7 in 2P/fire, VFX OFF, has three boss resumptions per variant, 5/3 later advances and an exit at 45%/43%. It does not certify all levels, branches, final bosses or modes; synthetic case names do not prove all real hooks.
- **Modern references:** the new PROGRESS pixels justify the explicit changes in 2.1: 20 frame-hash files plus two pause-frame CRCs in the mixed matrix (21 files). In 2000, 12 frame files plus the two pause CRCs (13 files). No Classic or state reference changed; the final reruns passed. Task without commit/push; 2000 data was not exported.

---

## 2026-10-01

### Launcher in Steam Deck Game Mode; release v0.4.1
- **Bug (v0.3.0–v0.4.0):** in Game Mode the game closed before the first screen, with the log "The launcher window resize did not settle: Can't stat: No such file or directory". It worked in Desktop mode. gamescope controls the window size and never applies the resize requested by the launcher, so `SDL_SyncWindow` times out. Since `e7f0d16` that timeout became a `return false` in `launcherChoose`, and `main` ended the game successfully. The "Can't stat" was a stale `SDL_GetError()`, because SDL does not set an error on timeout.
- **Fix:** `video_fit_launcher_window` no longer fails. The timeout becomes a log warning, and the launcher draws at the size the window has, following the subsequent resize events. With Cocoa and windows that settle, nothing changes.
- **Regression:** the `check_display.sh` mock gained a compositor that ignores resizes (timeout without error). The launcher must keep 1280×800 and fit the canvas to that size. The 2.1 suite (172 cases) passed without changing baselines. Confirmed on a physical Steam Deck in Game Mode with v0.4.1: the launcher opens normally.

---

## 2026-09-30

### CRT filter in the Modern presentation
- **Menu:** `CRT Filter` in Setup → Graphics, active only in Modern: Off, Scanlines, NTSC and Scanl + NTSC. The normal font measures 117 px for `Scanlines + NTSC` and 90 px for the abbreviation, within the 95 px column. The original `+` is an empty placeholder; a small cross occupies the already measured advance. The extra row uses the menu's adaptive spacing, without overlapping the status or the picker.
- **Port:** Shay Green's scalar kernel (`snes_ntsc 0.2.2`, LGPL 2.1+), brought from the Deadly Dave port with the credits and the license. RGB555, 8-bit RGB range, composite preset, three burst phases per presented frame and a 16 MiB table created only on first use. The last pixels of widths that leave a partial chunk also go into the blit.
- **Scanlines:** same constants as Deadly Dave (`>> 1`, luminance capped at 216), half-logical-line bands and proportional blending when a physical line crosses the band. NTSC runs only on the 200 source lines; the vertical expansion is done afterwards, per line. Heights that are multiples of twice the source use 400 lines; the others (including 600 px, an odd integer scale) use the Fit height in native pixels; small destinations keep 200.
- **Integration:** after the passes and the canvas capture in `modern_present_frame`, including on interpolated frames and modals. Own buffer and texture, recreated only when the dimensions change; Off keeps the original path. Aspect and mouse keep using the canvas and the same Fit rectangle of the presenter, now exposed by a video helper. Shared config `crt_filter`, default/unknown value = Off; no game state or RNG is altered.
- **Release measurement (`-O2`, Apple A18 Pro, canvas 427×200, average of 60 calls):** at 1080/2160 lines, Off 0.014/0.014 ms, Scanlines 0.382/0.494 ms, NTSC 0.311/0.305 ms and combined 0.847/1.328 ms per frame. The table took 7.711 ms to build; upload and GPU are outside these measurements.
- **Regression:** 6 new cases (four modes, combined at 2160 and picker), taking the 2.1 suite from 166 to 172. 13 synthetic fixtures (including odd heights of 1079/2161 px), filtered-buffer hashes, identical state/RNG in the four modes, config roundtrip, reuse/recreation at runtime and mouse in the modals of both variants. The coordinator explicitly authorized updating only `screen-setup.txt` and `modern-screen-setup-16x9.txt`: the new row changes the real Graphics screen, and freezing the fixture would hide that change. The other baselines stayed identical; the 153 cases of 2000 passed without changes.
- **Validation:** release and debug builds (`-O0`, `-Werror`, asserts), gcc-16 audit on all touched `.c` files and fixtures with ASan/UBSan. Review PNGs for 2.1 only (menu/picker and four modes at 1920×1080 and 3840×2160), outside git. Codex worker; no branch change, commit or push.
- **Origin:** request of 2026-09-28, with the user's references in `deadly-dave` (scalar port of `snes_ntsc`, half-line scanlines) and in `antivirus-95` (crt-geom style pass). User's decision: no curvature and no mask; vignette could go in. Theory in [CRT.md](CRT.md).

### Beveled frame in the Modern HUD
- **Glass frame around the playfield (`c752fde`)**, at the user's request, from a reference image.
  - Drawn: double fillet at the panel/playfield junction (white-cyan touching the game, orange outside), slight warm lightening of the panel near the edge, two fixed glints per side (rows 11 and 133), inner shadow on the 6 edge columns and the 3 bottom rows of the playfield, and a subtle border on top of the message strip.
  - **User's decision:** everything on the canvas logical grid, no drawing at screen resolution. The fillet is 1 logical px and enters the regression hashes.
  - **Order:** fillet and glints after the ambilight and before the HUD, so text and bars stay on top. The shadow comes after the ambilight sampling, and the strip border only touches the background, never the glyphs.
  - **Scope:** Modern panel mode only. At 4:3 the panels would be 28 px, below the minimum of 51 (`MODERN_HUD_MIN_PANEL_WIDTH`), so 4:3 stays on the fallback without panels and does not change. Classic, screens outside gameplay and all state baselines remain identical.
  - **Regression:** 15 Modern baselines from 2.1, 15 from 2000 and the two Modern pause aggregates in each variant's `final-regression.txt` changed. The suites pass with 166 and 153 cases.
  - **Worker:** Codex `gpt-6.1-sol` (medium), dispatched with `--inject`.
- **Second pass (`e3325be`)**, after the user's test: the fillet did not react to the glints and the glints were mirrored, which looked repetitive.
  - **Bloom on the fillet:** away from the glints the fillet is cyan (~135,170,193); near each glint it rises to almost white in a quadratic falloff of 40 to 56 rows, spreads over three panel columns and leaks slightly into one playfield column. A faint warm haze surrounds the core.
  - **Different glints per side:** each side has its own static table (row, peak, trail, radius and bloom strength): left at rows 11 and 133, right at 38 and 166.
  - **Review:** the first version of the bloom was rejected, because it only rose from ~150 to ~190 within ±10 rows and the fillet had lost its cold tone. The worker redid it with the values measured in the report.
  - **Regression:** the same baselines as the first pass changed.

### Health bars on ultrawide screens
- At the user's request: the columns of the shield/armor/energy block took up 1/3 of the panel and moved too far apart (48 px at 21:9, 96 px at 32:9). Now they are at most 36 px (`VIT_COL_MAX_W`), which still fits the full labels, and a limited block aligns to the outer edge like the rest of the panel. Below the limit (16:9, 16:10) nothing changes. Only the Modern 21:9 and 32:9 baselines of Tyrian 2000 changed. Worker: Codex `gpt-6.1-sol` (medium); the first version centered the block and was sent back in review.

### Windows arm64 removed from the CI and releases
- User's decision (very little adoption). The CI failure of `a2bec4d` was in that runner's `setup-msys2`: `gpg` died while populating the pacman keyring and the sync refused the signatures; the rerun passed. On that runner MSYS2 is emulated x64, and the regression shell scripts are slow (19 min per push against 12 on x86_64). Windows on ARM runs the x86_64 package through system emulation. v0.3.1 still shipped with the arm64 package; from v0.4.0 on, it did not.

---

## 2026-09-29 / 09-30

### [2000] Final rounds: 3b, 3c, installer, launcher with Install, Phases 4 and 7
- **Integrated into `modernization`:** 3b, game rules (`b49b22c`); installer (`ce9731e`); 3c, menus and scoreboards (`6be3c20`); Install flow in the launcher (`fb6e172`); Phase 7 (`7d132a1`); Phase 4 (`d39de50`). Each passed `make regress` (then 166 cases plus the guards) and `make regress-2000` (148 cases), without changing any 2.1 baseline.
- **Scoreboard:** 3b and 3c created parallel scoreboard modules. The 3c worker unified the two in `src/highscores.[ch]` on integration.
- **Phase 7:** the audit of Modern on 2000 found no defect in the renderer, because tags and light come from the object's context and not from the sprite id. The HUD uses the semantic labels. `--regress-demo-hud-check` proves, in both variants, that the demos follow the active mode's HUD (user's decision).
- **Phase 4:** complete Timed Battle; episodes 1–5, 9-ship arcade, Super Tyrian, Destruct and Christmas are exercised through the real menus. `--regress-flow` is a virtual keyboard that walks the menus; there is also a sweep of the 74 levels.
- **To check on DOS (Phase 4):** the 16-bit overflow of the time bonus in the fork; the battle→episode mapping (placeholder sections); section 44 of E1 with nonexistent shapes; the lives bonus; music and cancellation of the battle scoreboard; Pretzel Pete's sprite size; Super Tyrian state 8; the fork's approximations in events 58/59/68 and in trail 198.
- **CI on Windows:**
  - The launcher hashes came out with CRLF (`fopen "w"`); fixed with `"wb"` (`da4fcb1`). The pixels are identical on the three systems.
  - `%zu` does not pass MinGW's `-Werror` and `python3` was missing from MSYS2 (`4991bbd`).
  - The installer tests used a fake curl as a shell script, which `CreateProcess` does not execute, and ended up on the real curl (network). They were replaced by a C stub pointed to by absolute path in the test spec (and a safeguard that blocks network access). Two runs on the `ci/instwin` branch passed on the three systems; merge `916ee80`. A real Windows bug was also fixed: read-only files copied from a folder could not be removed.
- **Pauses and models:** the Codex quota ran out with the 4 GPT-6.1 Sol workers near the end, and they were finished by Sonnet 5.5 workers. Later the Claude limit closed the Sonnet workers' terminals; the sessions were resumed with `claude --resume` and reattached to new dispatches, with no loss. For new agents, the user chose Codex GPT-6.1 Sol with medium reasoning.

---

## 2026-09-29

### Release v0.2.1
- **Release v0.2.1** at commit `6e76162`, at the user's request. Fix release on top of v0.2.0: the remapping screens no longer overflow the width, and the 1P HUD does not show "Player 1". The CI passed on the three systems and attached the five packages. `master` stayed at `a81e640` and only advances if the user asks.

### HUD without "Player 1" in 1P
- `46d8be8`, at the user's request. The name only appears in two-player games; in 1P and arcade the rows move up and the bars are recentered. Worker: Codex `gpt-6-luna` until the quota ran out, then Sonnet 5.5 in the same worktree.

### Release v0.2.0, `master` aligned and remapping fixed
- **Release v0.2.0** at commit `a81e640`, at the user's request, with the five packages attached by the CI. Includes colored light, ambient particles, the light limit for big shots, the taller bars, the pic-1 header seam and the parallel regression.
- **`master` advanced by fast-forward** to `a81e640`, at the user's request: 176 commits from `modernization`, with no merge commit.
- **Remapping screens (`67e063e`).**
  - **Cause:** with two mappings on one action, the control text went past x=320. The pic-1 screen stopped widening in Modern (no free column left) and the overflow reappeared on the next row, on the left.
  - **Labels:** a value that does not fit in x ≤ 310 becomes short labels (`A/RB`, `LY-/UP`, `RT+/H12X-`), and `+N` comes in as a last resort. The original text stays whenever it fits, so Classic does not change. Long key names shorten the same way (`L Shift`, `KP …`, `SC n`).
  - **Edge clipping:** font glyphs that cross the surface edge are now clipped instead of wrapping around.
  - **Regression:** gained the `joystick-multi` and `keyboard-long` screens and ended with 164 cases, in ~56 s in parallel. Worker: Codex `gpt-6-sol`.

### HUD without divider, pic-1 header, bars, Mega Cannon, parallel regression and agent swap
- **Black divider above the bars removed (`fa23d6d`)**, at the user's request.
- **Agents:** the user's OpenCode Go quota ran out in the middle of three tasks (Mega Cannon, pic-1 header, taller bars). The attempts were ended by closing the terminals, and the same tasks were reopened with `--retry-of`, in the same worktrees, with Claude Sonnet 5.5 (high effort). Then the user set **Codex as the default worker**: `gpt-6-sol` (high effort) on most tasks and `gpt-6-luna` (high) on the very simple ones.
- **Header of the widened pic-1 screens (`993ace9`).**
  - **Cause:** when the panel content reaches x=310 (Shield purchase, Quit dialog), the widening point falls on column 311, which is the light right bevel of the title box, and the inserted strip repeated it as a flat block.
  - **Fix:** the header rows (y ≤ 33) repeat the last interior column (310). Mouse mapping does not change, and Classic remains identical. The dark segment at the top of the box (y=6, x 285–293) is already in the original pic 1.
  - **Regression:** gained the `--regress-screen=shield` screen and ended with 158 cases.
- **Health bars 50% taller (`ed0c9fb`).**
  - 1P and arcade: from 52 to 78 px. Their block is centered between the top block and the bottom stack (generator above the cheat warning), which is now attached to the foot of the panel.
  - Special weapon: with a special equipped, the rows under the icon are a bit closer together, because the worst case (arcade + special + cheat at 16:9) only fits that way.
  - Compact 2P: the bars grow as much as the width allows: 39 px at 21:9 and 32:9, 35 at 16:9 and 29 at 16:10; the limit is the foot of the panel.
  - Footer: the cheat warning uses 1 to 3 rows depending on the width, and the 2P timer goes on a single row when it fits.
- **Mega Cannon light at power 6 (`c116186`).**
  - **Cause:** the orb is drawn with four sprites of 70 to 95 bright pixels each, against fewer than 30 in small shots. So it added up to 8 times the light of a Pulse-Cannon and saturated a wide plateau. The colored light was not the cause: it even reduced the orb's light.
  - **General fix:** each sprite writes its emissive footprint (number of bright pixels) into the tag byte, next to the class. A player shot with more than 32 bright pixels has its emission scaled to that reference value.
  - **Measured effect:** the Mega Cannon's light dropped 3.1×, and the Laser's and Zica Laser's about 2.5×. Small shots, explosions, enemy shots and items do not change; cost ~0.02 ms.
  - New test options `--regress-front-weapon` and `--regress-front-power` (311–312) set up the armament in `--regress-script`.
  - **False alarm:** the agent saw parallel runs diverge, but its measurement driver did not pin `--regress-seed` and fell back to `time(NULL)`. The suite's cases pin the seed.
- **Parallel regression (`68cde70`)**, at the user's request. `tools/regress.sh -j N` (or `REGRESS_JOBS`) defaults to the number of CPUs. Each case runs in a worker with its own buffer and the output comes out in declaration order; heavy cases start first; the script is portable to Bash 3.2 and MSYS2. The suite dropped from ~245–293 s to ~67–93 s on the 6-core Mac, with cases and baselines unchanged (byte-for-byte identical outputs, including under load). CI green on the three systems. Done by the first Codex worker (`gpt-6-sol`); Orca 1.4.215/216 did not recognize Codex v0.158 as ready (`agent_readiness` timed out), so the task was injected into a hand-opened terminal with `dispatch --inject`.
- **Steam Deck performance overlay:** the user saw the overlay (MangoHud) not appear in the Linux build. The video code is equivalent to Deadly Dave's (static SDL3 3.4.16, default renderer), and later the overlay came back with no change at all. A renderer log task (Codex `gpt-6-luna`) was aborted at the user's request.

### Release v0.1.0, colored light and ambient particles
- **Release v0.1.0** at the user's request at commit `93b373b`, with the Linux x86_64/arm64, Windows x86_64/arm64 and macOS universal packages attached by the CI. On releases, the CI now skips the regression suite (`521fc4b`; later replaced by package reuse, see 2026-10-09); pushes still run it.
- **Resumption:** the new session needed `orca orchestration run-use --id run_e0cad877e2fd` to be able to dispatch again (`consumer_fenced`).
- **Ambient particles (`ambient`):** embers on lava, snow on ice, mist on water and in blur, fine dust in space and dust elsewhere (62 levels sampled). They only lighten or blend slightly, emit no light, use their own RNG and follow Effects (Low sparse, High a bit more). Very subtle; the user would evaluate while playing.
- **Light with the object's color (`lightcol`):** the audit confirmed that the light came from the white cores (blue enemy shot: 94–98% of the energy in the fire block). Now each marked pixel carries the object's representative color (the saturated tone of the sprite's dominant hue family), which tints the light and bloom. Practically the same intensity and unchanged cost (~0.47 ms on High).
- The regression ended with 156 cases.

### [2000] Parallel round: CI, launcher art, 3b, 3c, launcher and installer
- **2000 CI (`d773095`):** `tools/fetch_t2000_data.sh` downloads from Camanis on every run, checks the size and the SHA-256, extracts safely outside the checkout and verifies the manifest. The three workflows run `make regress-2000` after 2.1, with no cache. On failure, they upload only `*.txt`/`*.log`; releases still carry only 2.1. On Windows, the new baselines came out with CRLF; fixed with `test/regress-2000/** eol=lf` in `.gitattributes` (`a3787f4`), and the CI went green on the three systems.
- **Launcher art (`cfa3c12`):** generated by a Codex GPT-6 Luna worker. Two 960×1080 panels without text and two 800×320 "TYRIAN 2.1"/"TYRIAN 2000" signs with alpha, original and without ™, in `assets/launcher/`, with the prompts in `SOURCES.md`.
- **Workers going down:** the machine rebooted during the limit pause. The 4 Sonnet workers (3b, 3c, launcher, installer) died without a report, and the uncommitted work was saved in `refs/keep/<worktree>-snap1`. They were resumed in the same worktrees.
- **Workers' model:** Codex **GPT-6.1 Sol high** from then on, by the user's decision. It required Codex 0.159.1, because 0.158 does not know the model and shows the name in lowercase in the footer.
- **Worker type:** the four GPT-6.1 Sol workers stopped at the Codex usage limit, all near the end; the trees are in `refs/keep/<worktree>-snap2`. 3b, 3c and the installer were resumed by **Claude Sonnet 5.5 high** workers, and the user decided that the following ones go back to Sonnet 5.5.
- **Launcher (`ca400ec`):** the worker left the report ready; review, commit and merge stayed with the coordinator.
  - **Opening:** the launcher opens on every normal start. `--variant`, regress and selftest still go straight to the game.
  - **Drawing:** the screen is drawn directly on the SDL renderer, at the physical size of the window. The 4 PNGs are embedded in the binary: `tools/embed_assets.sh` in the Makefile and `visualc/embed_assets.ps1` in VS. The texts use SDL's debug font at integer scale.
  - **Startup order:** the choice goes through the data provider before any asset or save of the variant. Only then come the 2b migration, the saves and `launcher/last_variant` in `opentyrian.cfg` (shared; used only to preselect the panel).
  - **Tests:** 10 hash `--regress-launcher` cases, independent of the data, go into `regress-2000`. `make regress` ended at 164 cases plus the guards and `regress-2000` at 29 cases, all PASS.
  - **Pending at the time:** wire up the installer (done right after), validate on native Windows/VS and on the physical Deck.
- **User's decisions:** 2000 will also have Modern mode with the new HUD, and the attract demos follow the active mode's HUD (Phase 7).

### [2000] Phase 3a: schemas, loaders and `regress-2000` (`d1a6bd7`)
- **Worker:** Claude Sonnet 5.5 high, ~1h20, with a pause at the usage limit.
- **Schemas:** `src/game_schema.[ch]` with `GameDataSchema`/`GameStringSchema` tables for 2.1 and 2000, with no variant `if` in the loaders. The arrays grow to the maximum of the two versions, and 2.1 reads the same bytes in the same order. Covered: items with ID banks, texts with the final sections of 2000, 13 shape banks and the extra ship bank (IDs >500), 14 images/24 palettes, 31 effects with shifted voices, 126 credits and 5 episodes. Semantic labels (player 1/2, timer) replace the raw indexes of `miscText`.
- **`--variant=2000`:** selects a validated 2000 installation, with no per-file fallback. Default search: `TYRIAN2000_DATA`, `tyrian2000/` next to the executable, `./tyrian2000`. The 2000 save is 4,722 bytes in `tyrian2000/`, and the default names come from the HDT.
- **2000 events:** 58, 59, 68, 83, 84, 85 and 99 were skipped, with a log, until 3b; 68 stopped running the 2.1 rule.
- **What ran:** 2000 reaches the title and starts Episode 1, and all E1–E5 levels start without a crash in the debug build.
- **Tests:** `make regress-2000 TYRIAN2000_DATA=<folder>` with 19 deterministic cases (without claiming fidelity) and its own manifest of 79 files; `make regress` gained `check_no_t2000_data.sh`, which refuses any 2000 file in the tree and exempts the 39 identical to 2.1; the 164 2.1 cases pass without changing baselines.
- **Pending at the time:** arcade/Super Tyrian tables and new scoreboards (3b); menus 3/12/15 with extra rows, episode 5 spacing, the 2000 mark on the title and pic-1/pic-2 heuristics (3c); static offsets (`pcxpos`, music) that need a reset if the launcher switches versions in the same process.

### [2000] Phase 3 begins
- **The user approved the three documents in `docs/t2000/`** ("Approve and go ahead").
- **Division of Phase 3:** 3a, per-variant data schemas, loaders and the first `make regress-2000` (goal: 2000 loads and Episode 1 starts); 3b, T2K events, enemy replacement and rules (sidekicks with charge, rear "None", twiddles) as hooks; 3c, conflicts with the Modern menus and HUD.
- **Session workers:** Claude Sonnet 5.5 high, at the user's request.
- **User's decision (launcher):** the launcher images would be generated by a Codex GPT-6 Luna high worker, and only those; the rest stay Sonnet 5.5. The visual reference sent by the user was recorded with the approved adjustments: no arena, title without a copy of the logo, art at window resolution. The launcher was thus an exception to the procedural-art-only rule.

### [2000] Phase 2b: per-variant saves and migration (`dec73ff`)
- **Worker:** Codex `gpt-6-sol` high, ~15 min, with one question answered: publish the copy with `link` + `unlink` on POSIX and `rename` on Windows and on FAT/exFAT.
- **Layout:** the user root holds the shared files (`opentyrian.cfg`, the 28-byte `tyrian.cfg` with detail, gamma, keys, joystick and volumes, `newsh$.shp` and the log); `tyrian21/` holds `tyrian.sav` and the recorded demos (`demorec.N`); `tyrian2000/` is reserved.
- **Conscious deviation from `integration.md`:** `tyrian.cfg` is also shared, because it only stores presentation and controls (decision 4 of the original §12: shared configuration, separate saves and progress).
- **API:** `userFileOpenKind`/`userFileExistsKind`, with a shared, save or demo kind, in `src/file.c`. `userFileOpen` still points to the root.
- **Migration** (2.1 only, before `loadSaves`, never in regress/selftest): copies the root `tyrian.sav` only if it is exactly 2,502 bytes; writes to an exclusive temporary and publishes without overwriting; never touches the original or an existing destination; for any other size, skips with a warning; if the copy fails, the session reads the root save read-only and writes no save at all, so a blank save never shows up hiding the old one.
- **Tests:** `tools/check_user_paths.sh`, inside `make regress`, in a sandbox with `--regress-user-root`/`--regress-user-files`. Covers byte-for-byte migration, repetition, existing destination, wrong sizes, failures and retry, save round trip, configs in the root, demos, portable and XDG roots and isolation of regress and selftest.
- **Gate:** the 164 cases pass without changing baselines (61 s in the integrated tree).
- **For those upgrading from v0.2.x:** the first launch copies the save. An old binary keeps using the root save, and progress starts to diverge between the two.
- **Windows fix (`d313c43`):** `stat` below a file returns `ENOENT` on Windows and `ENOTDIR` on POSIX, so a file named `tyrian21` did not turn on read-only mode. The migration now explicitly checks the root and the namespace folder, and `check_user_paths.sh` names the step that failed. Worker: Claude Sonnet 5.5 high. CI green on the three systems; Linux arm64 needed a new run because the runner hung for 45 min on compilation.

### [2000] Phase 2a: variant, provider and `--variant=` (`4c7ec1c`)
- **Worker:** Codex `gpt-6-sol` high, ~11 min.
- **`src/game_variant.c`:** immutable descriptors for 2.1 and 2000, with name, log label, save namespace, episodes and demos. Selecting 2000 returned "unavailable" at this stage.
- **`src/game_data.c`:** the `GameDataProvider` took over the data search, with the same order as before (`--data`, executable/bundle folder, `TYRIAN_DIR`, cwd). It also does the refusal of 1.x and 2000, with the same messages, and only opens files for reading, with no absolute paths or `..`. `dataFileOpen` goes through it.
- **`src/bootstrap.c`:** reads `--variant=` and `--data` before SDL, config and saves. An unknown or conflicting value is an error; user files are disabled for regress/selftest right at the start; `JE_paramCheck` accepts both options without reapplying them.
- **Log:** one line at startup with the variant, the data root and the validation status.
- **Tests:** `tools/check_variant_bootstrap.sh`, called by `make regress`: `--variant` errors, identical hashes with and without `--variant=2.1`, short and abbreviated forms of `--data`, refusal with a 13-bank header generated by the script (no original data), no fallback between folders and no user file created.
- **Gate:** the 164 cases pass without changing baselines (81 s in the integrated tree).
- **Deviations from `integration.md`, accepted:** the descriptor only has the fields in current use; `gameVariantSelect` returns a status instead of `bool`; a repeated `--data` still takes the last occurrence, as before; the startup reordering for the launcher was left for 2b and Phase 6.

### [2000] Phase 1 completed: research
- **Worker:** Codex `gpt-6-sol` high, resumed at the user's request (~22 min). The previous attempt with Sonnet had been stopped before any edit, at the user's request. Documents reviewed and brought into `docs/t2000/`:
  - `fork-diff.md`: own diff of the fork against the merge-base `967c12e` (50 files, +1,292/−476 lines), 187 classification lines and the evidence for each gameplay difference;
  - `data-formats.md`: 2.1 × 2000 formats, file by file, and which of our loaders would break;
  - `integration.md`: risks per file, the Phase 2 C99 API (`game_variant.h`, `game_data.h`, `bootstrap.h`, `user_paths.h`) and the `regress-2000` suite.
- **Main findings:**
  - the file format is the same, and the counts change: a second bank of weapons and enemies, more strings, 13 shape banks, 31 sound effects (the voices change ID), 14 images and 24 palettes;
  - the 2000 save is 4,722 bytes, the same 2,502 as 2.1 plus unencrypted scoreboards. The 2.1 loader would accept the prefix and truncate the file on save, so separate namespaces are mandatory;
  - event 68 is a random explosion in 2.1 and an enemy replacement in 2000, and has to sit behind the variant;
  - the loaders for strings, items, shapes, images, sounds and credits would break with the 2000 data; the guard in `src/opentyr.c` refused that data before anything else;
  - the tag buffer's colors and classes already come out of the analysis of each sprite, so a per-ID color table is not needed.
- **Corrections to the plan:** five demos, not four, all the same as in 2.1; no Actions cache in the CI (a public repository's cache can be restored by fork PRs); the fork's "fix" in the upgrade menu does not apply to our code, which already protects the index; at the end of each item block 77 bytes are left over that the fork does not read (open, without inventing an ID); the fork marks the new events and the Flying Punch trail as approximations, so fidelity to DOS needs a manual comparison.
- **Validation:** GCC-16 C99 build with `-Werror` and the 164 cases passing, with no baseline change. No 2000 data entered the worktree or the documents, only names, sizes and checksums.

### [2000] Mapping and initial decisions
- **User's briefing:** Tyrian 2000 integrated into a single application, 16:9 launcher, installer that downloads straight from the official source with SHA-256, manual and GOG installation, separate saves, modern renderer in both variants and no 2000 data distributed.
- **Initial research:** the `KScl/opentyrian2000` fork is a thin layer (~1,300 lines in ~50 files) over OpenTyrian, 44 commits ahead of upstream and 19 behind; the official `tyrian2000.zip` (Camanis) is 5,051,363 bytes, SHA-256 `348bc76e…1667` and 100 files, with no license text (the `readme.txt` is Eclipse/Stealth's 1999 release note, and the freeware status comes from Camanis's announcement); our `EPISODE_MAX` was already 5 (`EPISODE_AVAILABLE` 4). Candidate fork pins: tag `v2000.20250408` (`573ccd6`) and `master` `aad5aca` (2026-02-22), which adds `dfe1050` ("Charging sidekicks do not auto-fire in Tyrian 2000"). The fork has no arena: only `--net` on the command line, never tested on 2000.
- **Correction to the briefing:** Modern Tyrian has no multiplayer arena, so the launcher cannot advertise one.
- **User's decisions:**
  - fork at `master` (`aad5aca`);
  - download from Camanis with the pinned hash; manual installation and GOG detection as alternatives;
  - 2000 data downloaded in the CI, checked by hash and running `make regress-2000`;
  - presentation and control shared between the variants; saves, high scores and progress separate;
  - **always open on the launcher, embedded in the binary**, including on the Steam Deck; `--variant=` only for regression and automation;
  - work on `modernization`, phase by phase, with 2.1 always green.
- **Plan phases:** 1 research, 2 variant/provider/saves, 3 fork core, 4 complete Tyrian 2000, 5 installer, 6 launcher, 7 modern renderer on 2000, 8 final regression.
- **Rules for this track's agents:** do not rewrite the whole fork, do not copy fork files in bulk, do not mix 2.1 and 2000 rules, do not change historical behavior by preference, never add 2000 data to Git, do not assume the data has the code's license, do not remove anything from 2.1, do not scatter variant `if`s; make shared abstractions, small changes, validate data before loading and keep the downloader out of the engine. Workers do not commit, push or switch branches and do not edit the plan's documentation files.

### Tyrian 2000 track opened
- The user passed a briefing to integrate Tyrian 2000 (launcher, data installer, separate saves, modern renderer in both variants). The track got its own plan and diary (later merged into this file).

---

## 2026-09-28

### Steam Deck test; Pentium detail fixed in Modern
- **Analog stick bugs found by the user on a real LCD Deck:** the ship only moved at an angle, with the stick and with the D-pad (the radial momentum target was applied whole to any nonzero axis, and the stick's rest noise contaminated the cross axis, including on the D-pad, which adds into the same slot); minimum movement came out in jerks. The `analog2` agent split the target per axis and made slow movement use only the sub-pixel step, with no momentum; the tests now use noise on the cross axis.
- **Analog on the Deck fixed (`467d25a`):** the momentum target is projected per axis like the step, and a stick within 5° of an axis snaps to it (zero deviation in the 9 cases with noise, including the D-pad). Below the maximum step the ship moves only with the sub-pixel step, with at most 1 px difference between ticks. A fully tilted stick remains identical to the original. Regression with 150 cases.
- **User's decision:** in Modern, the detail level is fixed at Pentium and the option disappears from the menu, so as not to offer a worse version; Classic keeps the option and the player's choice.
- **Pentium detail on the main branch (`420984d`):** in Modern the detail is fixed at Pentium (or SuperWild with the cheat) and the row disappears from the menu. Classic keeps the player's choice in `tyrian.cfg`, and a fresh install starts at Pentium. The detail flags are visual only: the state hash is the same at the six levels and in both presentations. The Modern regression cases now run at `-d4` (plus one `-d6`), and the suite ended at 151 cases. README updated (`c24a846`). The merge of the `pentium` branch (`bc79b21`) had been aborted midway by a pause from the user and was resumed later (removal of the conflicting Modern `-d2`/`-d3` baselines, renaming of the `stick-modern-noise`/`crawl` cases to `-d$MODERN_DETAIL`, `tools/regress.sh --update`).
- **New README (`67fa07f`):** in the antivirus-95 style, with banner and SVG headers generated by `docs/readme/generate.py`, captures regenerable by `docs/screenshots/capture.sh` and all options documented. It came back for a round of adjustments: captures without "INSERT COIN" and two claims corrected (Classic identical only with `--starfield-speed=100`; Modern's analog stick changes the movement).
- **Strength bar with a blue line (`a999780`):** the ceiling of the bars' highlight was `base + 15`, and the strength base (113) is not aligned to the 16-color block, so the full bar reached index 128, which is blue. The ceiling is now the end of the block (`base | 15`).

### Steam Deck, analog control and Quit modal on the main branch
- **Steam Deck (`54c411b`).** Deck detection (`SteamDeck=1` or Jupiter/Galileo board); on first run it opens fullscreen, in Modern, with automatic aspect (16:10 at 1280×800). The log goes to `~/.config/opentyrian/opentyrian.log` on the Deck, and `--log-file` (310) works on any system. The HIDAPI/libusb backend went into the static SDL3. Guide in [STEAM_DECK.md](STEAM_DECK.md), and a summary inside the `.tar.gz`. The package still ships with the 2.1 game data (user's decision; the CI already included the data, the plan item said otherwise and the `deck` agent raised pros and cons). Still to be validated on a real Deck (done later, see 2026-10-01).
- **Quit modal (`quit-centre`).** The box is centered and the shadow hangs as in the original, cut at the limit of the widened panel; at 21:9/32:9 the box stops at that limit. The high score name dialog had the same defect and was fixed along with it.
- **Regression interrupted by a signal.** During the merge, a case ended at frame 2210 with exit 0 and without the final message, and passed when run alone. SDL turns SIGTERM/SIGINT into a quit event, and the game exited with `exit(0)`, so the harness saw a clean exit with a truncated file (probable origin: another agent ending its own game processes). Now the regression turns off SDL's signal handlers (the harness reports "killed by signal") and a quit event aborts with an error.
- **Smooth movement, stage 4 (`1092367`).** Palette fades and HUD bars interpolated at the monitor rate; regression with 145 cases.
- **Analog control (merge `analog`), Modern only** — user request, part of the Deck support. The first delivery came back for correction: the stick displacement entered the acceleration (`accelXC` → `x_velocity`) 4× stronger than in the original analog, and the inverted-controls smoothie did not invert the stick in Modern. Approved version:
  - radial dead zone of 0–20% (default 10%, per controller, `--deadzone`);
  - linear curve up to 75% and a momentum target scaled by the curve, with monotonic steady-state speed up to the original 8 px/tick at 75%;
  - with the stick fully tilted, the trajectory is identical to the old analog; the network stays consistent because the transmitted accel is still ±1;
  - Xbox/Deck standard buttons, all remappable and persisted.

  On the diagonal the stick is radial: the maximum is 9.7 px/tick, against 11.3 of the old analog, which was faster on the diagonal. The regression gained a virtual stick (`--regress-stick`) and ended with 148 cases. The code already had proportional analog (`joystick_axis_reduce()` in `src/joystick.c`, which removes the `threshold` and divides by the `sensitivity`, adding into the `mouseXC`/`mouseYC` accumulator, limited to ±30 and applied as `(mouseXC ± 3) / 4` px per tick); the new curve replaces that reduction in Modern only.

### Four agents in parallel
- The user approved parallelizing where possible, in their own worktrees: `quit-centre` (center the Quit modal box; the previous fix centered the shadow and the box ended ~18 px to the left), `smooth4` (stage 4 of smooth movement), `analog` (radial dead zone of 0–20%, progressive speed up to 75%, remappable buttons) and `deck` (readiness of the Linux `.tar.gz` for the Steam Deck: glibc, SDL3 backends, defaults at 1280×800, logs, "Add to Steam" guide).
- The ambient particles still had no position set in the queue.

### Pause, muzzle, glass HUD and stars at 25%; rare CI failure resolved
- Integrated into `modernization`:
  - pause (P), menu (ESC) and help (F1) keep the Modern HUD on the sides, with "PAUSED" centered on the playfield; a new regression case opens the menu on the last frame (`7fd7a72`);
  - shot flash centered on the visible sprite of the projectile, the ship and the enemies (`7f4593d`);
  - vertical bars with a gradient like the original's `JE_dBar3` and "glass" side panels (background blurred at 68%–32%) with a 1 px shadow under the text (`ab66dae`);
  - background stars at 25% of the previous speed, with sub-pixel movement, in Classic and Modern; `--starfield-speed=PERCENT` (10–100) and the `starfield_speed_percent` key in the config adjust it (`0ffde66`).
- **Rare CI failure on Windows (arm64):** the cause was the Modern canvas allocated with `malloc` and read before being fully written on frame 0. macOS hands out zeroed pages and Windows does not, which is why only frame 0 of the Modern demo1 cases diverged, with a different hash each time. Confirmed locally by filling the canvas with 0xAA; fixed with `calloc` (`f9f0782`).
- The menu case gained its own frames case so that `tools/regress.sh --update` generates its baseline (`19059cc`).
- **User request:** analog control with configurable dead zone and a progressive curve up to 75% (delivered, see above).
- **User saves erased by the regression (fixed).** The `--regress-script` cases play a real level and reach the "LAST LEVEL" autosave at the start of the level; since the regression does not load the saves, `saveSaves()` wrote the empty table to `~/.config/opentyrian/tyrian.sav`. Every `make regress` (mine or the agents') erased the user's saved games. Now regression and selftest cut access to the user directory (`userFilesDisable()`); config and saves are never written in these modes.

### Steam Deck on the roadmap
- The user set Steam Deck support as a priority (2026-09-28): Linux x86_64 build as a `.tar.gz` with the binary directly, no AppImage/Flatpak, statically linked with SDL3/SDL3_net and depending only on glibc (≥ 2.34), with the SteamOS backends loaded at runtime. **User's decision:** the package carries the same as the original OpenTyrian distributes, only the freeware Tyrian 2.1 data (`tyrian21.zip` from camanis.net via `get_data.sh`) and its license; nothing commercial, to avoid legal trouble. Release generated by the CI from a tag. To validate on the Deck: 1280×800 (16:10) fullscreen with the panels in the 16:10 geometry, native controller with no Steam Input template, "Add to Steam" from Desktop Mode with a step-by-step in the README, easy-to-find logs, performance at 60/90 Hz.
- Also added to the roadmap, for the future: CRT filter with NTSC/Composite (Blargg) and scanlines (delivered on 2026-09-30).

### Batch of fixes from the user's test; next priorities
- Integrated into `modernization`: Load/Save, Quit and Ship Specs screens (`b55bcde`); light only from shots, explosions, items and VFX, with a subtle retune (`76af276`); readable WARNING, star flash, aligned muzzle and fixed RNG order of the VFX, which was the cause of the red CI (`c4d3b7f`); music stopped when going back to the logos (`eb8c9b0`); Modern HUD follows the level fade and clean intro (`397b5db`).
- The CI of `c4d3b7f` failed only on Windows arm64, with two cases ending with no output ("first differing line 0"); the next commit passed on the three platforms (the real cause, the canvas `malloc`, was found the next day, see above).
- In progress, in their own worktrees: fast stars on ASTEROID/ASTEROID2 (`starfield`), off-center GAME OVER in Modern (`gameover`), HUD with vitals on the right and vertical bars (`hud-layout`).
- **Priorities set by the user for after the batch:** (1) tidying the plan and the rare CI failure on Windows arm64; (2) sharpness on Retina (`SDL_WINDOW_HIGH_PIXEL_DENSITY`); (3) finishing smooth movement (stage 4); (4) light with the object's color; (5) accessibility in the future; (6) the position of the ambient particles in the queue was left to be confirmed.
- **Modern HUD, layout (user request):** swap the sides, with shield, armor and generator/strength on the **right** panel, as in the original, and weapons and sidekicks on the left; the bars of those three become **vertical** and a bit longer. Applies to 1P, 2P and arcade and to 16:10/16:9/21:9; goes in after the `hud-fade` merge, because both touch `src/modern_hud.c`.

### Resumption: scaling and VFX on the main branch
- No ghost opencode sessions after the reboot. The Windows CI of `a5aff13` also went green.
- **Scaling and light levels (`159c519`, merge `ecf68e0`).**
  - Software scalers removed: the frame is converted at 1x and the GPU scales.
  - Fit uses sharp bilinear: integer nearest pre-scaling into a cached render target, then a linear pass. Pixel edges fall on a uniform grid (rms 0.04 px, against 0.28 px for nearest).
  - Modern: always Fit with PAR 1.2; Scaling Mode and Pixel Aspect are hidden in the menu.
  - Light: single Off/Low/High selector, default Low. High = the old low, Low = half; `medium` becomes an alias of high.
  - Cost on the software renderer: +1.9 ms/frame, the upper bound; on the GPU it is one extra quad. Real window at ~119 fps.
  - On merge, the two `modern-light-*` baselines changed only in the intro/fade rows from the HUD fix.
- **VFX part 1 (`e88167b`, merge `fbd9d80`).**
  - Smoke and rings use the engine's nibble blend/darken, with no dithering.
  - Off/Low/High levels, default Low; `medium` becomes an alias of high. Cost with demo2 in Modern 16:9: +0.007 ms/frame on high.
  - On merge: the Graphics menu gained "Lighting" and "Effects" (the Effects item also became clickable with the mouse, which was missing). The `vfx-*` ones changed only in the intro/fade rows.
  - Visual Studio's `.filters` had ended up with invalid XML when the scalers were removed and was fixed; `vfx.c/h` went into the project. 128/128 in the regression.
- **To investigate (open at the time):** the window does not request `SDL_WINDOW_HIGH_PIXEL_DENSITY`. On a Retina screen, macOS upscales a point-sized backbuffer 2x, so the sharp bilinear runs at logical resolution.

### Nuked OPL3, jitter and HUD fixes; pause to shut down the machine
- **Nuked-OPL3 (`570aa48`, merge `1536c6c`).** Upstream `765ec962` (LGPL-2.1+), in OPL2 mode, generated at 49716 Hz and converted by a continuous polyphase resampler. It is integer-only, with Nuked's own noise LFSR, and Park–Miller is gone. Cost of ~3% of the audio callback. Only the music/mix rows of `audio.txt` changed. As a bonus, a bug in the resampler's phase decomposition when lowering the rate was fixed. CI green on the three systems.
- **Bugs from the user's test fixed (`d9c9c21`, merge `a5aff13`).**
  - Jitter: the continuous pan origin (`x − 24·bp`) is interpolated and then re-expressed against the current tick's map. The new `--regress-interp-smoothness` flagged 303 events on demo1-d2 and now flags 0.
  - Flickering classic HUD: the gameplay composition is held during the intro, death fades and end-of-demo fades and the end-of-level animation. `--regress-gameplay-check`: 0 frames without the panels.
  - The new options ended up at 287–289; 285/286 are reserved for VFX. The Modern gameplay baselines changed only in the intro/fade frames.
- **Pause (user went to shut down the machine).** Two jobs (VFX round 1, in `refs/keep/vfx-wip`; scaling round 2, in `refs/keep/scaling-wip`) were saved in a ref with a handoff note, and were resumed and integrated in the next entry (merges in the order scaling → VFX, because both touch the Graphics menu in `src/opentyr.c` and in `params.c`/`config.c`). They were functional and green; the VFX report had §9 updated to 125/125.

### Regression green on the CI on the three systems
- **`b4f42b6`: 123/123 on Linux (x86_64, arm64), macOS and Windows (x86_64, arm64).**
- Three causes of non-portability fixed:
  1. CRLF in the Windows checkout: `.gitattributes` with `eol=lf` for `test/regress/**` and the scripts.
  2. Audio: SDL's resampler uses float/libm, and the OPL noise used libc's `rand()`. Now there is an integer polyphase sinc and a local Park–Miller (later replaced by Nuked's LFSR).
  3. State hash: `Player.cash` is `unsigned long` and was hashed with `sizeof`, which gives 8 bytes on LP64 and 4 on LLP64. Now all scalars go as fixed-width little-endian, without changing the baselines.
- All frame hashes matched across platforms from the first run: the game's drawing is portable.
- The full sweeps (`--replay-check`, `--interp-check`) stay in the manual workflow `regress-full.yml`.

### Lighting on the branch; audio converter; levels redefined by the user
- **Lighting (`ceadcf3`, merge `6b3d7cc`).** Bloom + dynamic light map in `src/modern_bloom.c`, only on the playfield of Modern gameplay frames. In review, the square halos (three box passes, isophotes at ~9% of round), the block upscaling (now bilinear), the blown-out cores (screen-style blend) and the excessive strength were fixed. Cost < 0.5 ms. The options became 281–284 on merge.
- **User's decision:** even "low" was too strong. Three levels, **Off / Low / High**: High = the "low" of then, Low = half. A single "Lighting" selector in Setup → Graphics controls bloom and light together. Default: Low.
- **Audio converter (`1c16c81`, merge `34093b2`).** Polyphase sinc with a Kaiser window in integers, the same on any platform. Flat up to 4.98 kHz and rejection > 90 dB (SDL's let the first image through at −6 dB). Only the sfx/mix hashes changed. In the same conflict resolution, the `.vcxproj` went back to CRLF (the lighting merge had converted it to LF).
- CI on Windows: the frames match; the 4 `state-*` cases failed from the first line because of a type width in the state hash (LLP64) — fixed in the entry "Regression green on the CI".

### User tests: bugs and scaling decisions
- The user tested the binary. Three bugs became tasks, and two decisions were made:
  - **Bug: the scenery and clouds "jump" when the ship moves.** Cause: the horizontal pan follows the ship. The background row's x wraps around (`mapXOfs % 24`) while the map pointer advances a tile, and the interpolation slid ~23 px the wrong way on every tile. Task `interp-fix`, with a smoothness check in the regression (fixed, see above).
  - **Bug: the classic HUD flickers at the start of the level.** The first frames came out as a menu frame. Same task.
  - **Bug: "Fit 4:3" and "Fit 8:5" were the same in Modern.** Unified into "Fit"; the difference moved to Pixel Aspect, which now also applies in Classic. Task `scaling`.
- **Decision:** the correct pixel aspect for the art is Original (1.2, drawn for 4:3 CRT). In Modern, the user should not be able to choose the worst option. Pixel Aspect and Scaling Mode leave the Modern menu, and scaling is always "sharp bilinear": integer pre-scale with nearest and then the final fractional fit with linear. This leaves uniform pixels, without mixed 4 and 5 px lines that shake on scroll, with exact aspect. Classic's Fit uses the same path.
- **Decision:** the software scalers (hq2x etc.) are removed from the game: "we only want pixel-perfect pixels". The Classic window now uses integer-multiple sizing.

### Audio: only the Nuked OPL3 emulator
- Diagnosis: mono output; OPL2 emulator derived from DOSBox from 2010; mixer with hard clipping; 8-bit effects at 11 kHz.
- In the CI, the agent had swapped SDL's sinc converter for linear interpolation just to make the audio test portable. Rejected in review, because it worsens the player's sound. Asked for its own polyphase sinc converter, in fixed point and identical on all platforms, with quality equal to or better than SDL's and a measured frequency response.
- **User's decision:** of the proposed improvements (positional stereo, Nuked OPL3, soft limiter and an OGG track supplied by the user), only **Nuked OPL3** (reference emulation of the chip, LGPL-2.1+, compatible with GPL-2+). The other three will not be done.
- Order: Nuked starts after the sinc converter on the branch. It generates sound at 49716 Hz and needs that converter, in continuous mode, to reach 44.1 kHz.

### S2 screens delivered; first CI with regression; reference becomes the official zip
- **S2 (`2b27f5e`, merge `38ab9f1`).** In Modern, jukebox, ship specs and credits draw on an 8-bit surface the width of the canvas (`modern_screen_begin()`), with the game's own routines. The jukebox star projection only reads state: it spends no RNG and writes no state.
  - Navigation map: the first version (wide star chart behind the centered frame) was rejected in review, because the screen jumped position relative to the other shop screens and showed a grid to the right of the panel, with no spatial meaning. It ended up with S1's widened panel.
  - What blocked the widening was the right margin that the map paints (x 314..319). The cut now ignores elements beyond the last possible cut column (311), and a full-height fill in the bottom strip follows the edge.
  - Also coming out in review: ship specs ship displaced twice (it covered the text); credits text centered in the black area, to the left of the art; map test state (palette 18 and level list).
  - The weapon simulator stays as in S1: widening the window would require redoing the shop layout.
  - 5 new screens in `--regress-screen` and 12 cases; the regression ended at 121.
- **Maintenance (`7909049`).** The Setup → Graphics menu gained Presentation, Aspect, Pixel Aspect and Smooth Motion; before, Modern was only reachable through the cfg or the command line. The options apply immediately and are dimmed in Classic. Modern opens a window in the chosen format, at ~80% of the usable screen area. The README documents all options. The data lock (`test/regress/data-manifest.txt`, size + POSIX `cksum`) refuses different data and says which file does not match. The Visual Studio project went back to listing all the sources, without a tested compilation. Regression with 109 cases.
- **Discovery:** the official zip downloaded by `./get_data.sh` has `newsh9.shp` with 38831 bytes; the local reference copy had 34888 bytes, identical to its `newsh^.shp`. That is why a fresh clone could not run the regression, and the CI never ran `make regress`: it only compiled.
- **User's decision:** the test reference becomes the official zip from `get_data.sh`, and the CI now runs the regression (worktree `regress-ci`: swap the manifest, regenerate only the affected baselines with proof that the difference comes from `newsh9.shp`, create the CI jobs with a data cache — later the cache was removed for 2000 and the lock came to check the official zip).
- **First CI with regression (`bdfc0e0`):** macOS green. On Linux (x86_64 and arm64), only the `audio` case failed, with all frames and states matching the baselines generated on the Mac. On Windows, the data lock refused everything, probably because of CRLF in the checkout.

### 60 fps or more: stages 1–3
- **Stages 1–2 (`f35198f`, merge `f7535d7`).** Per-tick draw list at the primitive level, with object identity, and replay with proof of byte-for-byte equality (82/82 cases; ~0.17 ms per frame). On merge, `--regress-replay-check` became option 275, next to `--regress-screen` (274). The regression ended at 103 cases.
- **Stage 3 (`09441a8`, merge `b9b65da`).** Modern in-game presents at the monitor's rate with interpolated motion. While waiting for the fixed tick (`delayUntilElapsed` in `JE_starShowVGA`, now `interp_present_gameplay()` in `src/interp.c`), the game presents frames at `alpha = elapsed / tick period`, with vsync. Without vsync, it sleeps until the next refresh; it never spins. The deadline stays on the tick's absolute grid.
  - Measured on a real window on this Mac (120 Hz panel, although `SDL_GetCurrentDisplayMode` reports 60): **~120 fps** presented, p99 of 9–9.5 ms, logic at ~34.8 ticks/s, same as the original. Cost of ~1.1 ms per presented frame at 16:9.
  - Matching by identity + order within the identity, with a fixed hash index. Snap on birth, jump > 64 px and sheet change (slot reuse). Background rows match by the map row with the pan removed (`row_key`), so the scroll does not jump at the tile rollover (stage 1's identity, layer and screen row, jumped 28 px when the map advanced a row of tiles). Starfield and superpixels are interpolated by index. HUD, texts and boss bars use the current tick.
  - The renderer keeps a reference of the tick's frame: the water, lava, blur and iced filters blend with the destination and start from it. At 120 fps these filters are applied over intermediate frames; the tick's frame stays exact.
  - Proofs: `--regress-interp-check`, with alpha = 1 byte-for-byte equal to the real frame in 52 cases and 0 overshoots. `--regress-interp-alpha=A` captures intermediate frames, and `--regress-realtime` measures the pace. Regression with 107 cases.
  - `smooth_motion` option in the cfg's `[video]` (on by default) and `--smooth-motion=on|off`. Only applies in Modern.
  - Process lesson: after a `worker_done`, the review fix has to go through `worker-start --terminal` in the same terminal. A plain `terminal send` works, but the following `worker_done` is rejected for revoked capability, and only the screen confirms completion.

### Screens outside the game: S1 delivered (`296cf7e`)
- Title and pic 2 menus with Vert-; all the shop frame screens (game menu, upgrade, purchase, options, data cubes, keyboard, joystick, load/save) with the widened right panel; pic 5/11 with a solid border. No pic 1 screen fell back to blur.
- The fixed cut columns of the first attempt (158/310) blocked almost all screens: the footer help row crosses the whole screen and the data cube reader goes up to x=310. The solution was a single cut, decided by content: right after the panel's rightmost element (y < 184), with the bottom strip handled separately (the background follows, the help text stays in place).
- Accepted side effect: the box title ("Game Menu") is not recentered, because a second cut on the left would cut the help row. The highlight strip of the selected item ends where the original panel ended.
- `--regress-screen=NAME` renders 13 screens without a window with the game's own functions (in screen mode, `hasInput()` says there is input, and the frame limit ends it). That is 28 new cases: Modern 16:9 and 21:9, plus the Classic ones, which prove that the harness does not alter the 8-bit screens. The regression now has 99 cases.
- Mouse: the piecewise mapping was verified by a temporary check (the inserted strip falls on the cut column, inside the items' clickable area).
- Navigation by simulated keys did not work on this machine, hence `--regress-screen`.

---

## 2026-09-27

### Resumption after shutdown; pause to shut down
- **Pause:** the screens' S1 was deliberately interrupted before the shutdown so that the opencode service would not resume the session on its own. The work stayed uncommitted in the working tree (`src/modern.c/.h`, `video.c/.h`, `picload.c`, `keyboard.c`, `mouse.c`), with a backup in `refs/keep/screens-s1-wip` (stash `507e703`) and a handoff note in `.worker-reports/phase1-screens-s1-handoff.md`. The CI was red on Linux and Windows for a missing `#include <stdio.h>` in `src/modern.c`; the fix went in together with S1. On resumption: check that no opencode session was left busy, hand S1 to a new agent and remove the test hooks, paying attention to keyboard events injected in `keyboard.c`/`mouse.c`, which are outside the agreed scope.
- **Resumption:** no ghost session. S1 went to a new agent with the note, in the main checkout, which would also create `--regress-screen`. The user asked for priority on 60 fps or more and very much wants to see the lighting improved. In parallel, in separate worktrees: `framerate` (design and stages 1–2) and `lighting` (bloom plus dynamic light map from the palette's emissive pixels; shots, explosions and flames light the terrain with their own color; per-object lights would come later, fed by the draw list). The S2 screens waited for S1, because they use the same compositor.

### Only the new HUD in the match; blur in menus
- On game frames with panels, the compositor copies only the 264×184 playfield. The original side bar and bottom strip are still drawn in the 8-bit frame (logic intact), but do not enter the canvas. The panels end up 81/82 px at 16:9, and 16:10 now has panels (60 px). The 16 pixels under the playfield become a strip with the level name and the game message, which arrives through a read-only hook in `JE_drawTextWindow` and in the message erasing. The HUD gained the weapons' energy bar (`PWR`), the only vital datum that only the original bar showed.
- Screens outside the game swapped the ambilight for blur of the screen itself.
- Review: complete bar→HUD inventory, captures checked (1P and 2P at 16:9, 16:10, 21:9, arcade, real title and menu screens), hooks checked (`--textErase` is still evaluated the same), clean GCC audit, `make regress` 71/71 twice. The `state-*` ones and the Classic and 4:3 baselines did not change.

### Reboot, ghost session and complete HUD (stage 2) — `5a5952b`
- The Mac rebooted in the middle of the stage 2 fixes and took down Orca, the agent and the scratchpad. A new agent finished the fixes: lives and money on separate rows, names with extra spaces trimmed before the cut, the special icon reserve only in 2P, and the case `state-scenario-spotlight-2p-d3`, the first that runs the lives block, and therefore the writes to `tempW`.
- Intermittent failures in review ("no output written" in demo1-d6, divergence in iced) came from a ghost session contending for `test/regress/actual/` (see the technical finding "opencode sessions come back on their own" in [MODERNIZATION.md](MODERNIZATION.md)). With it interrupted: 70/70 twice, clean GCC audit. The ghost session also revealed that the old frame 855 failure was the wrong `data/`.
- The user approved the look ("very pretty"), but does not want the original and the new HUD together (decision 2026-09-27: only the new HUD in Modern; lives follow the original — only in arcade and 2P — and shield and armor keep bar and number).

### Modernized HUD, stage 1 delivered
- The `hud` worktree agent draws the elements that used to be inside the playfield onto two off-screen 8-bit surfaces, one per panel, and `modern_build_frame` composes those surfaces over the ambilight. The minimum width of 51 px comes from the boss bar. At original 16:10 (32 px) and square 16:9 (18 px), everything stays inside the playfield, so as not to leave a half HUD.
- Proof: the state-hash streams of demo 1, demo 3 and spotlight are byte-for-byte equal between Classic and Modern 16:9 with the HUD moved. That became the 3 `state-*` cases. Only the 3 `modern-wide-*` baselines changed, as expected.
- Review: diff checked (all `tempW` writes preserved; the boss bar update is still separate from the drawing), 1P real and 2P captures with boss bars (headless probe). After the merge: build ok, clean GCC audit, gamepad self-test ok, `make regress` 69/69.
- For stage 2: at 16:9 the panels had only ~53 px, so a one-column layout fit; at 21:9 more fits. Long player names in network play need truncation. P2 should be aligned to the outer edge.
- **Preparation:** the HUD drawing has side effects on the logic (`JE_inGameDisplays` writes to the global `tempW`, which the pan reads; `draw_boss_bar` updates `boss_bar[]` while drawing). The task created the permanent proof `--regress-state-out`, a hash of the game state per frame.
- **Decision (accepted by the user):** the widescreen side space goes to the HUD, in two stages — (1) elements that used to be inside the playfield into the panels; (2) complete panels with weapon names and level, sidekick ammo and charge, numeric shield and armor and 2P layout, with a prototype and captures for approval. All on the 320×200 grid, with the game's fonts and sprites and procedural frames.

### Harness immune to real input
- The agent closed the leak at the input boundary: `handleSdlEvents` discards everything but `SDL_EVENT_QUIT` in regress mode; `poll_joysticks` is inert; `windowHasFocus` is fixed at true (the release build pauses on its own without focus); `SDL_HINT_MAC_BACKGROUND_APP` is turned on before `SDL_Init`.
- In this session `osascript` could no longer deliver keys to the process, which never became frontmost. The proof used a temporary in-process injector: an Up key via `SDL_PushEvent` at frame 855 of the flip scenario. Without the discard, 3/3 diverge from frame 858; with the discard, 3/3 match the baseline.
- Review: diff of 4 files, build ok, clean GCC audit, gamepad self-test ok, `make regress` 66/66.
- Note: the Visual Studio project in `visualc/` had not listed `modern.c` or `regress.c` since Phase 0 (fixed in the maintenance `7909049` of 2026-09-28); the Windows CI uses MSYS2 and was not affected.

### Gamepad integrated
- The `gamepad` branch went into `modernization` in merge `85f741a`. The conflicts were only additions: both README sections stayed, and `--selftest-gamepad` became option 269, after the widescreen options (266–268). After the merge: build ok, clean GCC audit, `--selftest-gamepad` passed and `make regress` closed at 66/66.

### Widescreen and PAR delivered (Phase 1a); extended view dropped
- The widescreen agent delivered the Modern mode's `aspect` and `pixel_aspect` settings (`b1e4313`). The canvas has width `round(200 × PAR × aspect)`, with the original frame centered and procedural, deterministic, darkened "ambilight" sides. During the game they use column 263 of the playfield, skipping the HUD. Mouse mapping accounts for the frame offset, and the cost is ~65–72 µs per frame at 16:9.
- In review: diff read, screen captures checked (4:3 and 16:9, original and square), `make regress` with 66/66 (the 63 old ones with baselines intact, plus 3 `modern-wide-*` at 16:9) and clean GCC 16 audit. The agent ended the turn before finishing and was resumed (technique for unblocking opencode workers recorded in the global CLAUDE.md).
- Minor pending items noted by the agent: in Integer mode, with PAR 1.2, a small window (640×400) ends up with almost square scale; and the window still follows the size of the Classic scaler. (Resolved in the scaling rework of 2026-09-28.)
- **Extended view dropped by the user** after the measurements (next entry): of the three options (background only, background with objects, give up), they chose to give up. The widescreen sides stay with only the procedural fill.

### Extended view measured
- An agent in a separate worktree instrumented the 8-bit renderer (then reverted everything), swept the whole pan and ran the 62 levels of the 4 episodes plus the 5 demos. Full report: `.worker-reports/phase1-extview-investigation.md`.
- **Result:** it is possible to show ~24 px of real art per side, with the right one poorer (64% covered on average). Showing the enemies in those strips also runs into the drawing gate of `JE_drawEnemy` and would require widening the level framebuffer. Showing objects there would give the player information that the original did not.
- **Numbers:** each background layer is drawn as a 12-tile window (288 px) sliding over a 336 px (bg1/bg2) or 360 px (bg3) map. Summing the whole pan range, there is art at x ∈ [-45,315) in bg1, [-41,342) in bg2 and [-69,369) in bg3, with the playfield at [24,288). The margin guaranteed at any pan is asymmetric: ~45 px on the left and only 3 px on the right (7 px where there is dense bg2/bg3). Six levels have no background at all (E1:L1, E2:L5, L7, L8, L9 and E4:L14). With 24 px per side, the left strip has on average 94% real art and the right 64%. Already-simulated enemies appear in these strips in 11–21% of frames, but `JE_drawEnemy` only animates and draws enemies at x ∈ (-29,300), which limits the reveal on the right to ~12 px, and shots visibly vanish at the right edge in ~1.5% of frames. Two cautions for any future implementation: `blit_sprite2*` used the global `VGAScreen->pitch` instead of the surface's pitch (`sprite.c:559,636,672,708`, fixed later), and `draw_background_2/3` advance the scroll inside the drawing function, so they cannot be called twice per frame.

### Gamepad delivered; harness vulnerable to real input
- The gamepad agent (worktree `gamepad`) went down once with an HTTP 400 from the provider and was resumed. It delivered SDL3's Gamepad API with standard mapping, hot-plug and remapping by name in the cfg; the legacy joystick path did not change. Approved; commit `1a33820` on the local branch `gamepad`; `--selftest-gamepad` self-test with a virtual gamepad (51 checks). Integration was left for after widescreen, because both touch `README.md`, `params.c` and `opentyr.c`.
- In verification an intermittent failure appeared in the `flip` scenario (5 cases, around frame 855), attributed to real input injected by the `osascript` of the widescreen agent's captures; isolated, it passed 3/3. It later turned out that the probable cause of the failure was another one: the repository's `data/` had a `newsh9.shp` different from the official Tyrian 2.1, which diverges exactly at frame 855 of the flip scenario, when that sprite appears. The protection against real input remains valid as hardening.
- SDL3 hot-plug events: `SDL_EVENT_GAMEPAD_ADDED/REMOVED` arrive even with events disabled, but `SDL_EVENT_JOYSTICK_ADDED/REMOVED` do not; a gamepad generates both pairs, so it is necessary to deduplicate by instance id.

### Question about widening the horizontal field
- The user asked whether the native horizontal scroll could be used to show more field in widescreen. Analysis: the "scroll" is only the background parallax (front layer ~70 px, middle ~47, back ~24), the maps are 336–360 px, and shots and enemies have removal limits close to the original edge (enemies exist from -80 to 340; enemy shots vanish at x ≤ 0 / > 275 and the player's at x < -34 / > 290). Only ~20–24 px per side are viable without inventing art or changing gameplay. The initial decision was to go ahead with the extended view at that limit (replaced later after the measurements: dropped). Changing despawn limits to widen further was out of scope, because it would alter the balance.
- **Splash screens and menus:** the user asked whether widescreen was possible on splash screens and menus. Answer: yes, filling the sides with an enlarged, blurred and darkened copy of the screen itself; reorganizing each menu to use the width would require redoing screen by screen, for a small gain. The user later preferred real content where viable (Vert-: enlarge the image to the width and crop top and bottom, ~25% of the height at 16:9; authorized on splash screens, the main screen and in-game menus), with an inventory deciding screen by screen between Vert-, real extension/reorganization and blur. The inventory (`.worker-reports/screens-inventory.md`, tool `tools/dump_screens.py`) showed that no image has art beyond 320 px: Vert- works on the title (the crop only takes sky and the bottom of the planet, and logo and menu are separate layers) and is acceptable on the menus' pic 2; on pic 1 (shop, weapons, data) it breaks the frame and misaligns the windows, on the logos it crops art and on the story it crops unique art. Navigation map, ship specs, starfields and credits get real extension. Pic 2 loses the line "AN EPIC MEGAGAMES PRODUCTION ©1994" embedded in the footer. For pic 1, the user preferred widening the plain right panel by repeating a strip of its inner columns (original art at 1×, no blur; it is only composition, the game still draws 320 px), with the ship frame on the left.

### CI green on the three platforms; Phase 0 completed
- Actions was triggered manually through the API (on forks the push did not trigger it the first time; later it started triggering). It took 4 rounds to get everything green: (1) `libxtst-dev` missing on Linux and an outdated MSYS2 database without `sdl3-net` on Windows (`108e69a`); (2) `palette.c:87` (`const` pointer-to-array), which GCC rejects with `-pedantic` and clang accepts; an audit with GCC 16 on the 55 files confirmed it was the only one; (3) `PKG_CONFIG_PATH` only in the compile step, which broke `make install`; (4) green on Linux x86_64/arm64, macOS and Windows x86_64/arm64 (`e5a6d11`).
- Lesson: audit every change in `src/` with `gcc-16` and the CI flags. Addendum: the audit on macOS also does not catch a missing standard header (e.g. `snprintf` without `<stdio.h>`), because the SDL headers on macOS already pull in those headers; only the CI on Linux/Windows catches it, so push early and watch the CI.
- **Phase 0 completed.** Phase 1 started in parallel: widescreen/PAR in the main checkout and gamepad in a local worktree.

### Modern pipeline on the CPU (Phase 0d) and first real CI
- A new agent delivered the modern pipeline and the Classic/Modern setting, approved on the first review. The 63 cases pass. Without passes, Modern comes out byte-for-byte equal to Classic's "None" scaler; cost ~37 µs per frame. Verified with a real window in Modern mode. The passes' contract (`src/modern.h`): `void pass(ModernFrame *)`, they run in registration order, once per presented frame, over the canvas on the logical grid; deterministic, no RNG, without touching game state; they may read `src` (8-bit indexes) and `palette` and write `pixels`.
- CI round 1: macOS green; Linux broke for lack of `libxtst-dev` and Windows because of an outdated MSYS2 database without `sdl3-net`. Round 2: macOS and Windows arm64 (clang) green; Linux and Windows x86_64 (GCC) failed on `palette.c:87`. (See the entry above.)

### Decision: modern composition on the CPU, no GPU backend of our own
- The "GPU backend with palette in the shader" was replaced by a composition pipeline on the CPU (technical decision by the coordinator, revisable). Since all effects stay on the 320×200 grid, the cost of light, bloom, particles and widescreen on the CPU is trivial: 64 thousand pixels per frame, well under 1 ms. Doing it on the CPU keeps everything portable, with no shaders for Metal/Vulkan/D3D, and deterministic, so Modern's output can be covered by hash baselines like the rest. SDL3's `SDL_Renderer` remains only to upload the texture and scale with nearest. If any effect requires screen resolution in the future, we reassess.
- **User's decision (frames above 35 Hz):** the user wants at least 60 fps, preferably the screen's refresh, and the logic must not depend on the frame rate. Logic on the original fixed tick, drawing by interpolation from a draw list recorded per tick (Phase 2), right after the menu screens.

### CI and packaging on SDL3 (Phase 0c-3b) — `55baaae`
- A new agent migrated `make_macos.sh` (official SDL3 3.4.16 and SDL3_net 3.2.0 frameworks, pinned by SHA-256, universal app), `make_linux.sh` (static SDL3 compiled from source, pinned and verified), the 3 workflows, the MSVC project and the README. Approved on the first review.
- Validated locally: the generated app is universal (x86_64 + arm64), loads the frameworks from inside the bundle (no Homebrew path), opens without a window and reaches the title screen; `codesign --verify` passes; 53/53 in the regression test.
- GitHub Actions had never run on this fork (0 runs, the default on forks); Linux, Windows and MSVC would only be validated after the owner turned on Actions.

### Network on SDL3_net (Phase 0c-3a)
- A new agent ported `network.c` to SDL3_net with the same protocol, approved on the first review. Two headless instances on 127.0.0.1 complete the CONNECT/ACK handshake. The in-game lock-step was not exercised, because it needs input from both sides.
- Intentional behavior change: a host that does not resolve now produces an error after 10 s; before, the game hung silently.
- The two pending audio adjustments went in in the same package: static buffer in the callback and a 1024-frame hint. The 53 cases still pass.
- Note: Homebrew's `sdl3-net.pc` comes with an empty `prefix=`. That is why the Makefile takes the paths from `sdl3` and only the library names from `sdl3-net`, which may fail on systems with the two packages in different prefixes, such as the static Linux build. Installed on this machine: `brew install sdl3_net` (3.2.0).
- There were 38 `SDLNet_Read16/Write16` calls scattered across `mainint.c`, `tyrian2.c` and `game_menu.c`; today they use `network_read16/write16` (`src/network.h`). The protocol is point-to-point UDP, 2 players, in lock-step with the game loop; SDL3_net has no bind, so the filter by opponent address and port is explicit.

### Core migrated to SDL3 (Phase 0c-2)
- A new agent ported the 44 files to SDL3's native API, approved on the first review. The 53 baselines pass without regeneration, including the audio byte for byte (`SDL_ConvertAudioSamples` produces the same bytes as sdl2-compat's `SDL_AudioCVT`).
- Points of attention handled: inverted error checks (`bool`), `SDL_GetTicks()` truncated to Uint32, explicit nearest scaling, per-window text input, desktop fullscreen via `SDL_DisplayID`, audio via `SDL_AudioStream`, and headless drivers with the new names (`SDL_VIDEO_DRIVER`/`SDL_AUDIO_DRIVER`).
- Verified with a real window on macOS: the title screen appears with sharp pixels and the correct palette; the keyboard navigates the menus; Alt+Enter toggles fullscreen, back and forth. Not yet manually verified at the time: text typing (high score and save name), mouse, joystick and sound on a real device.
- The CI was broken on this branch until Phase 0c-3, because it still installed SDL2.

### Audio baseline delivered (Phase 0c-1)
- A new agent delivered `--regress-audio`, approved on the first review. The audio callback was extracted unchanged into `audio_mix()`, which the harness calls directly, without opening a device. The negative tests (volume, conversion rate) break exactly the expected lines.
- The SDL3 migration was split into three sequential tasks: (1) audio baseline; (2) core; (3) network, CI and release.
- Findings: the audio pipeline is mono, S16, 44100 Hz (`11025 × OUTPUT_QUALITY`), 1024-sample buffer; the music is an OPL emulator plus the LDS player in pure C, without SDL; the effects are 29 in `tyrian.snd` and 9 voices in `voices.snd`, at 8 bits and 11025 Hz; the only resampling happens in `loadSndFile()` (`src/nortsong.c`); the OPL percussion used libc's `rand()`, not `mt_rand`. The baselines depend on libm and the machine's toolchain (`powf`/`pow`/`sin`, `cosf`/`sinf`), so they were valid for this machine (macOS arm64, clang) until the CI gained portable baselines (see 2026-09-28).

---

## 2026-09-26

### Smoothies coverage delivered (Phase 0b)
- A new agent delivered 5 synthetic scenarios that cover the 6 rendering paths that the demos did not exercise; approved on the first review (commit right after `d09f352`). Verified: the 52 pairs pass (~34 s), there is no fixed path in the files, and the scanner runs. The negative test (changed constant in `water_filter`) breaks only the 4 water scenarios.
- Findings: the demos (`demo.1`–`5`) never activate `smoothies[]`; map of the levels that turn on each smoothie (lava in E1/L16 and several in E4; water in E1/L17 and many in E4; blur and iced blur only in E4; spotlight in E1/L15 and E1/L16; vertical flip in E4/L12 and E4/L13; bits 7 and 8 never used); the jump events (type 54) and conditional jumps (61/66/70/71) make the static sweep unreliable (`tools/scan_smoothies.py` lists candidates, only execution confirms); `lvlPos` stores two entries per level and the last one of episode 4 is the item block. Without invincibility, the player dies in 358–1185 frames, before most events.
- **Coordinator incident:** a `git commit -a` to adjust the plan published the agent's still-unreviewed work. Undone with reset and `--force-with-lease`. Rule adopted: only add files by explicit path.

### Decisions: effects at original resolution and new art only procedural
- The user decided that modern effects (VFX, light, bloom) stay at the same resolution as the original game, the 320×200 grid, computed there and enlarged with the same scaling as the sprites. This removed the "mixed resolutions" risk and the aesthetic decision planned for Phase 2.
- The project will have no artist: new art only if generated by code (noise, gradients, derivation/recoloring/composition of the original sprites and tiles, particles, shaders), on the 320×200 grid. Nothing hand-drawn, no sprites redrawn in high resolution. With that, extra parallax layers (fog, dust, starfields, blurred or darkened versions of the existing tiles) become viable. Later exception: the art of the 2000 launcher panels (2026-09-29).
- No open decision remained.

### Regression test delivered (Phase 0a) — `682abeb`
- The agent delivered the harness in two rounds. In the review of round 1, two problems were sent back: (1) it had moved `JE_paramCheck()` to before `loadConfiguration()`, which broke `--xmas`/`--no-xmas` outside test mode; (2) with `processorType` fixed at 2, lava, water and the `wild` blend were left out. The fix restored the original order (with a pre-scan of the arguments) and now sweeps detail levels 1–6.
- Verified: the 30 pairs (demo × level) pass, no temporary probe was left in the code, and the negative test (1 pixel changed) fails on every pair from frame 51 on.
- Findings: timing depends on `getFrameCount2Ticks()` as a phase accumulator (warning animation in `fonthand.c:260` and respawn in `mainint.c:2410`); user files stay outside the data directory; the `processorType` default is inconsistent (without a config the engine uses 3, but `JE_initProcessorType()` documents 2; levels 2, 3 and 5 give identical output in the demos, because `smoothScroll` is forced to true afterwards).

### Start of Phase 0
- Decided: Phase 0 approved, SDL3 backend (GPU/renderer API with shaders and new gamepad API; SDL3 3.4.16 already installed via Homebrew), and the workflow (OpenCode agents with DeepSeek V4.1 Flash via Orca; the coordinator reviews, commits and pushes). The order of Phase 0 puts the demo regression test before the migration to SDL3, because it is its safety net.
- Branch `modernization` created and published to `origin`. Orca run `run_e0cad877e2fd`; first task: headless demo regression test (task `task_9f4f56a107b2`).
- Discovery: this machine's Homebrew provides `sdl2-compat` 2.32 on top of SDL3 3.4.16, that is, the build was already running on SDL3 through the compatibility layer; SDL2_net was not installed, so the build came out without network. The demos use a fixed seed (`src/demo.c:42`, `mt_srand(32402394)`), natural candidates for a deterministic test.

### Feasibility analysis
- Analyzed the OpenTyrian code against the modernization proposal (rendering, main loop, timing, RNG, backgrounds, HUD, input).
- Conclusion: viable as a presentation layer, provided that the gameplay/render boundary (snapshot, events and tag buffer) and the demo regression test are created first. The "gameplay × rendering" boundary that the proposal presupposed did not exist in the code.
- Relevant discoveries (detailed in [MODERNIZATION.md](MODERNIZATION.md), "Technical notes on the code"): 100% software rendering, 8-bit indexed, 320×200 (`src/video.c:71`); logic and drawing interleaved in `JE_main`, `JE_drawEnemy` does much more than draw; `mt_rand` shared with drawing paths (repeated explosions, `JE_doSP`), on which demos and netplay depend; logic at ~34.8 Hz (PIT 0x4300, `frameCountMax = 2`), at 60 Hz the image judders because 35 does not divide 60; visible playfield of 264×184 with part of the HUD drawn inside it; parallax already exists (3 layers + horizontal pan, starfield and smoothies); there is already a spotlight, per-level color filters and `fade_white`; the original has no screen shake; OpenTyrian keeps `tyrian.cfg` and `tyrian.sav` in `~/.config/opentyrian` or next to the executable (portable), never next to the DOS data.
- Plan created. Pending at the time: backend decision (SDL3 × SDL2+OpenGL) and go-ahead for Phase 0.
- Items that already existed in the original game: display and input (desktop fullscreen, window, scalers and Center/Integer/8:5/4:3 modes; keyboard remapping in `src/config.c:297`; joystick through the legacy `SDL_Joystick` API, not `SDL_GameController`).
- Original risk register: determinism break (demos and netplay) → regression test and separate RNG; 35 Hz judder on 60 Hz screens → interpolation; mixed resolutions → resolved by the 320×200 grid decision; divergence from upstream OpenTyrian → concentrate changes in new modules; contradictions in the proposal (high-resolution sprites, extra parallax) → resolved by procedural-only art.
