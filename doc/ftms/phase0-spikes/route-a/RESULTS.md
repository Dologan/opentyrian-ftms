# Phase 0 spikes 0.2 + 0.3 (Route A: OpenTyrian as a client of the Flywheel bridge)

Throwaway proof of concept. Nothing committed or pushed. Build worktree (left in place):
`/tmp/claude-0/-home-user/2934c9fe-a343-5108-8d9a-4c6da189a80a/scratchpad/ot-upstream` (detached HEAD 624e2f2, with the changes in `game-integration.patch` applied, uncommitted).
`SCRATCH=/tmp/claude-0/-home-user/2934c9fe-a343-5108-8d9a-4c6da189a80a/scratchpad`.

Bottom line: **Route A works.** A ~320-line, thread-free C99 WebSocket client on SDL2_net talks to the unmodified bridge, and a headless OpenTyrian run on the stock demo level visibly responds to watts (weapon fire is gated by the bike). The one real hazard is SDL2_net's blocking `connect()`.

## Files in this directory

| File | What |
|---|---|
| `bike_ws.c`, `bike_ws.h` | the WebSocket client (357 + 54 lines incl. comments; 320 non-comment/non-blank in .c) |
| `bike_ws_test.c` | standalone console tester / latency logger / sim-cadence driver (88 lines) |
| `game-integration.patch` | `git diff` of the worktree (new files added with `git add -N`): +557/-1 over 9 files. It contains bike_ws.* again plus `bike_game.c/.h` glue and the small hooks in params.c, opentyr.c, keyboard.c, tyrian2.c, shots.c |
| `logs/` | trimmed raw logs: `test_sim.log`, `test_replay.log`, `game_sim_run.log`, `driver_game_run.log`, `game_reconnect_run.log` |

## Spike 0.2: native build baseline

Host: 4 cores, gcc 13.3.0, SDL2 2.30, SDL2_net 2.2.

```
cd $SCRATCH/ot-upstream
make                 # serial, cold:            real 48.7 s   (user 34.7 s, sys 10.4 s)
make clean; make -j4 # cold:                      real 18.1 s
make clean; make debug -j4   # -O0 -g3 -Werror:   real  7.7 s, exit 0
```

- **Warnings: zero** in release, and `make debug` (which adds `-Werror`) passes. Flags are `-std=iso9899:1999 -pedantic -Wall -Wextra` so new code must be strict C99 (no `strdup`, no POSIX unless guarded). I hit exactly that once (`strdup` -> `SDL_strdup`).
- Binary size: release `opentyrian` = 634,632 bytes (text 563 KB, bss 656 KB); debug = ~2.18 MB. With the route-A code added the release binary is 640,888 bytes (+6.2 KB).
- `WITH_NETWORK=auto` found SDL2_net, so the stock netplay code is compiled in too (relevant: SDL2_net is already a dependency of the game, so Route A adds no new library).
- Headless boot:
  ```
  cd $SCRATCH
  HOME=$SCRATCH/home SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 10 \
     ./ot-upstream/opentyrian --data=$SCRATCH/data/tyrian21
  ```
  Boots, finds data (`--data=DIR` / `-t`, `customDataDirPath` in params.c), logs the normal missing tyrian.cfg/.sav warnings (first run, harmless), "Assuming mouse detected", and sits in the intro/title loop until `timeout` kills it (exit 124, user CPU 0.23 s in 10 s). No crash, **no blockers**. Release builds play the intro logos (`#ifdef NDEBUG intro_logos()` in opentyr.c:~869), which costs about 7 s before the title; debug builds skip them.

## Spike 0.3: Route A ingress

### 1. The client (`bike_ws.c/.h`)

