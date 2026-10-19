# Structural inventory for the PROGRESS indicator

Investigation on 2026-10-07, read-only, with no builds, game execution or baseline updates. Scope: episode scripts and the events of the physical levels, as interpreted by this checkout's loaders. Current decision: horizontal **PROGRESS** in the style of SHIELD, frozen during a recognized boss and during a repeated stretch. An early/alternative natural exit may end at the current percentage, below 100%; 100% requires the positive condition of the main objective. The existing boss HP bars remain.

The 2.1 data in `data/` and the 2000 data already present in the user's installation location on macOS were analyzed. `TYRIAN2000_DATA` was not set. No download, installation, data copy or export of events, texts, maps or images was done. The parser and its metadata-only output stayed in scratch outside the repository. The counts below belong to the original static investigation; the later documentation review keeps them and distinguishes the conservative implementation from its execution coverage.

## What is being counted

**Physical level** is the record selected by `lvlFileNum`: the header of `tyrianN.lvl` contains pairs of offsets and a final entry, therefore `(offset_count - 1) / 2` records. The final entry is EOF in E1–E3 and the start of the item block in E4/E5; the item block is not a level. Reference: `src/lvllib.c`, `analyzeLevel`; `src/tyrian2.c`, `JE_loadMap`; [data-formats.md](t2000/data-formats.md).

**Logical section** is each record started by `*` in the `levelsN.dat` file. There are scenes, menus, choices and endings in addition to `]L` load commands. The loader uses the numeric field from byte 25 of `]L` to select the physical level, the field from byte 9 for the next section and, when that is zero, `mainLevel + 1`. Sections and loads are not a list of reachable levels: they are only a syntactic inventory. The same physical level may serve several routes and modes.

`--regress-level=E:L` selects a physical level in the scenario; `--regress-script=E:L` goes through the script and its logical section. Do not automatically reuse the physical IDs in this table as section IDs. The five demos only cover E1, physical levels 9, 12, 13, 7 and 5, in both variants; they do not constitute coverage of this inventory.

## Measured static coverage

| Variant/episode | Offsets | Physical levels | Serialized events | Logical sections | Load commands | Valid physical levels referenced | Out-of-range loads |
|---|---:|---:|---:|---:|---:|---:|---:|
| 2.1/E1 | 37 | 18 | 12270 | 42 | 18 | 18 | 0 |
| 2.1/E2 | 25 | 12 | 10210 | 24 | 12 | 12 | 0 |
| 2.1/E3 | 25 | 12 | 11326 | 25 | 12 | 12 | 0 |
| 2.1/E4 | 41 | 20 | 19532 | 51 | 20 | 20 | 0 |
| 2000/E1 | 37 | 18 | 12324 | 48 | 20 | 18 | 1 |
| 2000/E2 | 25 | 12 | 10205 | 24 | 12 | 12 | 0 |
| 2000/E3 | 25 | 12 | 11376 | 25 | 12 | 12 | 0 |
| 2000/E4 | 41 | 20 | 19586 | 51 | 20 | 20 | 0 |
| 2000/E5 | 17 | 8 | 6186 | 20 | 12 | 8 | 0 |

**2.1:** 62 physical levels, 53,338 events, 142 sections and 62 load commands. Exclusive categories: sequential=16, conditional=6, boss=23, backward=16, return=1.

**2000:** 70 physical levels, 59,677 events, 168 sections and 76 load commands. Exclusive categories: sequential=18, conditional=5, boss=27, backward=19, return=1.

All 62 physical levels of 2.1 and the 70 of 2000 have at least one valid syntactic reference. This does not show that each one is reachable in any mode or difficulty. In 2000/E1, two loads reference L5; there is also a load in section 44 that requests physical ID 20, outside the 18 records of that container. It is a structural exception to investigate through the interpreter's real path, without concluding that the load is executed or that a twentieth level exists in E1. In 2000/E5, L1 has three references; L3 and L4 have two each. Do not use the number of loads as the number of levels.

## Categories and reading the table

