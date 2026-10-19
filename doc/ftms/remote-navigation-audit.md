# Remote-only navigation audit (T3b.4)

Read-only static audit of the input paths, at commit `6933222`. No code was changed and nothing was run.
Line numbers are `src/` anchors at that commit.

**Remote key set used:** arrow keys, Enter, Esc, left click (at the pointer, remote OK), right click
(remote Back), and media keys. Space is treated as Enter by every menu that accepts it.

**Method:** for each screen, the menu loop's mouse branch (`mouseGetInput`), keyboard branch
(`keyboardGetInput`), and exit conditions were traced. Mouse hit rectangles and the button checks were
read in each branch. The media-key question was answered by searching the whole tree for
`SDL_SCANCODE_AC_*`, `AUDIO*`, `MEDIA*` and `SDLK_MEDIA*`. There are no matches outside the regression
harness, so no media key reaches any screen.

## Summary

- **Keyboard path (arrows, Enter, Esc): complete on every screen,** with two caveats. Both name-entry
  prompts accept a blank name (see below), and the Super Arcade and Super Tyrian unlock codes are typed
  letters, so they are not reachable with a remote. Those codes are optional and do not block progress.
- **Media keys: dead.** No handler on any screen, so the remote cannot pause, open the in-game menu, or
  move between screens with its media keys. This is the largest gap for the remote.
- **Right-click Back is inconsistent.** It works on the setup, episode, difficulty and gameplay menus,
  the in-game menu, the help screens, the high score board and the save/load screen. It does nothing on
  the shop and the launcher. On the title screen it quits the game. On the quit prompt, a right-click in
  the Yes box quits.
