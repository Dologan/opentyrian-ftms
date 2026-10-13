# Phase 0 research: web route (OpenTyrian to WebAssembly, Web Bluetooth FTMS)

Date of research: 2026-10-13. Scope: information gathering only. No existing files were modified.

Confidence labels: **[V]** verified in a primary source this session (repo file, spec, MDN data, Emscripten/SDL doc). **[S]** secondary source or search snippet only. **[U]** unverified or conflicting.

---

## 1. Existing OpenTyrian / Tyrian web ports

| Project | What it is | Engine | Status | Source |
|---|---|---|---|---|
| `opentyrian/opentyrian` (upstream) | Canonical C port. | SDL2 (+ optional SDL2_net). Makefile uses `-std=iso9899:1999` (C99). **[V]** | Active. HEAD commit 2026-10-09. **[V]** No Emscripten or web code. **[V]** | https://github.com/opentyrian/opentyrian |
| `midzer/opentyrian`, branch `emscripten` | Fork with an Emscripten build. Last commit `c398647` on 2024-07-06 ("add EMSCRIPTEN.md"). **[V]** | SDL2 (`-sUSE_SDL=2`). **[V]** Build is `emmake make` plus a link line (see section 4). **[V]** | Live demo at https://midzer.de/wasm/opentyrian/. The page showed a "Downloading... / Click here to load / ~12mb" loader. I did not play it. **[V]** Fork `master` snapshot is from 2023-01-10 and has no web code. **[V]** | https://github.com/midzer/opentyrian/tree/emscripten, https://midzer.de/wasm/opentyrian/ |
| `midzer/opentyrian`, branch `sdl1` | SDL1 variant. Not inspected. | SDL1 **[V]** (branch exists) | n/a | https://github.com/midzer/opentyrian |
| OSDN `opentyrian-dll`, branch `emscripten` | David Ludwig's 2020 WIP port: README.emscripten, data preload, save persistence across reloads. Last commit Jan 2020. | Not confirmed. | Stale. **[S]** (OSDN host did not resolve from this sandbox; known only from search snippets) | https://osdn.net/projects/opentyrian-dll/scm/hg/opentyrian-dll |
| Neoteo article | Press coverage of midzer's browser build (aspect modes, scalers, jukebox). | | **[S]** | https://www.neoteo.com/en/opentyrian-the-classic-vertical-scroller-right-in-your-browser |
| libretro core | None found. The 2019 libretro forum thread says the port was announced but stalled, and that "I don't think so for OpenTyrian." `libretro-fetch.sh` on `libretro-super` master has no Tyrian entry. **[V] for the fetch script, [S] for the forum** | | Dead end. Use DOSBox cores to run the original DOS game. | https://forums.libretro.com/t/are-reminiscence-flasback-port-and-opentyrian-still-in-development/25174, https://github.com/libretro/libretro-super |

**Licence.** OpenTyrian code: GPL-2.0 (the COPYING file is the GPLv2 text). **[V]** Source headers may say "or later"; I did not check every file. Game data: Tyrian 2.1 is freeware. The upstream README says so and points to `https://camanis.net/tyrian/tyrian21.zip`. **[V]** The midzer repo does not ship data files; the user must supply them. **[V]** Redistribution of the data inside a Flywheel build needs a separate check of the freeware terms.

---

## 2. Web Bluetooth support matrix (as of Oct 2026)

Primary data: MDN browser-compat-data `api/Bluetooth.json` (raw GitHub, fetched this session). **[V]** Dated OS notes come from the WebBluetoothCG implementation-status page. **[V]** Note: MDN itself says Chrome's `getDevices` is flagged, which I could not reconcile with Flux (section 3). Treat getDevices as **[U]** until tested on the target machines.

