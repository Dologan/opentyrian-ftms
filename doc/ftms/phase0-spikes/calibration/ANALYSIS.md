# Phase 0 calibration: driving OpenTyrian's generator with exercise-bike watts

Scope: gameplay-economics study only. No tracked source modified, nothing committed. Data: Tyrian 2.1 `tyrian.hdt` (episodes 1-3) and, for a cross-check, `tyrian4.lvl` (episode 4 item block). Everything below that is a number from the game data was extracted by `hdt_reader.py` and tabulated by `economics.py` (both in this directory, run with `python3 -I`). Loadouts used for "typical stage" figures are my own illustrative picks, flagged as such.

## 0. Findings in brief

1. **The pool is a tank filled by a constant tap.** `power` (0-900) gains `powerSys[gen].power` per frame (10 / 14 / 19 / 25 / 30 / 50 for the six generators, 348-1739 units/s at 34.78 fps). Every shot (front, rear and sidekicks) pays `poweruse` once; shield regen pays `tpwr*20` per shield point. Nothing else uses the pool.
2. **Stock balance is already "starved by design".** With an illustrative loadout for each stage and the stock generator for that stage, continuous fire is only sustainable 78-86% of the time, and a full pool empties in 2.6-6.5 s of full fire (T6). Healing shields at the same time drops sustainable fire to 33-70%. So the game already degrades gracefully when income is short: a starved weapon simply retries every frame and fires whenever the pool reaches `poweruse` (shots.c:321), i.e. fire rate becomes income-limited rather than being switched off.
3. **Recommended mapping: option C (generator-scaled, effortNorm-shaped, with a floor).** `income/frame = max(8, G_stock * (0.5 + effortNorm(W/FTP)))`. A rider at 75% FTP gets exactly the stock generator (vanilla balance preserved, upgrades still matter); at or above 110% FTP they get 1.5x; at zero effort they get 0.6x, never less than 8/frame, which is one Pulse-Cannon L1 (7.5/frame), so easy riding can always fire the starting gun. It is also frame-rate-setting invariant, which A and B are not.
4. **Latency is not the hard part for this mechanic.** The 900-unit tank already low-pass filters (fill time 0.5-2.6 s), so the Q200B's ~0.91 Hz notifications plus a 3 s EMA are adequate, and late stop detection (5-18 s on the Q200B) only means a few extra seconds of income, never a penalty. Port only EMA + effortNorm + staleness for a first build (about 150 lines of C); stop detector and surge can follow.
5. **Disable for**: two-player, every `onePlayerAction` mode (1P Arcade, all Super Arcade ships, Super Tyrian, ENGAGE), network games, and demo playback/recording. Power is either pinned at 900 or must be bit-identical across runs.

## 1. Mechanics verified against source

Brief claims checked line by line. "OK" = matches, "Correction" = brief was slightly off.

| Claim | Where | Result |
|---|---|---|
| `power += powerAdd` once per frame, clamp 900 | `tyrian2.c:1117-1120` | OK |
| Pinned to 900 for 2P and all `onePlayerAction` | `tyrian2.c:1112-1115` | OK. `onePlayerAction` is also set for 1P Arcade (`menus.c:206`), every Super Arcade ship (`tyrian2.c:3599`), Super Tyrian (`tyrian2.c:3663`), ENGAGE (`tyrian2.c:2430`) and restored from any save with `super_arcade_mode != NONE` (`config.c:469`). The brief's "Super Arcade pins 900" is therefore a subset |
| `powerAdd = powerSys[generator].power` | `varz.c:322` (inside `JE_getShipInfo`) | OK. `powerAdd` is an unsigned int (`config.c:172`) and is only refreshed when `JE_getShipInfo` runs (level start `tyrian2.c:751`, shop `game_menu.c:245,571`, edited ships `mainint.c:2951,2994`). A live mapping must bypass `powerAdd` at the point of use, not in `JE_getShipInfo`. It is an integer, so fractional income needs a remainder accumulator |
| Shot drains `poweruse` | `shots.c:321-323` | OK. If the pool is short the function returns before touching `shotRepeat`, so the weapon retries on the next frame |
| Sidekicks drain too | `mainint.c:4327,4348` call `player_shot_create(options[].wport, ...)` | Confirmed; nearly all use port 16 (`poweruse` 10), Zica Flamethrower uses port 4 (20). Limited-ammo sidekicks only fire on their own buttons; infinite-ammo ones also on the main fire button |
| Specials use the pool | `varz.c:667-698` | **No**: specials cost shield or armor (`special[].pwr`), not power. They call `player_shot_create(0, ...)` for weapon-type specials and port 0 has `poweruse` 0 |
| Shield regen | `tyrian2.c:825` `shieldT = tpwr*20` (a `JE_byte`); `1072-1085` | OK: +1 shield every 15 frames while `shield < shield_max (=2*mpwr)` **and `power > shieldT`**. The 15-frame countdown only advances while `power > shieldT`, so a starved pool also stalls shield regen. 2P regen is free (`1057-1070`). Shields start the level at `mpwr`, max is `2*mpwr` |
| Pool is empty at the start of every level | `tyrian2.c:747` `power = 0` | **New fact.** Fill time 0.5-5 s depending on income (T9). The upgrade screen starts at 500 (`game_menu.c:2248`) |
| Shots per second = frames/s / shotrepeat | `shots.c:500` sets `shotRepeat = shotrepeat` after firing; `mainint.c:4084-4093` decrements once per frame while > 0 and fires on the frame it is 0 | **Correction**: period is `shotrepeat + 1` frames, so shots/s = fps / (shotrepeat+1). The brief's formula overstates rate by (r+1)/r, e.g. 33% for `shotrepeat` 3. The upgrade-screen simulator uses the same logic (`game_menu.c:3160-3210`, power gain at `3225`) |
| ~34.8 frames/s | `nortsong.c:49-58`, `config.c:586-631` | OK. Tick = `0x4300 * 1000*88*3/315e6` = 14.375 ms, `frameCountMax` 2 ticks = 28.75 ms = **34.78 fps** at game speed "Normal" (default `gameSpeed = 4`, `config.c:699`). Other speeds: Pentium Hyper 48.55 fps, Slow 34.78 (pentiumMode only), Slower 23.19, Slug 14.04 |
| hdt format | `doc/tyrian.hdt.txt` vs `episodes.c:62-158` | One doc/code mismatch: doc says `powerSys.power` is s8, code reads U8. Irrelevant (max 50). Generator name is spelled "Advanced MircoFusion" in the data |

