# Hardware playtest checklist (Surface + Q200B)

This is the calibration session for the bike integration. It tells us whether the
constants (FTP, Strength, floor, bias, cap, the superbomb cooldown) feel right on a
real bike, and how the controls behave on the Surface. Plan for 45 to 60 minutes.

## 1. Setup

1. Build or unpack the bike-enabled OpenTyrian for Windows, with the Tyrian 2.1 data.
2. Have the Flywheel checkout next to it (or note its path) and `uv` installed.
3. Switch the Q200B on, wake it by pedalling, and make sure nothing else holds its
   Bluetooth connection (close the phone app).
4. Start everything with the launch helper. It starts the Flywheel bridge, waits for
   `/healthz`, starts the game with `--ftms-bridge=127.0.0.1:8787` and stops the bridge on exit:

   ```
   tools\ftms\play-with-bike.cmd --flywheel C:\path\to\flywheel --source ble -- --ftms-log=C:\Users\you\ftms-session1.csv
   ```

   (On Linux or macOS: `tools/ftms/play-with-bike.sh --flywheel PATH --source ble -- --ftms-log=...`.)
   Everything after `--` goes to the game, so `--ftms-log=FILE` writes the calibration CSV.
   Use a new file name for each run, because the log is overwritten.
5. In the game open Setup > Bike and Controls: Enabled on, Generator on, HUD on. Check
   the link dot reaches green (Streaming) while you pedal.
6. Set your FTP. Best: run `flywheel-bridge calibrate` in the Flywheel checkout (a warm-up, the hardest
   even 8 minutes you can hold, a cool-down; FTP is 90 percent of that average, in this bike's own
   watts). Setup > Bike and Controls then shows it beside FTP (FTP Source: Bridge) from the next level.
   Otherwise type one under FTP Source: Manual (start with 200 W). `--ftms-ftp=W` on the command line
   beats both the bridge's FTP and the typed one, so leave it off when you want the bridge value.
   If your Flywheel offers intensity presets, the FTP row then reads like `196 W Easy 85 %`:
   the game plays at the preset's FTP. Try each preset (Recovery, Easy, Normal, Hard, Very hard) for
   a level and check that the same watts give a lower effort bar on a harder preset (its FTP is higher) and that the row and
   help line name the preset. Presets apply from the next level.
7. Play Tyrian level 1 (or any level you know well). Use a weapon you like, and note the
   generator you start with.

## 2. What to try

Spend about 5 minutes on each block and note what you felt.

1. **Effort bands at FTP 200.** Ride easy (about 40 percent of FTP), moderate (about 70 percent)
   and hard (100 percent and above) for 90 seconds each. Watch the power bar and the HUD
   readout. Note if easy riding keeps you alive, moderate feels like a good flow, and hard feels
   rewarding.
2. **FTP values.** Repeat the moderate block at FTP 150, 200 and 300. The bar response should
   shift with it. Note the value that matches your real effort.
3. **Strength.** Try 50, 100 and 200 percent. Note when the generator feels stingy or too generous.
4. **Stopping.** Stop pedalling for 10 seconds, then restart. Note how long the HUD and the bar
   take to notice that you stopped (stop detection lag) and what the bar does while stopped.
5. **Surge effects, one at a time.** Turn on only one of superbomb, sidekick, special and shield.
   Do two or three hard intervals (30 to 60 seconds, 130 percent of FTP) per effect. Note if you
   can feel it, and if it triggers too often or too rarely. For superbombs, note how many you
   get in 10 minutes of riding (the cooldown is 60 seconds, the cap is 5).
6. **Auto-fire and the reserve.** Switch auto-fire on in the menu, press its key (default A)
   in play to toggle it, and try reserve Off, 25 and 50 percent while firing continuously.
   Note whether the reserve keeps enough power for your shield and whether AUTO / HOLD in the
   HUD matches what the ship does.
7. **Air mouse (G60S Pro remote).** Steer for a full level with Air mouse on. Try sensitivity
   50, 100 and 200 percent, and smoothing on and off. Note drift, jitter and whether the
   buttons fire and pause as expected.
8. **Touch.** Turn Touch on and play with a finger: drag to steer, tap zones for fire and
   special. Note missed taps and whether the drag feels too fast or too slow.
9. **Link loss.** While playing, close the bridge window (or unplug the dongle) for 10 seconds,
   then restart it. The bar should blend back to stock about 3 seconds after the loss
   and return to the bike after reconnecting. Note how long the reconnect took.

## 3. What to record

Per block, write one line in a text file, with the clock time (for example `14:32 FTP 200,
strength 100, hard: bar always full, felt too easy`). The CSV has no notes column, so the
clock time and the CSV `t_s` (seconds since the level began) are how we line things up. Note
the time on a watch when each block starts.

Also note: the real FTP guess, the strength setting you ended with, the generator and weapon, the
Windows version and whether the Surface was on mains power.

The CSV (`--ftms-log`) has one row per second of gameplay while a level runs, with these columns:
`t_s, watts, cadence, link, effort_w, effort_norm, surge_meter, refill_per_frame,
stock_power_add, power, shots_fired, shots_refused, shield, superbombs, autofire, blend`.
`refill_per_frame` is the average power units added per frame in that second, `stock_power_add`
what the unmodified generator would have added, `blend` is 1 when the game is on the stock refill
(link lost) and 0 when the bike is fully in charge. `shots_refused` counts shots the
game could not fire for lack of power.

## 4. What to send back

1. The CSV files from each run (`ftms-session1.csv`, ...).
2. Your notes file.
3. If you tested the air mouse or touch: the input probe log. Run `opentyrian.exe --input-probe`
   (see `controls-probe.md`), press the items listed there, and send
   `%APPDATA%\OpenTyrian\input-probe.log` (or the file beside the exe in portable mode).
4. The bridge log if something odd happened. The helper writes it under
   `%LOCALAPPDATA%\OpenTyrian-FTMS\logs` (Linux: `~/.local/state/opentyrian-ftms/logs`).
5. `opentyrian.cfg` (it holds the `[ftms]` section with your final settings).

## 5. Questions we want answered

- Does the game feel good at easy, moderate and hard effort? What is the effort at which it
  starts to feel like a fair fight, and at which it becomes trivial?
- Is the generator too stingy or too generous at your FTP? Which FTP and Strength values
  should be the defaults? Would you change floor, bias or cap?
- Latency: how long after you start pedalling hard does the bar respond (a stopwatch, or the
  time difference between a `watts` jump and a `refill_per_frame` rise in the CSV)?
- Stop detection lag: after you stop, how many seconds until the HUD shows it and the bar drops
  to the minimum?
- Superbombs: do you get too many or too few with the 60 second cooldown and cap of 5? Do you
  want the cooldown in the menu?
- Do shots get refused while you play normally (`shots_refused` in the CSV)? Does auto-fire
  with the reserve strike a good balance?
- Which air mouse and touch behaviours need changing, according to the probe log and your notes?
- Is the HUD readable on the Surface screen, and is the link dot clear enough?