Exclusive categories, by priority: **return** (event 76 or a jump with special destination 65535); **backward** (an explicit jump to a time less than or equal to the event's serialized time, including setting an active timer); **boss** (event 79 with some non-zero link); **conditional** (event 61/63/66/70/71/75/80 or timer); **sequential** (none of the previous signals). “Sequential” means absence of these signals, not proof of linear execution. An event 57 arms a later jump on enemy death; comparing it with its setup time is only a conservative signal. Events 38 and `JE_eventJump` have different index searches; the classification does not simulate these searches.

The **11/36** column counts the two end markers. **79** counts all boss-bar commands, including deactivation; the boss category requires a non-zero link. Additional signals overlap categories: **C** condition; **T** timer 67, or 84 in 2000; **P** map stop 4, or 83 in 2000; **R** return; **B** backward. Quantities are metadata; they are not extracted event streams or times.

### Variant 2.1

| Physical | Events | 11/36 | 79 | Category | Signals |
|---|---:|---:|---:|---|---|
| E1/L1 | 456 | 1/1 | 1 | boss | P |
| E1/L2 | 731 | 1/0 | 1 | boss | P |
| E1/L3 | 323 | 2/0 | 0 | conditional | C P |
| E1/L4 | 240 | 0/1 | 0 | sequential | — |
| E1/L5 | 545 | 1/2 | 1 | boss | P |
| E1/L6 | 440 | 3/0 | 0 | sequential | — |
| E1/L7 | 517 | 2/0 | 0 | sequential | — |
| E1/L8 | 584 | 2/1 | 1 | boss | P |
| E1/L9 | 1009 | 1/1 | 1 | boss | C |
| E1/L10 | 248 | 1/1 | 0 | sequential | — |
| E1/L11 | 1214 | 2/0 | 0 | conditional | C |
| E1/L12 | 832 | 2/1 | 1 | boss | P |
| E1/L13 | 380 | 1/0 | 0 | sequential | — |
| E1/L14 | 838 | 2/0 | 0 | sequential | — |
| E1/L15 | 1246 | 1/1 | 1 | boss | C |
| E1/L16 | 833 | 0/1 | 2 | backward | C B |
| E1/L17 | 1000 | 1/0 | 2 | backward | C B |
| E1/L18 | 834 | 1/0 | 0 | backward | C B |
| E2/L1 | 1752 | 1/1 | 1 | backward | C B |
| E2/L2 | 828 | 1/0 | 1 | boss | C P |
| E2/L3 | 178 | 2/1 | 0 | sequential | — |
| E2/L4 | 421 | 2/1 | 0 | conditional | C P |
| E2/L5 | 1304 | 2/0 | 0 | sequential | — |
| E2/L6 | 1299 | 1/2 | 1 | backward | B |
| E2/L7 | 610 | 1/0 | 2 | backward | B |
| E2/L8 | 946 | 2/0 | 0 | conditional | C |
| E2/L9 | 1256 | 2/2 | 0 | backward | C B |
| E2/L10 | 812 | 1/1 | 1 | boss | — |
| E2/L11 | 552 | 1/1 | 1 | boss | — |
| E2/L12 | 252 | 1/0 | 0 | sequential | — |
| E3/L1 | 1205 | 1/0 | 1 | boss | C P |
| E3/L2 | 390 | 1/1 | 0 | sequential | — |
| E3/L3 | 1021 | 2/0 | 0 | sequential | — |
| E3/L4 | 903 | 1/1 | 1 | boss | — |
| E3/L5 | 786 | 2/0 | 0 | sequential | — |
| E3/L6 | 2259 | 1/1 | 1 | backward | B |
| E3/L7 | 672 | 1/1 | 0 | sequential | — |
| E3/L8 | 1182 | 1/1 | 1 | boss | — |
| E3/L9 | 818 | 2/0 | 0 | conditional | C |
| E3/L10 | 724 | 2/1 | 1 | boss | P |
| E3/L11 | 656 | 1/2 | 1 | boss | P |
| E3/L12 | 710 | 2/1 | 0 | sequential | — |
| E4/L1 | 1109 | 2/0 | 1 | boss | C P |
| E4/L2 | 970 | 2/0 | 0 | sequential | — |
| E4/L3 | 1129 | 2/1 | 1 | boss | C |
| E4/L4 | 904 | 2/0 | 2 | boss | C P |
| E4/L5 | 675 | 3/1 | 2 | boss | T P |
| E4/L6 | 640 | 1/1 | 0 | conditional | C |
| E4/L7 | 1338 | 1/1 | 3 | boss | C T P |
| E4/L8 | 750 | 1/1 | 1 | backward | C B |
| E4/L9 | 1444 | 1/1 | 2 | boss | C P |
| E4/L10 | 552 | 2/0 | 0 | sequential | — |
| E4/L11 | 909 | 2/1 | 2 | boss | C T |
| E4/L12 | 1243 | 2/0 | 1 | boss | C |
| E4/L13 | 1006 | 1/1 | 2 | backward | C B |
| E4/L14 | 1022 | 2/1 | 5 | backward | C B |
| E4/L15 | 1969 | 1/0 | 3 | backward | C B |
| E4/L16 | 666 | 1/1 | 5 | backward | C B |
| E4/L17 | 473 | 1/0 | 0 | backward | C B |
| E4/L18 | 595 | 0/0 | 0 | return | C R B |
| E4/L19 | 1288 | 1/0 | 1 | backward | C B |
| E4/L20 | 850 | 2/0 | 2 | backward | C T B |
### Variant 2000

| Physical | Events | 11/36 | 79 | Category | Signals |
|---|---:|---:|---:|---|---|
| E1/L1 | 481 | 1/1 | 2 | boss | P |
| E1/L2 | 731 | 1/0 | 1 | boss | P |
| E1/L3 | 323 | 2/0 | 0 | conditional | C P |
| E1/L4 | 240 | 0/1 | 0 | sequential | — |
| E1/L5 | 552 | 3/2 | 1 | boss | T P |
| E1/L6 | 441 | 3/0 | 0 | sequential | — |
| E1/L7 | 517 | 2/0 | 0 | sequential | — |
| E1/L8 | 602 | 2/1 | 1 | boss | P |
| E1/L9 | 1010 | 1/2 | 1 | boss | C |
| E1/L10 | 248 | 1/1 | 0 | sequential | — |
| E1/L11 | 1214 | 2/0 | 0 | sequential | — |
| E1/L12 | 832 | 2/1 | 1 | boss | P |
| E1/L13 | 379 | 1/0 | 0 | sequential | — |
| E1/L14 | 838 | 2/0 | 0 | sequential | — |
| E1/L15 | 1246 | 1/1 | 1 | boss | C |
| E1/L16 | 833 | 0/1 | 2 | backward | C B |
| E1/L17 | 1003 | 1/1 | 3 | backward | C B |
| E1/L18 | 834 | 1/0 | 0 | backward | C B |
| E2/L1 | 1752 | 1/1 | 1 | backward | C B |
| E2/L2 | 829 | 1/0 | 2 | boss | C P |
| E2/L3 | 178 | 2/1 | 0 | sequential | — |
| E2/L4 | 421 | 2/1 | 0 | conditional | C P |
| E2/L5 | 1304 | 2/0 | 0 | sequential | — |
| E2/L6 | 1299 | 1/2 | 1 | backward | B |
| E2/L7 | 610 | 1/0 | 2 | backward | B |
| E2/L8 | 946 | 2/0 | 0 | conditional | C |
| E2/L9 | 1256 | 2/2 | 0 | backward | C B |
| E2/L10 | 806 | 1/1 | 1 | boss | — |
| E2/L11 | 552 | 1/1 | 1 | boss | — |
| E2/L12 | 252 | 1/0 | 0 | sequential | — |
| E3/L1 | 1236 | 1/0 | 1 | boss | C P |
| E3/L2 | 390 | 1/1 | 0 | sequential | — |
| E3/L3 | 1025 | 2/0 | 0 | sequential | — |
| E3/L4 | 902 | 1/1 | 1 | boss | — |
| E3/L5 | 786 | 2/0 | 0 | sequential | — |
| E3/L6 | 2259 | 1/1 | 1 | backward | B |
| E3/L7 | 672 | 1/1 | 0 | sequential | — |
| E3/L8 | 1197 | 1/1 | 1 | boss | — |
| E3/L9 | 818 | 2/0 | 0 | conditional | C |
| E3/L10 | 725 | 2/1 | 2 | boss | P |
| E3/L11 | 656 | 1/2 | 1 | boss | P |
| E3/L12 | 710 | 2/1 | 0 | sequential | — |
| E4/L1 | 1109 | 2/0 | 1 | boss | C P |
| E4/L2 | 970 | 2/0 | 0 | sequential | — |
| E4/L3 | 1188 | 2/1 | 1 | boss | C |
| E4/L4 | 904 | 2/0 | 2 | boss | C P |
| E4/L5 | 676 | 3/2 | 2 | boss | T P |
| E4/L6 | 641 | 2/1 | 0 | conditional | C |
| E4/L7 | 1329 | 1/1 | 3 | boss | C T P |
| E4/L8 | 749 | 1/1 | 1 | backward | C B |
| E4/L9 | 1447 | 1/1 | 2 | boss | C P |
| E4/L10 | 552 | 2/0 | 0 | sequential | — |
| E4/L11 | 909 | 2/1 | 2 | boss | C T |
| E4/L12 | 1243 | 2/0 | 1 | boss | C |
| E4/L13 | 1006 | 1/1 | 2 | backward | C B |
| E4/L14 | 1022 | 2/1 | 5 | backward | C B |
| E4/L15 | 1969 | 1/0 | 3 | backward | C B |
| E4/L16 | 666 | 1/1 | 5 | backward | C B |
| E4/L17 | 473 | 1/0 | 0 | backward | C B |
| E4/L18 | 595 | 0/0 | 0 | return | C R B |
| E4/L19 | 1288 | 1/0 | 1 | backward | C B |
| E4/L20 | 850 | 2/0 | 2 | backward | C T B |
| E5/L1 | 197 | 0/0 | 0 | sequential | — |
| E5/L2 | 311 | 1/0 | 2 | boss | — |
| E5/L3 | 1000 | 2/0 | 1 | boss | C T |
| E5/L4 | 983 | 2/1 | 3 | boss | T P |
| E5/L5 | 514 | 1/0 | 3 | backward | B |
| E5/L6 | 1442 | 1/1 | 1 | boss | — |
| E5/L7 | 894 | 1/1 | 1 | backward | C B |
| E5/L8 | 845 | 1/1 | 1 | backward | B |

## End, final boss and intermediate boss

Event **11** starts an exit or forces `reallyEndLevel`, depending on the parameter and on absent players. Event **36** arms `readyToEndLevel`: the exit waits for `enemyOnScreen == 0` and no pending explosions. There is also an exit associated with the background stop and `waitToEndLevel`. For this reason neither the last event, nor the greatest serialized time, nor the last 11/36 marker alone represents a guaranteed duration or success.

In both variants, E1/L4 and L16 have only 36; E4/L18 has neither 11 nor 36. In 2000, E5/L1 also has neither of these markers. These last two records are priority exceptions: there may be a return, an external route or finalization by state, and a denominator must not be fabricated from the maximum time. The other physical levels have at least one of the two markers; this is presence coverage.

Event **79 does not distinguish a final boss from an intermediate one**: it only associates up to two links with the bar. `draw_boss_bar` queries active enemies, clears links without valid armor and may consolidate the bars; event 70 depends on the existence of enemies per link, not on a “boss” classification. A boss may have no 79 bar. Background stop, remaining ordinary enemies and explosions may also prolong the level. No percentage should be derived from armor.

There are active 79 commands in 36/62 physical levels of 2.1 and 43/70 of 2000. In serialized order, an end marker exists after the last active 79 in 35 and 42 of them, respectively; the exception in both is E1/L16. These are **structural candidates for a final stretch**, not 35/42 confirmed final bosses: a jump may divert, repeat or anticipate the exit, and a later spawn may belong to the same boss. We do not classify a spawn after 79 as proof of an intermediate boss. Confirming the distinction requires observing “bar active → bar ended → path resumed” versus “bar active → completion”, with the real route. The number of 79s is likewise not the number of bosses.

## Jumps, loops and conditions

`curLoc` follows the background speed and may advance with `forceEvents` even when the background is stopped; it is not wall-clock time nor irreversible distance. Event 38 reassigns `curLoc`/index; 54 calls `JE_eventJump`; 57 arms an enemy-death jump; 70 jumps if enemy groups have disappeared; 71 depends on the map position. `JE_eventJump` stores `returnLoc = curLoc + 1` and treats destination 65535 as a return. Event 76 arms a return when the enemies leave. E4/L18 presents return and backward jump in both variants. An indicator that directly uses `curLoc / last_time` may regress, improperly reach a remote destination or advance inside a loop.

Backward-jump candidates: E1/L16–18; E2/L1, L6, L7, L9; E3/L6; E4/L8, L13–20, in both variants. Add E5/L5, L7, L8 in 2000. The static comparison uses the command's own time, not the dynamic instant of execution. It does not prove that the loop was taken nor does it alone identify all cycles: chained jumps, return destination and event mutations depend on state.

Events 61 and 60 relate flags and enemy death; 63 depends on 2P or 1P arcade; 80 depends on 2P; 66 uses `initialDifficulty <= threshold`, although the English comment suggests another comparison. 75 chooses a random link or skips events when it finds no candidate, including adjusting `curLoc`. Do not run RNG to analyze progress. Timer 67 jumps on expiry; 84 does so only in Timed Battle. 83 in 2000 reuses map stop, 85 acts only in Timed Battle; do not apply 2.1 semantics to reassigned 2000 events (`gameEventCase`, `src/game_rules.c`).

In the logical script, `]G` offers choices, `]J` jumps, `]2` depends on 2P/arcade, `]w` depends on the ship, `]t` on the timer, `]l` on death; `]H` and `]h` depend on difficulty. `]T` selects the battle section and `]q` ends the battle only in that mode; `]Q` ends the episode. These commands are not time endpoints of the physical level.

| Variant/E | Route commands present (syntactic counts) |
|---|---|
| 2.1/E1 | `]J`=8, `]2`=1, `]G`=17, `]H`=1, `]h`=1, `]Q`=1 |
| 2.1/E2 | `]G`=11, `]Q`=1 |
| 2.1/E3 | `]G`=12, `]H`=1, `]Q`=1 |
| 2.1/E4 | `]J`=8, `]2`=2, `]w`=1, `]t`=2, `]l`=1, `]G`=22, `]Q`=1 |
| 2000/E1 | `]J`=8, `]2`=1, `]G`=17, `]H`=1, `]h`=1, `]T`=1, `]q`=1, `]Q`=1 |
| 2000/E2 | `]G`=11, `]Q`=1 |
| 2000/E3 | `]G`=12, `]H`=1, `]Q`=1 |
| 2000/E4 | `]J`=8, `]2`=3, `]w`=1, `]t`=2, `]l`=1, `]G`=22, `]Q`=1 |
| 2000/E5 | `]J`=1, `]G`=5, `]T`=1, `]q`=1, `]Q`=1 |

## Relation to the current implementation

The [runtime contract](progress-runtime.md) describes the conservative observer actually implemented. It chooses in memory the most distant positive 11/36 milestone, without proving reachability, and follows local advancement with a budget up to 99%. It does not use these categories as certified profiles nor implement a per-level graph. Forward jumps do not credit skipped addresses; backward jumps freeze below the previous frontier. Return and 38/75 replacements invalidate support; absence of a denominator starts unavailability, omitted from the HUD. Thus E4/L18 and 2000/E5/L1 are fallback cases, not resolved complete paths.

Boss pauses only when there is an active enemy with positive armor linked to the existing bars. The implementation does not universally distinguish an intermediate/final boss, does not measure damage and does not fill PROGRESS by the disappearance of the bar. The main objective requires execution of the chosen milestone, sufficient position and a natural exit resolved with a survivor, with no negative block. **An early or alternative exit preserves the current percentage below 100% until reset, with no promotion by a later milestone during warp.** `reallyEndLevel` or campaign advancement in isolation remain insufficient.

Graphs with per-stretch weights, call/return profiles, encounter identity and certification per variant/mode are future work. The 62/70 inventoried levels demonstrate static coverage of records and references, not useful advancement in all maps, reaching all branches or victory over bosses.

## Execution coverage: separate scope

The `tools/check_progress.sh` guard contains scalar fixtures and initial OFF/ON probes in E1/L6, E1/L4, E1/L16 and E4/L18, plus E5/L1 in 2000. The 300-frame probes require events/advancement where applicable; they do not certify backward jumps/returns merely by selecting a structural candidate. The 1:3 script exercises input/movement, autofire and pause. The E1/L1 boss is an accelerated fixture, not a natural path nor a final defeat. Negative fixtures use the model's common latch, without proving all gameplay hooks.

The coordinator confirmed final release probes in both variants: E1/L6 ends at 52% with an early exit, E1/L4 resolves the main objective at 100% and E4/L7 in 2P/fire, VFX OFF, shows three boss resumptions with later advancement (5/3) and ends at 45%/43%. Details and limits are in [progress-runtime.md](progress-runtime.md). The coordinator confirmed the final release suites: **172 cases 2.1 and 153 cases 2000, with the existing guards and the PROGRESS ones passing**. This validates the executed matrix, without turning the inventory into per-path certification. No PASS is inferred from this inventory or from reading the source. The matrix below deals with expanding coverage, without requiring universal certification to deliver the conservative fallback.

## Coverage expansion still needed

1. Extend the E1/L6 (early exit) and E1/L4 (main objective) probes to simultaneous causes, pending enemies/explosions and persistence of the presentation across different overlays/fades.
2. E1/L16 and L17, E2/L1 and E3/L6: record metadata of jumps taken, pauses and resumptions; verify that the bar does not grow during repetition nor regress when returning. E1/L16 is a priority because of an active 79 with no later marker in serialized order.
3. E4/L18 in both variants, and E5/L1 in 2000: resolve the real finalization/return and select a denominator before considering them covered. Check the out-of-range load in logical section 44 of 2000/E1 through the interpreter and the battle routes.
4. Beyond the resumption with advancement already observed in E4/L7, distinguish final-boss identity/stages: verify absence of a bar, two links, deactivation, transformations and pending explosions. The presence of 79 does not satisfy this expansion.
5. E4/L5, L7, L11 and L20: compare expired/non-expired timer; E4/L20 includes a backward jump. In 2000, add E1/L5 and E5/L3–4 in the common mode and in Timed Battle, plus E5/L5, L7–8 for loops.
6. Exercise 1P Full Game, arcade and 2P on the `]2` routes, events 63/80; normal/hard difficulty and both sides of each 66 threshold present; `]G` choice, `]w` ship, `]l` death and `]t` timer. The representative minimum does not replace per-path coverage.
7. Death, giving up, restart/backup, bonuses and battle completion: never promote failure to 100%. Menu pause and level transition must preserve/reset only the appropriate visual state.

Use `--regress-level` for physical probing and `--regress-script`/`flow` for the real section/menu path; coverage logs must demonstrate the effective execution of the markers and transitions. This documentation task did not run these trials; results received from the coordinator are identified separately. All 2000 evidence must remain in hashes/metadata and aggregated coverage; do not generate screenshots nor event dumps. The inventory does not authorize baseline changes; the Classic invariants remain mandatory.

## Read-only reproduction

The procedure does not require a build: Python 3, `struct`, `collections`, `hashlib` and `pathlib`. Open files in binary read mode; never export the buffers. For 2000, use only an existing `TYRIAN2000_DATA` or an installation already present for the user. A missing installation means coverage unavailable, never authorization to download.

Exact algorithm of the inventory: read the header u16 and the u32 offsets; check for an odd count and ordered offsets; for each even-index offset before the final one, skip 8 bytes, read the u16 enemy count, skip their u16 IDs, read the u16 event count and decode that number of records in memory with `struct.unpack_from('<HBhhbbbB', buffer, posição)`, step 11. Confirm that the end of the events does not go past the next offset. Count 11, 36 and 79; classify by the predicates above. Timer counts by the presence of the type; a timer backward jump requires an activation parameter equal to 1 and a destination less than or equal to the command's time. The set of jumps compared for backward is 38/54/57/70/71; the set of special destinations used for return is 54/70/71, plus the presence of 76. Jump s16 destinations are reinterpreted as u16 (`value & 65535`). Active boss means the first or second parameter of 79 non-zero. Final candidate means the index of the last active 79 before the index of the last 11/36, without reachability analysis.

For scripts: read a u8 size followed by that number of bytes; decrypt from back to front with XOR of the loader key `{204,129,63,255,71,19,25,62,1,99}`, repeated every ten bytes, and of the previous encrypted byte (except index zero). Count `*` records and `]` commands; only for `]L`, convert the field from index 25 as `atoi` (first integer with optional spaces/sign). Group references by physical ID and record counts, never text. This syntactic scan does not perform the contextual line consumption by `]I`, `]W`, `]Q` or `]h`; therefore the command counts must be checked against the real interpretation, especially the E1 exception. To reproduce the categories, use event sets exactly as declared in this section and in the legend.

SHA-256 of the analyzed containers and scripts (input identification, no content):

| Variant/E | `tyrianN.lvl` | `levelsN.dat` |
|---|---|---|
| 2.1/E1 | `b41e532214fa8cea36e37fed6512b357c6ac9c8c3267899ceb4a258fcb1bf0b9` | `e82164c96abb994450d416a3ab0e5fe6479fd4db831cd0f203a60dfe4aa78dcb` |
| 2.1/E2 | `3e18b124cd4ff5f766e5bf2e833eb79d3118010a253a30dc4c0ae6f483ae50e9` | `9aa94b70aae2b24b16c295ed86e392ac16873306012bdd89e0cf995d605178ea` |
| 2.1/E3 | `0995c7e2279995485576e02b21ac8599b4924c35af5afffdede69c01994b9937` | `0d935cd7bb3c84451d9f57dbcd31a8d515205b18fb050070a416e9b7411dfbd3` |
| 2.1/E4 | `181fde04387ba47e6b49a7f27010ac92fe00566a5f9fef075d74e4c4b2ebef75` | `891b9ecbdd223df291077fd95626f37ac03b527ca6fd50f76198067a5edebdb4` |
| 2000/E1 | `54cbb00b57b84aa992a70cfcb170cb1edeb59507873723630d60080ba8351352` | `2c2f5b8509ac44442592580c329f2b3c48a4bf972df7d049a9f174cc2c81c427` |
| 2000/E2 | `930e7c4a5452c6c79406d8f7126fdaace9994635d9f4bcdfb645d693a22575b6` | `3ec8a84e6f83500792b8e1415d91176bd5431046d4d9f0910a58ee0715b84533` |
| 2000/E3 | `6a5b94a7d3e4f5a228bc0f702cfe035c8f9f03b20a3f02867d25e95b4249ce86` | `0d935cd7bb3c84451d9f57dbcd31a8d515205b18fb050070a416e9b7411dfbd3` |
| 2000/E4 | `6589b898094887925e7717c8663c4b8b5e4b76f3c89a7898ed9f4691c1f2cc16` | `5c12be86d167f7ad3ffb6821ab19da6edf8a2a252ef03b7044f959ee16871482` |
| 2000/E5 | `275b5aaf55caecee8dc95e26f7cf118a9ee265f095ce61945b9ece56a4523323` | `788710b2321fc58b8fbe43da960be8bb8370ef5595f63c62e5ed3a17e008797f` |

Confidence: high for counts/format, moderate for structural risk signals, not established for reachability, actual number of bosses, duration and success. Local sources of semantics: `src/tyrian2.c` (`JE_loadMap`, `JE_eventSystem`, `JE_eventJump`, `draw_boss_bar`, `start_level`), `src/helptext.c` (decryption), `src/game_rules.c`, `src/regress.c`, `tools/scan_smoothies.py`; no changes to those files.