Episode 4 (Tyrian 2000) keeps its item block at the last level offset of `tyrian4.lvl` (`episodes.c:87`, `lvllib.c:41-51`). Cross-check with `hdt_reader.py tyrian4.lvl`: generators, shields and sidekicks are **identical** to episodes 1-3, ship 13 costs 30000 instead of 65535, and 24 weapon patterns differ in `shotrepeat` (so a few weapon drains in T4 shift slightly in episode 4). The generator and shield findings hold for all four episodes.

## 2. Data extraction

`hdt_reader.py`: pure-stdlib reader following `JE_loadItemDat` (header: u32 offset at file start; 7 u16 counts asserted against 780/42/6/13/30/10/850; then 781 weapons x 0x50 bytes, 43 ports, 47 specials, 7 generators, 14 ships, 31 sidekicks, 11 shields). Generator names came out of the file exactly as expected: Standard MR-9, Advanced MR-12, Gencore Custom MR-12, Standard MicroFusion, Advanced MircoFusion (sic), Gravitron Pulse-Wave. Shield, ship and weapon names (e.g. Pulse-Cannon, Atomic RailGun, USP Talon) also decoded cleanly, and the whole item block ends at byte 88130 of the 2.1 `tyrian.hdt`, consistent with the file layout.

Files in this directory: `items_generators.csv`, `items_weapon_ports.csv`, `items_port_levels.csv` (every port x mode x level with `shotrepeat`, shots/s, drain u/s), `items_shields.csv`, `items_ships.csv`, `items_sidekicks.csv`, `economics_tables.md` (all generated tables).

#### T1. Generators (`powerSys`, `power += powerAdd` once per frame)

| id | name | power/frame | units/s (x34.78) | s to fill 0->900 | cost | = watts at k=0.1 u/frame/W |
|---|---|---|---|---|---|---|
| 1 | Standard MR-9 | 10 | 348 | 2.6 | 500 | 100 W |
| 2 | Advanced MR-12 | 14 | 487 | 1.8 | 2000 | 140 W |
| 3 | Gencore Custom MR-12 | 19 | 661 | 1.4 | 5000 | 190 W |
| 4 | Standard MicroFusion | 25 | 870 | 1.0 | 10000 | 250 W |
| 5 | Advanced MircoFusion | 30 | 1043 | 0.9 | 15000 | 300 W |
| 6 | Gravitron Pulse-Wave | 50 | 1739 | 0.5 | 50000 | 500 W |


Reading: the six generators are 100, 140, 190, 250, 300, 500 "watts" if you scale 0.1 unit/frame per W, which is a convenient mnemonic and nothing more.

## 3. Per-second economics

Units: 1 power unit = 1 count of `power`. Frame rate 34.78/s. Weapon drain = `poweruse / (shotrepeat+1)` per frame while the fire button is held. Weapon level (1-11) changes the pattern and therefore `shotrepeat`: full per-level tables are in `items_port_levels.csv` and a summary is in Appendix T4.

#### T2. Shields (`shieldT = tpwr*20`, +1 shield every 15 frames while shield < 2*mpwr and `power > shieldT`)

| id | name | tpwr | mpwr | shield max (2*mpwr) | drain while regenerating (u/frame) | (u/s) | full refill 0->max: time / pool units |
|---|---|---|---|---|---|---|---|
| 1 | Structural Integrity Field | 5 | 2 | 4 | 6.67 | 232 | 1.7 s / 400 |
| 2 | Advanced Integrity Field | 3 | 3 | 6 | 4.00 | 139 | 2.6 s / 360 |
| 3 | Gencore Low Energy Shield | 4 | 4 | 8 | 5.33 | 186 | 3.5 s / 640 |
| 4 | Gencore High Energy Shield | 6 | 5 | 10 | 8.00 | 278 | 4.3 s / 1200 |
| 5 | MicroCorp LXS Class A | 8 | 6 | 12 | 10.67 | 371 | 5.2 s / 1920 |
| 6 | MicroCorp LXS Class B | 6 | 7 | 14 | 8.00 | 278 | 6.0 s / 1680 |
| 7 | MicroCorp LXS Class C | 5 | 9 | 18 | 6.67 | 232 | 7.8 s / 1800 |
| 8 | MicroCorp HXS Class A | 9 | 10 | 20 | 12.00 | 417 | 8.6 s / 3600 |
| 9 | MicroCorp HXS Class B | 7 | 12 | 24 | 9.33 | 325 | 10.4 s / 3360 |
| 10 | MicroCorp HXS Class C | 6 | 14 | 28 | 8.00 | 278 | 12.1 s / 3360 |