| Platform / browser | requestDevice | getDevices | Notes | Source |
|---|---|---|---|---|
| Chrome, Windows 10/11 | Yes (Chrome 70+ on Windows) **[V]** | Flagged in MDN **[V]** | Implementation-status says Windows needs Win10 1703 (Creators Update). The same page says 1706 elsewhere. **[U]** | MDN BCD, WebBluetoothCG status |
| Chrome, macOS | Yes (Chrome 56+; macOS-only until Chrome 70) **[V]** | Flagged **[V]** | Mac requires OS X Yosemite+. **[V]** | MDN BCD |
| Chrome, ChromeOS | Yes, no flag. **[V]** | Flagged **[V]** | | WebBluetoothCG status |
| Chrome, Android | Yes (Android 6.0+). **[V]** | Flagged **[V]** | | MDN BCD, WebBluetoothCG status |
| Chrome, Linux | Partial. "Linux support is not enabled by default." (Chrome 70 entry). **[V]** Needs `chrome://flags/#enable-experimental-web-platform-features`. **[V]** Kernel 3.19+, BlueZ 5.41+. **[V]** | Flagged **[V]** | Flux README says the same flag applies on Linux. **[V]** | MDN BCD, Flux README |
| Edge (Chromium) | Yes, from Edge 79. **[V]** | Mirror of Chrome **[V]** | Edge flag form: `edge://flags/#enable-experimental-web-platform-features`. **[V]** | WebBluetoothCG status, Flux README |
| Opera | Desktop unreliable recently; Android since v46. **[V]** | | | WebBluetoothCG status |
| Samsung Internet (Android) | Shipped without flag in v6.4. **[V]** | | | WebBluetoothCG status |
| Firefox | No. Bug 674737. **[V]** Only via third-party extension (WebBT), which needs a native companion. **[S]** | No | | MDN BCD, search snippet |
| Safari (macOS) | No. WebKit bug 101034. **[V]** | No | | MDN BCD |
| Safari iOS / Chrome iOS | No. Flux says "Not Supported by the Web version". **[V]** | | | Flux README |
| iOS workaround: Bluefy | Third-party iOS browser (PNN SOFT) that adds Web Bluetooth. Requires HTTPS. **[S]** Not tested. | | | https://apps.apple.com/app/id1492822055 |

**Requirements and behaviour**

| Item | Finding | Source |
|---|---|---|
| Secure context | Required (HTTPS or localhost). **[V]** | MDN requestDevice, developer.chrome.com |
| User activation for requestDevice | Must be called from a user gesture (click, pointerup, touchend). **[V]** | developer.chrome.com/docs/capabilities/bluetooth (page dated 2015; treat as **[S]**) |
| Chooser | Each `requestDevice` opens the chooser unless it is not needed. Filters: services, name, manufacturer data (Chrome 92), exclusions (Chrome 114). **[V]** | developer.chrome.com |
| Previously granted devices | `getDevices()` returns devices the origin may already access, including out of range or powered off. **[V]** MDN says it is flagged in Chrome 85+. **[V]** Flux calls `getDevices()` to build exclusion filters. **[V]** A 2020 Chrome proposal pairs `getDevices()` with `watchAdvertisements()` for reconnect without a chooser. **[S]** Anecdotal 2021 reports say the advertisement event never fires on Linux/macOS. **[S]** | MDN getDevices, Flux web-ble.js, lists.w3.org 2020Jul |
| Persistent permissions | "Persistent Device Permissions" flagged (Chrome 83). **[V]** | WebBluetoothCG status |
| GATT invalidation | GATT attributes are invalidated on disconnect. They must be fetched again after reconnect. **[V]** (developer.chrome.com, 2015-dated page) | developer.chrome.com |
| Kiosk: enterprise policy | `DefaultWebBluetoothGuardSetting`: 2 = block, 3 = ask (default). No "allow" value. **[V]** `WebBluetoothAllowedForUrls` could not be verified. **[U]** | Microsoft Edge policy docs (same semantics in Chromium), Chromium code review |
| Kiosk: whether kiosk auto-grants | Not verified. Expect the chooser to appear. Pair in the kiosk profile once and check whether the grant survives restarts. **[U]** | |

**Windows 10 status (Surface Pro 3 target).** Microsoft keeps Edge and WebView2 updated on Windows 10 22H2 until October 2028. **[S]** Google's enterprise ESU support to Oct 2028 is **[S]** (search summary only). Consumer Chrome support on Windows 10 after Oct 2025 is **[U]**: I could not confirm a Google statement.

**Chrome 159 preview.** Its release notes contain no Web Bluetooth entries. **[V]** https://chromestatus.com/release-notes/159

