# Input probe (T3b.0)

`--input-probe` is a diagnostic mode. It opens the normal game window and logs
every SDL event it receives, raw from `SDL_PollEvent`, with none of the game's
handlers in the way. Use it to record what SDL3 actually reports for the
G60S Pro air-mouse remote and for the touchscreen on a Windows Surface, before
the bike controls (T3b.2 and T3b.3) are mapped to them.

It needs the Tyrian 2.1 data, as the game does. It skips the launcher, the
title and the menus.

## Run it on Windows

From the folder that holds `opentyrian.exe`, in a command prompt:

```
opentyrian.exe --input-probe
```

The window opens on a black screen with instructions and the last 12 events.
Stop it by pressing **Escape three times within two seconds**. It also stops
on window close, or by itself after **5 minutes**.

The mode uses the same window and display settings as a normal start.

## What to press

Press each item once, wait about a second between items, and note what the
screen and the log show.

- Every remote button, including **OK, Back, Home, Menu**.
- Media keys: play/pause, next, previous, stop, mute.
- **Volume up** and **volume down**.
- **D-pad**: up, down, left, right, and the centre press if there is one.
- **Air mouse**: move it slowly, then fast; click the left button; hold it
  still and move only the wrist; if the remote has a gyro, move it through
  each direction.
- **Touchscreen**: a single tap, a long press, a drag in each direction, a
  two-finger tap, a two-finger drag, and a pinch.
- Press **F2** once. The status line switches between `relative=off` and
  `relative=on`. Repeat the air-mouse test in each mode and note whether the
  pointer or the motion values differ.
- Finish with **Escape three times**.

Every press, release, motion and tap is logged, so there are no timing
requirements. Look for the events the remote sends that do not match a
keyboard, mouse or gamepad: for example, a remote key that appears as
`KEY_DOWN` with a scancode name, a `PAD_BTN_DOWN` from a paired gamepad,
or a `MOUSE_BTN_DOWN` with no touch involved.

## Where the log goes

The probe writes **`input-probe.log`** to the user directory:

- `%APPDATA%\OpenTyrian\input-probe.log`, the normal location;
- beside `opentyrian.exe` instead, if `opentyrian.cfg` is in that folder
  (portable mode).

The first lines of the log give the platform, the video driver and the SDL
version. The start and end lines mark the run. Each event is one line:

```
   1234 ms KEY_DOWN       sc=4 (A) key=A (0x00000061) rep=0 mod=0x0001
```

- `ms` is the SDL event timestamp in milliseconds since SDL started.
- The name is the event type. Event types the probe does not name print as
  `OTHER` with their raw type number.
- `sc` and `key` are the SDL scancode and keycode, with their names.
- `MOUSE_MOTION` shows `xrel` and `yrel`. Once a second, a summary line sums
  the motion events and the `xrel`/`yrel` values of the last second, but only
  for seconds that had motion.
- `FINGER_*` lines give the finger ID, the normalised `x` and `y` (0 to 1),
  the `dx` and `dy` deltas, and the pressure.
- `TEXT_INPUT` text is printable ASCII only; other bytes show as `?`.

Send the whole file back, with the results table filled in.

## Results table

Copy this table into the result note. Write `none` when nothing was logged.

| Input | Expected (what you pressed) | Event type(s) logged | Key, scancode or button | Repeats or extra events | Notes |
|---|---|---|---|---|---|
| OK | | | | | |
| Back | | | | | |
| Home | | | | | |
| Menu | | | | | |
| Play/pause | | | | | |
| Next track | | | | | |
| Previous track | | | | | |
| Stop | | | | | |
| Mute | | | | | |
| Volume up | | | | | |
| Volume down | | | | | |
| D-pad up | | | | | |
| D-pad down | | | | | |
| D-pad left | | | | | |
| D-pad right | | | | | |
| D-pad centre (if any) | | | | | |
| Air mouse move (F2 off) | | | | | |
| Air mouse move (F2 on) | | | | | |
| Air mouse left click | | | | | |
| Air mouse hold, wrist move | | | | | |
| Gyro move (if any) | | | | | |
| Touch tap | | | | | |
| Touch long press | | | | | |
| Touch drag | | | | | |
| Two-finger tap | | | | | |
| Two-finger drag | | | | | |
| Pinch | | | | | |
| Escape x3 (exit) | | | | | |

Add the Windows build and the Surface model at the top of the note, and say
whether the remote was paired by Bluetooth or USB receiver.