- SDL2_net only: `SDLNet_TCP_Open` (blocking connect), `SDLNet_AllocSocketSet(1)`, `SDLNet_CheckSockets(set, 0)` + `SDLNet_SocketReady` + `SDLNet_TCP_Recv`. No threads.
- Handshake: `GET /ws` with `Upgrade: websocket` and a fixed `Sec-WebSocket-Key`; the 101 reply is consumed inside `bike_ws_poll()` (state machine CLOSED -> HANDSHAKE -> OPEN), so `bike_ws_open()` only blocks on the TCP connect. `Sec-WebSocket-Accept` is not verified (spike). After the 101 it sends `{"type":"hello","client":"game","v":1}`.
- Frames: parses 7/16/64-bit lengths, text, ping (replies with a masked pong, echoing the payload), close (echoes and drops). Client frames are masked (xorshift key). Fragmented messages and frames over the 16 KB RX buffer are not supported (aiohttp does not fragment; oversize drops the link).
- JSON: `strstr`-based scanner for `"powerW"`, `"cadenceRpm"`, `"seq"`, `"wall"`, `"source"`, and `age.power`; `null` gives `has_power=false`. It relies on the bridge's compact separators (`json.dumps(..., separators=(",",":"))`, but also tolerates a space after the colon).
- API: `bike_ws_open(host,port)`, `bike_ws_poll(&sample)` (returns telemetry frames parsed, 0 = nothing, -1 = link down; keeps the newest), `bike_ws_send_text()`, `bike_ws_state()`, `bike_ws_close()`. Auto-reconnect every 2 s lives inside `bike_ws_poll()`.
- Compiles clean with `-std=iso9899:1999 -pedantic -Wall -Wextra -Werror` (it is built that way as part of the game).
- **Bug found and fixed during the spike:** auto-reconnect passed its own `host_name` buffer back into `bike_ws_open`, which `snprintf`ed it onto itself (UB). The reconnect silently never worked until I fixed it; the fixed file is what is in this directory.

Standalone tester:
```
gcc -std=gnu99 -pedantic -Wall -Wextra -O2 -o bike_ws_test bike_ws.c bike_ws_test.c $(pkg-config --cflags --libs sdl2 SDL2_net)
./bike_ws_test HOST PORT SECONDS [cadence@sec,...]
```

### 2. Against the real bridge

Bridge started from `/home/user/flywheel/bridge` with `uv sync` (only `.venv` was created) and
`uv run flywheel-bridge run --source sim --no-record --data-dir $SCRATCH/bridge-data` (serves `ws://127.0.0.1:8787/ws`, sim at the default 0.91 Hz).

```
./bike_ws_test 127.0.0.1 8787 36 0@3,60@9,90@15,110@21,0@27      # logs/test_sim.log
t= 2.99s seq=9  src=sim power=98W  cad=56rpm age=0ms lat=5.1ms
t= 3.01s >>> send {"type":"sim","cadenceRpm":0} (ok)
t= 6.29s seq=12 src=sim power=39W  cad=26rpm      <- coasting down
t= 9.00s >>> send ... 60   t=11.79s power=96W cad=55
t=15.01s >>> send ... 90   t=17.28s power=186W cad=90
t=21.00s >>> send ... 110  t=22.78s power=245W cad=109
t=27.01s >>> send ... 0    t=32.66s power=0W  cad=0
SUMMARY: 33 telemetry frames in 36 s (0.92 Hz), mean inter-arrival 1099 ms (max 1107), mean latency 5.6 ms (max 12.2), state=2
```
- Sim power tracks cadence smoothly (cadence 0/60/90/110 gives about 0/100/180/245 W, with a ramp of ~3 samples); `canSetResistance` etc. are not needed.
- **Message rate = the notification rate, not 10 Hz:** the bridge only emits a snapshot per bike notification (sim default 0.91 Hz, like the Q200B), coalesced to *at most* 10 Hz. So a real Q200B gives ~1 sample/s, and the game must hold/smooth between samples (the glue eases with a 0.15/frame filter and treats > 3 s without data as 0 W).
- **Latency:** bridge `wall` vs. local epoch (same host, so clocks agree): mean 5.6 ms, max 12 ms. Negligible next to the 1.1 s sample period. (Bridge-to-game transport latency for a real bike over loopback would be about the same; BLE adds its own.)
- Ping/pong: the aiohttp server pings every 20 s with `heartbeat=20`; the 36 s and 70 s runs stayed connected, so masked pongs are accepted.
- Replay (`flywheel-bridge replay ../shared/fixtures/rides/q200b-run1.raw.jsonl --speed 1 --loop`, `logs/test_replay.log`): `src=replay`, 36 frames / 40 s (0.90 Hz), power 213-242 W at cadence 60-68 rpm, later dropping to 15-34 W, latency mean 5.3 ms. Parser handles real Q200B data fine, including the 0.9 Hz cadence.
- Bridge stopped and the `flywheel` tree is untouched (`git status` clean; no `data/` dir was created because I passed `--data-dir`).

