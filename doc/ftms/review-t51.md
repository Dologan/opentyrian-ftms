# T5.1 review: bike/controls feature (v0.7.0..HEAD, src/tools/Makefile/visualc)

Scope: static review of the diff, with callers traced. The new .c files and the touched hooks (keyboard, mainint, tyrian2, params, modern_hud) compile cleanly with `gcc -std=iso9899:1999 -pedantic -Wall -Wextra -fsyntax-only -DWITH_NETWORK` against the SDL3 3.2 headers in the scratchpad. No binary was built and no regress was run. The SDL3_net behaviour below was checked against the vendored SDL3_net-3.2.0 source in `scratchpad/mt-spike`.

Overall: with the master switch off, the core gate, the hash fold, config save, RNG use and the regress pin look right. I found no path that changes game state, RNG order or the regress state hash with the bike flags off. The fixed-point engine and the rules have no overflow at realistic inputs, and the WebSocket and JSON parsers are bounds-safe. The confirmed problems are in the build files, settings persistence and gating of the menu translation.

## CONFIRMED

### 1. HIGH: committed merge-conflict markers in `visualc/opentyrian.vcxproj:241-246`
```
    <<<<<<< HEAD
    <ClInclude Include="..\src\bike_controls.h" />
    =======
    <ClInclude Include="..\src\bike_menu.h" />
    <ClInclude Include="..\src\bike_touch.h" />
    >>>>>>> claude/focused-ritchie-ahh2sn
```
- **Origin:** introduced by merge 652ee26 and still present at HEAD.
- **Failure:** MSBuild/Visual Studio refuses to load the project (invalid XML), so the MSVC build is broken. CI does not catch this because Windows CI uses MSYS2 and the Makefile.
- **Fix:** keep all three lines (`bike_controls.h`, `bike_menu.h`, `bike_touch.h`), keep CRLF, and add a guard such as `grep -n '^<<<<<<<\|^>>>>>>>' visualc/*` to `check_bike.sh` or `regress.sh`. The `.filters` file is fine, and every new .c/.h is listed once in both files.

### 2. MEDIUM: test-only and CLI bike options leak into the saved config (requirement 7)
- **Where:** `bike.c:410-421` (`bike_test_level_set` writes `bike_settings.master = true; generator = true`), `params.c:974-1001` (`--ftms-bridge`, `--ftms-ftp` call `bike_settings_set`), `bike.c:336-360` (`bike_config_save`).
- **Mechanism:** `bike_settings_pinned` is only set by `bike_regress_pin`, so outside regress the next `saveConfiguration()` writes these values to `[ftms]`. That happens in the launcher (`opentyr.c:1318`) and in `JE_tyrianHalt` (`varz.c:428`).
- **Failure:**
  - A developer who runs `--ftms-test-level=1:3` and quits through the menu has `enabled = on` and `generator = on` saved permanently.
  - `--ftms-bridge` and `--ftms-ftp` likewise become persistent, so a one-off CLI override silently changes later runs.
  - `check_bike_bridge.sh` is safe only because it isolates `HOME` and kills the game.
- **Fix:** apply CLI and test overrides to a run-scoped overlay, as `forced_off` does, or snapshot the settings after `loadConfiguration()` and have `bike_config_save` write the snapshot whenever an override flag was given. At minimum, set `bike_settings_pinned = true` in `bike_test_level_set`.

### 3. MEDIUM: `--ftms-off` does not disable the remote and touch menu translation
- **Where:** `bike_controls.c:69-72` (`bike_controls_remote()` = `master && air_mouse`) and `bike_touch.c:78-81` (`bike_touch_on()`). Neither checks `forced_off`, which `bike_gate_open()` and `bike_hud_visible()` do check.
- **Failure:** with `air_mouse = on` saved and `--ftms-off` given, the menus still translate:
  - left click becomes Enter and right click becomes Esc (`keyboard.c:399-419`), and the launcher does the same (`launcher.c:1780`);
  - Esc on the title asks to quit instead of quitting;
  - the quit dialog defaults to No;
  - name entry is prefilled with `PILOT`;
  - touch FINGER events are consumed.

  The help text says `--ftms-off` forces the bike features off for this run.
- **Fix:** add an exported `bike_master_on()` (`master && !forced_off`) and use it in `bike_controls_remote`, `bike_touch_on` and the `JE_inGameSetup` bike row.

### 4. MEDIUM: remote menu translation cannot be toggled on its own, and two settings do nothing
- **Where:** the `[ftms]` keys `remote_buttons` and `pointer_smoothing` are parsed, saved and loaded (`bike.c:138-141`, `273-278`), but nothing reads them. `grep` finds no use outside `bike.c`, and neither appears in `bike_menu.c`.
- **Failure:** the click translation, the quit-confirm default, `PILOT` and the right click opening the in-game menu are all tied to `air_mouse`. A player with a real mouse who enables Air Mouse (for example, for pointer sensitivity) loses normal click behaviour in every menu and has no switch to get it back. This conflicts with "every feature individually toggleable".
- **Fix:** gate the menu translation on `remote_buttons`, defaulting to on when `air_mouse` is on, or drop the dead keys. Either way, implement or remove `pointer_smoothing`.