Shield regen is expensive: it costs 4-12 units/frame while healing, as much as a typical front gun, and a ship starts a level with `mpwr` shield points, so topping up to `2*mpwr` costs `mpwr*tpwr*20` = 180-1800 pool units (the last column of T2 is 0 to max, twice that), up to two full pools.

#### T6. Illustrative loadouts per game stage: demand vs. stock generator (all weapons firing continuously)

Loadouts are my own illustrative picks (ids from T4/T5), not extracted from game data.

| stage | front (port, lvl) | rear | sidekicks | weapon drain u/frame | u/s | stock gen (u/frame) | sustainable fire % (no shield regen) | s to drain full pool at 100% fire | shield regen u/frame (while healing) | % fire while also healing shield |
|---|---|---|---|---|---|---|---|---|---|---|
| S0 New game (levels 1-2) | Pulse-Cannon L1 (7.5) | - (0.0) | - (0.0) | 7.5 | 261 | Advanced MR-12 14 | 100% | inf | 8.0 | 80% |
| S1 Early (~$10-30k) | Multi-Cannon L3 (8.0) | Vulcan Cannon L2 (6.7) | Single Shot Option, Single Shot Option (3.3) | 18.0 | 626 | Advanced MR-12 14 | 78% | 6.5 | 8.0 | 33% |
| S2 Mid (~$50-100k) | Zica Laser L5 (11.2) | Rear Heavy Missile Launcher L4 (11.2) | Vulcan Shot Option, Vulcan Shot Option (6.7) | 29.2 | 1014 | Standard MicroFusion 25 | 86% | 6.2 | 8.0 | 58% |
| S3 Late (~$200k+) | Hyper Pulse L6 (16.7) | Rear Mega Pulse L6 (12.9) | Zica Flamethrower, Vulcan Shot Option (8.3) | 37.9 | 1317 | Advanced MircoFusion 30 | 79% | 3.3 | 12.0 | 48% |
| S4 Max-burn (worst case) | Lightning Cannon L11 (35.0) | Rear Heavy Missile Launcher L11 (15.0) | Zica Flamethrower, Zica Flamethrower (10.0) | 60.0 | 2087 | Gravitron Pulse-Wave 50 | 83% | 2.6 | 8.0 | 70% |


What this says:

- **Per-frame demand of a typical loadout** grows from 7.5 (starter gun) to 18, 29, 38 and a worst-case 60 units/frame, i.e. 261 to 2087 units/s. The generator ladder (14 / 25 / 30 / 50) keeps pace, always leaving a small deficit (sustainable fire 78-86%). The designers sized the tank (900) to bridge 3-6.5 s of full fire.
- **The starter loadout is comfortably inside the starter generator** (7.5 vs 14, 100% fire, spare 6.5/frame for shield regen). That is the floor to protect: any easy-riding income must still be at least about 8/frame.
- **Shield regen is the main second consumer.** When healing at the same time as full fire, S1 sustains only 33% of fire. Any mapping that cuts income will make shields stall first (the regen countdown pauses when `power <= shieldT`), then fire rate. That is a feel risk and the reason for hook 5.3 (decouple shield regen from the pool).
- Individual high-drain weapons (Lightning Cannon L11 35/frame, Rear Mega Pulse L1 30) can match or exceed even the Advanced MircoFusion (30) by themselves, so the "I can't fire forever" feel exists at every tier.

## 4. Candidate watts-to-generator mappings

Common ingredients (all from Flywheel `effort-engine.ts`): `r = EMA3s(watts) / FTP`; `effortNorm n(r)` = piecewise through (0, 0.10), (0.40, 0.15), (0.75, 0.50), (1.10, 1.00), flat above 1.10, and 0 when not moving. The design rule is stated in the `effort-engine.ts` header ("Reward intensity without punishing an easy spin: `effortNorm` has a floor while moving, and nothing here ever produces a penalty signal") and PLAN.md (Context: "reward intensity without punishing easy riding"; section 4 Charging: "Pulse has a non-zero floor, so the ship can always shoot"; section 4 Difficulty: "Effort buys advantages, never harder enemies") and `shooter-sim.ts:391` charges energy at `5 + 30*effortNorm` per second while moving. So a floor is the house style, and nothing may be a penalty signal.

Output is in **power units per frame** (what replaces `powerAdd`). Constant floor `F = 8`.

| | Formula | Generator item matters? | FTP matters? | Frame-rate invariant? |
|---|---|---|---|---|
| A. Absolute | `max(F, 0.1 * W)` (250 W = 25 = MicroFusion) | no | no | no |
| B. FTP-relative, fixed tier | `max(F, 25 * n(r) / n(1.0))`, n(1.0) = 0.857 | no | yes | no |
| C. Generator-scaled | `max(F, G * (0.5 + n(r)))`, `G = powerSys[gen].power` | yes (scales everything) | yes | yes (relative to stock) |

#### T7. Candidate mappings: power units PER FRAME handed to `power += ...` (steady-state smoothed watts)

