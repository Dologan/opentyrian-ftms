# Route B spike: native BLE FTMS in OpenTyrian (no Python bridge)

Phase 0 viability study, throwaway spike. Timebox about 2 h. Nothing committed or pushed; `/home/user/flywheel` untouched.
Date of research: 2026-10-13. Everything marked **(verified)** was run in this Linux container; everything marked **(unverified)** is from docs or source reading only. There is no Bluetooth adapter, no BlueZ daemon and no Windows or macOS machine here.

Contents of this directory:

```
RESULTS.md
Makefile                    make test | make replay      (strict C99: -std=c99 -pedantic -Wall -Wextra -Werror)
src/ftms_parse.{h,c}        Indoor Bike Data + Cycling Power + Control Point + Feature decoders, C99, zero deps
src/simpleble_probe.c       adapter list / FTMS scan / optional connect+notify against SimpleBLE's C API
src/bluez_dlopen_probe.c    BlueZ over system D-Bus with libdbus-1 loaded by dlopen()
test/test_ftms_parse.c      runs every vector in flywheel's ftms-vectors.json + hand-built CPS/cadence/encoder cases
test/replay_raw.c           replays flywheel *.raw.jsonl through the C parser, diffs against Python-decoded samples
test/tinyjson.h             60-line JSON DOM reader (tests/tools only, not for the game)
```

## 1. Parser port: done, all vectors pass

Ported from `bridge/src/flywheel_bridge/ble/ftms.py`, `cps.py` (Cycling Power 0x2A63 fallback **does exist** in flywheel, including the crank-cadence derivation), `control_point.py`, plus Feature (0x2ACC) decode and the Set Target Resistance encoder.

Design choices:
- Fixed-point output as on the wire (`speed_centi_kmh`, `cadence_half_rpm`, `met_tenths`) plus a `present` bitmask, so there is no float-rounding disagreement and "absent" is distinguishable from a real zero (a vector covers this).
- Bit 0 "More Data" inversion handled like the Python: speed present when the bit is CLEAR.
- No cross-packet state in the decoder; fragment merging happens in the mailbox ("latest value per field"), as in the Python telemetry aggregator.
- Truncation returns `FTMS_ERR_SHORT` and zeroes the output; trailing bytes ignored. Sign handling of `sint16` avoids implementation-defined casts. No allocation, no globals, pure functions (thread-safe). Object code is 3.3 KB of text at -O2.

```
$ make test
cc -std=c99 -pedantic -Wall -Wextra -Werror -O2 -o build/test_ftms_parse test/test_ftms_parse.c src/ftms_parse.c -lm
build/test_ftms_parse /home/user/flywheel/shared/fixtures/ftms-vectors.json
indoorBikeData: 18 vectors
controlPointResponse: 6 vectors
338 checks, 0 failures
```

Extra checks beyond the shared vectors: 4 Cycling Power cases (crank, balance-byte skipping, wheel+crank, truncation), crank cadence incl. uint16 wraparound and the 3 s stall-to-zero rule, the resistance encoder (rounding, clamping, NaN), Feature decode with the real bike's `0x5287 / 0x0c`.
Also verified: clean under `-fsanitize=address,undefined` (only complaint was intentional leaks in the test JSON reader), and a mutation test (flip the inverted-bit check) fails 53 of 338 checks, so the tests do bite.

### Replay of recorded rides: byte-identical to the Python decoder