## PLAUSIBLE (high-probability, not run)

### 5. MEDIUM: `bike_ws_close()` calls `NET_Quit()` on every reconnect, which can block the game loop
- **Where:** `bike_ws.c:858-872`, called from `bike_service()` (`bike.c:479-483`) whenever the host or port changes or the source turns off. `bike_service` runs inside `handleSdlEvents()`.
- **Mechanism:** in SDL3_net 3.2.0, the last `NET_Quit` (refcount reaches 0) sets `resolver_shutdown` and runs `SDL_WaitThread` on each resolver thread. A thread that is still inside a blocking `getaddrinfo` holds the caller until the lookup returns. `RESOLVE_TIMEOUT_US` only abandons the request; the thread keeps running.
- **Trigger:** enter an unresolvable or slow name in the Bridge row, for example a `.local` name or a dead DNS server, so the status sits at Connecting. Then edit the address again or switch the master off. The menu or game freezes until the OS resolver gives up, which can take 5-30 s. This contradicts design §7.10 ("never stalls the frame").
- **Fix:** call `NET_Init` once (lazily) and `NET_Quit` only in `bike_shutdown`/exit. A host change should only `release_net()`.

### 6. LOW: the bridge sample age wraps after about 71.6 minutes
- **Where:** `bike.c:607` computes `age_us = (uint32_t)(now - arrival_us)`.
- **Failure:** if the WebSocket stays open (the bridge keeps pinging) but sends no telemetry for 2^32 µs, the age wraps to near 0. For about 3 s the old sample (for example 250 W) then counts as fresh: `link_up` and `fresh` are both true, so moving restarts, effort rises and the generator gets bike income.
- **Fix:** saturate, e.g. `age_us = d > UINT32_MAX ? UINT32_MAX : (uint32_t)d`.

### 7. LOW: the superbomb cooldown resets at every level start
- **Where:** `bike_rules.c:26`.
- **Failure:** the design says a grant comes at most once per `superbomb_cooldown` of gameplay time. Because the surge meter carries across levels and the cooldown does not, short levels or a restart after death allow grants more often than once per 60 s.
- **Fix:** do not reset `superbomb_cd_us` in `bike_rules_level_start`, or document the reset.

### 8. LOW: stock mouse behaviour changed with everything off
- **Where:** `keyboard.c:367-391`. `mouseHasMotionInput = true` used to be set for every `SDL_EVENT_MOUSE_MOTION`; it is now set only when `xrel || yrel` is non-zero, even when remote mode is off.
- **Effect:** a zero-delta motion event (warp or enter with no movement) no longer re-evaluates the menu hover. Regress drops events, so baselines are unaffected, but this is a stock-input deviation with the master switch off.
- **Fix:** keep the stock assignment unconditional when `!bike_controls_remote()`:
  `if (!bike_controls_remote() || touch) mouseHasMotionInput = true;` with the rel test applied only in remote mode.

### 9. LOW: `--regress-bike-feed` and `--regress-bike-events` act outside regress mode
- **Where:** `params.c:958-970`, `bike_controls.c:341-342`.
- **Effect:**
  - A feed makes the source `SCRIPT` in normal play, so the generator runs from a canned feed when master is on.
  - The events file can set `youAreCheating` or equip items on player 1, which can then be written to the save.
- **Mitigation:** both need an explicit flag. This matches existing `--regress-*` practice (e.g. `--regress-reverse-y`), so it is not accidental.
- **Fix:** ignore both (or exit with an error) unless `regress_active()`.

### 10. LOW: input edge cases
- **Auto-fire toggle collides with custom bindings.** The default toggle key is `A`, and the gamepad toggle (right-stick click) is hard-coded (`bike_controls.c:38`, `187`). Neither is checked against `keySettings[]` or the joystick assignments, so a player who binds A to movement (WASD) toggles auto-fire on every left press. Suggest skipping a toggle key that equals any `keySettings` entry, or warning in the menu.
- **A held zone finger keeps swallowing menu taps.** `bike_touch.c:309`: `swallow_mouse` stays true while any finger from play is still down, so taps in the in-game menu opened by a two-finger or MENU zone tap are swallowed until all fingers lift.
- **The launcher ignores the touch exclusion.** In remote mode the launcher translates every left click (`launcher.c:1780`) without the `bike_touch_is_touch_mouse` exclusion that `keyboard.c` uses, so with both air mouse and touch on, a finger tap anywhere acts as Enter on the focused row.