Definitions: r = W/FTP; n(r) = Flywheel `effortNorm` piecewise curve through (0,0.10) (0.40,0.15) (0.75,0.50) (1.10,1.00), flat above 1.10.
- A absolute: `max(8, 0.1*W)`  (generator item ignored)
- B FTP-relative, fixed tier: `max(8, 25 * n(r)/n(1.0))`, n(1.0)=0.857  (generator item ignored; r=1.0 -> 25 = MicroFusion)
- C generator-scaled: `max(8, G * (0.5 + n(r)))`, G = stock `powerSys[gen].power` (r=0.75 -> exactly stock; r>=1.1 -> 1.5x; r=0 -> 0.6x)

| FTP | watts | r | n(r) | A | B | C gen2 (G=14) | C gen4 (G=25) | C gen6 (G=50) | stock gen2 / gen4 / gen6 |
|---|---|---|---|---|---|---|---|---|---|
| 150 | 100 | 0.67 | 0.42 | 10.0 | 12.2 | 12.8 | 22.9 | 45.8 | 14 / 25 / 50 |
| 150 | 150 | 1.00 | 0.86 | 15.0 | 25.0 | 19.0 | 33.9 | 67.9 | 14 / 25 / 50 |
| 150 | 200 | 1.33 | 1.00 | 20.0 | 29.2 | 21.0 | 37.5 | 75.0 | 14 / 25 / 50 |
| 150 | 250 | 1.67 | 1.00 | 25.0 | 29.2 | 21.0 | 37.5 | 75.0 | 14 / 25 / 50 |
| 250 | 100 | 0.40 | 0.15 | 10.0 | 8.0 | 9.1 | 16.2 | 32.5 | 14 / 25 / 50 |
| 250 | 150 | 0.60 | 0.35 | 15.0 | 10.2 | 11.9 | 21.2 | 42.5 | 14 / 25 / 50 |
| 250 | 200 | 0.80 | 0.57 | 20.0 | 16.7 | 15.0 | 26.8 | 53.6 | 14 / 25 / 50 |
| 250 | 250 | 1.00 | 0.86 | 25.0 | 25.0 | 19.0 | 33.9 | 67.9 | 14 / 25 / 50 |


Reading T7 (units/frame; compare stock 14 / 25 / 50):

- **A** ignores FTP: a 100 W rider gets 10/frame whether that is a hard or an easy effort for them. On the Q200B, watts are mostly a function of the knob (`hardware-results.md`: 35 W at resistance 1.3, 438 W at 7.6, at similar cadence), so absolute mapping measures the knob position, not effort. Generators are cosmetic. Easy to explain, wrong for fairness.
- **B** is fair across riders (150 W is 100% FTP for FTP 150 and only 60% for FTP 250), saturates at 29 above 110% FTP, and also makes the generator item cosmetic. Upgrades would then only matter through weapons, and a 50000-credit Gravitron would be worth nothing.
- **C** keeps vanilla balance at r = 0.75, makes every generator worth buying, and uses the rider's FTP. A weak-FTP rider and a strong-FTP rider on the same generator see the same game at the same relative effort. Gravitron at r = 1.1 gives 75/frame, which is large; the cap can be tightened (e.g. multiplier 1.3 instead of 1.5) if playtests show the late game becomes trivial, since every extra over `poweruse` demand is wasted once the pool is full at 900 anyway.

#### T8. Same riders: % of continuous fire sustained (income / demand, capped at 100%) for S1 and S2 loadouts

S1 demand 18.0 u/frame with gen2 (stock 14 -> 78%); S2 demand 29.2 u/frame with gen4 (stock 25 -> 86%).

| FTP | watts | A: S1 / S2 | B: S1 / S2 | C: S1 (gen2) / S2 (gen4) |
|---|---|---|---|---|
| 150 | 100 | 56% / 34% | 68% / 42% | 71% / 79% |
| 150 | 150 | 83% / 51% | 100% / 86% | 100% / 100% |
| 150 | 200 | 100% / 69% | 100% / 100% | 100% / 100% |
| 150 | 250 | 100% / 86% | 100% / 100% | 100% / 100% |
| 250 | 100 | 56% / 34% | 44% / 27% | 51% / 56% |
| 250 | 150 | 83% / 51% | 57% / 35% | 66% / 73% |
| 250 | 200 | 100% / 69% | 93% / 57% | 83% / 92% |
| 250 | 250 | 100% / 86% | 100% / 86% | 100% / 100% |


Reading T8: the 150-FTP rider at 100 W (an easy 67% FTP spin) gets 71% / 79% fire in S1 / S2 under C and 100% at FTP. The 250-FTP rider at the same 100 W (a very easy 40% FTP) gets 51% / 56% under C. In S1 A is a little kinder to that rider (56%) but in S2 A and B collapse to 34% and 27% because their income is blind to how far the ship has progressed; C scales with the generator, so progress is felt equally at every tier. Nobody drops below the 8-unit floor, and a rider holding 75% FTP gets exactly the original game.

**Recommendation: C**, with these refinements for the implementation:

