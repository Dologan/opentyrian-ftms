# Phase 0 research: BLE and sidecar options for FTMS input

Date of research: 2026-10-13. Scope: Route C (ship the Python bridge as a sidecar) and Route B (native BLE libraries callable from C). Sources are URLs with the date they were read, or the local file they came from. "Verified" means the claim was seen in a fetched or local source during this pass. "Unverified" means it is general knowledge or a source could not be read (503/404 or a page that failed to render).

## Summary

- Route C is buildable today. A one-file PyInstaller build of the bridge (`run --source sim`) produced a 14 MB Linux executable that serves `/healthz` on 127.0.0.1:8787 and exits cleanly on SIGTERM. A one-dir build is 34 MB and starts faster. The Windows/WinRT freeze path was not built (no Windows host here).
- bleak 3.0.2 on Windows does not use `bleak-winrt`. It depends on the `winrt-*` PyWinRT projection packages (3.2.1 in the lock). Those are the packages PyInstaller has to collect on Windows.
- SimpleBLE is not a GPL-2.0-or-later-compatible choice for distribution as things stand. Since 2025-01-20 it is under BUSL-1.1 with a GPL-3.0 change license after four years per version. The earlier LICENSE.md I could read (v0.7.0) is a BSD-3-Clause / GPLv3 dual license, not MIT.
- SDL2 and SDL3 do not provide a BLE GATT API. SDL3 has `SDL_CreateProcess` (since 3.2.0). SDL2 does not appear to have it.
- For a native C build, the direct options are WinRT via C++/WinRT on Windows, BlueZ D-Bus on Linux, and an Objective-C shim over CoreBluetooth on macOS. SimpleBLE and btleplug each cover all three but add license or toolchain costs.

---

## A. Route C: Python bridge as a sidecar executable

### A1. Packaging tools