### 3. Game integration (`game-integration.patch`)

Changes:
- `params.c`: `--bike=HOST[:PORT]` (default port 8787) and a test hook `--bike-demo` (skips the title screen and plays the built-in attract demo level; `titleScreen()` returns immediately with `playDemo=true`).
- `opentyr.c`: `bike_game_init()` right after arg parsing (blocking connect).
- `keyboard.c`: `bike_game_service()` at the top of `handleSdlEvents()`. Every menu and level loop already passes through that function, so the socket is serviced (pings answered) everywhere, not only in-level.
- `tyrian2.c` (level loop, power bar): `power += (bike_host != NULL) ? bike_power_add() : powerAdd;` in place of the stock refill (line ~1118).
- `bike_game.c`: watts-to-units mapping. `k = powerAdd / 150`, i.e. 150 W gives exactly the equipped generator's stock per-frame refill (30/frame for the demo's generator = 1050 units/s, with 900 = full bar). `smooth_w += (target_w - smooth_w)*0.15` per frame; a float accumulator `acc += smooth_w*k`, adding the integer part per frame (so 50 W gives 10 units/frame without rounding losses). A once-per-second stderr line logs watts, power, refill and shots fired/blocked.
- `shots.c`: counters for shots fired vs. refused for lack of power.
- Not done: reading `powerSys` for several generators (not needed: `powerAdd` is already `powerSys[generator].power`), pause/menu behaviour, and the shop's second refill in `game_menu.c:3225` (weapon-test screen still uses stock).

Reaching gameplay headlessly: **yes**. `--bike-demo` goes straight into the stock demo level (demo.1, then 2, 3 ... as they finish), running at the normal 35 frames/s with the dummy video driver.

```
HOME=$SCRATCH/home SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./ot-upstream/opentyrian \
   --data=$SCRATCH/data/tyrian21 --bike=127.0.0.1:8787 --bike-demo      # debug build
(and, concurrently, ./bike_ws_test 127.0.0.1 8787 46 0@2,60@14,90@26,110@38  to steer the sim)
```
Trimmed output (`logs/game_sim_run.log`; columns: game-seconds, watts received, bar value, shots in the last second):
```
t= 5s  rx_watts=0   added/s=0     power=0/900    shots+0  blocked+84   <- cadence 0: bar empty, guns dead
t=15s  rx_watts=46  added/s=151   power=62/900   shots+8  blocked+36   <- 60 rpm step: bar refills, firing resumes
t=19s  rx_watts=112 added/s=674   power=317/900  shots+14 blocked+0
t=31s  rx_watts=189 added/s=1302  power=890/900  shots+19 blocked+0    <- 90 rpm
t=43s  rx_watts=251 added/s=1696  power=890/900  shots+14 blocked+0    <- 110 rpm: bar pegged at the cap
```
At 0 W the demo's autopilot tries to fire ~70-140 times/s and is refused every time (`blocked+84..140`); at >= ~100 W the bar sustains continuous fire (blocked drops to 0). Refill per second scales linearly with watts (46 W: 151/s; 112 W: 674/s; 189 W: 1302/s; 251 W: 1696/s, vs. the stock 1050/s at 150 W).

Link loss and reconnect (`logs/game_reconnect_run.log`; bridge killed at game-time ~10 s, restarted ~10 s later):
```
t= 9s link=2 rx_watts=110                    t=13s rx_watts=0 smooth=6 (3 s stale timeout) ... power drains to 8
t=10s link=0 (peer closed; polling every 2 s, refused connect is instant on localhost)
t=21s link=0   t=22s link=2   t=23s rx_watts=112   (reconnected ~1-2 s after the bridge was back up)
```
frames/s stayed 35 throughout, so a refused connect on localhost costs nothing per frame.

A release-build (NDEBUG) binary with the same code compiles clean (640,888 B); the runs above used the debug build, which skips the 7 s intro logos. An earlier release run before I moved the poll hook into `handleSdlEvents()` never completed the handshake (the first hook position was not on the level path); that is how I found that the level loop does not go through `waitUntilElapsed()` (it uses `JE_starShowVGA` -> `delayUntilElapsed` plus `handleSdlEvents`).

### 4. Measurements and risks

