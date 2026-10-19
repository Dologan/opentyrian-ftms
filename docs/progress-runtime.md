# PROGRESS: contract and conservative implementation

State as of 2026-10-07. This document describes `src/modern_progress.[ch]`, the hooks in `tyrian2.c`/`mainint.c` and the composition in `modern_hud.c`/`modern.c`. Reading the sources does not prove execution: validation results are kept separate below. The [structural inventory](progress-levels.md) contains counts and static signals, without certifying complete routes.

## Visual decision and meaning

**PROGRESS** is a continuous horizontal bar, filled from left to right with the palette, ramp, borders and glow of **SHIELD**. It measures an estimate of script advancement; it does not measure remaining time, enemies killed or damage to the boss. During a recognized boss, the percentage freezes. When the encounter ends, the continuation may resume if the estimate is still available. The existing boss HP bars remain.

**An early or alternative natural exit may end with the current percentage, below 100%.** Leaving the level, advancing the campaign or the boss bar disappearing does not prove the main objective was met. Filling to 100% requires the positive condition described below, with no earlier negative cause. There is no universal identification of a final boss nor direct promotion by its death.

## Model as actually implemented

The observer holds only its own presentation state. The hooks receive decisions already made by the game; they do not draw random numbers, do not call `JE_searchFor` and do not write gameplay arrays, flags, cursors or RNG. Event interpretation goes through `gameEventCase`, respecting the variant's table. In Classic, the observation hooks are inert.

| Part | Current behavior | Limit |
|---|---|---|
| Denominator | `modern_progress_begin` scans the loaded events in memory and chooses the greatest positive time of a semantic action 11/36; ties choose the last index found. | It is an estimate of a main milestone, without reachability analysis. It is not the last serialized event, the sentinel or a guaranteed duration. |
| Availability | Without a milestone, or with action 76/return 65535 in actions 54/70/71, it starts with `percent=-1`. The HUD omits the indicator. | Return routes do not have a certified call/return profile. Do not fabricate zero or a denominator from the greatest address. |
| Advancement | Uses observed increments of `curLoc` in the current stretch and the remaining budget up to 99%; integer percentage, monotonic per attempt. | The bar does not promise accuracy of physical distance or remaining time. |
| Forward jump | Does not credit the skipped address; keeps the percentage and redistributes only the remaining budget in the continuation up to the milestone. | If it reaches/passes the milestone by a jump, it invalidates the estimate. |
| Backward jump/loop | Freezes below the already observed frontier; resumes when it passes it. | Repetition adds no credit. It does not model a complete graph or the number of iterations. |
| Return/replacement | Return invalidates support; replacement by actions 38/75 also freezes the existing estimate. | The index searches of these actions are not equivalent to `JE_eventJump`. |
| Wait | Pauses for boss, no living players, `returnActive`, stopped scroll without `forceEvents`, `readyToEndLevel`, warp/finalization or an already registered end. | Clock movement during a wait is discarded, with no retroactive credit on release. |

The recognized boss is an active enemy with positive armor and a link corresponding to one of the two existing bars, in the first 100 slots queried. Setting the bar without a corresponding enemy is not enough to pause. This does not recognize bosses without a bar, link generations, transformation phases or mandatory final groups; removal/despawn is not equivalent to final defeat. The waits do not depend on the VFX queue.

## Exits and precedence

`modern_progress_event` records execution of the index chosen as the milestone. `modern_progress_end(true)` requests resolution of a natural exit: it only considers the main objective if that milestone was executed and `curLoc >= endpoint`. `modern_progress_resolve_end` queries the current survival state, without depending on a stale `allPlayersGone`; one survivor is enough in 2P. If the main condition is missing, `modern_progress_observer_finish(false)` ends the estimate at the current percentage (`ended`/`early_ends`), without forcing 100%. This closure stays latched until reset: a later milestone/callback during warp does not promote the ended attempt.

Abandonment, forced skip, demo end/interruption, synthetic flow end and unauthorized timeout block later promotion. Permanent death during natural resolution also blocks; during temporary death/respawn, the absence of living players only pauses and discards clock advancement, without canceling or starting a new attempt. The harness frame limit only ends the probe, and is not a level success. A main completion already confirmed keeps the 100% latch until the next reset, even if there is a later exit.

The timer policy contains an authorial exception for E4/L5 in the common mode, shared by the variants, allowing the survival route to be resolved by the normal main condition. Timed Battle and other expirations remain conservatively blocked. This exception does not certify all timers, modes or paths of E4; its execution evidence must be identified in validation, not deduced from the comment in the source.

`endLevel`, `reallyEndLevel`, `mainLevel`, the result animation and survival, in isolation, do not constitute an enumeration of success. Events 11/36 observed early are not enough either; case 36 waits for the real condition of enemies/explosions. The implementation uses hooks at the real decision points, without modifying these game outcomes.

## Reset and presentation

After `JE_loadMap` and `modern_level_reset`, `modern_progress_begin` restarts the attempt and counters. Pause, redraw, respawn and start of warp do not reset the observer. Pending resolution also occurs before moving on to the next level.

Drawing queries the snapshot and does not advance the observation. `modern_build_frame` redraws PROGRESS after the interpolation of the existing bars and before composing the panels/message strip, including frames held during warp/fade. The new bar has no interpolated record: a confirmed 100% appears in the next existing presentation, without adding ticks. Fades continue to affect the composition.