| Tool | Latest seen | License | Python support | Notes | Source (read date) |
|---|---|---|---|---|---|
| PyInstaller | 6.22.3, released 2026-09-12 | GPLv2-or-later with a special exception that allows distributing non-free programs built with it (verified in local `pyinstaller-6.22.3.dist-info/METADATA`) | 3.8 to 3.15 (PyPI text) | Bootloader and output: the COPYING file grants "unlimited permission to link or embed compiled bootloader and related files" (verified). | https://pypi.org/project/pyinstaller/ (2026-10-13); https://raw.githubusercontent.com/pyinstaller/pyinstaller/develop/COPYING.txt (2026-10-13) |
| Nuitka | 4.3, released 2026-10-10 | AGPLv3-or-later for Nuitka itself | 2.6, 2.7, 3.4 to 3.14 (PyPI) | Output license: the handbook says the AGPL does not extend to produced apps (secondary source, unverified against Nuitka's own text). The homepage says "Apache", which conflicts with the PyPI metadata. | https://pypi.org/project/Nuitka/ (2026-10-13); https://pydevtools.com/handbook/reference/nuitka/ (secondary); https://nuitka.net/doc/commercial-license (2026-10-13) |
| PyOxidizer | Not confirmed | MPL-2.0 (general knowledge, unverified) | n/a | Search returned only pre-2025 docs (0.1.0 through 0.17 index). Current maintenance could not be confirmed. Not recommended without checking the GitHub activity. | https://gregoryszorc.com/docs/pyoxidizer/0.1.0/status.html (old) |

Sizes and startup were measured for PyInstaller only (see A3). Nuitka sizes are conflicting in the sources:

- The pistack.xyz test (2026-09-10, single script, Linux) gave 11.4 MiB for PyInstaller onefile and 35.6 MiB for Nuitka onefile, with startup about 150 ms and 50 ms. Source: https://www.pistack.xyz/posts/2026-09-10-python-executable-packaging-pyinstaller-nuitka-cxfreeze-comparison/
- Other sources disagree on the direction. Build times are consistently faster with PyInstaller (30 to 60 s versus 5 to 15 min for a medium app, secondary). Source: https://beatsyncpro.ai/blog/nuitka-vs-pyinstaller (2026, secondary).

### A2. Dependency facts for the bridge (from the repo, not PyPI)

Source of the table is `/home/user/flywheel/bridge/uv.lock` and `pyproject.toml` (read 2026-10-13). The project requires Python `>=3.11`.

| Package | Locked version | License | Platform | Notes |
|---|---|---|---|---|
| bleak | 3.0.2 (PyPI: 2026-05-02) | MIT (verified in `bleak-3.0.2.dist-info/METADATA`) | all | Requires Python 3.10+ (PyPI). |
| winrt-runtime | 3.2.1 | MIT (PyPI page, read 2026-10-13) | Windows | PyWinRT runtime. |
| winrt-windows-devices-bluetooth, -advertisement, -genericattributeprofile, -enumeration, -radios, -foundation, -foundation-collections, -storage-streams | 3.2.1 | MIT (presumed same project; per-package license not individually read) | Windows | Eight projection packages pulled by bleak on `win32`. **bleak-winrt is not in the lock.** Bleak 3.x uses these directly. |
| dbus-fast | 5.2.0 | MIT (verified in local METADATA) | Linux | BlueZ D-Bus client. |
| pyobjc-core, pyobjc-framework-corebluetooth, pyobjc-framework-libdispatch | in lock | MIT (pyobjc, general knowledge, unverified) | macOS | CoreBluetooth bindings. |
| aiohttp | 3.14.4 (sdist 2026-10-05) | Apache-2.0 AND MIT (vendored llhttp) (verified in local METADATA) | all | Transitive: aiohappyeyeballs, aiosignal, attrs, frozenlist, multidict, propcache, yarl. |

bleak 3.0.x release notes (https://github.com/hbldh/bleak/releases, read 2026-10-13): 3.0.0 on 2026-03-22 (breaking: GATT errors now `BleakGATTProtocolError`; `adapter` kwarg deprecated; CCCD writes raise `ValueError`), 3.0.1 on 2026-03-25, 3.0.2 on 2026-05-02. No WinRT or dependency changes are listed for 3.0.x. Platform minimums per PyPI: Windows 11 build 22000+, BlueZ 5.55+, macOS 10.15+.

### A3. Measured build (Linux, scratch copy)

Method: copy of the bridge at `/tmp/claude-0/-home-user/2934c9fe-a343-5108-8d9a-4c6da189a80a/scratchpad/bridge-freeze`, `uv sync` (uv 0.11.32, Python 3.13.16), `uv pip install pyinstaller` (6.22.3), launcher calling `flywheel_bridge.__main__.main`. Build commands: `pyinstaller --onefile` and `pyinstaller --onedir`, both with default options (no hidden-import or collect flags).

| Mode | Size | Time to `/healthz` 200 (2 runs) | Result |
|---|---|---|---|
| one-file | 14.3 MB (single binary) | 956 ms, 1203 ms | `{"ok": true, "version": "0.1.0", "protocol": 1, "source": "sim"}`; process exited on SIGTERM with log "Shutting down"; port 8787 closed afterwards |
| one-dir | 34 MB (folder) | 633 ms, 530 ms | same |

- Build time: about 26 s for one-file.
- Environment: this is Linux only and the sim source. The bleak import path on Linux loads `dbus-fast`, so the BLE stack is partly exercised, but no adapter was present and no scan ran.
- Not measured: a Windows build, the `winrt-*` collection, a macOS build, `--source ftms` with a real bike, and the Windows AV/SmartScreen behavior.
- Side effect to note: an early `uv sync` created a `.venv` directory inside the original `/home/user/flywheel/bridge`. I removed it. `pyproject.toml` and `uv.lock` in that directory are byte-identical to the scratch copy.

### A4. Freezing bleak on Windows (what to expect)

| Issue | Status | Source |
|---|---|---|
| bleak 3.x imports `winrt.*` modules that PyInstaller's static analysis may not see | Unverified in a Windows build. The usual fix is `--collect-submodules winrt` or `--collect-all winrt`, or hidden imports. | PyInstaller docs say hidden imports occur with dynamic imports and recommend `--debug=imports` to find them: https://pyinstaller.org/en/stable/when-things-go-wrong.html (2026-10-13). The `winrt` specifics are general knowledge. |
| Importing `pythoncom` (pywin32) switches the thread to STA and async WinRT calls never complete in a console app | Documented by bleak. If pywin32 is pulled into the freeze, set `sys.coinit_flags = 0` before the import. | https://bleak.readthedocs.io/en/latest/troubleshooting.html (2026-10-13) |
| Onefile `multiprocessing` re-spawn loops in frozen apps | Reported in a PyInstaller discussion. The bridge does not use multiprocessing, so this is a precaution. | https://github.com/orgs/pyinstaller/discussions/8395 (search snippet) |
| Onefile extracts to a `_MEI` temp dir on each launch; a crash or kill leaves it behind | Documented. Onefile is a little slower to start. | https://pyinstaller.org/en/stable/operating-mode.html (2026-10-13) |
| `--runtime-tmpdir` and `TMPDIR` for `noexec` /tmp | Documented. | https://pyinstaller.org/en/stable/when-things-go-wrong.html (2026-10-13) |

### A5. Antivirus and SmartScreen

- PyInstaller's own docs do not cover antivirus (checked 2026-10-13).
- Community reports say PyInstaller onefile builds are flagged by AV engines more often, partly because of the bootloader-and-archive pattern and because they are unsigned. Sources are forum threads and vendor blogs, so treat them as anecdotal: https://discuss.python.org/t/pyinstaller-false-positive/43171 and https://learn.microsoft.com/en-us/answers/questions/4078397/where-executables-created-by-pyinstaller-are-being
- Microsoft's current guidance (updated 2026-08-29, read 2026-10-13), https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options:
  - Unsigned Win32 apps get a strong SmartScreen block.
  - EV certificates no longer bypass SmartScreen (removed in 2024). OV and EV both build reputation over time.
  - Azure Artifact Signing (formerly Trusted Signing): about $9.99/month per the page; the Azure pricing page did not render a number (unverified). Eligibility: organizations in US, Canada, EU, UK; individuals US and Canada. A third-party source says it is limited more narrowly, so check current terms.
  - OV certificate: $150 to 300/year; keys must be on an HSM or token since June 2023.
  - SignPath Foundation offers free signing for qualifying open-source projects (per the same page).
- Practical reading for Phase 0: onedir reduces the self-extract pattern, signing with a stable identity is the main lever, and VirusTotal plus publishing SHA256 checksums helps reviewers.

### A6. Licensing of the sidecar

| Component | License | Implication |
|---|---|---|
| OpenTyrian | GPL-2.0-or-later (from the user's brief; COPYING in repo) | Baseline. |
| bleak, dbus-fast, winrt-*, pyobjc (CoreBluetooth) | MIT | GPL-compatible. |
| aiohttp | Apache-2.0 AND MIT | Apache-2.0 is GPL-3.0-compatible and not GPL-2.0-only-compatible. For a GPL-2.0-or-later game, distributing combined code under GPL-3.0 is possible; this is a point for legal review. |
| PyInstaller bootloader | GPLv2-or-later with bootloader exception | The exception lets compiled bootloader and related files be embedded without the GPL applying to the bundled program. Verified in COPYING. |
| Python interpreter and stdlib | PSF License (general knowledge, unverified in this pass) | Permissive; normally shipped with PyInstaller output. |
| Nuitka output | Per Nuitka docs and secondary sources, not covered by AGPL | Only relevant if Nuitka is chosen. |

Mere aggregation: a GPL program that launches a separate program and talks to it over a socket or pipe is generally treated as two programs in aggregate, not one combined work. The GNU GPL FAQ states this (https://www.gnu.org/licenses/gpl-faq.html#MereAggregation). This FAQ page returned 503 during this pass, so the quotation is unverified. Ship the license texts and the bridge source (or an offer) with the sidecar regardless.

### A7. Process lifecycle for a C game launching a child

| Concern | Portable in SDL2 C? | Recommended mechanism | Source |
|---|---|---|---|
| Launch a child | SDL2 has no process API. SDL3 has `SDL_CreateProcess(args, pipe_stdio)` since 3.2.0. The SDL2 wiki page for `SDL_CreateProcess` returned 404, which supports "not in SDL2". | SDL3: `SDL_CreateProcess`. SDL2: `CreateProcessW` on Windows, `posix_spawn` or `fork`+`execv` on Linux and macOS. | https://wiki.libsdl.org/SDL3/SDL_CreateProcess (read 2026-10-13); https://wiki.libsdl.org/SDL2/SDL_CreateProcess (404, read 2026-10-13) |
| Kill the child when the game dies or crashes | No | Windows: a Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. Closing the last handle kills all processes in the job, including after a crash. Linux: `prctl(PR_SET_PDEATHSIG, SIGTERM)` in the child, but the signal fires when the parent thread exits, not the process, so spawn from the main thread. The flag is cleared for setuid binaries. macOS has no PDEATHSIG. | https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects (2025-07-16); https://man7.org/linux/man-pages/man2/PR_SET_PDEATHSIG.2const.html (read 2026-10-13) |
| Portable fallback for all three | Yes | Give the child a stdin pipe from the game. The child exits on EOF. The bridge does not currently watch stdin, so this needs a small change to the bridge's entry point (recommendation, not verified against the bridge source). | engineering recommendation |
| Port conflicts | Yes | Default 8787 on 127.0.0.1 (`DEFAULT_PORT`). The game should check `/healthz` on the chosen port before trusting it: the response includes `version`, `protocol` and `source`. Pick a port from a list or pass `--port`. Note: the bridge logs the bound port, which the game can read from stderr. | bridge source `src/flywheel_bridge/__init__.py`, `__main__.py`; `/healthz` verified in A3 |
| Readiness | Yes | Poll `/healthz` with a short timeout. Measured time-to-ready was 0.5 to 1.2 s on Linux. | A3 |
| Shutdown | Yes | Bridge handles SIGTERM and logs "Shutting down" (verified A3). Windows: `TerminateProcess` or the job object. | A3 |

---

## B. Route B: native BLE libraries callable from C

### B1. Library comparison

| Library | Platforms | API | License (verified) | Latest release / activity | Static link from C | Notes | Source (read date) |
|---|---|---|---|---|---|---|---|
| SimpleBLE (SimpleCBLE C wrapper) | Linux (BlueZ over D-Bus), Windows (WinRT), macOS and iOS (CoreBluetooth), Android (JNI) | C++ core, C wrapper (SimpleCBLE) | **BUSL-1.1 since 2025-01-20.** Licensor: The California Open Source Company LLC. Additional Use Grant: production use only if Non-Commercial. Change License: GPL-3.0; per-version Change Date four years after first distribution. Non-Commercial Perpetual Use Grant. Commercial use needs a paid license. | v1.2.0 on 2026-09-28 (breaking C API refactor). v1.1.0 on 2026-08-10 removed the legacy BlueZ backend. v1.0.0 on 2026-07-15. v0.14.0 on 2026-05-02 (security fixes). v0.12.1 on 2026-02-12 removed the `simpleble-c` target in favor of `simplecble`. | Not verified. Docs silent. | Pre-2025 license: the LICENSE.md at tag v0.7.0 is dual: BSD-3-Clause for non-commercial and open-source, GPLv3 or paid commercial for commercial use. **Not MIT**, despite the commercial-license FAQ mention of MIT projects. Versions between v0.7.x and 2025-01-20 were not checked. | https://docs.simpleble.org/licensing (2026-10-13); https://raw.githubusercontent.com/OpenBluetoothToolbox/SimpleBLE/v0.7.0/LICENSE.md (2026-10-13); https://github.com/OpenBluetoothToolbox/SimpleBLE/releases (2026-10-13); https://docs.simpleble.org/simpleble/platform_notes (2026-10-13) |
| btleplug (Rust) | Linux (BlueZ), Windows (WinRT), macOS and iOS (CoreBluetooth), Android | Rust. No shipped C FFI (an old 0.5.1 docs roadmap item). A C wrapper would be a Rust staticlib + cbindgen. | crates.io record: MIT / Apache-2.0 / BSD-3-Clause. The GitHub LICENSE page 404'd, and a secondary README summary says BSD-3-Clause with MIT/Apache parts carried over from Rumble. **Inconsistent; check the repo LICENSE before use.** | 0.13.5 on 2026-10-10; 0.13.4 on 2026-10-03; 0.13.3 on 2026-09-28. Active. | Possible as a Rust staticlib. Needs Rust toolchain and tokio; cross-compiling for Windows and macOS adds build complexity. | Host/central role only; no Classic Bluetooth. | https://crates.io/api/v1/crates/btleplug (2026-10-13); https://docs.rs/crate/btleplug/latest (secondary, 2026-10-13) |
| gattlib (C) | Linux with BlueZ only. Windows and macOS not mentioned. | C core, Python bindings `gattlib-py` | Split: BlueZ >=5.40 code BSD-3-Clause; legacy BlueZ v4 code and `dbus/bluez5/lib/uuid.c/h` GPL-2.0-or-later. **Mixed; GPL-2.0-or-later parts would be fine for this game, but a clean-room check is needed.** | Last release date not captured (page gave no date). | Yes for a C library on Linux. Needs BlueZ / D-Bus libs. | Only useful for the Linux half. | https://github.com/labapart/gattlib (2026-10-13) |
| BlueZ D-Bus directly | Linux | D-Bus `org.bluez.GattCharacteristic1` (`StartNotify`, `AcquireNotify`, `Value`) | BlueZ is GPL-2.0-or-later. D-Bus IPC from a separate process does not link BlueZ. | BlueZ 5.55+ is the bleak minimum (PyPI). | Yes, with libdbus or sd-bus. | The bridge's `dbus-fast` path is the same protocol. | https://www.bluez.org/bluez-api/ and gatt-api.txt returned 404/503 (unverified). Minimum version from https://pypi.org/project/bleak/ (2026-10-13). |
| Windows WinRT GATT from C++ | Windows 10/11 | C++/WinRT (or raw WinRT ABI). `Windows.Devices.Bluetooth.GenericAttributeProfile`: `GetGattServicesAsync`, `WriteClientCharacteristicConfigurationDescriptorAsync`, `ValueChanged`. | Windows SDK. Not an open-source license; a system API. | Microsoft doc updated 2026-09-21. | Yes. Links only system components. | MS docs: apps must declare `bluetooth` in `Package.appxmanifest`. SimpleBLE docs say unpackaged EXEs do not need it for peripheral mode. Clarify for Win32 EXE before shipping. | https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/gatt-client (read 2026-10-13) |
| Win32 `BluetoothGATT*` (bluetoothleapis.h) | Windows 8+ | C. `BluetoothGATTGetServices`, `BluetoothGATTRegisterEvent`. Imports from BluetoothAPIs.dll. | Windows SDK. | Doc last updated 2024-02-22. | Yes. | The remarks speak of "profile drivers" and the doc gives no user-mode guidance. Not verified for an ordinary app. Prefer WinRT unless a spike proves otherwise. | https://learn.microsoft.com/en-us/windows/win32/api/bluetoothleapis/nf-bluetoothleapis-bluetoothgattgetservices (read 2026-10-13) |
| macOS CoreBluetooth | macOS 10.15+ | Objective-C and Swift framework. No C API. Needs an `.m` shim in a C project. | Apple SDK. | n/a | Yes, linking the framework. | Needs a Bluetooth usage string in Info.plist when the app bundle is used. | Apple page returned only the title (unverified details). Permission note from btleplug secondary README. |

### B2. Other fitness apps and FTMS libraries

| Project | Platforms | BLE stack | License | Notes | Source |
|---|---|---|---|---|---|
| qdomyos-zwift | README: macOS, Linux, iOS, Android. Windows appears only in packaging file names. | Not identified. Qt project, so probably QtBluetooth (unverified). | GPL-3.0 (sidebar) | Claims FTMS support. A reference for FTMS behavior, not a code source for a GPL-2.0-or-later game. Latest release not captured. | https://github.com/cagnulein/qdomyos-zwift (2026-10-13) |
| GoldenCheetah | Windows, Linux, macOS | Not identified (probably Qt). | Not verified | 3.6 release notes list Windows 10 BTLE and FTMS support, plus BTLE control for several trainers. | https://www.goldencheetah.org/RELEASE/release-notes_3.6.0.html (search snippet 2026-10-13) |
| pyftms (Python, bleak) | cross-platform | bleak | not checked | Mature FTMS client for Indoor Bike. Reference only. | https://github.com/dudanov/pyftms (search snippet 2026-10-13) |
| FTMS.NET (.NET) | net8.0-windows, net8.0-macos (Linux not seen) | pluggable | not checked | Reference only. | https://www.nuget.org/packages/FTMS.NET/ (search snippet) |

No C FTMS library with an open license was found in this pass. A C implementation of FTMS would mean porting the Indoor Bike Data parsing (0x2AD2) from the Bluetooth SIG spec, which is small.

### B3. SDL and BLE

- SDL3 HIDAPI defines a Bluetooth bus type, `SDL_HID_API_BUS_BLUETOOTH`, and `SDL_hid_ble_scan` for Steam Controller pairing on iOS and tvOS. Both are since 3.2.0. These are HID-layer features, not GATT access to arbitrary services. Source: https://wiki.libsdl.org/SDL_hid_bus_type (search snippet 2026-10-13).
- I found no SDL2 or SDL3 API for BLE GATT services, characteristics or notifications. Conclusion: SDL does not offer a BLE GATT API. Confidence: medium, based on search results and the absence of such pages, not a full header audit. The authoritative check is `SDL_hidapi.h` and the SDL3 API category list.
- SDL3's process API (`SDL_CreateProcess`) is a separate, usable feature for Route C. See A7.

---

## C. Open items and gaps

1. Build the bridge's Windows one-file and one-dir with `--collect-all winrt` (or `--collect-submodules winrt`) on a Windows machine, then run `--source sim`. Measure size and startup on the Surface. This is the largest remaining unknown for Route C.
2. Decide the BLE license path before any Route B work. SimpleBLE versions since 2025-01-20 are BUSL-1.1 and are not GPL-2.0-or-later-distributable without a commercial license. The v0.7.x BSD/GPLv3 dual license was not checked for intervening versions.
3. Confirm btleplug's license from its repo LICENSE, since the crates.io record and the secondary README disagree.
4. Check gattlib's last release date and whether it builds on the Linux targets.
5. PyOxidizer's maintenance status is unconfirmed. Nuitka's license wording conflicts between the homepage and PyPI.
6. The GNU GPL FAQ and the BlueZ gatt-api.txt pages returned errors during this pass. Re-read them before citing.

---

## Sources

- https://pypi.org/project/bleak/ (read 2026-10-13)
- https://github.com/hbldh/bleak/releases (read 2026-10-13)
- https://bleak.readthedocs.io/en/latest/troubleshooting.html (read 2026-10-13)
- https://pypi.org/project/bleak-winrt/ (did not render, 2026-10-13)
- https://pypi.org/project/pyinstaller/ (read 2026-10-13)
- https://raw.githubusercontent.com/pyinstaller/pyinstaller/develop/COPYING.txt (read 2026-10-13)
- https://pyinstaller.org/en/stable/operating-mode.html (read 2026-10-13)
- https://pyinstaller.org/en/stable/when-things-go-wrong.html (read 2026-10-13)
- https://pyinstaller.org/en/stable/license.html (read 2026-10-13; no bootloader text)
- https://github.com/orgs/pyinstaller/discussions/8395 (search snippet 2026-10-13)
- https://discuss.python.org/t/pyinstaller-false-positive/43171 (search snippet 2026-10-13)
- https://pypi.org/project/Nuitka/ (read 2026-10-13)
- https://nuitka.net/doc/commercial-license (search snippet 2026-10-13)
- https://pydevtools.com/handbook/reference/nuitka/ (secondary, 2026-10-13)
- https://www.pistack.xyz/posts/2026-09-10-python-executable-packaging-pyinstaller-nuitka-cxfreeze-comparison/ (secondary, 2026-10-13)
- https://beatsyncpro.ai/blog/nuitka-vs-pyinstaller (secondary, 2026-10-13)
- https://gregoryszorc.com/docs/pyoxidizer/0.1.0/status.html (old, 2026-10-13)
- https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options (updated 2026-08-29; read 2026-10-13)
- https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/gatt-client (updated 2026-09-21; read 2026-10-13)
- https://learn.microsoft.com/en-us/windows/win32/api/bluetoothleapis/nf-bluetoothleapis-bluetoothgattgetservices (updated 2024-02-22; read 2026-10-13)
- https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects (updated 2025-07-16; read 2026-10-13)
- https://man7.org/linux/man-pages/man2/PR_SET_PDEATHSIG.2const.html (read 2026-10-13)
- https://wiki.libsdl.org/SDL3/SDL_CreateProcess (read 2026-10-13)
- https://wiki.libsdl.org/SDL2/SDL_CreateProcess (404 on 2026-10-13)
- https://wiki.libsdl.org/SDL_hid_bus_type (search snippet 2026-10-13)
- https://docs.simpleble.org/licensing (read 2026-10-13)
- https://docs.simpleble.org/simpleble/platform_notes (read 2026-10-13)
- https://docs.simpleble.org/ (read 2026-10-13)
- https://raw.githubusercontent.com/OpenBluetoothToolbox/SimpleBLE/v0.7.0/LICENSE.md (read 2026-10-13)
- https://github.com/OpenBluetoothToolbox/SimpleBLE/releases (read 2026-10-13)
- https://pypi.org/project/winrt-runtime/ (read 2026-10-13)
- https://crates.io/api/v1/crates/btleplug (read 2026-10-13)
- https://docs.rs/crate/btleplug/latest (search snippet 2026-10-13)
- https://github.com/labapart/gattlib (read 2026-10-13)
- https://github.com/cagnulein/qdomyos-zwift (read 2026-10-13)
- https://www.goldencheetah.org/RELEASE/release-notes_3.6.0.html (search snippet 2026-10-13)
- https://github.com/dudanov/pyftms (search snippet 2026-10-13)
- https://www.gnu.org/licenses/gpl-faq.html (503 on 2026-10-13)
- https://www.bluez.org/bluez-api/ (503 on 2026-10-13)
- https://raw.githubusercontent.com/bluez/bluez/master/doc/gatt-api.txt (404 on 2026-10-13)
- Local: `/home/user/flywheel/bridge/uv.lock`, `/home/user/flywheel/bridge/pyproject.toml`; scratch build under the session scratchpad (`bridge-freeze`).