- Compute `income_q = floor-limited float`, add to an `uint` remainder accumulator, and hand the integer part to `power += ...` each frame (`powerAdd` is an unsigned int; 14.4 would otherwise round).
- `moving == false` (silent or stopped): use `F/2 = 4`, not 0. Tyrian has no free weapon, and a rider who stopped mid-boss should not be defenceless for something the sensor lag caused.
- Under-FTP zone: leave `0.5 + n(r)` as is; r = 0.4 gives 0.65 G.
- The shop's generator preview and `game_menu.c:3225` simulator should use the r = 0.75 value (= stock) so shop numbers stay comparable.
- Make the whole curve a four-number config (floor, knee multiplier at r=0.75, cap multiplier, n-knots) so playtesting can retune without recompiling.

#### T9. Time to fill an empty pool (power = 0 at every level start, tyrian2.c:747) at various incomes

| income u/frame | 5 | 8 | 10 | 14 | 19 | 25 | 30 | 50 |
|---|---|---|---|---|---|---|---|---|
| seconds to 900 | 5.2 | 3.2 | 2.6 | 1.8 | 1.4 | 1.0 | 0.9 | 0.5 |


The empty pool at level start costs a stock player 1.8 s (gen 2) and the floor-income rider 3.2 s before first full burst; fine. If it feels bad, start each level at e.g. 300 (a one-line change at `tyrian2.c:747`) for flywheel mode only.

## 5. Sparse, laggy bike data

Facts from `docs/hardware-results.md` (Q200B): notifications about every 1.1 s (0.91-0.92 Hz); power appears to be an estimate computed from cadence and the knob position (R^2 0.988 from a 5-term polynomial); power is exactly 0 only when cadence reads 0; the bike holds the last cadence for about 3 s after the pedals stop and then reads 0, 5.6 s and 18.1 s after stopping in two runs; resistance is an integer 0-9 following a manual knob.

| Topic | Recommendation | Why / Flywheel value |
|---|---|---|
| Sample handling | Latch the latest sample, sample-and-hold, advance the EMA every game frame with `dt = 1/34.78`. Do not extrapolate | Same as `EffortEngine.step` (fixed dt, latest sample). Deterministic given a recorded stream |
| Smoothing | One EMA, `tau = 3 s` (Flywheel `effortTauS`). Step response: 63% after 3 s, 90% after 6.9 s; plus about 0.55 s average hold from 1.1 s sample spacing. If feel is sluggish try `tau = 2 s` attack and 4 s release | The tank adds its own lag (0.5-2.6 s to fill), so more smoothing than 3 s is not needed. Keeping 3 s allows reuse of Flywheel's golden fixtures |
| Staleness timeout | Sample age > 3 s with link up means "not pedalling" (Flywheel `silentAsStoppedS = 3`, about 3 notify intervals, tolerating two lost packets). One lost packet (2.2 s gap) is invisible | Dropouts shorter than 3 s are simply held |
| Stop detection | Phase 1: none beyond the staleness timeout, plus "cadence < 15 rpm for 1.5 s" (`movingOffRpm`, `movingGraceS`). Phase 2: port the coast-down detector (cadence < 45 rpm and at least 35% below the max of the last 5 s) | Without it, the Q200B keeps reporting the old cadence/power for 5-18 s after the rider stops. For this mechanic that only means a few free seconds of income (harmless, never a penalty), so it is not on the critical path. It matters if you later pause the game or show a "keep pedalling" prompt |
| Link down (>= 1.5 s, Flywheel `sensorLostAfterS`) | Blend the income toward the **stock** `powerSys` value over about 3 s, show "BIKE LOST", blend back when the link returns. Never zero, never auto-pause by default | Not the rider's fault (Flywheel's `sensorLost` already refuses to blame the rider). Vanilla balance as fallback means a crashed bridge can't make a level unwinnable, and cannot be used to cheat beyond the original game |
| Before first sample / flag off | Stock | Same as above; also the safe default for headless/test runs |
| FTP | Store `ftpW` in config; default 200 W (`DEFAULT_FTP_W`, bike watts). Reuse the 8-minute test later: FTP = 0.9 * mean of pedalled seconds | Watts are bike-specific ("bike watts"), so FTP has to be measured on the bike |
| Power null | Q200B always reports power. Keep the cadence-only fallback curve only if a bike lacks it | Flywheel falls back to `powerModel` / cadence curve |

### Flywheel code to port for a standalone C build

| TS file | Lines (all / code only) | Needed for C build | Rough C size |
|---|---|---|---|
| `game/src/core/effort/effort-engine.ts` | 528 / 420 | Phase 1: `step()` front half (stale/fresh sample choice, power selection), `effortEma`, `snapshot().effortNorm` + `EFFORT_NORM_KNOTS`, `updateMoving` with grace (about 50 lines of TS). Phase 2: `updateCoastDown`/`start`/`stop` (lines 304-363). Phase 3 (hooks): `updateSurge` + `surgeIntensity` (lines 377-433). Skip: zones, streak, compliance, baseline unless surge is ported | about 150 LOC Phase 1, about 80 more for the stop detector, about 70 more for surge |
| `game/src/core/effort/filters.ts` | 52 / 42 | `Ema` (time-constant form `a = 1 - exp(-dt/tau)`), `piecewise`, `clamp` | about 25 LOC |
| `game/src/core/recording/ftp.ts` | 62 / 37 | `ftpFromTestBlock` only (suggestFtp optional) | about 20 LOC, can wait |
| `game/src/core/telemetry/power-model.ts` | 30 / 11 | Only for bikes with no power field | about 15 LOC, can wait |
| `game/src/core/ride/ride-flow.ts` | 169 | Only the `sensorLost` timing (1.5 s) and the `autoPauseAfterS` (10 s) idea | about 30 LOC |
| `game/src/core/telemetry/protocol.ts` / `ws-source.ts` | 171 / 154 | Only if the C build talks to the existing Python bridge over WebSocket; transport is a separate spike | out of scope here |
| tests | `effort-engine.test.ts` 296, `effort-engine.fixtures.test.ts` 60, `shared/fixtures/rides/q200b-run*.samples.json` | Port as a golden-vector harness (dump TS signals per step, diff against C) | small |

