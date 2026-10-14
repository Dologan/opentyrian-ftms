# Bike & Controls: design (gate G1)

Base: modern-tyrian `v0.7.0` (`677e5c0`), C99 + SDL3. All `file:line` anchors are at that commit. Plan: `on-the-flywheel-repo-wise-wand.md`. Where this document and the plan disagree, this document wins; each deviation is marked **(dev)**.

## 1. Decisions in one screen

| Question | Decision |
|---|---|
| Arithmetic | **Integer fixed point (Q16.16 in `int32_t`, time in µs) for everything that can reach game state**: effort engine, rules, auto-fire, pointer scaling and smoothing. No `float`/`double`/`exp` on that path. CI compares committed hashes on x86-64 Linux, arm64 Linux, macOS arm64 (clang contracts FMAs by default) and Windows. Floats are allowed only in HUD text formatting. |
| Effort reference | Flywheel `effort-engine.ts` + `filters.ts`. The C port must match golden output from the real TS within a tolerance (§5). |
| Transport polling | **(dev)** `bike_service()` is called from `handleSdlEvents()` (`keyboard.c:189`) so the socket stays alive in pause, shop and menus. It only writes the mailbox. The mailbox is consumed once per gameplay frame in `bike_frame_tick()`. It is inert under regress, where the source is never `BRIDGE`. |
| Stopped rider | Follow mapping C literally: when not moving, `effortNorm = 0`, so income is `max(F, bias·G)`. If stopping should hurt more, lower `bias`. No separate idle constant. |
| Shield-regen surge | Bonus only: the regen interval shrinks from 15 toward 8 frames with effort. Pool cost is unchanged, so it is never worse than stock. |

## 2. Modules and public APIs

New files all go in `visualc/opentyrian.vcxproj` and `.filters` (CRLF), and each needs the `-pedantic -Werror` audit. Each header below lists its exact functions.

**`src/bike_effort.[ch]`**: pure C99. Includes only `<stdint.h>` and `<stdbool.h>`, with no SDL and no globals, so it is unit-testable.
```c
typedef int32_t bq16;                         /* Q16.16; 65536 == 1.0 */
enum { BIKE_EV_MOVING_START=1, BIKE_EV_MOVING_STOP=2, BIKE_EV_SURGE_START=4,
       BIKE_EV_SURGE_END=8, BIKE_EV_OVERDRIVE=16 };
typedef struct { uint32_t ftp_w; bq16 moving_on_rpm, moving_off_rpm;          /* 25, 15 */
  uint32_t moving_grace_us, silent_stop_us, start_confirm_us;                 /* 1.5 s, 3 s, 0.25 s */
  uint32_t effort_tau_us, fast_tau_us, baseline_tau_us;                       /* 3 s, 1.5 s, 75 s */
  bq16 baseline_floor_pct, surge_ratio, surge_min_pct;                        /* .5, 1.2, .6 */
  uint32_t surge_hold_us, surge_exit_us, surge_fill_us, surge_warmup_us;      /* 2,1,8,30 s */
  bq16 coast_max_rpm, coast_drop_frac; uint32_t coast_window_us; uint8_t coast_resume_samples; /* 45,.35,5 s,3 */
} BikeEffortConfig;
typedef struct { bool has_sample, link_up, power_valid; uint32_t seq;         /* seq change == new sample */
  bq16 power_w, cadence_rpm; uint32_t age_us; } BikeEffortInput;
typedef struct { bool moving, stale, surging; bq16 power_w, cadence_rpm, effort_w, effort_pct,
  effort_norm, baseline_w, surge, surge_meter; uint8_t events; } BikeEffortSignals;
typedef struct BikeEffortState BikeEffortState;   /* full definition in the header (stack-allocatable) */
void     bike_effort_config_default(BikeEffortConfig *c, uint32_t ftp_w);
void     bike_effort_init(BikeEffortState *s, const BikeEffortConfig *c);
void     bike_effort_step(BikeEffortState *s, const BikeEffortConfig *c, uint32_t dt_us,
                          const BikeEffortInput *in, BikeEffortSignals *out);
bq16     bike_effort_norm(bq16 effort_pct);                 /* knots (0,.10)(.40,.15)(.75,.50)(1.10,1.0) */
uint32_t bike_ema_alpha_q30(uint32_t dt_us, uint32_t tau_us); /* 1-e^-x, Taylor to x^5, x=dt/tau<=0.25 */
uint64_t bike_effort_digest(const BikeEffortState *s);      /* FNV-1a over fixed-width LE fields */
```
- **Rounding.** All signed multiply-shifts go through one helper that rounds the magnitude and reapplies the sign. In C99, `>>` on a negative value is implementation-defined, so it is never used on one.
- **EMA state** is Q16. The update is `v += round(diff_q16 * alpha_q30 >> 30)`, computed in `int64`.
- **Coast-down window**: a ring of 64 `(t_us, cadence)` entries.

