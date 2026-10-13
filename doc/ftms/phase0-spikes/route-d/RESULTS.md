# Route D spike: OpenTyrian in the browser via Emscripten (+ Web Bluetooth FTMS)

Status: **spike succeeded.** Unmodified upstream game sources build with emcc + ASYNCIFY and run in headless Chromium: title screen, menus, level load and gameplay at the native ~35 fps tick, config/save persistence via IndexedDB, and a JS-to-C FTMS data path. Time used: well inside the 2 h box. Nothing committed or pushed. Tracked sources in `/home/user/opentyrian-ftms` are untouched.

Files in this directory:
- `wasm.patch`: git diff of the worktree. It contains only **new** files (`src/ftms_web.c`, `web/shell.html`, `web/persist-pre.js`) and **zero edits to existing OpenTyrian sources**.
- `build-wasm.sh`: the exact emcc command (paths hard-coded to the scratchpad).
- `playwright-test.js`, `playwright-persist-test.js`: test drivers.
- `title-screen-10s.png`, `game-menu.png`, `gameplay-level1.png`: screenshots.

Worktree: `$SCRATCH/ot-wasm` (HEAD 624e2f2). Build output: `$SCRATCH/web/`. `$SCRATCH` is `/tmp/claude-0/-home-user/2934c9fe-a343-5108-8d9a-4c6da189a80a/scratchpad`.

## 1. Prior art

Search results were thin. The coordinator pointed to the one real port:
- **midzer/opentyrian, branch `emscripten`** (last commit 2024-07-06), live demo https://midzer.de/wasm/opentyrian/. A shallow clone is at `/home/user/midzer/opentyrian`.
- Their approach is **ASYNCIFY, not restructuring**. `grep EMSCRIPTEN src/` in their tree finds nothing, so there are no `#ifdef __EMSCRIPTEN__` source changes. They only add `EMSCRIPTEN.md` and `funcs.txt`.
- Their link line:
  `emcc -flto -O3 -fno-rtti -fno-exceptions *.o -o index.html -sUSE_SDL=2 -sASYNCIFY -sASYNCIFY_IGNORE_INDIRECT -sENVIRONMENT=web --preload-file data/ -sEXPORTED_RUNTIME_METHODS=['allocate'] -sASYNCIFY_ONLY=@../funcs.txt -sSTACK_SIZE=131072 --closure 1`
- Other search hits (OSDN `opentyrian-dll` repo with an `emscripten` branch from Jan 2020, and the awesome-emscripten list) gave nothing more concrete.

**What carries over to current upstream**

| midzer item | Carries over? |
|---|---|
| Strategy: no source changes, ASYNCIFY makes `SDL_Delay` yield | Yes. Confirmed independently: current upstream builds and runs with no source edits. |
| `-sUSE_SDL=2 -sASYNCIFY --preload-file` | Yes, same. |
| `-sSTACK_SIZE=131072` | **Essential, and must be larger for us.** Emscripten 6.x defaults to a 64 KB stack. Our `JE_loadMap` has about 64 KB `pic_buffer` plus 9 KB `mapBuf` on the stack. With the default stack the game crashed with `memory access out of bounds` in `fread` from `JE_loadMap` on level start. It first looked flaky, with an intermittent early OOB, because it depends on the stack layout. `-sSTACK_SIZE=1MB` fixed it. 128 KB probably works too, but it is untested. |
| `funcs.txt` (`-sASYNCIFY_ONLY`) | **No, not as-is.** The tree is an older snapshot: 98 vs 106 files in `src/`, without `logging.c`, `memreader.c`, `demo.c`, the new config system and others. Names have changed (`service_SDL_events` is now `handleSdlEvents`; the `waitUntil*` family is new). The list also hard-codes SDL 2.0.x internals (`SDL_PeepEventsInternal`, `GLES2_RenderPresent`, ...) that differ in SDL 2.32. The *technique* carries over: we would regenerate the list. |
| `-sEXPORTED_RUNTIME_METHODS=['allocate']` | Obsolete in current emsdk. |
| `-sENVIRONMENT=web`, `--closure 1`, `-flto -O3` | Optional size and speed flags. Not tried by us. |

## 2. Toolchain

```
git clone --depth 1 https://github.com/emscripten-core/emsdk $SCRATCH/emsdk
./emsdk install latest      # -> 6.0.12
./emsdk activate latest
```

**Sandbox problem:** emcc downloads the SDL2 port from `github.com/libsdl-org/SDL/archive/...zip`. The agent proxy returned 403 for that (GitHub repo access not enabled). Workaround, which is sandbox-specific and not needed on a normal machine:
```
curl -L -o SDL2-2.32.10.zip https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.zip   # release assets are allowed
export EMCC_LOCAL_PORTS="sdl2=$SCRATCH/dl/sdl/SDL2-2.32.10"
```

## 3. Build