---

## 3. FTMS via Web Bluetooth: prior art and spec facts

**Spec.** Fitness Machine Service 1.0 (78 pages), with Errata 23224 which is mandatory for compliance. **[V]** Landing page: https://www.bluetooth.com/specifications/specs/fitness-machine-service-1-0/ . PDF: https://www.bluetooth.org/DocMan/handlers/DownloadDoc.ashx?doc_id=423422 (text extracted locally with `pdftotext`).

| Spec item | Finding | Source |
|---|---|---|
| Indoor Bike Data notify rate | "typically once per second while in a connection"; "the interval is not configurable by the Client". **[V]** | FTMS §4.9.1 |
| Fragmentation | "all the fields of this characteristic cannot be present simultaneously if using a default ATT_MTU size". **[V]** | FTMS §4.9.1 |
| Flags bit 0 (More Data) | 0 = Instantaneous Speed present; 1 = not present. **[V]** | FTMS §4.9.1.1 |
| Control Point write | Client writes the op code into the Control Point characteristic (GATT write). **[V]** | FTMS §4.16.1 |
| Request Control | Op code `0x00`, mandatory (M). **[V]** | FTMS Table 4.15 |
| Set Target Power | Op code `0x05`. Conditional: only if the Power Target Setting feature is supported (C.4). **[V]** | FTMS Table 4.15 |
| Start or Resume | Op code `0x07`, mandatory (M). **[V]** | FTMS Table 4.15 |
| Response | Op code `0x80`, then the request op code, then a result code. The response is indicated. **[V]** | FTMS §4.16 |

**Other UUIDs.** `0x1826` (Fitness Machine), `0x2AD2` (Indoor Bike Data), `0x2AD9` (Control Point), `0x2ACC` (Feature). **[V]** Long forms confirmed in Flux `src/ble/web-ble.js` (e.g. `00002ad2-0000-1000-8000-00805f9b34fb`). **[V]**

**Prior art.**

| Project | What it shows | Source |
|---|---|---|
| Flux (dvmarinoff), README names the app Auuki. AGPL-3.0. Last commit 2026-03-03. | PWA trainer app with FTMS over Web Bluetooth. Reads the FTMS UUIDs above. **[V]** Handshake: writes Request Control (`0x00`), and the control response callback releases the write lock. **[V]** No Start/Resume (`0x07`) found in `src/ble/ftms/control-point.js`. **[V]** Power writes use `writeWithRetry(4, 500 ms)`. **[V]** Reconnect logic and the `getDevices()` exclusion filters are in `src/ble/web-ble.js` and `src/ble/connectable.js`. **[V]** `connectable.js` has a TODO: "getDevices() fail" is not handled in `watch()`. **[V]** | https://github.com/dvmarinoff/Flux (commit c0d1d4a) |
| Flywheel's own Volcade notes | One real trainer sent **no notifications until Request Control and Start/Resume were written**. | `/home/user/flywheel/docs/PLAN.md` §1 (Volcade prior art) |
| Garmin dev forum | Anecdotal: a trainer with a vendor service gave no notifications while other apps connected. | https://forums.garmin.com/developer/connect-iq/f/discussion/282112/ble-notification-not-appearing-for-ftms-ble/1354941 **[S]** |

**Gotchas for this project.**
- Flux skips Start/Resume. Volcade needed it. Keep it best-effort and log the result, as the plan says. **[V]** (PLAN.md; Flux source)
- Indoor Bike Data is about 1 Hz. Browser `oncharacteristicvaluechanged` latency is fine at that rate, but do not design around 10 Hz. **[V]**
- Control writes should be serialised and wait for the response. Flux does this. **[V]**
- Reconnect: the GATT handles are dead after disconnect. Re-fetch services and characteristics. **[V]**
- Chrome Linux needs the experimental flag. Surface Pro 3 BT 4.0 issues (PLAN.md risks) apply equally to WinRT under Chrome. **[U]**

---

## 4. Emscripten + SDL2 practicalities