All of it is pure arithmetic on floats with no allocation, so it drops into the game loop (`dt = 1/34.78`) without threading issues except the sample hand-off from the transport thread (a mutex or a single-writer struct copy per frame).

## 6. Other hooks, and where the feature must be off

### 6.1 Candidate hooks

| Idea | Variables / places | Notes and verdict |
|---|---|---|
| Sprint or surge to extra bomb | `player[0].superbombs` (cap 10; pickup increments at `mainint.c:4752`; fired at `4162-4171` via button 2/3) | Grant +1 on Flywheel's `overdriveReady` event. Clean, discrete, matches Flywheel's banked overdrive. **Recommended first hook** |
| Surge to faster special recharge | `specialWait` (decremented at `varz.c:658`), `shotRepeat[SHOT_SPECIAL]` (`654-657`), `nextSpecialWait` (`varz.c:471`) | Do an extra decrement per frame while `surging`. Only affects specials that use the wait. Specials pay shield/armor, not the pool |
| Surge to faster sidekick charge | `sidekick[i].charge_ticks` (reset to 20 on fire, `mainint.c:4335`, decremented and charge stage incremented at `4395-4399`; stages `options[].pwr`: Charge Cannon 2, Zica SuperCharger 4) | Reduce `charge_ticks` reset to 10 while `surging`. Low-risk, visible |
| Effort to shield regen | `shieldWait` period 15 (`tyrian2.c:1061,1076`), cost `shieldT` (`825`) | Preferred shield design: stop charging the pool for regen and instead make the interval `24 - 12*effortNorm` frames (22 to 12). Mirrors Flywheel (`shooter-sim.ts:395`: shields `2 + 14*effortNorm` per second). Removes the double penalty seen in T6 (33% fire while healing). Single edit near `tyrian2.c:1072-1085` |
| Cadence to ship speed | `x_velocity`/`y_velocity` clamp +/-4 (`mainint.c:3805-3806`); `ships[].spd` is **unused** by the engine (no read besides loading) | Only as a bonus: raise the clamp to 5 above effortNorm 0.8. Never lower it below 4 (would be a penalty). Integer velocities make this coarse. Low value |
| Cadence to scroll | `map1YDelayMax` / `backMove` (`tyrian2.c:1161-1177`; written by level events `4184-4205`, `4568-4572`), `starfield_speed` (`748`, `4179`, cosmetic) | Do **not** touch `backMove`/`map1YDelayMax`: level events fire from `curLoc`, which advances with scroll, and scripts set these values themselves, so changing them rescales enemy density and boss timing. `starfield_speed` is safe if only cosmetic feedback is wanted |
| Weapon level boost while surging | `player[0].items.weapon[].power` | Rejected: HUD bar, shop and save logic read it |
| HUD watts / zone readout | power bar draw at `tyrian2.c:1122-1133`, text helpers in `fonthand.c` | Needed for feedback; cosmetic |

### 6.2 Where to leave the feature off (force stock `powerSys`)

| Mode | Condition in source | Why |
|---|---|---|
| Two-player (local) | `twoPlayerMode` | Power pinned at 900 (`tyrian2.c:1112`); one bike cannot represent two ships |
| 1P Arcade, Super Arcade, Super Tyrian, ENGAGE | `onePlayerAction` (also restored from saves, `config.c:469`) | Same pin; the arcade modes are balanced around unlimited power |
| Network game | `isNetworkGame` | Lockstep: each peer simulates all players from key packets (packet carries `curLoc`), so `power` must be identical on every machine; a local sensor would desync it |
| Demo playback | `playDemo` (`demo.c`) | Demos store items, seed and key stream only; shot timing depends on pool, so replay diverges unless income equals the recorded stock value |
| Demo recording | `recordDemo` (`--record`) | Same reason; recording with bike income would produce demos that fail on any other machine |
| `constantPlay` (unattended test loop) | `constantPlay` | No rider; use stock |
| Galaga mini-game | `galagaMode` | Own balance, forced weapon level 0 (`mainint.c:4090`); keep stock |
| Destruct | separate loop (`destruct.c`) | Doesn't use the pool; unaffected |
| Upgrade-screen simulator | `game_menu.c:2248,3225` | Fine to use; prefer the r = 0.75 (= stock) value so the shop is consistent |

One structural note: the cleanest implementation is a single function replacing the two `powerAdd` uses, `static inline uint power_income(void)` that returns `powerAdd` unless `flywheel_active && !(twoPlayerMode || onePlayerAction || isNetworkGame || playDemo || recordDemo || constantPlay || galagaMode)`. Everything else in the game keeps reading `power` normally.

## 7. Caveats and open points

