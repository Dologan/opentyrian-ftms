<p align="center">
  <img src="docs/readme/banner.svg" alt="Modern Tyrian" width="100%">
</p>

<p align="center">
  <a href="https://github.com/vittau/modern-tyrian/releases/latest"><img src="https://img.shields.io/github/v/release/vittau/modern-tyrian?style=for-the-badge&label=download&color=c9924e&labelColor=0a1322" alt="Download the latest release"></a>
  <a href="https://github.com/vittau/modern-tyrian/actions/workflows/linux.yml"><img src="https://img.shields.io/github/actions/workflow/status/vittau/modern-tyrian/linux.yml?style=for-the-badge&label=linux&labelColor=0a1322" alt="Linux CI status"></a>
  <a href="https://github.com/vittau/modern-tyrian/actions/workflows/macos.yml"><img src="https://img.shields.io/github/actions/workflow/status/vittau/modern-tyrian/macos.yml?style=for-the-badge&label=macos&labelColor=0a1322" alt="macOS CI status"></a>
  <a href="https://github.com/vittau/modern-tyrian/actions/workflows/windows.yml"><img src="https://img.shields.io/github/actions/workflow/status/vittau/modern-tyrian/windows.yml?style=for-the-badge&label=windows&labelColor=0a1322" alt="Windows CI status"></a>
  <a href="COPYING"><img src="https://img.shields.io/badge/license-GPL--2.0--or--later-53a8d8?style=for-the-badge&labelColor=0a1322" alt="License: GPL-2.0-or-later"></a>
</p>

<p align="center">
  <b>Tyrian 2.1 + Tyrian 2000</b> in one SDL3 application.<br>
  Classic gameplay, optional Modern presentation, Linux, Windows and macOS.
</p>

Choose either game in the built-in launcher. **Tyrian 2.1 data is included**;
install **Tyrian 2000** separately through the launcher.

<p align="center">
  <img src="docs/screenshots/launcher.png" alt="16:9 launcher with Tyrian 2.1 and Tyrian 2000 selection" width="100%">
  <br><sub>Launcher · 16:9</sub>
</p>

<p align="center">
  <img src="docs/screenshots/hero.jpg" alt="Tyrian 2.1 SAVARA IV in 16:9 with the Modern HUD and both CRT filters: scanlines and NTSC" width="100%">
  <br><sub>SAVARA IV · Modern HUD · CRT Both · 16:9</sub>
</p>

<p align="center">
  <img src="docs/screenshots/arcade.png" alt="Tyrian 2.1 TYRIAN in one-player Arcade mode, 16:9, with the Modern bevelled glass HUD" width="100%">
  <br><sub>TYRIAN · Arcade · Modern HUD · 16:9</sub>
</p>

## <img src="docs/readme/h-play.svg" alt="Play" width="100%">