**Upstream OpenTyrian blocking points.** The main loop is written as blocking code. The waits in `src/keyboard.c` (`waitUntilElapsed`, `waitUntilGetInput`, `waitUntilHasInputOrElapsed`, ...) loop with `SDL_Delay(SDL_POLL_INTERVAL)`, where `SDL_POLL_INTERVAL` is 10. **[V]** Network sync loops (`while (!network_is_sync())`) also block. **[V]** Frame timing emulates the DOS PIT via `SDL_GetTicks` and `frameCountMax` (`src/nortsong.c`, `src/config.c`). **[V]** The 35 fps figure is therefore not a constant in the source. **[V]**

**Main loop options**

| Option | Finding | Source |
|---|---|---|
| ASYNCIFY | Lets blocking C code yield. Overhead is "something like 50%" in size and speed. Output "can be much larger". Use `-O3`. **[V]** | https://emscripten.org/docs/porting/asyncify.html |
| ASYNCIFY_ONLY | A list of the only functions that can unwind the stack. Error-prone to maintain. **[V]** midzer uses `-sASYNCIFY_ONLY=@../funcs.txt` (43 functions). **[V]** | same, midzer `funcs.txt` |
| JSPI | Code size stays the same. Needs `JSPI_IMPORTS` / `JSPI_EXPORTS`. **[V]** Browser shipping status is **[U]**: a search snippet points at Chrome 137+, but the Chrome 137 blog URL returned 404 and the MDN compat data path was not reachable. | Emscripten asyncify docs; search snippets |
| Restructure the loop | `emscripten_set_main_loop(mainloop, 0, 1)` replaces `main()`'s loop. Requires rewriting all the `JE_*` blocking functions. Expensive. **[V]** | https://wiki.libsdl.org/SDL2/README-emscripten |
| Emterpreter | Old route; the docs give migration to `-sASYNCIFY`. **[V]** | Emscripten asyncify docs |

**Working reference build (midzer `emscripten` branch).** **[V]** from `EMSCRIPTEN.md` and `Makefile`:
- Build: `emmake make`, with `CC ?= emcc`, `CPPFLAGS += -sUSE_SDL=2`, `WITH_NETWORK := false`, `-O3 -flto -fno-rtti -fno-exceptions`.
- Link: `emcc ... -sUSE_SDL=2 -sASYNCIFY -sASYNCIFY_IGNORE_INDIRECT -sENVIRONMENT=web --preload-file data/ -sASYNCIFY_ONLY=@../funcs.txt -sSTACK_SIZE=131072 --closure 1`.
- `src/` is identical to midzer `master`. The only changes are in the Makefile and `EMSCRIPTEN.md`. (`diff -rq` on `src/` was empty.)

**Saves (IDBFS).**
- midzer's build has no IDBFS code. Config falls back to `opentyrian.cfg` in the working directory when `XDG_CONFIG_HOME`/`APPDATA` are unset (`src/file.c`). **[V]** In the browser this is MEMFS, so the save is probably lost on reload. **[U]** (inference, not run.)
- IDBFS: `FS.mkdir('/data'); FS.mount(IDBFS, {}, '/data');`, with `-lidbfs.js`. Populate with `FS.syncfs(true, cb)`, save with `FS.syncfs(false, cb)`. It is asynchronous. `autoPersist: true` is an option. **[V]** https://emscripten.org/docs/api_reference/Filesystem-API.html
- Alternatively, have the Flywheel bridge store saves, matching the plan's storage decision. **[U]** (design choice)

**Audio unlock.**
- SDL2's Emscripten audio uses a ScriptProcessorNode. While the AudioContext is `suspended`, the callback feeds silence. It resumes on `navigator.userActivation.hasBeenActive`. **[V]** https://github.com/libsdl-org/SDL/blob/SDL2/src/audio/emscripten/SDL_emscriptenaudio.c
- SDL wiki: "Modern web browsers will not permit web pages to produce sound before the user has interacted with them." Recommended: a "Click here to play!" splash. **[V]** https://wiki.libsdl.org/SDL2/README-emscripten
- OpenTyrian audio: `SDL_OpenAudioDevice` at 11025 × OUTPUT_QUALITY Hz, 256 × OUTPUT_QUALITY samples (about 23 ms). **[V]** `src/loudness.c`.
- Flywheel kiosk uses `--autoplay-policy=no-user-gesture-required` (PLAN.md §6). That should let the context start without a gesture. **[U]**, not tested.