- "Typical loadout per stage" (T6) is my own illustration (ports/levels chosen from the item list, not from play data). The conclusion (stock gen sustains about 80% fire) is robust to the exact picks, but the exact percentages are not. A quick check against recorded player saves would firm it up.
- Sidekick drain is computed with charge 0 (base `wpnum`); charged sidekick shots use `wpnum + charge` and different `shotrepeat`.
- Rear weapons have two firing modes for 8 ports; `items_port_levels.csv` includes both, T4 shows mode 2 for L1/L6.
- Only static analysis and data extraction were done; the game was not run, so fps is derived from the timer constants rather than measured.
- Q200B watts depend mostly on the knob (estimated power). A rider can raise "effort" by turning the knob instead of pedalling harder; FTP measured on the bike makes this self-consistent but it is a real property of this bike. Cadence is available and could be mixed in later (for example require cadence >= 40 rpm before any above-knee bonus).
- Episode 4 weapons differ in 24 `shotrepeat` values; re-run `hdt_reader.py` on `tyrian4.lvl` for exact numbers.

## Appendix: remaining generated tables

#### T3. Ships

| id | name | armor (`dmg`) | spd (unused by engine) | cost |
|---|---|---|---|---|
| 1 | USP Talon   Light Fighter | 10 | 0 | 6000 |
| 2 | SuperCarrot | 26 | -2 | 65000 |
| 3 | Gencore Phoenix | 17 | -1 | 12000 |
| 4 | Gencore Maelstrom | 20 | 0 | 15000 |
| 5 | MicroCorp Stalker | 25 | -2 | 20000 |
| 6 | MicroCorp Stalker-B | 27 | -2 | 25000 |
| 7 | Prototype Stalker-C | 28 | 0 | 50000 |
| 8 | Stalker | 15 | 0 | 10000 |
| 9 | USP Fang    Light Fighter | 14 | 0 | 8000 |
| 10 | U-Ship | 8 | 0 | 8000 |
| 11 | Silver Ship | 10 | 0 | 8000 |
| 12 | Nort Ship | 30 | 0 | 65000 |
| 13 | The Stalker 21.126 | 30 | 0 | 65535 |


#### T4. Weapon ports: continuous-fire drain in power units per frame (poweruse / (shotrepeat+1)); x34.78 for units/s

| port | name | poweruse | cost | L1 | L3 | L6 | L11 | (mode 2: L1 / L6) |
|---|---|---|---|---|---|---|---|---|
| 1 | Pulse-Cannon | 30 | 500 | 7.50 | 6.00 | 5.00 | 5.00 |  |
| 2 | Multi-Cannon | 40 | 750 | 10.00 | 8.00 | 6.67 | 6.67 |  |
| 3 | Mega Cannon | 120 | 1000 | 13.33 | 13.33 | 13.33 | 13.33 |  |
| 4 | Laser | 20 | 900 | 20.00 | 20.00 | 20.00 | 20.00 |  |
| 5 | Zica Laser | 90 | 1100 | 9.00 | 10.00 | 11.25 | 11.25 |  |
| 6 | Protron Z | 50 | 900 | 10.00 | 10.00 | 10.00 | 10.00 |  |
| 7 | Vulcan Cannon | 20 | 600 | 10.00 | 10.00 | 10.00 | 20.00 |  |
| 8 | Lightning Cannon | 70 | 1000 | 11.67 | 11.67 | 11.67 | 35.00 |  |
| 9 | Starburst | 20 | 900 | 6.67 | 10.00 | 6.67 | 6.67 |  |
| 10 | Multi-Cannon | 40 | 750 | 8.00 | 5.71 | 5.71 | 5.71 | 10.00 / 5.71 |
| 11 | Sonic Wave | 50 | 950 | 7.14 | 7.14 | 7.14 | 7.14 | 7.14 / 7.14 |
| 12 | Protron | 50 | 650 | 6.25 | 6.25 | 6.25 | 6.25 | 4.17 / 5.56 |
| 13 | Protron | 50 | 600 | 10.00 | 8.33 | 7.14 | 7.14 |  |
| 14 | Wild Ball | 60 | 800 | 6.67 | 6.67 | 6.67 | 6.67 | 6.67 / 6.67 |
| 15 | Vulcan Cannon | 20 | 500 | 6.67 | 5.00 | 6.67 | 10.00 |  |
| 16 | Miscellaneous Option Weapons | 10 | 0 | 1.67 | 10.00 | 2.50 | 5.00 |  |
| 17 | Missile Launcher | 50 | 850 | 5.56 | 4.55 | 3.85 | 3.85 |  |
| 18 | Fireball | 50 | 1000 | 4.17 | 4.17 | 4.17 | 4.17 | 4.17 / 4.17 |
| 19 | Mega Pulse | 60 | 900 | 15.00 | 12.00 | 10.00 | 10.00 |  |
| 20 | Heavy Missile Launcher | 90 | 1000 | 10.00 | 18.00 | 12.86 | 9.00 |  |
| 21 | Rear Heavy Missile Launcher | 90 | 1000 | 10.00 | 10.00 | 12.86 | 15.00 |  |
| 22 | Rear Mega Pulse | 90 | 900 | 30.00 | 18.00 | 12.86 | 12.86 |  |
| 23 | Banana Blast | 20 | 950 | 3.33 | 4.00 | 5.00 | 5.00 |  |
| 24 | Banana Blast Rear | 20 | 1100 | 4.00 | 4.00 | 0.95 | 1.25 |  |
| 25 | HotDog Front | 40 | 1100 | 10.00 | 8.00 | 10.00 | 5.71 |  |
| 26 | HotDog Rear | 40 | 1100 | 13.33 | 10.00 | 8.00 | 5.71 |  |
| 27 | Hyper Pulse | 50 | 1050 | 16.67 | 16.67 | 16.67 | 12.50 |  |
| 28 | Guided Micro Bombs | 60 | 1100 | 3.75 | 5.45 | 5.45 | 6.00 |  |
| 29 | Heavy Guided Bombs | 80 | 1000 | 3.81 | 3.81 | 3.81 | 3.81 |  |
| 30 | Scatter Wave | 40 | 900 | 8.00 | 6.67 | 6.67 | 6.67 |  |
| 31 | Guided Bombs | 40 | 800 | 4.44 | 6.67 | 3.64 | 2.50 |  |
| 32 | Shuruiken Field | 40 | 850 | 8.00 | 8.00 | 8.00 | 8.00 |  |
| 33 | Poison Bomb | 40 | 800 | 3.08 | 4.44 | 5.71 | 6.67 |  |
| 34 | Protron Wave | 40 | 750 | 8.00 | 5.71 | 5.71 | 5.71 |  |
| 35 | The Orange Juicer | 40 | 900 | 5.71 | 6.67 | 5.00 | 5.71 |  |
| 36 | NortShip Super Pulse | 40 | 1100 | 10.00 | 10.00 | 10.00 | 10.00 |  |
| 37 | NortShip Spreader | 40 | 1100 | 10.00 | 10.00 | 10.00 | 10.00 | 6.67 / 4.44 |
| 38 | NortShip Spreader B | 40 | 1100 | 6.67 | 4.44 | 4.44 | 4.44 |  |
| 39 | Atomic RailGun | 30 | 1101 | 7.50 | 7.50 | 7.50 | 7.50 |  |
| 40 | Widget Beam | 40 | 950 | 13.33 | 10.00 | 10.00 | 10.00 |  |
| 41 | Sonic Impulse | 50 | 1000 | 12.50 | 12.50 | 16.67 | 12.50 |  |
| 42 | RetroBall | 50 | 1000 | 10.00 | 10.00 | 10.00 | 10.00 |  |


