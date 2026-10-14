/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef REGRESS_H
#define REGRESS_H

#include <SDL3/SDL.h>
#include "modern.h"

#include <stdbool.h>
#include <stdio.h>

// Default presented-frame cap for a --regress-screen run when --regress-frames
// is not given.  Long enough for every screen's fade-in to settle so a
// --regress-snapshot near the end captures the steady state.
#define REGRESS_SCREEN_FRAMES 90

// Headless demo-playback / synthetic-scenario regression harness.
//
// When --regress-demo=N is given, the game replays recorded demo N without a
// window or audio, pins every setting that can affect the 8-bit framebuffer or
// gameplay, and appends a hash of each presented frame to --regress-out=FILE.
//
// When --regress-level=EPISODE:LEVEL is given instead, the game starts that
// level directly (no demo recording, no input, fixed loadout and RNG seed),
// runs for --regress-frames frames, then exits.  This reaches render paths the
// recorded demos never exercise (the smoothie filters and the two
// JE_starShowVGA special codes).
//
// Outside regress mode every entry point below is inert.

extern int regress_demo;              // 0 = disabled, 1..5 = demo to replay
extern int regress_scenario_episode;  // 0 = disabled, 1..4 = episode to start
extern int regress_scenario_level;    // lvlFileNum within that episode (1-based)
extern int regress_script;            // non-zero = run the episode script to reach the level
extern int regress_seed_set;          // non-zero = pin the RNG seed (--regress-seed)
extern unsigned long regress_seed;    // the pinned RNG seed
extern int regress_frames;            // scenario length cap; 0 = run to the end
extern const char *regress_out_path;  // where frame hashes are written
extern int regress_detail;            // processorType to use (1..6)
extern int regress_audio;             // non-zero = offline audio regression
extern int regress_modern;            // non-zero = force the Modern presentation
extern int regress_aspect;            // ModernAspect to force in regress mode; -1 = default (4:3)
extern const char *regress_bike_set;      // --regress-bike-set=K=V,... (NULL = none)
extern const char *regress_state_out_path;  // where per-frame state hashes are written
extern int regress_vfx;               // VfxLevel to force in regress mode; -1 = off
extern int regress_players;           // players to start a scenario with (1 or 2)
extern int regress_arcade;            // non-zero = start a scenario in 1-player arcade mode
extern const char *regress_screen;    // non-NULL = render one non-gameplay screen and exit
extern int regress_replay_check;      // non-zero = record and replay-check every level frame
extern int regress_interp_check;      // non-zero = record and validate the interpolated renderer
extern int regress_interp_smoothness; // non-zero = per-tick monotonic motion check
extern int regress_smooth_alphas;     // sub-frame alphas used by the smoothness check (>= 2)
void regress_coin_probe(bool after, int x0, int y0, int w);  // INSERT COIN stamp probe (demo-hud-check)
extern int regress_demo_hud_check;   // non-zero = assert a played demo follows the active mode's HUD
extern int regress_gameplay_check;    // non-zero = assert every in-level frame drops the classic sidebar
extern int regress_parallax_check;    // non-zero = assert the presentation never advances starfield/background
extern int regress_layer_check;       // non-zero = per tick, check the depth layer buffer (needs --regress-modern)
extern const char *regress_layer_png; // non-NULL = write the presented layer buffer as a false-colour PNG
extern unsigned long regress_layer_png_frame;  // presented frame for regress_layer_png
extern int regress_smooth_effects_check;  // non-zero = assert fade/HUD interpolation stays between ticks
extern int regress_realtime;          // non-zero = real window + real clock, for the pacing benchmark
extern double regress_bench_seconds;  // benchmark duration (default 20)
extern int regress_bloom_quality;     // ModernQuality pinned in regress mode; -1 = off
extern int regress_depth_quality;     // ModernDepth of the Depth setting pinned in regress mode; -1 = off
extern bool regress_depth_check;      // run the synthetic depth-shadow fixture and exit
extern bool regress_held_check;       // compare held in-level frames with the last live frame (needs --regress-modern and --regress-menu)
int regress_depth_selfcheck(void);
extern int regress_lighting_quality;  // ModernQuality pinned in regress mode; -1 = off