- **Pointer-only controls:** every menu hit-tests the pointer for hover and click, and several screens
  have pointer-only targets (volume sliders, the shop's weapon power buttons, the data cube back box).
  Each of these has a keyboard alternative except the save-name and name-entry OK/Cancel boxes, which
  have Enter and Esc.
- **Text entry never blocks progress.** Enter and Esc always work in the name prompts. The problem is
  that the result is a blank name, since there is no way to type letters with a remote.

## Table

| Screen | File:line | Works remote-only? | Problem | Proposed fix | Status |
| --- | --- | --- | --- | --- | --- |
| Launcher | `src/launcher.c:1612` (keys), `:1668` (Esc), `:1779` (mouse), `:1800-1830` (pointer) | Yes, with arrows, Enter and Esc | Right-click is ignored (`:1779` only accepts the left button), so there is no Back. Esc on the main panel quits the app with no confirmation (`:1668`). Motion hover moves focus (`:1800`), which an air mouse can trigger by drifting. | Treat right-click as Esc. Confirm Esc-quit, or make it a Back that returns to the panel. Keep the still-pointer filter (`xrel==0` at `:1779`). | Fixed in remote mode (T3b.2): OK acts on the focused row, Back steps out of a dialog and never quits, drift is ignored. Esc-quit confirm: open. |
| Title screen | `src/tyrian2.c:3433`; mouse `:3571-3606`; keys `:3630-3642`; exit `:3767-3772` | Yes, but Back quits | Esc (`:3640`) and right-click (`:3605`) both set `done`, and `done` returns `false`, which quits the game with no confirmation. | Route Back to the quit prompt (`JE_quitRequest`) or make it a no-op on the title. | Fixed in remote mode: Back opens the quit prompt (default No) instead of quitting. |
| Setup menu | `src/opentyr.c:259` (`setupMenu`); mouse `:608-688`; right `:683`; keys `:690-776`; Esc `:764` | Yes | Volume sliders and the value column take pointer x (`:656`, `:664`). Left/Right already adjust them (`:712-746`), so this is covered. | None needed. | No fix needed. |
| Setup picker | `src/opentyr.c:941-1030`; click `:969-973`; right `:979`; Esc `:1018` | Yes, with arrows and Enter | Hit-test bug at `:970`: the test compares `mouseInput.y` against an `x`-based bound (`mouseInput.y < xMenuItemValue + wMenuItemName`). A click past the value column can activate the picker. | Use `mouseInput.x < xMenuItemValue + wMenuItemValue`. | Fixed (unconditional): the click test now compares `x` with the value column. No 2.1 baseline changed (the regress harness delivers no mouse events). |
| Gameplay select | `src/menus.c:43`; right `:147`; keys about `:160-205` | Yes | None. Network row is greyed and not selectable, which is intended. | None. | No fix needed. |
| Episode select | `src/menus.c:230`; right `:330`; keys about `:355-370` | Yes | None. | None. | No fix needed. |
| Timed Battle select | `src/menus.c:407`; right `:506` | Yes | None. Left click on an item activates it, which is the intended behaviour. | None. | No fix needed. |
| Difficulty select | `src/menus.c:574`; right `:676` | Yes | None. | None. | No fix needed. |
| Shop and upgrades | `src/game_menu.c:337` (`JE_itemScreen`); list click `:1316-1357`; weapon power clicks `:1388-1410`; keys `:1423-1700`; Esc `:1446-1478` | Yes, with arrows, Enter, Esc, Left/Right | No right-click Back exists in this screen (no `SDL_BUTTON_RIGHT` in `game_menu.c`). The list activates a row on any button (`:1316-1357` has no button check), so a right-click on a row buys or activates it. The power ± targets are pointer-only, but Left/Right cover them (`:1680`). | Map right-click to Esc in this loop. Require `SDL_BUTTON_LEFT` before a list click acts (`:1316`). | Fixed in remote mode: right click is Esc, OK is Enter on the focused row, other buttons are dropped. |
| Data cube | Browse and select in `src/game_menu.c:1009-1123` and `:3109-3120`; sub-screen back box `:1267-1273` (pointer rect `258-290 x 159-171`); Back via `menu_esc` in `src/game_schema.c:107-111` | Yes, with Enter and Esc | Back box is pointer-only and takes any button. Right-click does nothing. | Same right-click-to-Esc mapping as the shop. Esc already works. | Fixed in remote mode (same translation). |
| Quit prompt | `src/game_menu.c:2675` (`JE_quitRequest`); defaults `quit_selected = true` at `:2677`; mouse `:2729-2745` (no button check); keys `:2745-2766` | Yes, but risky | A right-click in the Yes box quits (`:2735`), because the button is not checked. Enter confirms the default, which is Yes, so one accidental Enter quits. | Require the left button. Default the focus to No for remote play, with Esc as cancel (already Esc = No). | Fixed in remote mode: no pointer hit-test, the default is No. |
| Save/load | `src/mainint.c:643` (`JE_loadScreen`); mouse `:760-822`; right `:819`; keys `:826-870`; Esc `:868` | Yes | Slot clicks are pointer-only, but Up, Down, Left and Right cover them. Right-click Back works (`:819`). | None. | No fix needed. |
| Save-name entry | `src/mainint.c:2886` (`JE_operation`); text input starts `:2915`; OK/Cancel rects `:2969-2979` (no button check); keys `:2983-3001` | Yes, but the name is blank | No letters can be typed with a remote, so the save keeps the pre-filled name or nothing. Enter saves, so it does not block progress. | Arrow-driven letter picker, or a default name such as `SLOT n` when the field is untouched. | Fixed in remote mode: an empty slot is pre-filled with `PILOT`, so Enter saves a named slot. |
| High-score board | `src/mainint.c:1100`, `JE_highScoreScreenAt` `:1106`; right `:1264-1269`; keys `:1275-1300` | Yes | Arrow controls are pointer-only (`:1245-1256`), but the keys already page. | None. | No fix needed. |
| High-score name entry | `src/mainint.c:2010` (`JE_highScoreCheck`); name prefilled with 29 spaces `:2082`; text input `:2094`; OK/Cancel rects `:2165-2175` (no button check); keys Esc `:2189`, Enter `:2193`, Backspace `:2181` | Yes, but the name is blank | Enter accepts the blank default, so the board shows an empty name. No letters can be typed with a remote. Timed Battle scores cannot be cancelled (`:2201`), which is intended. | Arrow-driven letter picker (Up/Down letter, Left/Right position, Enter accept, Esc skip). Default to `PLAYER` when untouched. Require the left button on OK/Cancel. | Fixed in remote mode: pre-filled with `PILOT` (Backspace erases it), OK/Cancel boxes no longer react to any button. Letter picker: open. |
| In-game menu | `src/mainint.c:1515` (`JE_inGameSetup`), opened by Esc at `:3304`; mouse `:1645-1673`; right `:1718`; keys `:1725-1770`; Esc `:1763`; Quit `:1809-1823` | Yes, but Quit is unguarded | Quit ends the level at once with no confirmation (`:1823`). Enter on the item does that. Right-click Back works (`:1718`). | Route Quit through a confirm (the existing `JE_quitRequest` pattern). Map a remote key to the menu (see media keys). | Right click (Back) now opens it in play in remote mode. Quit confirm: open. |
| Pause | `src/mainint.c:3355` (`JE_pauseGame`); entered by P at `:3301`; resumes on any key except Ctrl/Alt, or any button (`:3418-3430`) | Yes, once entered | Entering pause needs P or a key the remote does not send. Media Play/Pause is not mapped. | Map media Play/Pause to P in `keysactive` (see media keys). | Fixed in remote mode: media Play/Pause pauses (SDL media scancodes; delivery on Windows is unverified until the probe runs). |
| Help pages | `src/mainint.c:217` (`JE_helpSystem`), page loop `:396`; left and right clicks by half-screen `:528-541`; right `:543-548`; keys `:554-590` | Yes | Left-click paging depends on pointer x, but the halves are coarse. Keys already page and exit. | None. | No fix needed. |
| In-game help | `src/mainint.c:1918` (`JE_inGameHelp`) | Yes | Any input dismisses it. | None. | No fix needed. |
| Options sliders (shop) | `src/game_menu.c:1277-1310` (pointer x); keys `:1649-1671` | Yes, with Left and Right | Pointer-only sliders, covered by keys. | None. | No fix needed. |

**Status (T3b.2).** The Status column covers the fixes that apply to remote use. They are active only when
`master && air_mouse` is on (`bike_controls_remote()`); with either off every path is the stock one. In remote
mode `keyboard.c` turns the OK button into Enter and Back into Esc (acting on the focused row, never on the
pointer), drops every other button, and ignores pointer motion for the menus, so hover cannot move the focus.
Media keys: Play/Pause pauses in play, next/previous change the rear mode and fire the sidekicks.

## Cross-cutting findings

1. **Media keys have no handler.** Grep finds no `AC_*`, `AUDIO*` or `MEDIA*` scancode in `src/` outside
   the regression harness. Every key event that is not one of the handled scancodes is dropped. The
   plan's mapping (play/pause to pause, next/previous to sidekick or rear mode) is not implemented yet.
2. **Right-click Back is not uniform.** It is handled on about ten screens. It is absent on the shop and
   the launcher, and it quits on the title and activates Yes on the quit prompt. A shared helper that maps
   `SDL_BUTTON_RIGHT` to Esc in each loop would remove most of the inconsistency.
3. **Button checks are missing** on the shop list, the shop's back box, the quit prompt and the two
   name prompts. Any button activates these. Requiring `SDL_BUTTON_LEFT` on the activation branch is a
   one-line fix each.
4. **Pointer visibility for an air mouse.** The game draws its own pointer sprite only while
   `mouseInactive` is false (`src/mouse.c:125`, `:146`). Any key press sets `mouseInactive = true`
   (`src/keyboard.c:267`), and motion with a delta clears it (`:380`). After a button press or an arrow
   key, the pointer is hidden until the air mouse moves again. The system cursor is hidden inside the
   window (`:372-374`). A remote user who presses Enter and then clicks may click on an invisible target.
5. **Hover selection.** The menus change the selection on pointer hover (for example `src/menus.c:132`).
   Air-mouse drift can move the selection, so the cursor sample plays on each change. The launcher has a
   still-pointer filter (`src/launcher.c:1779`) that the other menus lack.
6. **Blank names.** Both prompts can only produce blank or pre-filled names with a remote. This is the
   most visible gap for the remote-only user.

## Not audited

- The Destruct mini-game (`src/destruct.c`), which the T3b.4 list does not include.
- The Keyboard and Joystick remap screens, which need a key press to set each binding.
- Network play screens (`networkStartScreen`, `src/tyrian2.c:3349`).
- Runtime behaviour. Everything here is from reading the code.

## Fix order (suggested for the Sonnet pass)

1. Map media Play/Pause to P, and media Back or Menu to Esc, so pause and the in-game menu are reachable.
2. Add a shared `SDL_BUTTON_RIGHT` to Esc mapping in the shop, the save-name and name-entry loops.
3. Guard Quit: the title (Back), the in-game menu Quit and the launcher Esc each go through a confirm,
   and the quit prompt requires the left button and defaults to No.
4. Add a remote-friendly letter picker to the name prompts and the save-name prompt, with a non-blank
   default.
5. Fix `src/opentyr.c:970` and add the left-button checks listed above.