#### T5. Sidekicks (all fire through `player_shot_create`, so they drain too)

| id | name | port (poweruse) | shotrepeat | drain u/frame | ammo |
|---|---|---|---|---|---|
| 1 | Single Shot Option | 16 (10) | 5 | 1.67 | inf |
| 2 | Dual Shot Option | 16 (10) | 5 | 1.67 | inf |
| 3 | Charge Cannon | 16 (10) | 6 | 1.43 | inf |
| 4 | Vulcan Shot Option | 16 (10) | 2 | 3.33 | inf |
| 5 | Wobbley | 16 (10) | 3 | 2.50 | inf |
| 6 | MegaMissile    Ammo 5 | 16 (10) | 8 | 1.11 | 5 |
| 7 | Atom Bombs     Ammo 20 | 16 (10) | 10 | 0.91 | 20 |
| 8 | Phoenix Device Ammo 8 | 16 (10) | 50 | 0.20 | 8 |
| 9 | Plasma Storm   Ammo 10 | 16 (10) | 3 | 2.50 | 10 |
| 10 | Mini-Missile   Ammo 100 | 16 (10) | 2 | 3.33 | 100 |
| 11 | Buster Rocket  Ammo 30 | 16 (10) | 10 | 0.91 | 30 |
| 12 | Zica SuperCharger | 16 (10) | 9 | 1.00 | inf |
| 13 | MicroBomb      Ammo 60 | 16 (10) | 20 | 0.48 | 60 |
| 14 | 8-Way MicroBomb  Ammo 30 | 16 (10) | 20 | 0.48 | 30 |
| 15 | Post-It Mine   Ammo 20 | 16 (10) | 2 | 3.33 | 20 |
| 16 | Mint-O-Ship | 16 (10) | 4 | 2.00 | inf |
| 17 | Zica Flamethrower | 4 (20) | 3 | 5.00 | inf |
| 18 | Side Ship      Ammo 100 | 16 (10) | 3 | 2.50 | 100 |
| 19 | Companion Ship Warfly | 16 (10) | 5 | 1.67 | inf |
| 20 | MicroSol FrontBlaster | 16 (10) | 5 | 1.67 | inf |
| 21 | Companion Ship Gerund | 16 (10) | 4 | 2.00 | inf |
| 22 | BattleShip-Class Firebomb | 16 (10) | 5 | 1.67 | inf |
| 23 | Protron Cannon Indigo | 16 (10) | 4 | 2.00 | inf |
| 24 | Companion Ship Quicksilver | 16 (10) | 5 | 1.67 | inf |
| 25 | Protron Cannon Tangerine | 16 (10) | 5 | 1.67 | inf |
| 26 | MicroSol FrontBlaster II | 16 (10) | 4 | 2.00 | inf |
| 27 | Beno Wallop Beam | 16 (10) | 20 | 0.48 | inf |
| 28 | Beno Protron System -B- | 16 (10) | 30 | 0.32 | inf |
| 29 | Tropical Cherry Companion | 16 (10) | 4 | 2.00 | inf |
| 30 | Satellite Marlo | 16 (10) | 4 | 2.00 | inf |