**`src/bike_feed.[ch]`**: pure C99 plus stdio. It is shared by the game and the unit test.
- Parses the CSV feed and `const:` specs (§4).
- Parses decimals with integer arithmetic. No `strtod`, so the result does not depend on locale.
- API:
  - `bool bike_feed_load(BikeFeed*, const char *spec)`
  - `void bike_feed_at(const BikeFeed*, uint32_t tick, BikeFeedState *io)`, which yields `link_up`, `seq` and the sample values, plus the tick at which the sample arrived.
  - `bq16 bike_parse_decimal(const char*, const char **end)`, which the bridge client also uses for JSON numbers.

**`src/bike.[ch]`**: settings, source selection, mailbox, gate and frame tick. This is the only module the game core calls besides the rules hooks.
```c
typedef enum { BIKE_SOURCE_OFF, BIKE_SOURCE_SCRIPT, BIKE_SOURCE_BRIDGE } BikeSource;
typedef enum { BIKE_LINK_OFF, BIKE_LINK_CONNECTING, BIKE_LINK_WAITING, BIKE_LINK_STREAMING } BikeLink;
typedef struct { bool master, generator, hud;
  bool surge_superbomb, surge_sidekick, surge_special, surge_shield;
  bool autofire; uint8_t autofire_reserve_pct;            /* 0|25|50 */
  int  autofire_key, autofire_key2;                       /* SDL_Scancode values */
  bool air_mouse, pointer_smoothing, touch, remote_buttons; uint16_t pointer_sens_pct; /* 25..400 */
  uint16_t ftp_w;                                         /* 50..500 step 5, default 200 */
  uint8_t floor;                                          /* units/frame, default 8 */
  uint16_t bias_pct, gain_pct, cap_pct;                   /* 50, 100 ("Strength"), 150 */
  char bridge_host[64]; uint16_t bridge_port;             /* 127.0.0.1, 8787 */
} BikeSettings;
typedef struct { uint32_t seq; bool power_valid, cadence_valid; bq16 power_w, cadence_rpm;
  uint64_t arrival_us; } BikeSample;
typedef struct { BikeLink link; bool has_sample; BikeSample latest; } BikeMailbox;
typedef struct { BikeLink link; bool gate, generator_on, lost; bq16 watts, cadence, effort_norm,
  surge_meter; bool surging, autofire_on, autofire_held; } BikeHud;
extern BikeSettings bike_settings;              /* edited by menus; read every frame, never cached */
void  bike_settings_default(BikeSettings *s);   /* all features off (superbomb sub-toggle on, see §3) */
bool  bike_settings_set(const char *key, const char *value); /* one parser: cfg, CLI, --regress-bike-set */
void  bike_config_load(void); void bike_config_save(void);   /* [ftms] section, via config.c */
bool  bike_script_load(const char *spec);       /* --regress-bike-feed → SOURCE_SCRIPT */
void  bike_init(void); void bike_shutdown(void);
void  bike_service(void);                       /* transport poll; mailbox only */
void  bike_level_start(void); void bike_level_end(void);
void  bike_frame_tick(void);                    /* exactly once per level-loop pass */
bool  bike_gate_open(void);
BikeSource bike_source(void);
const BikeHud *bike_hud(void);                  /* read-only snapshot for HUD / menus */
bool  bike_hash_active(void); uint64_t bike_state_digest(void);
uint32_t bike_tick_count(void);
```
- **Source selection**:
  - `SCRIPT` when a regress feed was given.
  - Otherwise `BRIDGE` when `master && (generator || any surge || hud) && !--ftms-off`.
  - Otherwise `OFF`.