`build-wasm.sh`, effectively:
```
emcc -std=gnu99 -O2 -DNDEBUG -DTARGET_UNIX -DTYRIAN_DIR='"/tyrian"' -sUSE_SDL=2 src/*.c \
  -o web/index.html --shell-file web/shell.html --pre-js web/persist-pre.js -lidbfs.js \
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS -sASYNCIFY -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=64MB \
  --preload-file $SCRATCH/data/tyrian21@/tyrian -sSTACK_SIZE=1MB -lm
```
- All of `src/*.c` compiles, including `network.c`. Without `-DWITH_NETWORK` it only provides stubs and the globals the rest of the game links against, so **leaving it out causes undefined-symbol errors** (`isNetworkGame`, ...). The in-game "Modem/Network Game" entry shows greyed out.
- No compile errors, and no `#ifdef __EMSCRIPTEN__` patches were needed. `SDL_Delay` under ASYNCIFY yields to the browser, so `waitUntilElapsed`, `waitUntilGetInputOrElapsed`, `delayUntilElapsed`, the pause loop and the menu loops all work unchanged.
- The data dir is mounted at `/tyrian` through the existing `TYRIAN_DIR` define, so `file.c` finds it with no code change.
- The version string reads "v2.1.???" because `git describe` failed in the worktree. Cosmetic.

## 4. Test results (headless Chromium 1194, Playwright)

Screenshots show the full flow: title screen at 10 s, players menu, Game Menu, level 1 in progress.

Console: the game's INFO/WARN logs appear (`tyrian.cfg` missing on first run is expected). There are no errors except a favicon 404. Browser warnings: `ScriptProcessorNode is deprecated` (SDL2 audio), and GL ReadPixels stall messages from headless software GL.

**Sizes** (`-O2`, ASYNCIFY, no `-g`):

| file | raw | gzip -9 |
|---|---|---|
| index.wasm | 1,521,354 | 431,774 |
| index.js | 202,956 | 48,711 |
| index.data (Tyrian 2.1 data) | 11,483,700 | 4,750,739 |

Without ASYNCIFY the wasm is 1,173,231 raw / 343,191 gz, so ASYNCIFY costs about +30% code size (+350 KB raw, +90 KB gz). The data package is the main cost, about 4.8 MB gzipped.

**Frame pacing** (counting `texSubImage2D` and `drawArrays` per second on the WebGL context while playing level 1, holding fire and up):
- Gameplay: **34.8 uploads/s**, which is Tyrian's native ~35 fps tick. There is no visible slowdown in headless software-GL Chromium. The page's `requestAnimationFrame` runs at 60 Hz independently.
- Title menu: about 27 draws/s (the game redraws less often there).
- Not measured: CPU usage, long soak, input latency, real GPU.

**Persistence:**
- On first-run config creation the game wrote `tyrian.sav` into `/home/web_user/.config/opentyrian`. `file.c` resolves this via `$HOME/.config/opentyrian`, and Emscripten sets `HOME=/home/web_user`.
- `web/persist-pre.js` mounts IDBFS on that directory and loads it before `main`. It flushes every 5 s and on `pagehide` / `visibilitychange`.
- Verified in `playwright-persist-test.js`: a file written there survived a page reload.
- Not verified: the game's own save/load round trip across reloads. It only wrote `tyrian.sav` and should work, since it uses the same directory.

**FTMS path** (`src/ftms_web.c`, `web/shell.html`):
- `ftms_web_set(power, cadence, speed_x100)` is `EMSCRIPTEN_KEEPALIVE`. JS calls it through `Module.ccall`.
- The page includes a complete Indoor Bike Data (0x2AD2) parser, with the flags field handled including the inverted bit-0 speed-present rule.
- The page also has a "Fake FTMS notifications" button that synthesizes packets. Result: `ftms_web_get_power/cadence/seq` read back power ≈170 W, cadence ≈85 rpm, and `seq` incrementing.
- The "Connect bike" button is the real `navigator.bluetooth.requestDevice({filters:[{services:[0x1826]}]})` → `getPrimaryService(0x1826)` → `getCharacteristic(0x2AD2)` → `startNotifications()` flow. **It was not run**: there is no BLE hardware or Bluetooth stack in this sandbox.
- The game does not yet *consume* the globals (no gameplay hook). Doing so means adding a per-frame read, for example next to the keyboard poll in `handleSdlEvents`, or mapping power to a virtual axis or button.

## 5. Web Bluetooth status and the integration sketch

**Requirements:**
- Requires a user gesture for `requestDevice`.
- Requires a secure context (`https:` or `localhost`).
- Uses a user-driven chooser, so there is no silent auto-reconnect on a fresh page load. `watchAdvertisements()` and `getDevices()` exist but are flag/permission-gated.