- **1P/arcade:** left panel, label at y=142 and five-pixel bar at y=150, maximum width 144. In a narrow panel the percentage sits beside the bar on the second line. Preserves the sidekick and boss region from y=158.
- **2P normal:** right side of the title line in the strip under the playfield, 36-pixel bar; the level name is truncated to the remaining width. The lower message line remains available.
- **No panels/Modern 4:3:** indicator intentionally omitted so as not to cover existing information. Classic also gets no indicator.

These positions describe the implementation; assertions and reading coordinates do not replace visual review of layouts, overlays, resize and fades.

## Coverage and validation

`--regress-progress-check` (420) verifies limits, monotonicity, pause and latch in the snapshot and emits an aggregated `Progress coverage`, including samples of waits, boss/loop resumptions, post-boss advancement, early exits and final percentage. Counters only demonstrate the paths actually taken; zero does not certify absence of the mechanism in the map. `--regress-observer-off` (421) allows comparing observer OFF/ON. The 420–429 range is reserved in AGENTS.md.

`tools/check_progress.sh` separates three kinds of evidence:

1. **Authorial scalar fixtures** (`tools/progress_fixture.c`): jumps, return, loops, pause, resumption, latch, unavailability and negative causes. They exercise the model, not spawn, boss death or completeness of the real hooks. Cause names in the fixture do not prove execution of those causes in the game.
2. **Representative runtime probes:** 300 initial frames in E1/L6, E1/L4, E1/L16 and E4/L18; also E5/L1 at 2000. They compare state/RNG OFF/ON in both modes and the Classic framebuffer. They require events and advancement where applicable; negative availability is expected in cases without a denominator. An initial probe does not cover the full path nor guarantee that the backward jump/return was taken.
3. **Specific paths:** 1:3 script with input/movement, autofire and pause request; accelerated E1/L1 fixture via `--regress-boss`, with samples of active boss and freezing. The acceleration does not prove natural arrival at the encounter, a defeated intermediate boss or defeat of the final boss.

### Results confirmed by the coordinator

The probes below use the **final release**, without `--regress-boss` or acceleration. The results replace the earlier debug measurements on these paths; the cap is a probe limit, not a cause of victory.

| Probe | 2.1 | 2000 | Evidence and scope |
|---|---|---|---|
| E1/L6, cap 5000 | final 52%, `early_ends=1` | final 52%, `early_ends=1` | Early natural exit preserves the value; no block or success. |
| E1/L4 | final 100%, 40 success samples | final 100%, 40 success samples | 99 advances in each variant; main objective resolved and latch observed. |
| E4/L7, 2P/fire, VFX OFF, cap 15000 | 5542 boss wait samples, 3 resumptions, 5 post-boss advances; final 45% | 5684 boss wait samples, 3 resumptions, 3 post-boss advances; final 43% | Real resumption of advancement in both variants; both with early exit, no block/success. Does not certify final boss identity. |

The final scalar fixture passed **271 assertions**, including early closure that ignores later promotion, also with ASan/UBSan. Sanitizers cover this fixture, not the full engine. Debug and release builds, the GCC16 audit of the eight touched C files (including the fixture) and the guard for absence of 2000 data were confirmed. The visual review used real captures of 2.1 only, in 16:9, 16:10 and 2P; it does not certify all combinations of overlay/resize/fade.

The E4/L5 timer exception was probed in the debug build before the final adjustments to early closure and pause on death: with fire and cap 9000, both variants had 1950 boss wait ticks and one resumption; 2.1 confirmed 100% with 37 success samples, while 2000 ended early at 50%, without a block. This evidence has a different revision and scope than the release probes above; it does not certify all timers or survival paths.

The Modern image references change intentionally because of the new PROGRESS pixels: 20 frame-hash files in 2.1 and 12 in 2000, plus two pause-frame CRC entries in the mixed matrix of each variant. The four derived checks that diverged in the initial 2.1 round corresponded to the same Modern frames; their outputs were compared byte for byte against the corresponding Modern baselines. No Classic or state reference was changed. This records the explicit reason for the 2.1 change. The full reruns below validated the final references.

The coordinator confirmed zero exit from the final release suites: **`make regress`: 172 cases; `make regress-2000`: 153 cases**, using the already verified 2000 installation. The existing guards and `check_progress.sh` passed, including OFF/ON state/RNG equality, Classic framebuffer, real input/pause and accelerated boss. The GCC16 audit was repeated on the eight C files of the final revision; portability checks, the Visual Studio project XML, CRLF of the `.vcxproj`, whitespace and absence of 2000 data also passed. These checks do not constitute certification of all paths, modes or encounters. This documentation task did not run tests or change baselines; the PASS results are results received from the coordinator, separate from reading the sources. Tyrian 2000 remains restricted to hashes/metadata and aggregated coverage, without exporting strings, sprites, maps, streams or screenshots.

## Future work: route graphs

A future design may analyze stretches, transitions, weights and encounters in memory, with identity by variant/episode/physical level/mode and explicit policies for branches, finite loops, return, replacements and bosses without a bar. It must observe executed decisions, without repeating predicates with side effects or RNG, and preserve the advancement already shown.

**This graph and per-path certified profiles are not implemented.** The static inventory does not certify them; hashes identify inputs, they do not prove execution. There is no promise of a useful estimate at every level or universal accuracy. Expanding coverage requires real paths with observable milestones/exit categories, two links and boss transformations, 2P with a survivor, modes/difficulties, timeout, skip, abandonment/network, demos and persistence across warp/fade. An alternative exit below 100% remains part of the accepted contract.