- **`link_up`** is true when the transport is connected and either the bridge `status` is `streaming` or the last telemetry is under 3 s old.

**`src/bike_rules.[ch]`**: mapping C, link-loss blend and surge effects. It may include `player.h` and `varz.h`.
```c
void     bike_rules_level_start(void);                     /* remainder acc = 0 */
void     bike_rules_frame(const BikeEffortSignals *sig, bool link_up); /* blend + superbomb grant */
unsigned bike_rules_power_add(unsigned stock_add);         /* replaces powerAdd at the refill */
unsigned bike_rules_shield_wait(unsigned stock_wait);      /* 15 → 15 - round(7·n) */
unsigned bike_rules_sidekick_charge_ticks(unsigned stock); /* 20 → 10 while surging */
bool     bike_rules_extra_special_tick(void);              /* true while surging */
int32_t  bike_income_q16(unsigned G, bq16 effort_norm, uint8_t floor, uint16_t bias_pct,
                         uint16_t gain_pct, uint16_t cap_pct);  /* pure, unit-tested */
```
- **Mapping C**: `income = clamp(G·(bias + gain·n), F, G·cap)`.
  - `G` is the `powerAdd` read at the call site, i.e. the equipped generator.
  - Income is Q16 per frame and goes through an exact remainder accumulator: `acc += income; add = acc >> 16; acc &= 0xFFFF`.
- **Link-loss blend**: weight `w` (Q16) ramps toward 1 at 1/2 s per second once the link has been down for 1.5 s, and back toward 0 when the link returns. `out = lerp(income, stock_add·65536, w)`.
  - `w = 1` at session start, and whenever the gate goes from closed to open (e.g. a mid-level toggle), so income ramps in and never jumps.

**`src/bike_controls.[ch]`**: auto-fire, air mouse, touch and remote buttons, everything that edits input. **(dev)** These are kept out of `bike_rules`, because controls do not need a data source.
```c
void bike_controls_frame(void);                 /* toggle-key edge detection; called by bike_frame_tick */
bool bike_controls_autofire(unsigned power);    /* latch && (reserve==0 || power >= reserve·9) */
void bike_controls_pointer(JE_integer *mouseXC, JE_integer *mouseYC); /* sens scale + int one-euro */
bool bike_controls_translate_key(...); void bike_controls_finger(const SDL_Event *ev); /* T3b.2/3 */
void bike_controls_inject(const char *action, int a, int b); /* --regress-bike-events */
```

**`src/bike_ws.[ch]`**: the bridge client over SDL3_net. It is fully asynchronous and never blocks.
- The whole body is under `#ifdef WITH_NETWORK`; otherwise it is stubs that report `BIKE_LINK_OFF`.
- API:
  - `bool bike_ws_open(const char *host, uint16_t port)`
  - `void bike_ws_poll(BikeMailbox *mb, uint64_t now_us)`
  - `void bike_ws_close(void)`