## Checked and OK (requirements 1-3)

- **Gate (`bike.c:518-522`):** includes `master`, `!forced_off`, `level_active`, `!twoPlayerMode`, `!onePlayerAction`, `!isNetworkGame`, `!playDemo`, `!recordDemo`, `!galagaMode` and `!constantPlay`.
- **Hooks behind the gate:** auto-fire, injected fire, air play, touch play, the surge hooks, the superbomb grant and the generator are all gated. The `inputDevice = 0` override is only a parameter.
- **Network and demo streams:** no bike byte reaches them.
- **Regress pin:** under regress the source is never `BRIDGE` (`bike_service` checks `!regress_active()`), the master is pinned off, and the hash fold happens only when `bike_hash_active()`. No baselines changed (the `test/` diff is additions only). There is no `mt_rand` or float on the game-state path; `to_q16(float)` in touch is real-input only and exact.
- **Config file:** an all-default `bike_settings` with no existing section writes no `[ftms]` (memcmp over memset structs).
- **WebSocket/JSON parsing:**
  - Frame size is bounded by `RX_CAP - hdr`; reassembly is bounded by `MSG_CAP` with a NUL slot; control frames are limited to 125 bytes.
  - The JSON scanner stops on NUL.
  - `parse_q16` clamps, and the exponent loop is bounded.
  - Header values are length-checked.
- **Host:port entry:** bounded (`ADDR_MAX` 70, host < 64, port < 8 characters).
- **Event and CSV parsers:** bounded (`sprintf` is guarded by a length check).
- **Fixed-point overflow:** checked in the EMA, ratio, income and meter maths; all stay inside int64.
- **Portability:** no `%zu`, no VLAs, and designated initializers are fine under `/std:c17`.

## Simplification / hygiene

- `bike.h:113`: the comment still says "STUB until T3b.1 lands: always false"; it is implemented now.
- `bike_service` keeps its own `ws_open`/`ws_failed` statics on top of the `bike_ws` state machine. Folding the host-change detection into `bike_ws_open` would remove the close/open cycle behind finding 5.

---

## Resolution (T5.1-fix)

Fixed on branch `t51-fix`; commit ids are in `git log` (subjects name the item).

| # | Finding | Resolution |
|---|---|---|
| 1 | Conflict markers in `visualc/opentyrian.vcxproj` | Fixed: markers removed, all three headers kept, CRLF kept; every `bike*.{c,h}`, `input_probe.*`, `swar.h` appears once in `.vcxproj` and `.filters`. New guard `tools/check_no_conflict_markers.sh` (self-test, wired into `regress.sh` full and quick). |
| 2 | CLI/test overrides persisted | Fixed: `bike_override_set()` remembers the saved value per key; `bike_config_save()` writes it back. `--ftms-test-level` goes through it too. Check `bike-config-override` in `check_bike.sh` (also in quick) runs a real config save. |
| 3 | `--ftms-off` left remote/touch on | Fixed: `bike_master_on()` used by `bike_controls_remote`, `bike_controls_air_play`, `bike_touch_on`, `bike_hud_visible` and the in-game menu row. |
| 4 | Remote buttons tied to Air Mouse; dead keys | Fixed: `remote_buttons` drives menu translation (new "Remote Buttons" row); `air_mouse` is pointer steering only. `pointer_smoothing` removed (old key still parsed and ignored). Old configs with Air Mouse on and no `remote_buttons` load with it on. |
| 5 | `NET_Quit` per reconnect can block | Fixed: SDL3_net stays initialised (`bike_ws_shutdown` at exit only); `bike_ws_close` abandons a pending lookup with `NET_UnrefAddress` (the resolver thread holds its own reference). `check_bike_ws.sh` re-run: see the final report. |
| 6 | Sample age wraps at ~71.6 min | Fixed: `age_sat()` saturates at `UINT32_MAX` on both the script and bridge paths. |
| 7 | Superbomb cooldown reset per level | Fixed by choice: kept across levels and retries, reset by `bike_rules_new_game()` from `JE_initPlayerData`. Digest `sgride` changed (that ride restarts the level); no other digest moved. |
| 8 | Zero-delta mouse motion changed stock | Fixed: with remote mode off every motion event counts again, exactly as stock. |
| 9 | `--regress-bike-feed/events` outside regress | Skipped (explicit flags, same practice as other `--regress-*` options). |
| 10 | Auto-fire key A vs bindings | Fixed for keys: a toggle key equal to any `keySettings` entry is skipped. Skipped: held zone finger swallowing menu taps, launcher touch exclusion, gamepad toggle conflicts. |
| Hygiene | Stale "STUB" comment in `bike.h` | Fixed. `ws_open`/`ws_failed` statics in `bike_service` left as is. |