// --regress-stick=X,Y: install a synthetic analog stick (raw axis values) so a
// regress-script run drives the whole JE_playerMovement path with a stick and no
// hardware.  --regress-stick-log=FILE writes one "tick x y x_velocity y_velocity"
// line per player-1 tick for the trajectory comparison.  --regress-reverse-y
// forces the reverse-controls smoothie on.  All three are regress-only.
extern int regress_stick;                 // non-zero = inject the stick
extern int regress_stick_x, regress_stick_y;  // raw axis values (-32767..32767)
extern const char *regress_stick_log_path;    // NULL = no per-tick log
extern int regress_reverse_y;             // non-zero = force the reverse-controls smoothie on

// --regress-front-weapon=N / --regress-front-power=N: override player 1's front
// weapon id and power level in a --regress-script run so a specific weapon can
// be captured headless (used with the -c/--constant autofire cheat).  -1 = leave
// the built-in loadout; inert outside regress mode.
extern int regress_front_weapon;          // -1 = not overridden
extern int regress_front_power;           // -1 = not overridden

// --regress-loadout=widest: give the players the items with the widest names of
// the variant's data (the two widest ports and sidekicks, the widest generator),
// so a HUD run shows the longest names it can meet.  The choice is computed from
// the data, so no text of it is kept anywhere.
extern int regress_loadout_widest;

// --regress-items-new: give the players the variant's most distinctive items
// (highest ship, Flying Punch port, a chargeable and a plain sidekick).
// --regress-fire: a scenario fires, pulses the sidekicks and sweeps the ship.
extern int regress_loadout_new;
extern int regress_fire;
void regress_scenario_input(void);

// --regress-xmas: run with Christmas mode on (regress runs otherwise pin it off
// and skip its prompt), to cover the Christmas shape and voice files.
extern int regress_xmas;

// Applies the --regress-front-weapon/--regress-front-power overrides to player
// 1's item set.  Called after JE_initPlayerData() on the --regress-script path;
// a no-op when neither override was requested.
void regress_apply_loadout(void);

bool regress_stick_active(void);

// Called once per player-1 tick from JE_playerMovement; writes the log line when
// --regress-stick-log was given, otherwise does nothing.
void regress_stick_log_tick(int player_x, int player_y, int x_velocity, int y_velocity);

// --regress-menu=NAME: open an in-level presentation screen (the ESC in-game
// menu, the P pause, or the F1 in-game help) on the run's last presented frame
// (--regress-frames - 1), so the Modern gameplay-composition check and the
// snapshots can cover it.  Those screens present straight through JE_showVGA
// and then block on input, so the frame cap ends the run on the first menu
// frame.  Regress-only: the request is inert outside regress mode and when the
// run has no frame cap.
#define REGRESS_MENU_NONE   0
#define REGRESS_MENU_INGAME 1
#define REGRESS_MENU_PAUSE  2
#define REGRESS_MENU_HELP   3

extern int regress_menu_kind;  // REGRESS_MENU_* requested; NONE = disabled

// Returns the requested REGRESS_MENU_* exactly once, on the frame the run should
// show it, then NONE.  Called from the level loop; a no-op unless a
// --regress-menu was requested and the run has a frame cap.
int regress_take_menu_request(void);

// True when regress mode was requested.
bool regress_active(void);

// True when the real-time pacing benchmark was requested (--regress-realtime):
// regress still pins the demo and settings, but the clock is the wall clock and
// a real window is opened.
bool regress_realtime_active(void);

// True when a synthetic level scenario was requested.
bool regress_scenario_active(void);

// True when the level should be reached through the episode script
// (--regress-script): the script's own screens (WARNING text, item screen, ...)
// are then drawn and captured, instead of jumping straight to the level.
bool regress_script_active(void);

// True when --regress-screen was requested: draw one non-gameplay screen into a
// deterministic state, present --regress-frames frames and exit.  The harness
// reuses the game's own screen functions, so their blocking input waits return
// immediately in this mode (the frame cap ends the run).
bool regress_screen_active(void);