**Browser support** (from MDN browser-compat-data and the Chromium implementation-status page found by search; the freshest data was around 2025 and I could not confirm any 2026 change):
- Chrome / Edge on Windows, macOS, ChromeOS, Android: yes.
- Linux: Chrome documentation said "behind flag" (`chrome://flags/#enable-experimental-web-platform-features`; needs BlueZ ≥ 5.41). Treat Linux as flag-dependent and re-check on a real machine.
- Firefox and Safari: no native support. Firefox has a third-party WebBT add-on that needs a native companion app.
- iOS: no Web Bluetooth in any iOS browser (they all use WebKit).

## 6. Assessment

**Effort to reach a production-quality browser build: about 3 to 6 days.** The core port took minutes. Remaining work:
1. Regenerate a minimal `-sASYNCIFY_ONLY` list. `-sASYNCIFY_ADVISE` shows about 1,900 functions instrumented in the naive build, mostly through indirect calls in SDL and libc. This recovers code size and CPU. Note that `-sASYNCIFY_IGNORE_INDIRECT` is risky without care. Roughly 1 day.
2. Replace ASYNCIFY's busy-yielding with a cleaner pacing model, or keep it. Pacing is already good enough. Decide whether to use `-sASYNCIFY_STACK_SIZE` tuning and `requestAnimationFrame`-aligned presents.
3. Add the gameplay hook for the bike (power/cadence → game input), plus a calibration and fallback UX.
4. Pointer-lock / `SDL_SetRelativeMouseMode` behaviour, key capture, canvas scaling and fullscreen (the shell sets pixelated CSS scaling and a fullscreen button, but SDL's own fullscreen path was not exercised).
5. Data delivery: 11.5 MB (4.8 MB gz) preload. Consider lazy fetch with a progress UI and caching in IDB or the Cache API. **Licensing: Tyrian 2.1 is freeware but redistribution terms need review before hosting the data publicly.**
6. Test across browsers and real hardware (BLE, audio, low-end GPUs).

**Risks:**
- **ASYNCIFY overhead:** +30% code size and some CPU for stack unwinding and rewinding. Measured gameplay pacing is fine. The 1 MB wasm stack is needed (the 64 KB default crashes), and the ASYNCIFY save buffer also grows with deep stacks. `-sASSERTIONS` / `-sSTACK_OVERFLOW_CHECK` would catch the next case early.
- **Audio unlock:** SDL2's Emscripten audio uses the deprecated `ScriptProcessorNode` on the main thread. This is exposed to underruns when ASYNCIFY-sleep slices are long, and to autoplay policy. A user gesture (Start button) is needed before the AudioContext runs. Audio was **not verified** in this headless test (I only saw the audio init succeed with the deprecation warning).
- **Fullscreen:** needs a user gesture. The shell's button calls `canvas.requestFullscreen()`.
- **Saves persistence:** IDBFS works, but writes are flushed asynchronously. The 5 s timer plus `pagehide` can lose the last few seconds if the tab is killed. Clearing site data wipes saves, so consider an export/import button.
- **Pointer lock / Esc:** Esc is also the browser's fullscreen exit key. Inside a game that uses Esc for pause/menu this needs handling.
- **Web Bluetooth reach:** Chromium desktop and Android only. Firefox, Safari and iOS cannot use it. Treat the Flywheel bridge or a native companion as the fallback.
- **Upstream drift:** the zero-patch approach means rebasing onto upstream is trivial, but any new blocking loop works only if it goes through `SDL_Delay`. A busy-wait loop without a yield would hang the tab.

**Embedding in the Flywheel shell** (`/home/user/flywheel/game`, Vite + TypeScript + three.js; `src/modes/shooter` already exists):
- Yes. The wasm build outputs a plain `index.js` plus `index.wasm` plus `index.data`, and these can sit in Vite's `public/` directory. It is loaded in an `<iframe>` or a second `<canvas>`. The only things needed from the host are the `Module` object (`canvas`, `locateFile`) and a JS call into `ftms_web_set`.
- An important simplification: Flywheel already normalizes bike data into `Telemetry` (`cadenceRpm`, `powerW`, `speedKmh`, ... in `src/core/telemetry/protocol.ts`) and gets it from its local bridge over `/ws` (Vite proxy to 127.0.0.1:8787). **The embedded game could take that stream and call `ftms_web_set`, so the page would not need its own Web Bluetooth code at all**, and this also works in browsers without Web Bluetooth.
- The wasm build runs one game loop on the main thread with ASYNCIFY. It would compete with three.js for the main thread, so run it in a separate iframe or tab. A web worker plus `OffscreenCanvas` is possible but a bigger change (Emscripten `-sOFFSCREENCANVAS_SUPPORT` / proxying; not tried).
- Keyboard focus and audio unlock need to be handled once, by the host.

## 7. Not done / stopping point

- Web Bluetooth was only written and not run (no hardware or BLE stack).
- No audio verification, no real-GPU or long-soak measurement, no gameplay hook for FTMS data.
- No `ASYNCIFY_ONLY` optimisation (only the advise output was generated, see above).
- No `-sENVIRONMENT=web`, closure or `-flto` tuning.