- After the upgrade it sends `{"type":"hello","client":"game","v":1}`.
- It parses `telemetry` messages (`v == 1` only; `null` sets the field's `*_valid = false`) and `status.state`.
- Reconnect backoff is 0.5, 1, 2, 4 then 5 s.
- `arrival_us` comes from `SDL_GetTicksNS()/1000`.

## 3. Settings

- **Precedence**: `[ftms]` in `opentyrian.cfg` < CLI < regress pin.
  - Load in `loadOpenTyrianConfig` (`config.c:275`) and save in `saveOpenTyrianConfig` (`config.c:461`), following the `modern_depth` pattern (`config.c:404-412`, `486`).
- **Default**: `master = 0` makes a fresh config stock. Every feature defaults off **except `surge_superbomb = 1`**. That one only matters once master, a source and the effort engine are all on, which satisfies both "defaults off" and T3.1.
- **`[ftms]` keys** (the same names are accepted by `bike_settings_set`):
  - `enabled` (master), `generator`, `hud`
  - `surge_superbomb`, `surge_sidekick`, `surge_special`, `surge_shield`
  - `autofire` (`off`|`toggle`), `autofire_reserve` (0|25|50), `autofire_key`, `autofire_key2` (SDL scancode names; defaults `A` and `Application`, i.e. the remote's Menu key, pending the T3b.0 probe)
  - `air_mouse`, `pointer_sensitivity`, `pointer_smoothing`, `touch`, `remote_buttons`
  - `ftp`, `floor`, `bias`, `strength` (gain), `cap`
  - `bridge_host`, `bridge_port`
- **CLI ids 440–459** (add to the AGENTS table). No name may be a prefix of another. `check_bike.sh` scans `params.c` for prefix pairs.

  | id | option | id | option |
  |---|---|---|---|
  | 440 | `--regress-bike-feed=FILE\|const:W[,RPM]` | 450 | `--ftms-bridge=HOST[:PORT]` |
  | 441 | `--regress-bike-log=FILE` | 451 | `--ftms-ftp=W` |
  | 442 | `--regress-bike-set=K=V[,K=V…]` | 452 | `--ftms-off` (forces master off for this run) |
  | 443 | `--regress-bike-events=FILE` | 453 | `--ftms-log=FILE` (T5.2) |
  | 444–449 | reserved | 454 | `--input-probe` (T3b.0); 455–459 reserved |

- **Regress pin**: next to `regress.c:1356`, call `bike_settings_default()` and force `master = 0`. Then apply `--regress-bike-set`. `--regress-bike-feed` implies `master = 1, generator = 1`. Set `bike_settings_pinned`, which makes `bike_config_save()` leave `[ftms]` untouched.
- **Menus**: `SETUP_MENU_BIKE = 4` (`opentyr.h:68`, `opentyr.c:283`).
  - The Setup menu gets a "Bike..." row.
  - The Bike menu holds master, Bike power, FTP stepper, Strength, Floor, HUD, Bridge, a read-only Status row, "Surge..." and "Controls...", plus Done. That is 11 rows, within the `items[12]` limit.
  - Surge and Controls are child menus.
  - Mid-game: add a C-string "Bike..." row to `JE_inGameSetup` (`mainint.c:1514`). It is hidden when `isNetworkGame` and calls `setupMenu()` with `setup_menu_start = SETUP_MENU_BIKE`.
  - Changes take effect on the next `bike_frame_tick()`, because nothing is cached.

## 4. Frame hooks, gate and determinism

**Gate**:
```
bike_gate_open() = master && level_active && !twoPlayerMode && !onePlayerAction && !isNetworkGame
                   && !playDemo && !recordDemo && !galagaMode && !constantPlay
```
- `level_active` is set in `bike_level_start` and cleared in `bike_level_end`.
- Each feature then ANDs its own toggle. The generator and surges also need `source != OFF` and a sample.

| Hook | Where (v0.7.0) | Call |
|---|---|---|
| Level end | `tyrian2.c:614` (`start_level:`) | `bike_level_end()` |
| Level start | `tyrian2.c:835` after `power = 0;` (stays 0) | `bike_level_start()` → resets the rules accumulator only. The EMA keeps riding across levels. |
| Frame tick | `tyrian2.c:1053`, first statement after `level_loop:` | `bike_frame_tick()`: dt, feed/mailbox → `bike_effort_step` → `bike_rules_frame` → `bike_controls_frame`. Runs with the gate closed too (it only advances counters). Note that `goto level_loop` (`tyrian2.c:2276`) re-runs it, exactly as the stock refill re-runs. |
| Shield regen | `tyrian2.c:1161` | `shieldWait = bike_rules_shield_wait(15);` |
| Refill | `tyrian2.c:1203` | `power += bike_generator_on() ? bike_rules_power_add(powerAdd) : powerAdd;`. The `twoPlayerMode \|\| onePlayerAction` pin at `1197-1200` is untouched. |
| Auto-fire | `mainint.c:3769`, after the keyboard block and before "Reverse controls" (`3770`). This is before network packing (`3872-3882`). | `if (playerNum_ == 1 && bike_controls_autofire(power)) button[0] = true;` Manual fire is OR-ed earlier, so the reserve never blocks it. |
| Pointer | `mainint.c:3735` (`mouseXC += mouseXR`) | `bike_controls_pointer(&mouseXC, &mouseYC)` when `air_mouse`. The ±30 clamp at `3788-3795` is kept. |
| Sidekick surge | `mainint.c:4731` | `charge_ticks = bike_rules_sidekick_charge_ticks(20);` |
| Special surge | `varz.c:633`, after `specialWait--` | `if (bike_rules_extra_special_tick() && specialWait > 0) specialWait--;` |
| Superbomb | inside `bike_rules_frame` | On `BIKE_EV_OVERDRIVE`: `if (player[0].superbombs < 10) ++…`, as the pickup at `mainint.c:5091` does |
| Transport | `keyboard.c:189` `handleSdlEvents()` | `bike_service()` |
| HUD | `modern_hud.c:875` status panel; generator row `662` | Reads `bike_hud()` only. It never writes, never calls `mt_rand`, and never touches `bike_settings`. |

The upgrade-screen simulator (`game_menu.c:2541`, `3649`) keeps stock behaviour.

**dt**:
```
dt_us = getFramePeriodTicks10() * max(frameCountMax,1) * 1000 / 1024
```
- This is integer math; it gives exactly 28750 µs at Normal speed.
- `pentiumMode` alternates 2 and 3, and level events can change `frameCountMax`. Both are read at tick time.
- `bike_clock_us += dt_us` per tick.

**Feed CSV** (`test/bike/feeds/*.csv`): `tick,watts,cadence,connected`.
- `tick` is `bike_tick_count()`: the number of `bike_frame_tick` calls since process start, starting at 0. Ticks must be strictly ascending.
- Each `connected = 1` row is a **new notification**: `seq++`, and its arrival is the clock at that tick.
- An empty `watts` or `cadence` field means null.
- `connected = 0` means the link is down from that tick, and the values are ignored. `connected` defaults to 1.
- `#` lines and a header row are allowed.
- Sample age is `bike_clock_us - arrival_us`.
- After the last row the feed goes silent, which reads as stopped after 3 s.
- `const:W[,RPM]` emits a row every 32 ticks (0.92 s, like the Q200B).
- The `--regress-bike-events` CSV is `tick,action[,a,b]`, for example `autofire_toggle`, `set,key=value`, and `mouse_rel,dx,dy` / `button` / `finger` added by T3b.

**State hash** (`regress.c:744`):
- Only when `bike_hash_active()` (regress with a feed, or with master pinned on), fold `power`, `lastPower` and `bike_state_digest()`.
- The digest covers effort state, rules accumulator and `w`, auto-fire latch, feed cursor, `bike_clock_us` and tick count, all as fixed-width little-endian values.
- With master off nothing is folded, so the 207 baselines stay byte-identical.

**Log**: `--regress-bike-log` writes one line per tick: `tick link moving surging effort_w_q16 norm_q16 income_q16 add power w_q16 af_on af_held events`.

## 5. Effort engine port and golden fixtures

- **Port**:
  - Front half of `step()`: fresh/stale selection, power or the cadence-only fallback curve when `powerW` is null.
  - `Ema`, `piecewise`, `clamp`.
  - `effortNorm`.
  - `updateMoving` with the 0.25 s start confirmation and coast-down (`updateCoastDown`, `start`, `stop`).
  - `updateSurge` with baseline warm-up (`tau = min(75, 5 + movingTime)`, and `dt/4` while surging), `surgeIntensity`, the surge meter and overdrive.
- **Skip**: zones, streak, compliance, the power model and level inference.
- **Do not port `RideFlow`**: Tyrian has no world-speed or pause. Only the 1.5 s sensor-lost constant is reused, in the blend.
- **Golden generator**: `tools/ftms/gen_effort_golden.mjs` (Node 22, `FLYWHEEL_DIR=…`).
  - It bundles the real `effort-engine.ts` with flywheel's own `node_modules/esbuild` and steps it at `dt = 0.02875` over **the same feed CSVs** with the same semantics: a new object per row, `age = clock - arrival`, and `linkUp` from the row.
  - It writes `test/bike/golden/<feed>.csv` with columns `tick,effort_w,effort_norm,moving,stale,surging,surge_meter,events`, and the outputs are committed.
  - Feeds: const 150, ramp, dropout (2 s and 5 s), stop/coast, surge after 30 s of warm-up, FTP 50/500 edges, and Q200B runs 1 and 2 (converted by T2.3a).
- **C test**: `test/unit/bike_effort_test.c` is built by `tools/check_bike_effort.sh` the way `check_game_rules.sh` is built (`-std=iso9899:1999 -pedantic -Werror`, no SDL, no data), and runs in `regress-quick` and `regress`.
- **Tolerances**:
  - `effort_w` within ±(0.25 W + 0.2 %)
  - `effort_norm` and `surge_meter` within ±0.002 and ±0.01
  - Each boolean transition and each overdrive event must exist in both outputs, with the same count and within ±2 ticks of each other.
  - It also tests `bike_income_q16` against a hand table (calibration T7) and the alpha series against reference values.

## 6. Auto-fire

- **Availability**: `autofire = toggle` makes the in-play toggle available. The latch starts off at each launch and survives levels.
- **Toggle**: in `bike_controls_frame()`, a rising edge on `keysactive[autofire_key|autofire_key2]` flips the latch. Later, a touch tap zone (T3b.3) or an injected event can flip it too. Edges are ignored while the gate is closed.
- **Reserve**: auto-fire is held back while `power < reserve_pct·9`, i.e. a percentage of 900.
  - There is no hysteresis, so fire becomes income-limited at the reserve level.
  - 25 % is 225, which is above the largest `shieldT` (9·20 = 180), so shields keep regenerating.
  - Manual fire is unaffected.
- **HUD**: Modern status panel shows `AUTO` when on, dimmed with `HOLD` when held back. Classic shows nothing in v1.
- **Never** a new `keySettings` slot, because the demo/save layout depends on them.

## 7. Risks and edge cases

1. **2P and all `onePlayerAction` modes** (arcade, Super Arcade, Super Tyrian, ENGAGE, restored saves): the gate is closed and power stays pinned at 900. The menu Status row shows "Off in this mode".
2. **Network, demo playback and demo recording**: the gate is closed, so no bike byte reaches packets or demo streams. Bike regress cases must use `--regress-script` / `--regress-level`, **never `--regress-demo`**, because demo playback closes the gate.
3. **Upgrade-screen simulator**: stays stock.
4. **Level start**: `power = 0` is unchanged, and the floor guarantees a fill in at most 3.2 s.
5. **Frame speeds**: income is per frame relative to stock `G`, so it is invariant across speeds. Effort timers use the real `dt`. `frameCountMax == 0` counts as 1.
6. **Link loss**: blend to stock after 1.5 s, and never zero. A silent link (no data but still connected) means "stopped", not "lost". Before the first sample, the game plays stock.
7. **Generator upgrades scale bike income**: this is intended, and `cap` limits Gravitron.
8. **Relative mouse mode is only enabled after pause or in-game setup** (`mainint.c:1511`, `3473`, while `tyrian2.c:621` disables it). In 1P, `inputDevice` is 0, so the mouse is already live. T3b.2 must call `mouseSetRelative(true)` at level start when `air_mouse` is on. This corrects the plan's `{1,2}` reading.
9. **Variants**: the feature is variant-agnostic (`powerAdd` comes from tables). Tyrian 2000 Timed Battle follows the gate as is. Add one 2000 smoke case to the optional tier.
10. **Hot loop cost**: `bike_service` is non-blocking. A DNS lookup or connect to a blackholed host runs asynchronously in SDL3_net and never stalls the frame.
11. **Settings scope**: settings are cfg-only and never in `tyrian.sav`, whose fixed 2,502/4,722-byte layout is untouched.