flywheel raw log format (`bridge/src/flywheel_bridge/rawlog.py`): one JSON object per line, `t` ms monotonic, `wall` epoch ms; notification lines `{"char":"2ad2","hex":"..."}`; event lines `{"event":"scan"|"device"|"phase"|"handshake",...}`. Lines for vendor characteristics (the bike's 0001020304... service) and 2ad9/2ada are present and ignored. Malformed lines are skipped. `*.samples.json` is the Python decoder's per-packet output (`t, cadenceRpm, powerW, resistance`).

```
$ build/replay_raw .../q200b-run1.raw.jsonl 20 .../q200b-run1.samples.json
    t(s)  watts  cad_rpm   res
   17.45    185     58.0     6
   38.31    220     62.0     6
   59.16     15     95.0     0
  ...
  246.75    143     37.0     7
-- 217 Indoor Bike Data packets (1186 other lines skipped), 0 decode errors
-- compared with 217 Python-decoded samples: 0 mismatches => IDENTICAL

$ build/replay_raw .../q200b-run2.raw.jsonl 60 .../q200b-run2.samples.json | tail -2
-- 310 Indoor Bike Data packets (1701 other lines skipped), 0 decode errors
-- compared with 310 Python-decoded samples: 0 mismatches => IDENTICAL
```

So 527 real packets from a real bike (XiaoQiao Q200B, notifies at about 0.9 Hz) decode identically in C and Python. The replay tool also demonstrates the "latest-sample" merge the mailbox needs.

## 2. BLE library evaluation

### 2.1 SimpleBLE licensing, as found in the git history (verified by reading `LICENSE.md` at each tag)

| Version (date) | License | Usable for a GPL-2.0-or-later game? |
|---|---|---|
| v0.1.0 to v0.6.1 (to 2023-03) | **MIT** | Yes, no strings |
| commits up to `6b83bea` (2023-11-30), untagged, between 0.6.1 and 0.7.0 | **MIT** (LICENSE.md still MIT; includes fix #275 "advertised services on BlueZ uses UUIDs") | Yes. This is the **last MIT snapshot** |
| v0.7.0, v0.7.1 (2024-02-16) | Confusing mix: "BSD-3 for non-commercial/open-source ... GPLv3 for commercial" | Avoid (ambiguous) |
| v0.7.2/0.7.3 to v0.8.1 (2024-03 to 2024-11) | **GPLv3** or paid commercial license (dual) | Legally workable: GPL-2.0-or-later + GPLv3 combine into a GPLv3 binary. Loses "GPL-2.0-only" downstream and needs the whole distributed work relabelled GPLv3 |
| v0.9.0 (2025-01-20) to **v1.2.x (current)** | **Business Source License 1.1**. Additional Use Grant: production use only if "Non-Commercial"; Change License GPL-3.0, change date = 4 years after each version's first release (so v0.9.0 converts in Jan 2029; nothing converted yet) | **No.** BUSL is not an open-source license; its field-of-use restriction is a "further restriction" incompatible with GPL. Even though "open-source projects that generate no revenue" are listed as non-commercial examples, downstream redistributors of a GPL game would not be bound by that grant and GPL forbids imposing it |

Source: `git show <tag>:LICENSE.md` in a clone of OpenBluetoothToolbox/SimpleBLE; docs.simpleble.org/licensing says the BUSL switch was 2025-01-20. The 2026-10-12 commits restructure LICENSE.md into the standard BUSL layout, they do not loosen it.

Implication: the only clean options are (a) vendor an MIT-era snapshot (`6b83bea` or v0.6.1) and own it as a fork, or (b) pay/ask for a commercial licence (pointless for a GPL project). I do not recommend building a GPL game on a library whose maintained line is source-available-only.

Consequences of the old MIT snapshot I measured in the git log: v0.6.1 lacks "advertised services on BlueZ use UUIDs (#275)" (needed to filter on 0x1826 at scan time on Linux; present at `6b83bea`), and the macOS fix "notifying from standard (16-bit) UUIDs (#326)" landed only in 2024-07, i.e. under GPLv3. 0x2AD2 is exactly a standard UUID, so a macOS notify bug is likely in any MIT snapshot (unverified, no Mac).

### 2.2 Comparison table

| Candidate | Platforms | License / GPL-2.0-or-later compat | Maintenance (as of 2026-10) | Build / link implications for OpenTyrian | Effort to integrate |
|---|---|---|---|---|---|
| **SimpleBLE, MIT snapshot** (`6b83bea`) | Win10+, Linux (BlueZ), macOS 10.15+, iOS, Android. C API `simpleble_c/*.h` (adapter scan, connect, notify, write) | MIT: fine | Frozen in 2023 for us; upstream moved to BUSL and has 279 commits since 2026-04, none usable | **C++ library.** Needs CMake and a C++ compiler, libstdc++ (static OK), fmt (vendored). Linux backend = bundled SimpleDBus+SimpleBluez over **libdbus-1** (links `-ldbus-1`, which drags libsystemd, libcap, libgcrypt ...). Windows backend = **C++/WinRT** (`#include <winrt/...>`), MSVC officially; MSYS2 now ships `mingw-w64-{ucrt-x86_64,clang-aarch64,...}-cppwinrt` 3.0.260818.1 so GCC/clang may work **(unverified)**. macOS = ObjC++ (.mm), CoreBluetooth. Backends are about 7.0k lines (win 3.0k, linux 1.5k, mac 2.9k) + SimpleBluez/SimpleDBus 6.7k | Lowest code (about 250 lines of C shim, `src/simpleble_probe.c` is the seed), highest build-system and ownership cost |
| **btleplug** (Rust) | Win10, Linux (BlueZ via `dbus` crate + bluez-async), macOS >= 10.15 (CoreBluetooth via objc2), iOS, Android | BSD-3-Clause (LICENSE.md) / "MIT/Apache-2.0/BSD-3-Clause" (Cargo): fine, and all Rust deps are permissive | **Very active**: v0.13.5 tagged 2026-10-10, 73 commits since 2025-10 | **No C API exists**; we write one (about 150 to 250 lines Rust). I built it (below): a 9.4 MB `.a` that links to 787 KB stripped incl. tokio, panic=abort, LTO, opt-s. Needs Rust toolchain on every CI target; Windows needs a UCRT-compatible Rust target for the UCRT64 job and a gnullvm one for CLANGARM64 (MSYS2 has rust packages for both, **unverified** that the C-linking works). Linux still links libdbus-1 dynamically unless the `dbus` crate's `vendored` feature is used. macOS has no MAC address (feature table blank) | Medium: small Rust glue, but a second language and toolchain in a pure-C99 project |
| **gattlib** | **Linux only** (BlueZ over GDBus/glib; also an old BlueZ4 path) | `BSD-3-Clause OR GPL-2.0-or-later` (SPDX in CMakeLists/headers): fine | Stale: last commit 2024-07-17, last tag 0.7.2 | Needs glib/gio and `bluetooth/*.h` headers; much heavier than needed and shared-lib heavy: worst fit for the self-contained Linux binary | Useless for the primary target (Windows) |
| **BlueZ D-Bus directly** (libdbus-1 via **dlopen**, or sd-bus) | Linux only | libdbus is AFL/GPL-2.0+ dual and loaded at run time; BlueZ D-Bus API is an IPC interface, not linked code: fine | BlueZ GATT D-Bus API (`org.bluez.GattCharacteristic1`) has been stable since 5.x | **No new link dependency at all**: `src/bluez_dlopen_probe.c` builds with headers only and has `ldd` = libc only (verified). Missing libdbus or bluetoothd degrades to a message, the game still runs. This is the same pattern SDL uses and what keeps the "bare debian:bookworm-slim runs `opentyrian --help`" CI step green | About 700 to 900 lines C |
| **WinRT** `Windows.Devices.Bluetooth` | Windows 10+, x64 and ARM64 | OS API: fine (system-library exception) | Microsoft | **No C projection.** mingw-w64 11 headers contain `windows.devices.enumeration.h` but **no** `windows.devices.bluetooth*.h` (verified by unpacking `mingw-w64-x86-64-dev`). So the realistic route is a **small C++ file** using C++/WinRT, exposed through a C header, compiled with MSYS2 `cppwinrt` (package exists for ucrt64 and clangarm64: the two CI configs) or MSVC (the repo already has a `visualc/` solution). Compiling Windows.Devices.Bluetooth with GCC is **unverified**: this is Phase 1 spike #1 | About 600 to 800 lines C++ |
| **Win32 `BluetoothGATT*` API** (bluetoothleapis.h, bthledef.h) | Windows 8+ | OS API | Legacy but stable | Plain C and MinGW-friendly (`-lbluetoothapis`/`BluetoothApis.dll`), but mingw-w64 11 has no `bluetoothleapis.h` (declare by hand) and as I understand it works only for **already paired** devices found via SetupAPI, with no advertisement scanning **(unverified)**: acceptable only as a fallback ("pair the bike in Windows Settings first") | About 500 lines C |
| **CoreBluetooth** | macOS 10.13+/iOS | OS API | Apple | One `.m` file, `-framework CoreBluetooth -framework Foundation`, compiled by the existing `make_macos.sh` clang. Needs `NSBluetoothAlwaysUsageDescription` in the generated Info.plist (the script writes one already, key must be added) or the process is killed on first use. No MAC addresses, only a per-machine peripheral UUID | About 400 to 500 lines ObjC |

### 2.3 Hands-on: SimpleBLE (MIT snapshots) on Linux (verified)

Commands (scratch `routeb/`; both v0.6.1 and `6b83bea` built the same way):

```
git clone --no-single-branch --filter=blob:none https://github.com/OpenBluetoothToolbox/SimpleBLE.git
git worktree add ../sb-mit-last 6b83bea
cmake -S simpleble -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$PWD/../inst
cmake --build build -j8        # 40 s wall on this box; libfmt vendored header-only; needs libdbus-1-dev only
cmake --install build          # libsimpleble.a 3.6 MB, libsimpleble-c.a 110 KB
gcc -std=c99 -pedantic -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -I inst/include -I src -c src/simpleble_probe.c
g++ -o simpleble_probe probe.o ftms_parse.o libsimpleble-c.a libsimpleble.a -static-libstdc++ -static-libgcc -ldbus-1 -lpthread
```

Note: pure-C consumers still link with `g++` (or add `-lstdc++`); OpenTyrian's Makefile is `CC ?= gcc` C-only, so it would need a C++ link step.

Run with no adapter and no system bus (this container):

```
$ ./simpleble_probe 2000
no Bluetooth adapter found (expected inside this container)
bluetooth enabled: no
adapters: 0
exit=1
```
No crash or exception with D-Bus absent: SimpleBLE reports zero adapters, which the game can show as "no Bluetooth adapter". Scan, connect and notify paths are **compile-checked only**.

Sizes (stripped, x86-64, probe program including the 3 KB parser):

| Variant | Size | Runtime `ldd` |
|---|---|---|
| trivial C `hello` | 14 KB | libc |
| SimpleBLE probe, dynamic libstdc++ | 0.90 MB | libdbus-1, libstdc++, libgcc_s, libc, libsystemd, libcap, libgcrypt, liblz4, liblzma, libzstd, libgpg-error |
| + `-static-libstdc++ -static-libgcc` | 2.0 MB | libdbus-1 + its systemd chain + libc |
| + libdbus-1.a static | 2.26 MB | still **libsystemd.so.0** chain (Debian's libdbus-1.a references `sd_listen_fds`; no `libsystemd.a` shipped) |
| fully `-static` | link fails | undefined `sd_listen_fds`, `sd_is_socket` |

Binary size impact on top of OpenTyrian: **+0.9 to +2.3 MB** (OpenTyrian release is on the order of 0.6 MB, so it roughly 3 to 4x's the binary).

CI impact (read `.github/workflows/linux.yml` and `make_linux.sh`):
- `make_linux.sh` itself only fails if `ldd opentyrian | grep libSDL2`. Linking libdbus-1/libstdc++ dynamically does **not** trip that.
- **But** the workflow then runs the packaged binary in a bare `debian:bookworm-slim` container ("Verify the package is self-contained") with `opentyrian --help`. A binary with `NEEDED libdbus-1.so.3` fails to start there (bookworm-slim does not ship libdbus-1; **expected, docker is not available here to confirm**). So SimpleBLE on Linux needs either libdbus built from source without systemd and linked statically (like SDL2 is today, doable: libdbus is autotools/cmake, about 1.5 MB) or a patch to load it with dlopen. The dlopen approach, proven in `bluez_dlopen_probe.c`, makes the check pass with no change.
- Also needs `g++` and cmake in the Linux/Windows/macOS jobs, plus `libdbus-1-dev` which the Linux job already installs.

### 2.4 Hands-on: btleplug as C static library on Linux (verified)

A 25-line `staticlib` crate exposing `int btle_probe(int scan_ms)` (Manager + adapters + optional FTMS-filtered scan), built with `opt-level="s"`, LTO, `panic=abort`, `strip`:

```
$ cargo build --release            # 9 s incremental after deps (about 45 s cold), crates fetched via the proxy
-rw-r--r-- libbtle_ffi.a  9,360,378
$ gcc -o t t.c libbtle_ffi.a -ldbus-1 -lpthread -ldl -lm ; strip -> 787,264 bytes
$ ./t
btleplug Manager::new: Failed to connect to socket /run/dbus/system_bus_socket: No such file or directory
btle_probe -> -1
```
Clean error return, no panic. NEEDED: libdbus-1.so.3 (+ systemd chain), libm, libgcc_s, libc. Same dynamic-libdbus problem as SimpleBLE, same fixes (`dbus` crate `vendored` feature, or accept). 0.8 MB is smaller than SimpleBLE with static libstdc++. Every platform build then needs `cargo`, plus `lipo` of two targets on macOS, plus vetting the transitive crates (about 100) for GPL-compat (all MIT/Apache/BSD in practice, not audited).

### 2.5 Hands-on: BlueZ over D-Bus with dlopen'd libdbus (verified)

```
$ gcc -std=gnu99 -Wall -Wextra -Werror $(pkg-config --cflags dbus-1) -o bluez_probe src/bluez_dlopen_probe.c -ldl
$ ldd bluez_probe        -> linux-vdso, libc.so.6, ld-linux     (nothing else)
$ ./bluez_probe
libdbus-1 loaded via dlopen
BLE unavailable: cannot reach system D-Bus: Failed to connect to socket /run/dbus/system_bus_socket: No such file or directory
$ ls -la bluez_probe     -> 16,920 bytes
```
(`-std=gnu99` rather than `c99` only because POSIX `dlsym` returns `void*` assigned to function pointers; the rest of the code is strict C99.) Three distinct failure messages are available (no libdbus, no bus, no bluetoothd). Not tested against a real bluetoothd, so the GATT part (`StartDiscovery` with UUID filter, `Device1.Connect`, `GattCharacteristic1.StartNotify` plus `PropertiesChanged` on `Value`, or `AcquireNotify`) is estimated, not run.

## 3. Native architecture sketch

### 3.1 Layers

```
 game loop (single thread, SDL)                      BLE worker thread (one per backend)
 ┌──────────────────────────┐                       ┌─────────────────────────────────────────┐
 │ bike_poll(&view) / frame │◄── mailbox ───────────│ backend callbacks (library/OS threads)  │
 │ bike_cmd(CONNECT, id)    │── command queue ─────►│ link state machine (ble_link.c)         │
 │ setup-menu device list   │◄── device list ───────│ ftms_parse.c (pure)                     │
 └──────────────────────────┘                       └─────────────────────────────────────────┘
                                                       ble_backend.h  (vtable: scan, connect,
                                                       discover, subscribe, write, close)
                                                       ├ backend_bluez.c   (dlopen libdbus)
                                                       ├ backend_winrt.cpp (C++/WinRT, MTA thread)
                                                       ├ backend_corebt.m  (CoreBluetooth)
                                                       └ backend_replay.c  (test: raw.jsonl + mock clock)
```
`ble_backend.h` is a plain C vtable so that (a) the platform code is isolated, (b) the state machine is testable with a fake backend, (c) a SimpleBLE or btleplug backend could be dropped in later without touching the rest.

### 3.2 Thread-safe latest-sample mailbox

BLE callbacks run on library/OS threads (WinRT thread pool, CoreBluetooth dispatch queue, D-Bus loop thread). OpenTyrian is single threaded, and I found no `SDL_Thread`/pthread use in `src/`. Callbacks must never block and never touch game state.

- Callback: `ftms_parse_indoor_bike_data()` (pure), then under a small `SDL_mutex` (or a seqlock on `SDL_atomic_t`) merge into `bike_latest`: per-field value + per-field `arrival_ms` (monotonic), `seq` counter, `packets`. Merge, not overwrite, because of More-Data fragments (replay tool shows the logic).
- Game frame: `bike_poll()` copies the struct (about 64 bytes) under the same lock. Game derives "pedalling" from `age(power) < ~3 s` and treats a silent bike as stopped, not as sensor loss, exactly like the Python bridge (some bikes stop notifying when the flywheel stops).
- Same mutex guards a status record `{state, message[96], device_name[64], notify_hz}` and a small device list (max 16) written by scan callbacks and read by the setup menu.
- Commands (connect to id X, rescan, disconnect, set resistance) go game to worker through a tiny ring queue; the worker owns all backend calls.

### 3.3 Connection state machine (mirror of `link.py`)

`IDLE -> SCANNING -> [SELECT] -> CONNECTING -> DISCOVERING -> SUBSCRIBING -> HANDSHAKING -> STREAMING -> RECONNECTING -> SCANNING ...`, `ERROR` with a human hint.

1. **Scan** filtered on service UUID 0x1826 (also accept an advertisement-less match on name for bikes that omit it, as in `matches()`), 10 s timeout. If a remembered device id exists, auto-select it; otherwise publish the list and wait for the menu.
2. **Connect** (20 s timeout). Windows: `BluetoothLEDevice::FromBluetoothAddressAsync`; macOS: retained `CBPeripheral`; Linux: `Device1.Connect`.
3. **Discover** with the cache disabled on Windows (`BluetoothCacheMode::Uncached`, same lesson as `use_cached_services=False` in `link.py`). Pick 0x2AD2; else Cycling Power 0x2A63 (then power only, cadence via `cps_crank_cadence`). Neither: ERROR "Connected, but no Indoor Bike Data or Cycling Power".
4. **Subscribe** CCCD on 0x2AD2. Optional informational reads (0x2ACC, ranges, device info) never fail the link.
5. **Handshake (best effort)**: subscribe 0x2AD9 indications, write Request Control (`00`), wait 3 s for `80 00 01`, write Start/Resume (`07`). Any refusal or timeout is logged and ignored (some bikes stream only after this, most do not).
6. **Streaming** watchdog: if silent more than 10 s, repeat the handshake once; silence alone never drops the link.
7. **Disconnect** event goes to RECONNECTING: backoff 1 s doubling to 30 s, re-scan for the *same* id, reset on reaching STREAMING. A bike held by a phone app is simply not found: show the same hint text as `NOT_FOUND_HINT`.

### 3.4 Device-selection UI needs (inside OpenTyrian's menu system)

- A "Bike" entry in the setup menu: status line, list of scanned devices (name, RSSI bars, `[FTMS]` badge), Rescan, Forget, Disconnect. Keyboard/gamepad navigation only, existing font/menu helpers.
- Persist the chosen device id and a "bike enabled" flag in the existing config file. The id is a **string whose meaning is per-platform** (MAC on Windows/Linux, a UUID on macOS), so never display it as a MAC.
- In-game HUD: small status glyph and the live watts/rpm; falling back cleanly to keyboard/mouse control when the bike is absent. Auto-connect on launch when enabled and remembered.
- The game must start and run with BLE entirely unavailable (no adapter, no libdbus, Bluetooth off).

### 3.5 LOC estimate (C unless stated; includes comments, excludes tests)

| Piece | LOC | Status |
|---|---|---|
| `ftms_parse.{h,c}` | 320 | **done** (this spike) |
| `ble_backend.h` + `ble_link.c` (state machine, backoff, handshake, mailbox, command queue, config glue) | 450 to 600 | to do |
| Menu/HUD UI + config persistence | 250 to 350 | to do |
| **Linux backend** (dlopen libdbus, ObjectManager walk, signal matches, discovery filter, connect, notify, write) | 700 to 900 | to do |
| **Windows backend** (C++/WinRT, dedicated MTA thread, `BluetoothLEAdvertisementWatcher`, `GetGattServicesForUuidAsync`, `ValueChanged`, CCCD write, `ConnectionStatusChanged`, C ABI header) | 600 to 800 C++ | to do |
| **macOS backend** (CoreBluetooth delegate, ObjC) | 400 to 500 ObjC | to do |
| Replay/fake backend + tests (parser tests 172 + 101 + 135 JSON are done) | 300 more | partly done |
| Build glue (Makefile, `make_linux.sh`, `windows.yml`, `make_macos.sh`, `visualc` project, license files) | 100 to 200 | to do |
| **Total new code** | about 3,200 to 4,000 | (about 600 written) |

Person-time (single dev who knows the code, with hardware): common layer 4 to 6 days; Windows 3 to 5; Linux 3 to 4; macOS 2 to 3 (and a Mac to test on). So about 1.5 weeks for the Windows + Linux slice, about 3 weeks all in. Using a pinned SimpleBLE snapshot instead would cut backend code to about 250 lines but moves the cost to build integration, a fork to maintain, and the Linux dynamic-libdbus problem.

### 3.6 Testing story (no BLE in CI)

1. **Parser unit tests** (done): shared vectors file is the single source of truth with the Python and TypeScript sides; 338 checks, run in CI as `make test` in well under a second. The JSON path is an argument so the game repo can vendor a copy of `ftms-vectors.json` (or fetch it) without depending on the flywheel checkout.
2. **Replay tests** (done for the decoder): `replay_raw` feeds `*.raw.jsonl` and diffs against the Python decoder's output: 527 real packets identical. Vendor the two rides (a few hundred KB) as golden fixtures.
3. **State-machine tests** (to do): `backend_replay.c` implements `ble_backend.h` by emitting scan results, connect success/failure, notifications from a `.raw.jsonl` and disconnects on a mock clock, to assert: mailbox merge, silence-not-disconnect, handshake retry, backoff sequence 1,2,4,8,16,30,30, "device not found" hint, wrong-service ERROR. Pure C, runs in CI on all three OSes.
4. **Real-hardware checklist** (manual, per platform, per release): pairing-free connect, kill bike power mid-ride, walk away and back, second app already connected, Bluetooth toggled off/on, sleep/wake.
5. Stretch only: BlueZ `btvirt` plus a scripted peripheral on a Linux runner. Not worth it in Phase 1.

## 4. Recommendation for route B

**Go, with a native, thin, per-platform design behind a small C vtable; do not adopt a third-party BLE library.**

Reasons:
1. **Licensing.** SimpleBLE's maintained line (v0.9.0 to now) is BUSL-1.1, which cannot be combined into a GPL distribution. Only a 2023 MIT snapshot is usable, and it has known Linux/macOS FTMS-relevant bugs fixed later under other licenses.
2. **The job is tiny.** One service (0x1826), one notify characteristic, one optional 3-byte handshake. Generic libraries buy scan/pair/descriptor machinery we never use, at the cost of C++/Rust toolchains, +0.8 to 2.3 MB and (on Linux) a dynamic libdbus chain that breaks the project's own "runs in bare debian:bookworm-slim" CI gate.
3. **Linux is already proven** to be achievable with zero link deps (dlopen libdbus, 17 KB probe).
4. The parser, mailbox, state machine and replay tests, which are most of the logic and where the bugs will be, are platform-independent C99 and are already partly written and verified.

Proposed Phase 1 order (de-risk the hard bit first):
1. **Windows spike S1 (1 day, needs a Windows box or the GitHub `windows-latest` + `windows-11-arm` runners)**: in MSYS2 UCRT64 and CLANGARM64 `pacman -S mingw-w64-*-cppwinrt`, compile a 40-line C++/WinRT `BluetoothLEAdvertisementWatcher` scan and link into a C `main`. This is the single biggest unknown; if GCC cannot compile Windows.Devices.Bluetooth, fall back in this order: (a) compile only that one `.cpp` with clang from MSYS2 CLANG64/CLANGARM64, (b) MSVC for the Windows release build (repo already has `visualc/`), (c) Win32 `BluetoothGATT*` for paired devices.
2. Then the common layer + replay backend (CI-green without any BLE), then Windows backend on the Surface, then Linux, then macOS.
3. Escape hatch if schedule slips: a SimpleBLE `6b83bea` backend implementing the same vtable, built only on demand, disclosed as a stale MIT fork.

## 5. Risks

**Windows (primary target)**
- C++/WinRT under MinGW is the main uncertainty (see S1). MSYS2 packages exist for the exact two CI environments, which is encouraging, but nothing here proves `Windows.Devices.Bluetooth` headers generate and compile with GCC.
- The Surface may be ARM64 (CLANGARM64 CI job exists): the backend must build and be tested on both.
- COM apartment: SDL (from memory, not checked) initialises COM single-threaded on the thread that calls SDL_Init; WinRT `.get()` on an STA can throw or deadlock. Run all WinRT BLE calls on a dedicated MTA thread (SimpleBLE has `MtaManager` for exactly this reason).
- **GATT cache**: Windows caches the service table per device. A stale cache after bike firmware change makes characteristics vanish. Always use `BluetoothCacheMode::Uncached` (flywheel's `use_cached_services=False`, and SimpleBLE does the same).
- Pairing: FTMS bikes normally need no bonding and `FromBluetoothAddressAsync` works on unpaired devices once advertised. Devices previously paired through Windows Settings, or held by Zwift/phone apps, appear "connected" to another central and then stop advertising: surface it as "not found" with the existing hint. Pairing dialogs can pop up for bikes that request security. Unpackaged Win32 apps should need no manifest capability for BLE (unverified).
- Surface radios share Wi-Fi and can drop on modern standby; reconnect logic must be robust, and the 1 s notify rate of cheap bikes is the floor for latency.

**Linux**
- Needs `bluetoothd` running, adapter powered (some distros default `Powered=false`), BlueZ >= 5.50 for dependable GATT notify; user must be allowed on the D-Bus policy (active session user works on most desktops; headless/SSH needs the `bluetooth` group). Flatpak/Snap sandboxes block it.
- BlueZ caches `Device1` objects and GATT databases across runs (same stale-table class of bug); remove and re-discover on service-count change.
- libdbus runtime dependency must be dlopen'd to keep the self-contained gate green; arm64 job uses the same code.

**macOS (nice-to-have)**
- CoreBluetooth never exposes a MAC address: devices are identified by a per-machine peripheral UUID (not portable, can change after Bluetooth reset). Saved config must be platform-tagged.
- `NSBluetoothAlwaysUsageDescription` missing from the `.app` Info.plist means the OS kills the process at first use; TCC consent prompt appears once. Running the bare binary from a terminal needs the terminal app to have permission.
- Likely 16-bit-UUID notify pitfalls (the SimpleBLE bug above): use the full 128-bit form.

**Cross-cutting**
- GPL: nothing new linked; WinRT/CoreBluetooth/D-Bus are OS interfaces. If a third-party library is ever added, check `LICENSE` per version (SimpleBLE precedent).
- Quirks seen in the recordings: the test bike notifies at only about 0.9 Hz, and the same GATT carries a vendor characteristic (1074 of 1403 lines in run1 are vendor `...2b10` notifications that must be ignored).
- Testing without hardware only covers decode and logic; platform backends need manual hardware runs each release.

## 6. Commands to reproduce

```
cd /home/user/opentyrian-ftms/doc/ftms/phase0-spikes/route-b
make test        # 338 checks
make replay      # ride replay, 10 s print interval
make build/replay_raw && build/replay_raw /home/user/flywheel/shared/fixtures/rides/q200b-run2.raw.jsonl 30 /home/user/flywheel/shared/fixtures/rides/q200b-run2.samples.json
```
SimpleBLE / btleplug / BlueZ probe builds are described in sections 2.3 to 2.5 (their build trees live in the session scratchpad, not here).