// Renders the requested screen and exits once the frame cap (--regress-frames,
// default REGRESS_SCREEN_FRAMES) is reached.  Never returns.
void regress_screen_run(void);
void regress_screen_verify_mapping(void);

// Resets the presented-frame counter to 0 (the screen run starts its own
// frame timeline so setup frames do not eat into --regress-frames).
void regress_frame_reset(void);

// True when the offline audio regression was requested (--regress-audio).
bool regress_audio_active(void);

// Cheap argv scan for "--regress-demo"/"--regress-level"/"--regress-audio" only,
// run before config loading so the user's config/save files can be skipped.
// Does not set state.
bool regress_scan_args(int argc, char *argv[]);

// Run the offline audio regression to completion (loads sounds and songs,
// drives the mixer without an audio device, writes the baseline, closes the
// output).  Never returns past a missing --regress-out file.
void regress_audio_run(void);

// 64-bit FNV-1a hash of a byte buffer (the harness-wide hash).
Uint64 regress_fnv1a(const void *data, size_t size);

// The open regression output stream, or NULL outside a regress mode.
FILE *regress_output_file(void);

// Set up headless drivers, disable audio/joystick input, pin the configuration
// values that affect output or gameplay, and open the output file.
void regress_init(void);

// Set up the fixed state (RNG seed, episode, loadout, level number) for a
// synthetic level scenario.  Called from JE_loadMap() in place of
// beginPlayDemo() when regress_scenario_active() is true.
void regress_begin_scenario(void);

// Flush and close the output file.
void regress_finish(void);

// Deterministic substitute for the frame-pacing clock.  In normal mode this is
// SDL_GetTicks() << 10; in regress mode it is a virtual UQ22.10 ms clock that
// only moves when the game asks to wait, so playback runs as fast as possible
// while staying independent of wall-clock time.
Uint32 regress_clock_ticks10bit(void);
void regress_clock_advance_to(Uint32 target);

// Append one "<frame_index> <hash>" line for a presented 320x200 8-bit surface.
// When --regress-state-out is also set, a state-hash line for the same frame is
// appended to that stream too (see below).
void regress_capture_frame(SDL_Surface *surface);

// Registers a snapshot request: on presented frame `frame` save the presented
// image to `path` as a BMP (the Modern canvas when the run is Modern, the 8-bit
// frame otherwise).  `path` must stay valid for the whole run (argv storage is
// fine).  Returns false when the (small) request table is full.
bool regress_add_snapshot(unsigned long frame, const char *path);

// True when at least one --regress-snapshot was requested.
bool regress_has_snapshots(void);

// Append one "<frame_index> <hash>" line for the current Modern canvas
// (XRGB8888), hashing the visible w*4 bytes of each row and walking the pitch.
// The palette is already baked into those bytes.  Also emits the frame's state
// hash when --regress-state-out is set.
// CRT is pinned Off unless explicitly requested. Filter hashes are captured
// in the presenter after the normal canvas checks; game-state capture is shared.
extern int regress_crt_mode;
extern bool regress_crt_check;
int regress_crt_selfcheck(void);
extern int regress_crt_dst_h;
extern int regress_crt_window_w, regress_crt_window_h;
extern const char *regress_present_png;
extern unsigned long regress_present_png_frame;
void regress_capture_presented_frame(SDL_Renderer *renderer);
void regress_capture_crt_frame(const ModernFrame *frame);
void regress_capture_modern_frame(void);

// State-hash stream (--regress-state-out=FILE): one "<frame_index> <hash>" line
// per presented frame, covering the RNG state, both players' gameplay fields,
// the enemy/shot arrays, boss_bar[], tempW and the level event position.  The
// stream is presentation-independent, so a Classic run and a Modern run of the
// same case must produce byte-identical files.

extern const char *regress_data_audit_root;
// Metadata-only observer assertions; requires a gameplay scenario/demo/flow.
// OFF is test-only and never changes gameplay or RNG state.
extern bool regress_progress_check;
extern bool regress_observer_off;
extern int regress_boss;
extern const char *regress_handoff;
extern int regress_flow_gamepad;
void regress_audit_open(const char *path);
bool regress_gamepad_key(SDL_Scancode key, SDL_KeyboardEvent *out);

#endif /* REGRESS_H */