**Pointer lock and fullscreen.**
- `emscripten_request_pointerlock()` and `emscripten_request_fullscreen()` may only be raised inside a user-generated event handler. Use `deferUntilInEventHandler=true` to defer. **[V]** https://emscripten.org/docs/api_reference/html5.h.html
- `pointerlockchange` and `fullscreenchange` callbacks are available. **[V]** The docs do not cover re-acquiring lock after loss. **[U]**

**Other facts.**
- SDL with Emscripten needs Emscripten ≥ 3.1.35. **[V]**
- Preload data with `--preload-file` for large data (about 12 MB here), and `--embed-file` only for small files. **[V]**
- Single-threaded build: no COOP/COEP headers needed. **[V]** (SDL wiki: threaded builds need them.) Good for a kiosk page.

---

## 5. Source list

- https://github.com/opentyrian/opentyrian (upstream; SDL2, GPL-2.0 COPYING, Makefile `-std=iso9899:1999`)
- https://github.com/midzer/opentyrian (branches `master`, `emscripten`, `sdl1`)
- https://github.com/midzer/opentyrian/blob/emscripten/EMSCRIPTEN.md
- https://midzer.de/wasm/opentyrian/
- https://www.neoteo.com/en/opentyrian-the-classic-vertical-scroller-right-in-your-browser
- https://osdn.net/projects/opentyrian-dll/scm/hg/opentyrian-dll (search snippet only)
- https://forums.libretro.com/t/are-reminiscence-flasback-port-and-opentyrian-still-in-development/25174
- https://github.com/libretro/libretro-super (`libretro-fetch.sh`)
- https://raw.githubusercontent.com/mdn/browser-compat-data/main/api/Bluetooth.json
- https://developer.mozilla.org/en-US/docs/Web/API/Bluetooth/getDevices
- https://github.com/WebBluetoothCG/web-bluetooth/blob/main/implementation-status.md
- https://developer.chrome.com/docs/capabilities/bluetooth (2015-dated)
- https://www.bluetooth.com/specifications/specs/fitness-machine-service-1-0/
- https://www.bluetooth.org/DocMan/handlers/DownloadDoc.ashx?doc_id=423422 (FTMS 1.0 PDF)
- https://github.com/dvmarinoff/Flux
- https://apps.apple.com/app/id1492822055 (Bluefy)
- https://learn.microsoft.com/en-us/deployedge/microsoft-edge-browser-policies/defaultwebbluetoothguardsetting
- https://lists.w3.org/Archives/Public/public-web-bluetooth/2020Jul/0000.html (search snippet)
- https://www.windowslatest.com/2025/08/10/microsoft-wont-end-support-for-chromium-edge-and-pwas-on-windows-10-until-october-2028/
- https://chromestatus.com/release-notes/159
- https://wiki.libsdl.org/SDL2/README-emscripten
- https://github.com/libsdl-org/SDL/blob/SDL2/src/audio/emscripten/SDL_emscriptenaudio.c
- https://emscripten.org/docs/porting/asyncify.html
- https://emscripten.org/docs/api_reference/Filesystem-API.html
- https://emscripten.org/docs/api_reference/html5.h.html
- https://developer.chrome.com/blog/webassembly-jspi-origin-trial (search snippet; the 137 blog URL 404'd)

---

## 6. Open items (to test on hardware)

1. Run the midzer `emscripten` build locally to confirm the save behaviour, frame rate, and audio unlock.
2. Test `navigator.bluetooth.getDevices()` on Chrome for Windows 10 (Surface Pro 3) without the flag.
3. Test Start/Resume (`0x07`) against the Q200B. Flux omits it and Volcade needed it.
4. Measure ASYNCIFY overhead on the Surface Pro 3 (target: 35 fps at 320x200).
5. Verify `WebBluetoothAllowedForUrls` and kiosk grant persistence.
6. Check the JSPI shipping status in Chrome on the Surface.
7. Confirm the freeware terms of Tyrian 2.1 data before any redistribution in a Flywheel build.