- **LOC:** `bike_ws.c` 357 (320 code), `bike_ws.h` 54, `bike_game.c/.h` 107, hook edits in 5 existing files: ~40 lines, test program 88. Whole patch +557 / -1.
- **Threads:** none. All I/O is `CheckSockets(…, 0)` in `handleSdlEvents()`; per-call cost is a `select()` with zero timeout (no measurable effect: 35 frames/s unchanged). Up to 16 recv/parse iterations per call, 16 KB static RX buffer.
- **Blocking connect, the main hazard:** `SDLNet_TCP_Open` uses a blocking `connect()`.
  - Bridge down on localhost: refuses instantly (0 ms, `open(127.0.0.1:8787) -> FAILED in 0 ms`).
  - Unreachable/blackholed address (`10.255.255.1:8787` in this sandbox): **blocked 133,830 ms (~2.2 min)** before failing; the whole game would freeze that long, and the auto-reconnect would repeat it every attempt.
  - Mitigations for a production version: (a) only ever connect to loopback/known-up hosts and hide LAN connects behind a menu action; (b) do the connect on a helper thread or use raw non-blocking `connect()` + `select` (bypassing SDL2_net's TCPsocket; its socket handle is opaque, so that means writing ~100 lines of BSD-socket/Winsock code); (c) have the launcher start the bridge and wait for its port before launching the game. Also a hostname lookup (`SDLNet_ResolveHost`) can block on DNS.
- **Reconnect:** implemented (every 2 s, in `bike_ws_poll`), proven working. Staleness is handled in the game glue (3 s without a frame means 0 W), independent of the link state. Not yet handled: bridge up but bike not yet connected (status messages ignored; `powerW` would be null, which maps to 0 W; fine), a stalled-but-open TCP link (staleness covers the gameplay effect; no TCP keepalive), and the server closing the link if the game does not poll for ~30 s (pong deadline). This could only happen in a long blocking section such as loading.
- **Protocol robustness:** substring scanner is fine for the current schema but would mis-parse if the bridge added a nested field named `powerW`/`seq` earlier in the message, or sent fragmented/oversize frames. Accept-key not validated. No TLS (loopback only; the bridge is plain `ws://`). Windows needs `-lws2_32` (SDL2_net handles it) and Winsock errors on a refused connect take about 1 s there by default, so a blocking connect is worse on Windows.
- **Sample rate:** ~0.9 Hz from a Q200B. Game feel at 35 fps needs smoothing; the 0.15/frame filter lags about 0.2 s behind each sample and 1 s behind the rider. For a "feels responsive" game it may want cadence-derived instantaneous estimates (cadence at 1 Hz has the same problem) or prediction.
- **Two clients:** the sim cadence command is global; a second tester/controller client alongside the game works (this was the test setup).

## Assessment of Route A

Viable and cheap. Everything the route needs from the game side exists today: the bridge protocol is simple enough that the client is ~300 lines with no new dependencies (SDL2_net is already linked by OpenTyrian's netplay), it can be polled once per frame with no threads, and the power refill is one line in the level loop. Gameplay response to watts was demonstrated in the real game process (headless demo level, 35 fps, bar and weapon fire tracking watts, graceful behaviour on link loss and reconnect).

Effort to productionise (rough): 2-4 days.
1. Fix the connect blocking (thread or non-blocking connect), about 1 day, plus Windows/macOS checks.
2. Proper UX around the link: a "connecting/no bike/stale" HUD indicator, optional pause when pedalling stops, calibration of watts to power (per-generator or per-difficulty, FTP-relative instead of the fixed 150 W), and the shop weapon-test refill: 1-2 days.
3. Smoothing/feel tuning for the 1 Hz sample cadence; consider also using cadence/resistance as game inputs (speed, ship movement) and sending the bridge `control` commands: open-ended.
4. Hardening of the parser (Accept-key check, fragmentation, bigger frames), a unit test against `shared/fixtures/rides/*.raw.jsonl`, CI build under `-Werror` (already clean): 0.5 day.
5. Packaging: the game has to find/start the bridge (Python + uv + BLE stack) — the real cost of Route A is operational (two processes, a Python runtime), not the C code.

Caveats of this spike: single machine, loopback only; a simulated bike (plus replayed Q200B fixture on the standalone client only; the game was driven by the sim source, not by replay or real hardware); gameplay proven through the attract demo (autopilot, fixed weapons), not an interactive session; no visual check (dummy video driver), so the evidence is the power/shot counters rather than a screenshot.
