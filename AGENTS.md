# AGENTS.md — Modern Tyrian

A modernized OpenTyrian (C99, SDL3) that plays two games from one binary:

- **Tyrian 2.1**, the freeware release. Its data ships in `./data`.
- **Tyrian 2000**, ported from the KScl/opentyrian2000 fork at `aad5aca` (GPL-2.0). Its data is **never** in this repo; the user installs it.

Lineage: upstream OpenTyrian → the `vittau/modern-tyrian` modernization (the `docs/` folder and most of this file come from it) → this fork, which adds exercise-bike control through **Flywheel** (github.com/Dologan/flywheel, a separate repo; see the Flywheel section below). The default branch is `main`; `master` tracks upstream OpenTyrian. Work on feature branches and merge through PRs (with a merge commit, so the fork's history is kept); do not open or merge PRs unless the owner asks.

Documentation: `docs/MODERNIZATION.md` is the current state of both games (architecture §3, rules §2, test matrix §6.1, decisions in force §7, pending items §8) and `docs/CHANGELOG.md` is the dated history, newest first. Research lives in `docs/t2000/`:

- `fork-diff.md`: the fork ledger;
- `data-formats.md`: file formats, counts and the save layout;
- `integration.md`: the integration points.

## Hard rules

- **2.1 must not regress.** Classic mode is byte-exact with upstream: the same 8-bit framebuffer and the same RNG order. Every 2.1 baseline change needs an explicit reason.
- **The Modern layer is presentation only.** It never writes game state and never calls `mt_rand*`. Extra effects never go through `JE_setupExplosion`/`JE_doSP`.
- **Tyrian 2000 data never goes into git, releases, LFS, caches, logs or any project host.** That covers the zip, any file from it, and any extracted sprite, string, map or screenshot.
  - Tests commit only hashes and metadata (name, size, CRC).
  - `tools/check_no_t2000_data.sh` enforces this in `make regress` and before packaging.
- **Variant differences live in tables and hooks**, never in scattered `if (variant)`. Nothing is removed from 2.1 to fit 2000.
- **Private data stays out of this public repository.** No bike or PC identifiers (serial numbers, Bluetooth addresses, device names with a unique suffix), no raw hardware recordings or logs, no user names, home paths other than a placeholder, host names or network addresses, and no `claude.ai/code/session_` links or `Claude-Session:` trailers in commits, PR bodies or files. Use invented values in tests and docs. If a task seems to need such data, ask the owner where it should go instead of committing it. Found some already committed? Tell the owner; removing it means rewriting history, which needs their explicit go-ahead.
- **Port fork logic by understanding it.** Never paste fork files; cite the fork commit when porting.
- **Portability:** no absolute user paths in committed files. Regression outputs are opened with `"wb"` (Windows CRLF broke hashes before). New `.c/.h` files also go into `visualc/opentyrian.vcxproj` and `.filters` (the `.vcxproj` is CRLF).
- **Audit every touched `.c`:** `gcc-16 -std=iso9899:1999 -pedantic -Wall -Wextra -Wno-format-truncation -Wno-missing-field-initializers -Werror -fsyntax-only -DTARGET_UNIX -DWITH_NETWORK -I/opt/homebrew/include <file>`.
  - MinGW rejects `%zu` in the `logWarn`-family printf checks; cast to `unsigned long` and use `%lu`.

## Build and test

```sh
make                         # release;  `make debug` = -O0 -Werror, asserts on (run `make clean` between them)
make regress                 # full 2.1: 207 cases + guards; CI gate
make regress-quick           # critical 2.1 subset + cheap guards + optional installed 2000 data
make regress-2000 TYRIAN2000_DATA=<dir>   # full 2000: 183 cases, parallel; data required
REGRESS_ONLY='^depth-' make regress-quick  # regex over case labels (both suites)
TYRIAN2000_DATA=<dir> tools/regress-2000.sh -j 6 --case='^depth-' # --case overrides REGRESS_ONLY; both suites support -j / REGRESS_JOBS
tools/regress.sh --update                 # regenerate 2.1 baselines (intentional output changes only; NOT the bike digests, see the Flywheel section)
tools/regress-2000.sh --update-case=<label>   # regenerate one 2000 baseline; --only-launcher for launcher hashes
```

- **Quick tier:** `tools/regress.sh --quick` / `make regress-quick` compares existing baselines: `demo[1-5]-d4`, `scenario-{water,spotlight,flip,blur}-d4` (spotlight covers lava; flip also covers lava), `modern-demo1-d4`, `modern-wide-demo1-d4`, `modern-crt-scanlines`, `depth-tyrian-on-d4`, `depth-held-pause-{wide,smooth}-d4`, and `modern-pause` (pause state/frame rows of the full matrix). Optional 2000: `demo1-d4`, `modern-demo1-16x9-d4`, `depth-e1-level16-2p-on-d4`, `data-open-audit` (title baseline plus wrong-root rejection). Uses `TYRIAN2000_DATA` or a manifest-verified installer location; otherwise prints `2000 quick: skipped (no data)`. Measured total: 24.38 s with explicit 2000 data, 28.44 s with installer discovery, 19.63 s without data (6 cores, existing binary); see `.worker-reports/regress-quick.md` for costs.
- **Quick guards:** no-data, user-paths, variant-bootstrap and game-rules. Installer, full final matrix, display, progress and RNG-order remain in the full gate; quick omits their compilation, large scenario matrices and broad source scan. `REGRESS_ONLY` / `--case=REGEX` intersects the quick list; unlabelled guards run in full suites unless a filter is supplied, then only when their script basename matches. `REGRESS_SKIP_BUILD=1` reuses the binary; `REGRESS_TIMINGS=1` prints per-case wall times. Full CI targets stay unchanged.
- **Harness:** headless (`SDL_VIDEO_DRIVER=dummy`) with a virtual clock. Cases compare per-tick frame/state hashes. Coverage lines in the log, such as `Rule coverage: …`, `Rule fixture PASS: …` and `boss N active …`, prove that a path really ran; a hash alone does not.
- **Useful `--regress-*` options:**
  - bike UI (`doc/ftms/screens/`): `screen=bike-menu|bike-menu-nav|bike-menu-edit` (Setup > Bike and Controls with a scripted key walk), `menu=bike` (in-game menu, then its bike overlay), HUD with `--regress-bike-feed=const:W,RPM --regress-bike-set=hud=1`;
  - scenarios: `demo=N`, `level=E:L`, `script=E:L`, `screen=<name>`, `modern`, `aspect=`, `detail=`, `frames=N`, `players=N`, `seed=`;
  - `flow=` is a virtual keyboard that walks the real menus: Full Game, Timed Battle, arcade, Super Tyrian, Destruct;
  - `rules=<fixture>`, `items-new`, `fire`, `demo-hud-check`, `gameplay-check`, `data-audit`, `boss`, `handoff`, `gamepad`, `xmas`, `user-root=`/`user-files`.
  - CRT: `crt=off|scanlines|ntsc|scanlines+ntsc` (Modern filtered-output hashes), `crt-check` (synthetic fixtures + measurements), `crt-height=N`, `crt-window=WxH`, `present-png=FRAME:FILE` (2.1 only).
  - depth layers: `layer-check` (stamps the per-pixel layer buffer, compares the interpolated frame at alpha 1 with the live tick and logs `Layer coverage:` / `Layer check PASS`; needs `modern`), `layer-png=FRAME:FILE` (false-colour PNG of the presented layers, 2.1 only, local review). `tools/check_depth_layers.sh` also proves layers on/off leave frame and state hashes alone.
  - depth shadows (stage 2, `Depth:` Off/On, default On, config `modern_depth`): `depth=off|on` (legacy `low`/`high` alias On) pins the setting (regress runs keep it **off**, so every old baseline stays identical), `depth-check` runs the synthetic shadow fixture (no game data). `tools/check_depth_shadows.sh` runs a scenario with depth off/on, proves state/RNG identical, off = the existing baseline, and the `Depth shadows:` coverage line (`shadowed_px`, casters, `space_frames`).
  - held in-level screens (pause, in-game menu, in-game help; `src/modern_held.[ch]`): they present the frozen playfield through `JE_showVGA`, so they reuse the layer buffer, rank table, emission tag and light colours snapshotted from the last really presented frame, minus an overlay mask (playfield indices that differ from the snapshot: no shadow, no light, no emission there). `--regress-held-check` (id 434, needs `modern` and `--regress-menu=pause|ingame|help`) compares the held frame with the last live one outside the overlay; `tools/check_depth_held.sh` runs a menu scenario with every effect off, lighting only, Depth On, proves state/RNG identical and the `Depth held:` / `Light held:` / `Held check:` coverage lines (`shadowed_px`, `emitter_px`, `lit_px`, `overlay_px`, `overlay_changed_px=0`, `mismatch_max_dist`).
  - Launcher-only flags are parsed before `params.c`: `--regress-launcher=WxH,installed|missing,1|2[,about|message|…]`, `--launcher-png=`, `--launcher-flow=`.
- **`params.c` option-id ranges.** Pick a new range; beware getopt prefix matching (`--regress-loadout-new` broke `--regress-loadout`).

  | Range | Owner |
  |---|---|
  | 313 | `--variant` |
  | 314–315 | user paths |
  | 320–329 | 3b rules |
  | 330–339 | installer |
  | 350–359 | 3c |
  | 360–369 | Phase 4 |
  | 370–379 | Phase 7 |
  | 380–399 | Phase 8 |
  | 400–409 | display |
  | 410–419 | CRT filter |
  | 420–429 | progress |
  | 430–439 | depth layers (435–437: light/fog tuning: `--regress-light-scale`, `--regress-light-radius`, `--regress-fog-strength`) |
  | 440–459 | bike/FTMS: 440 `--regress-bike-feed`, 441 `--regress-bike-log`, 442 `--regress-bike-set`, 443 `--regress-bike-events`; 450 `--ftms-bridge`, 451 `--ftms-ftp`, 452 `--ftms-off`, 453 `--ftms-log`; 454 `--input-probe`; 455 `--ftms-test-level`, 456 `--regress-bike-rider`; 457–459 reserved |

- **Guards run by `make regress`:**
  - `check_no_t2000_data.sh`, `check_variant_bootstrap.sh`, `check_user_paths.sh`, `check_game_rules.sh`, `check_bike_effort.sh`, `check_bike.sh` (`make regress-bike`; bike core, digests in `test/bike/regress.txt`);
  - **Vendored Flywheel files are Flywheel's: never edit them here.** `check_bike_effort.sh` runs `vendor.py verify` first. `check_bike_ws.sh` is local only (not in `make regress`); see the Flywheel section below.
  - `check_installer.sh` (synthetic zips plus `tools/curl_stub.c` named by absolute path in a test spec; it can never reach the network);
  - `check_final_regression.sh` (the §7 matrix: data-open audit, save namespaces, UI save/load, bosses, pause, gamepad, launcher handoff);
  - `check_display.sh` (runtime display changes, modal centring, launcher first frame).
- **Never run the long `--interp-check`/`--smoothness-check` sweeps locally.** They run in the manual GitHub workflow `regress-full.yml`.
- **CI** (`.github/workflows/{linux,macos,windows}.yml`) runs on pushes and PRs to any branch:
  - build, `make regress`, then `tools/fetch_t2000_data.sh` (camanis.net, exact size + SHA-256, into `$RUNNER_TEMP`, **no Actions cache**, because a public repo's cache is readable from fork PRs) and `make regress-2000`;
  - a lightweight decision job skips docs-only pushes/PRs and pushes whose SHA already passed the same workflow on any branch; manual runs always build/test. Releases reuse complete push package artifacts for the tagged SHA (rename attachments only), building only if no successful push exists; missing/expired artifacts fail explicitly.
  - on failure it uploads only `*.txt`/`*.log`, and releases ship 2.1 data only.
  - Windows is MSYS2: no shell scripts through `CreateProcess`, MSYS vs Windows paths (use `cygpath`), and `python` must be installed.

## Architecture (startup order)

1. **`bootstrap.c`** (`gameBootstrapParse`), first in `main`: early flags (`--variant`, `--data`, regress/selftest). User files are disabled until known.
2. **`game_variant.[ch]`:** `GameVariantDef` for 2.1/2000 holds the names, `save_namespace` (`tyrian21`/`tyrian2000`), the episode count (4/5) and pointers to the schema, UI and rule tables. `gameVariantCurrent()`.
3. **`launcher.[ch]`:** the in-binary first screen, drawn with SDL at the native output size, with PNG art embedded via `tools/embed_assets.sh` → `obj/launcher_art.c` (VS: `visualc/embed_assets.ps1`).
   - It always opens on a normal start, including the Deck. `--variant=`, regress and selftest skip it.
   - `launcher/last_variant` in the shared `opentyrian.cfg` only preselects a panel.
   - Its INSTALL flow drives the installer, with a "place the files" screen when no file dialog exists (Deck Game Mode, `OPENTYRIAN_NO_DIALOGS`).
4. **`game_data.[ch]`:** `GameDataProvider` locates, validates (wrong-variant diagnostics both ways) and opens data read-only, with no per-file fallback between directories.
   - The 2000 search order is `--data`, `TYRIAN2000_DATA`, the installer location, `tyrian2000/` beside the exe, then `./tyrian2000`.
5. **`file.c`:** user root, per-variant namespaces and the one-time 2.1 migration (`userPathsMigrateLegacy21`), which runs before `loadSaves`. The configs (`opentyrian.cfg`, `tyrian.cfg`) are shared in the root.
6. **Loaders read `game_schema.[ch]`** (`gameSchema()`, `gameStrings()`, semantic labels such as `GAME_LABEL_*`) for item/enemy banks, string sections, shape banks (12/13), pictures and palettes, sfx and voices, credits and episodes.
   - Rules come from `game_rules.[ch]` (`gameRules()`: events 58/59/68/83/84/85/99, launch/spawn, twiddles, arcade ships, charging sidekicks, Flying Punch, cash, Timed Battle).
   - UI comes from `gameUi()` (menus, options, the Mouse menu, the title mark). `highscores.[ch]` holds both variants' boards.
7. **The Modern renderer** (`modern*.c`, `vfx*.c`, `drawlist.c`, `interp.c`) is variant-agnostic.
   - Light and tags come from the object context, not sprite ids.
   - The HUD uses semantic labels.
   - Attract demos follow the active mode's HUD.
   - Modals over the widened pic-1 layout use `modern_dialog_begin/end`.
   - **CRT filter** (`crt_filter.c`, `ntsc.c`): Modern-only, after canvas capture and all passes in `modern_present_frame`; read-only canvas, original Fit rect/mouse map. Output width follows NTSC, scanline height is `2*src` only for `dst % (2*src) == 0`, otherwise native Fit height (or `src` below `2*src`); buffer/texture resize only on dimension changes, ~16 MiB NTSC table built lazily and freed at shutdown. See `docs/CRT.md`.
   - Window changes must settle with `SDL_SyncWindow` before geometry is read, because Cocoa is asynchronous.

## Flywheel integration: what changed and where things live

Flywheel is a separate program and repo (github.com/Dologan/flywheel). It talks to the bike over Bluetooth and serves telemetry on `ws://127.0.0.1:8585` by default (the port moved from 8787; the Flywheel app writes the port it really uses to `runtime.json`) (protocol: Flywheel `docs/protocol.md`, presets: `docs/intensity.md`). On Windows it is now a Rust tray app (`flywheel.exe`, plus a console `flywheel-cli`); the older Python bridge (`flywheel-bridge`) is frozen as Flywheel's reference implementation and is the only Flywheel for Linux/macOS. This repo only consumes the protocol: **protocol or fixture changes start in Flywheel**, then get re-vendored here. You do not need a Flywheel checkout for `make regress` (the vendored copies suffice); you need one (or a built `flywheel-cli`) only for `check_bike_ws.sh live` and `vendor.py sync`. Design and history of the bike work: `doc/ftms/` (start with `README.md`, `design.md`, `implementation-plan.md`, `playtest-checklist.md`).

Status: none of this has run on real bike hardware. The Q200B's power is probably estimated rather than measured, so an FTP is only meaningful on the bike it was measured on.

- **Launchers.** Windows: `tools/ftms/play-with-flywheel.ps1` and `Play with Flywheel.cmd` (double-click, no console, message boxes for problems; the Windows CI package copies both next to `opentyrian.exe`). It starts the installed Flywheel tray app (registry `HKCU\Software\Flywheel` `InstallDir`, else `%LOCALAPPDATA%\Programs\Flywheel`) and finds its port from `runtime.json` in its data folder (`%LOCALAPPDATA%\Flywheel`, or `$env:FLYWHEEL_DATA_DIR`), confirmed by `/healthz` answering `"app":"flywheel"` (fallback: probe 8585). `mode` `conflict` (its port was taken, no bike) gets a message box and no bike. If Flywheel is not running it is started and polled for up to 20 s. The game then runs with `--ftms-bridge=127.0.0.1:<port>` and Flywheel stays running. Not run on Windows yet (no PowerShell in the dev container). The old Windows `play-with-bike.cmd/.ps1` (Python bridge) are gone. `tools/ftms/play-with-bike.sh` remains for Linux/macOS, where only the Python bridge exists; never run it beside the Flywheel app (port 8585).
- **Vendored data is owned by the Flywheel repo (MIT).** `tools/bike/vendor.py sync FLYWHEEL_DIR` copies it and rewrites `test/bike/VENDORED_FROM` (sync from Flywheel's `main` so the recorded commit stays reachable; the pinned commit currently predates a history rewrite there, though the file contents match Flywheel's current ones); `vendor.py verify` checks it (line endings normalised before the sha256, so CRLF checkouts pass). Run both as `python3 -I tools/bike/vendor.py ...`. Never hand-edit these 79 files; change Flywheel, then re-sync:
  - `test/bike/feeds/`, `test/bike/golden/` (except `c_digests.txt`), `test/bike/protocol/tolerance/`, `test/bike/protocol/rider/`, `tools/bike/trace_to_feed.py`.
  - **Fork-owned**: `test/bike/golden/c_digests.txt`, `test/bike/events/`, `test/bike/regress.txt`, `VENDORED_FROM`, `vendor.py`, `src/bike*.c`, `test/unit/bike_*_test.c`, `tools/check_bike*.sh`.
- **Default port** is 8585 (`BIKE_DEFAULT_PORT`; it was 8787, which clashes with wrangler, RStudio Server and Dask). Saving `[ftms]` writes every key, so old configs hold `bridge_port = 8787` unchosen: `bike_config_load` reads a saved 8787 as 8585 unless the section has `bridge_port_v = 2`, which every save writes; a later deliberate 8787 is kept.
- **Rider capability** (`src/bike_ws.c`): the hello is `{"type":"hello","client":"game","v":1,"caps":["rider","intensity"]}`; a bridge without capabilities never sends a rider, one without `intensity` sends the plain rider (played at the measured FTP). Parsing rules, shared with Flywheel's `test/bike/protocol/rider/manifest.txt`:
  - JSON string escapes are decoded; names and ids are ASCII only (anything above becomes `?`; control characters become spaces).
  - FTP is rounded half up to whole watts and clamped to 50..500.
  - A rider without a usable FTP keeps the FTP in use, unless it names a different id, which drops it.
  - **Intensity** (Flywheel `docs/intensity.md`): the nested `intensity` object is read in its own member loop. It is usable only with a whole `effectiveFtpW` 1..10000, a whole `percent` 50..150 and a non-empty `name`, and only when the message has a usable top-level FTP; then `BikeRider.eff_ftp_w` (clamped 50..500) is what plays and `ftp_w` stays the measured FTP. Anything else, or a later message without a usable block, plays at `ftp_w`. Never computed or cross-checked, never written to `[ftms]`.
  - Rider messages are handled before the `v` check and do not count as protocol proof, so they do not reset the reconnect backoff.
- **FTP precedence** (`bike_ftp_w()` in `src/bike.c`): `--ftms-ftp` > bridge FTP, i.e. the effective FTP under an intensity preset (`ftp_source=auto`, the default) > typed `ftp` (`ftp_source=manual`).
  - The bridge value is copied at level start and between levels, never mid-level (the effort config is rebuilt when FTP changes, so effort would jump); a mid-level change shows as pending.
  - It is never written to `[ftms]`. `bike_regress_pin` forces `ftp_auto=false`.
- **Regress hooks**: option id 456 `--regress-bike-rider=FTP[@PCT][:NAME]` (PCT 50..150 synthesises an intensity block, effective = (FTP*PCT+50)/100) and the `rider` action in `--regress-bike-events` CSVs (`test/bike/events/rider_mid.csv`: `300,rider,300:Ann`; `rider_intensity_mid.csv`: `300,rider,300@85:Ann`).
- **Bike menu** (`src/bike_menu.c`): 19 rows (18 settings + Done), 9 visible, scrolling, with the status and help lines pinned. New row **FTP Source** (Bridge/Manual). While the bridge FTP is in force the FTP row shows e.g. `230 W Ann` and is read-only (under an intensity preset `196 W Easy 85 %`, or `196 W Easy 85 % of 230 W` if it fits 130 px; the large font draws no parentheses; help `Easy 85 % of 230 W (Ann, test). Source Manual to edit.`); the help line says so, or that a new bridge FTP applies next level. The Setup entry is "Bike and Controls...".
- **Tests**:
  - `check_bike.sh`: `bike-ftp-source` (bridge FTP == the same number typed; Manual and `--ftms-ftp` win; a mid-level rider changes nothing) `bike-intensity` (`300@85` == typed 255; Manual and `--ftms-ftp` win; a mid-level preset waits) and `snap-menu-rider`, `snap-menu-intensity`. Four existing snapshot digests changed (`snap-menu`, `-nav`, `-edit`, `snap-ingame-bike`); the HUD snaps and all case digests are unchanged.
  - `check_bike_ws.sh [fake|tolerance|rider|blackhole|refused|live]` (default: all). Needs cc, SDL3 and SDL3_net (pkg-config), python3. `live` runs a real Flywheel with the simulator, kills and restarts it: set `FLYWHEEL_CLI=<flywheel checkout>/app/target/debug/flywheel-cli` (build it there with `cargo build -p flywheel-app`) to test against the Rust app, the one players use on Windows; otherwise it uses the Python bridge in `FLYWHEEL_DIR` (default `/home/user/flywheel`) through `uv`. **With neither, `live` prints SKIP and still exits 0, which looks like a pass.** Last run: all six pass against both (2026-10-19).
  - `check_bike_effort.sh` runs `vendor.py verify` before building the unit test.
- **Updating bike digests**: `tools/check_bike.sh BIN DATA OUT --update` (`tools/regress.sh --update` does not touch them). It rewrites `test/bike/regress.txt` (79 lines: 69 cases + 10 snaps) and, if PIL is installed, `doc/ftms/screens/*.png`. Review `git diff test/bike/regress.txt`: only the labels you meant to change may differ. `make regress` needs `data/` (`./get_data.sh`) for its final matrix.

## Data, saves, installer

- **2000 archive:** `https://www.camanis.net/tyrian/tyrian2000.zip`, 5,051,363 bytes, SHA-256 `348bc76e…87e921667`. The constants are in `installer.h`, and the 79-file manifest is `test/regress-2000/data-manifest.txt` = `installer_manifest.h`.
- **`installer.[ch]`** is a non-blocking SDL-thread job.
  - It detects, downloads through the system `curl` (no shell, https only), verifies, extracts with the vendored `third_party/miniz` (MIT, inflate only), validates, and installs by atomic rename.
  - It also installs from a zip or folder (GOG): non-canonical but valid data is accepted and logged.
  - Headless CLI: `--install-2000=`.
- **Install locations:**

  | Platform | Location |
  |---|---|
  | Windows | `%APPDATA%\OpenTyrian\data-tyrian2000` |
  | macOS | `~/Library/Application Support/OpenTyrian/data-tyrian2000` |
  | Linux/SteamOS | `$XDG_DATA_HOME` or `~/.local/share/opentyrian/data-tyrian2000` |
  | Portable (`opentyrian.cfg` beside the exe) | beside the exe |

  The downloaded `.part` file is deleted afterwards. Steam's sniper runtime ships curl.
- **Saves:**
  - 2.1: `tyrian21/tyrian.sav`, 2,502 bytes.
  - 2000: `tyrian2000/tyrian.sav`, 4,722 bytes (the 2,502-byte prefix + an unencrypted high-score suffix; an unknown 4-byte field is preserved).
  - Each variant never reads the other's save. The root 2.1 save is copied, never moved.

## Open items

- **DOS checks:** the fork's approximations (events 58/59/68, trail 198, Timed Battle routing and bonuses, Super Tyrian state 8, the Pretzel Pete sprite size) are listed in `docs/MODERNIZATION.md` §8 (history in the 2026-09-29/30 Phase 4 entry of `docs/CHANGELOG.md`). They need the DOS original.
- **Manual only:** a physical Steam Deck, the native Windows file picker and download, and a physical ultrawide/HiDPI Cocoa.