[Download the latest release](https://github.com/vittau/modern-tyrian/releases/latest),
extract it and run:

| Platform | Download | Run |
| --- | --- | --- |
| Linux / Steam Deck | `…-linux-x86_64.tar.gz` or `…-linux-arm64.tar.gz` | `./opentyrian` |
| Windows (x64; also runs on Windows on ARM) | `…-windows-x86_64.zip` | `opentyrian.exe` |
| macOS (Intel + Apple silicon) | `…-macos-universal.zip` | `OpenTyrian.app` |

The launcher accepts keyboard, mouse and gamepad input. Choose **INSTALL** on
the Tyrian 2000 panel to download its verified data from camanis.net, import a
zip, or use an existing folder such as a GOG copy. Steam Deck Game Mode shows
where to place files when a file picker is unavailable. Tyrian 2000 data is
never bundled in releases.

Each game has its own saves and high scores. Existing Tyrian 2.1 saves are
copied into the new namespace automatically; the original is kept.

| User files | Default location |
| --- | --- |
| Windows | `%APPDATA%\OpenTyrian` |
| macOS / Linux | `$XDG_CONFIG_HOME/opentyrian` or `~/.config/opentyrian` |

Configs are shared; saves live in `tyrian21/` and `tyrian2000/`. For portable
mode, place `opentyrian.cfg` beside the executable. The launcher's **About**
screen shows the data paths.

> [!NOTE]
> Builds are unsigned. On macOS, allow the app in **System Settings → Privacy &
> Security**, or run `xattr -dr com.apple.quarantine /path/to/OpenTyrian.app`.
> On Windows, choose **More info → Run anyway** if SmartScreen prompts.

## <img src="docs/readme/h-modern.svg" alt="What Modern mode brings" width="100%">

Select **Classic** or **Modern** in *Setup → Graphics → Presentation*.
Both games support Modern mode:

- **Widescreen** from 4:3 to 32:9, with automatic display aspect detection.
- **Bevelled glass HUD** with glowing edges, light glints and inset shadows,
  for campaign and arcade modes, including two players.
- **Smooth motion** at the display refresh rate, bloom, dynamic lighting and VFX, plus optional half-row scanlines and Blargg NTSC CRT filtering.
- **Sharp scaling and HiDPI output**, preserving the original pixel aspect.
- **Gamepad support**, remappable buttons, analog movement and hot-plugging.
- **Nuked-OPL3 audio** for the original FM music.

Graphics changes apply immediately. Dialogs remain centred and the HUD adapts
after display changes. Attract demos use the selected presentation mode.

Classic preserves the original 8-bit framebuffer and RNG order. Modern's
renderer reads game state without changing it. The shared starfield defaults
to 25% speed; `--starfield-speed=100` restores the upstream rate.

## <img src="docs/readme/h-deck.svg" alt="Steam Deck" width="100%">

<p align="center">
  <img src="docs/screenshots/steam-deck.png" alt="Steam Deck showing SAVARA IV with the bevelled glass HUD at its native 16:10 aspect, with black screen bezels preserved" width="100%">
  <br><sub>SAVARA IV · Modern HUD · Steam Deck 16:10</sub>
</p>

Add the extracted Linux `opentyrian` executable as a non-Steam game in Desktop
Mode, then return to Game Mode. Leave launch options empty: the launcher opens
normally, with fullscreen Modern presentation and native controller support.
See [Steam Deck setup and troubleshooting](docs/STEAM_DECK.md).

## <img src="docs/readme/h-controls.svg" alt="Controls" width="100%">

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Move | Arrow keys | Left stick / D-pad |
| Fire / confirm | Space / Enter | A |
| Rear weapon / back | Enter / Esc | B |
| Left sidekick | Left Ctrl | Y / LB |
| Right sidekick | Left Alt | X / RB |
| In-game menu | Esc | Start |
| Pause | P | Back / View |
| Fullscreen | Alt+Enter | — |

Remap controls in *Setup → Keyboard / Joystick*. Menus also accept the mouse.
For network play, use `--net HOST --net-player-name NAME --net-player-number N`
on both machines (UDP port 1333).

## Playing with an exercise bike

Modern Tyrian can take power from an FTMS exercise bike through a Flywheel
bridge. Everything is off by default, so the game plays normally unless you
turn the bike on.

You need:

- a Flywheel checkout (its `bridge/` folder runs the bike link),
- [uv](https://docs.astral.sh/uv/) on your `PATH`,
- an FTMS bike, or none at all: `--source sim` simulates one.

From the repository root, run the helper with the path to your Flywheel
checkout (or set `FLYWHEEL_DIR`):

```sh
tools/ftms/play-with-bike.sh --flywheel ../flywheel              # real bike
tools/ftms/play-with-bike.sh --flywheel ../flywheel --source sim # simulated bike
```

On Windows, run `tools\ftms\play-with-bike.cmd --flywheel ..\flywheel`. The
helper starts the bridge, waits for it, launches the game with
`--ftms-bridge=127.0.0.1:8787`, and stops the bridge when the game exits.
Anything after `--` goes to the game.

Turn the bike on in **Setup → Bike...**, which is also in the in-game menu.
The bridge options are `--ftms-bridge=HOST[:PORT]`, `--ftms-ftp=W` and
`--ftms-off`, if you prefer to start the bridge yourself.

Your FTP (best even 8 minute power x 0.9, in this bike's own watts) comes from
your Flywheel rider when the bridge has one: the Bike menu shows it beside
FTP, and **FTP Source** switches between the bridge's value and one you type.
`--ftms-ftp=W` beats both. A new bridge value applies from the next level.

## <img src="docs/readme/h-build.svg" alt="Build it" width="100%">

Requires a C99 compiler, GNU make, pkg-config, SDL3 and optionally SDL3_net.

```sh
make
./get_data.sh                 # freeware Tyrian 2.1 data
./opentyrian
```

- **macOS:** `brew install pkgconf sdl3 sdl3_net`; `./make_macos.sh` builds a universal app.
- **Linux:** `./make_linux.sh` builds with static SDL3 and SDL3_net.
- **Windows:** build with MSYS2 and its SDL3/SDL3_net packages; `visualc/` also contains a Visual Studio solution.
- **Development:** `make debug` or `make asan`; run `make clean` when switching build modes.

## <img src="docs/readme/h-options.svg" alt="Command-line options" width="100%">

Run `./opentyrian --help` for all options. Common ones:

| Option | Purpose |
| --- | --- |
| `--variant=2.1` / `--variant=2000` | Start a game directly, skipping the launcher |
| `--data=DIR` | Use an explicit game-data folder |
| `--install-2000=download` | Download and install Tyrian 2000 data |
| `--install-2000=PATH` | Install from a zip or folder |
| `--presentation=classic\|modern` | Select presentation |
| `--aspect=16:9` | Set Modern aspect (`auto`, `4:3`, `16:10`, `21:9`, `32:9` also supported) |
| `--no-sound` / `--no-joystick` | Disable audio / controller input |

For Tyrian 2000, `TYRIAN2000_DATA` or a `tyrian2000/` folder beside the executable
also works. The launcher installs data per user and shows its location in **About**.

## <img src="docs/readme/h-regress.svg" alt="Regression testing" width="100%">

```sh
make regress                              # Tyrian 2.1 + architecture/installer/display guards
make regress-2000 TYRIAN2000_DATA=DIR       # separately installed Tyrian 2000 data
```

CI runs both suites on Linux, macOS and Windows. Deterministic frame and state
hashes cover gameplay, menus, saves, bosses, gamepads and launcher handoff.
Tyrian 2000 test data is downloaded into temporary storage, never cached or
packaged. Long interpolation and smoothness sweeps run only in the manual
[full regression workflow](.github/workflows/regress-full.yml).

## <img src="docs/readme/h-credits.svg" alt="Credits and licence" width="100%">

> [!NOTE]
> Modern Tyrian was developed with the assistance of **Claude**, **Codex** and **OpenCode**.

- **Tyrian** was developed by **Eclipse Software** (Jason Emery and
  contributors) and published by Epic MegaGames in 1995; it was released as
  freeware in 2004. The freeware 2.1 data is redistributed under its own
  licence, [`doc/tyrian-freeware-license.txt`](doc/tyrian-freeware-license.txt).
- **OpenTyrian** is the open-source port this project is based on, by the
  [OpenTyrian Development Team](https://github.com/opentyrian/opentyrian).
- **Tyrian 2000** support is ported from [KScl/opentyrian2000](https://github.com/KScl/opentyrian2000), commit `aad5aca` (GPL-2.0). Its game data is installed separately.
- **Nuked-OPL3** by Nuke.YKT (commit `765ec962`) emulates the AdLib/OPL2 FM
  music. It is vendored under `src/nuked_opl3.c` / `src/nuked_opl3.h` and used
  under the GNU LGPL v2.1 or later; see `LICENSE-Nuked-OPL3`.
- **SDL3** and **SDL3_net** provide the platform layer. SDL3_net is only used
  for network play.
- The README art uses **Press Start 2P** by CodeMan38, under the SIL Open Font
  License; the font and its licence are in
  [`docs/readme/fonts/`](docs/readme/fonts/).

This project's own source is free software, released under the **GNU General
Public License v2.0 or later**; see [`COPYING`](COPYING).

- project: <https://github.com/opentyrian/opentyrian>
- irc: <ircs://irc.oftc.net/#opentyrian>
- forums: <https://tyrian2k.proboards.com/board/5>
