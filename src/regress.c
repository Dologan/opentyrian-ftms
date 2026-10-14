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
#ifndef _WIN32
#define _XOPEN_SOURCE 700
#endif
#include "bike.h"
#include "regress.h"
#include "regress_flow.h"

#include "config.h"
#include "demo.h"
#include "drawlist.h"
#include "episodes.h"
#include "fonthand.h"
#include "game_schema.h"
#include "interp.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "loudness.h"
#include "mainint.h"
#include "modern.h"
#include "modern_progress.h"
#include "crt_filter.h"
#include "modern_bloom.h"
#include "modern_depth.h"
#include "modern_held.h"
#include "mtrand.h"
#include "opentyr.h"
#include "palette.h"
#include "player.h"
#include "shots.h"
#include "tyrian2.h"
#include "varz.h"
#include "vfx.h"
#include "video.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// beginPlayDemo() reseeds with this constant; scenario mode matches it so the
// two modes share the same deterministic RNG stream shape.
static const unsigned long scenario_seed = 32402394;

const char *regress_data_audit_root = NULL;
int regress_boss = 0;
bool regress_progress_check;
bool regress_observer_off;
const char *regress_handoff = NULL;
int regress_flow_gamepad = 0;

#ifdef _WIN32
// _fullpath only removes relative segments; query the opened object to resolve
// junctions and symlinks as well, matching realpath on the POSIX runners.
static char *auditResolvedPath(const char *path)
{
	HANDLE handle = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return NULL;
	DWORD length = GetFinalPathNameByHandleA(handle, NULL, 0, FILE_NAME_NORMALIZED);
	char *result = length != 0 ? malloc((size_t)length + 1) : NULL;
	if (result != NULL && GetFinalPathNameByHandleA(handle, result, length + 1, FILE_NAME_NORMALIZED) == 0)
	{ free(result); result = NULL; }
	CloseHandle(handle);
	return result;
}
#endif

// Freeze and resolve the requested root, including symlinks. No assets are
// copied into the output; only the resolved filename and open intent are logged.
void regress_audit_open(const char *path)
{
	if (regress_data_audit_root == NULL)
		return;
#ifdef _WIN32
	char *root = auditResolvedPath(regress_data_audit_root);
	char *resolved = auditResolvedPath(path);
#else
	char *root = realpath(regress_data_audit_root, NULL);
	char *resolved = realpath(path, NULL);
#endif
	if (root == NULL)
	{
		logError("Data audit FAIL: cannot resolve root.");
		exit(EXIT_FAILURE);
	}
	// A missing optional file has no resolved target. The provider still logs
	// its absolute attempted filename; successful opens must resolve in-root.
	struct stat info;
	if (stat(path, &info) != 0)
	{
		logInfo("Data audit missing: %s", path);
		free(root);
		free(resolved);
		return;
	}
	const char *actual = resolved != NULL ? resolved : path;
	size_t len = strlen(root);
	while (len > 1 && (root[len-1] == '/' || root[len-1] == '\\')) --len;
	bool inside = strncmp(root, actual, len) == 0 && (actual[len] == '/' || actual[len] == '\\');
	logInfo("Data audit: %s", actual);
	free(root);
	free(resolved);
	if (!inside)
	{
		logError("Data audit FAIL: open outside selected root.");
		exit(EXIT_FAILURE);
	}
}

// Feed the same joystick-to-key adapter as real menu input, without opening a
// device or admitting any real SDL input. The selftest covers physical mapping;
// flows cover the resulting navigation in the real menus.
bool regress_gamepad_key(SDL_Scancode key, SDL_KeyboardEvent *out)
{
	Joystick injected;
	memset(&injected, 0, sizeof injected);
	injected.injected = injected.input_pressed = true;
	if (key == SDL_SCANCODE_RETURN) injected.confirm = true;
	else if (key == SDL_SCANCODE_ESCAPE) injected.cancel = true;
	else if (key == SDL_SCANCODE_UP) injected.direction_pressed[0] = true;
	else if (key == SDL_SCANCODE_RIGHT) injected.direction_pressed[1] = true;
	else if (key == SDL_SCANCODE_DOWN) injected.direction_pressed[2] = true;
	else if (key == SDL_SCANCODE_LEFT) injected.direction_pressed[3] = true;
	else return false;
	Joystick *old = joystick;
	int old_count = joysticks;
	joystick = &injected;
	joysticks = 1;
	SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
	push_joysticks_as_keyboard();
	joystick = old;
	joysticks = old_count;
	SDL_Event event;
	if (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_DOWN) <= 0)
		return false;
	*out = event.key;
	SDL_FlushEvents(SDL_EVENT_KEY_UP, SDL_EVENT_KEY_UP);
	return out->scancode == key;
}

int regress_seed_set = 0;
unsigned long regress_seed = 0;
int regress_demo = 0;
int regress_scenario_episode = 0;
int regress_scenario_level = 0;
int regress_script = 0;
int regress_frames = 0;
const char *regress_out_path = NULL;
int regress_detail = 2;
int regress_audio = 0;
int regress_modern = 0;
int regress_aspect = -1;
int regress_vfx = -1;
const char *regress_state_out_path = NULL;
int regress_players = 1;
int regress_arcade = 0;
const char *regress_screen = NULL;
int regress_crt_mode = -1;
bool regress_crt_check;
int regress_crt_dst_h;

int regress_replay_check = 0;
int regress_interp_check = 0;
int regress_interp_smoothness = 0;
int regress_smooth_alphas = 5;
int regress_gameplay_check = 0;
int regress_demo_hud_check = 0;
int regress_loadout_new = 0;
int regress_fire = 0;
int regress_parallax_check = 0;
int regress_layer_check = 0;
const char *regress_layer_png = NULL;
unsigned long regress_layer_png_frame = 0;
int regress_smooth_effects_check = 0;
int regress_realtime = 0;
double regress_bench_seconds = 20.0;
int regress_bloom_quality = -1;
int regress_lighting_quality = -1;
int regress_depth_quality = -1;
bool regress_depth_check = false;
bool regress_held_check = false;
int regress_menu_kind = REGRESS_MENU_NONE;

int regress_stick = 0;
int regress_stick_x = 0;
int regress_stick_y = 0;
const char *regress_stick_log_path = NULL;
const char *regress_bike_set = NULL;
int regress_reverse_y = 0;

// --regress-front-weapon / --regress-front-power: -1 = leave the built-in
// loadout.  Applied by regress_apply_loadout() right after JE_initPlayerData()
// on the --regress-script path.
int regress_front_weapon = -1;
int regress_front_power = -1;
int regress_loadout_widest = 0;
int regress_xmas = 0;

// Width of an item name as the HUD draws it: the data pads names with spaces.
static int trimmed_name_width(const char *name)
{
	char copy[64];
	size_t n = strlen(name);

	while (n > 0 && name[n - 1] == ' ')
		--n;
	n = MIN(n, sizeof copy - 1);
	memcpy(copy, name, n);
	copy[n] = '\0';
	return JE_textWidth(copy, TINY_FONT);
}

// The two widest of `count` items (ids 1..count) by name, best first.
#define WIDEST_TWO(best, second, count, name_of) \
	do { \
		int best_w = -1, second_w = -1; \
		for (unsigned int id_ = 1; id_ <= (count); ++id_) \
		{ \
			const int w_ = trimmed_name_width(name_of(id_)); \
			if (w_ > best_w) { second = best; second_w = best_w; best = id_; best_w = w_; } \
			else if (w_ > second_w) { second = id_; second_w = w_; } \
		} \
	} while (0)

#define PORT_NAME(id) (weaponPort[id].name)
#define OPTION_NAME(id) (options[id].name)
#define POWER_NAME(id) (powerSys[id].name)

static void apply_widest_loadout(PlayerItems *items)
{
	unsigned int port = 1, port2 = 1, side = 1, side2 = 1, gen = 1, unused = 1;

	WIDEST_TWO(port, port2, gameSchema()->port_max, PORT_NAME);
	WIDEST_TWO(side, side2, gameSchema()->sidekick_max, OPTION_NAME);
	WIDEST_TWO(gen, unused, gameSchema()->generator_max, POWER_NAME);
	(void)unused;

	items->weapon[FRONT_WEAPON].id = (JE_byte)port;
	items->weapon[REAR_WEAPON].id = (JE_byte)port2;
	items->sidekick[LEFT_SIDEKICK] = (JE_byte)side;
	items->sidekick[RIGHT_SIDEKICK] = (JE_byte)side2;
	items->generator = (JE_byte)gen;
}

// The most distinctive items of the variant's data, picked by their properties
// so no id is hard-coded: the highest ship, the Flying Punch sidekick (its
// weapon has the 198 trail) and a chargeable sidekick.  On 2.1 (no Punch) the
// right sidekick is a plain one, so the option stays usable on both variants.
static void apply_new_items_loadout(PlayerItems *items)
{
	const GameDataSchema *schema = gameSchema();
	unsigned int punch = 0, punch_sidekick = 0, charged = 0, plain = 0;

	for (unsigned int w = 1; w <= schema->weapon_max && punch == 0; ++w)
		if (weapons[w].trail == 198 && weapons[w].multi > 1)
			punch = w;
	for (unsigned int i = 1; i <= schema->sidekick_max; ++i)
	{
		if (punch != 0 && options[i].wpnum == punch && punch_sidekick == 0)
			punch_sidekick = i;
		if (options[i].wport == 0 || options[i].ammo != 0)
			continue;
		if (options[i].pwr > 0 && charged == 0)
			charged = i;
		if (options[i].pwr == 0)
			plain = i;
	}
	if (punch_sidekick == 0)
		punch_sidekick = plain;

	items->ship = (JE_byte)schema->ship_max;
	items->weapon[FRONT_WEAPON].power = 5;
	items->weapon[REAR_WEAPON].id = 15;   // Vulcan Cannon: the same id in both variants
	items->weapon[REAR_WEAPON].power = 3;
	items->sidekick[LEFT_SIDEKICK] = (JE_byte)(charged != 0 ? charged : 1);
	items->sidekick[RIGHT_SIDEKICK] = (JE_byte)(punch_sidekick != 0 ? punch_sidekick : 2);
	items->generator = (JE_byte)MIN(4u, schema->generator_max);
}

// --regress-fire: a scenario has no recorded input, so this holds the fire
// button, pulses both sidekick buttons and sweeps the ship across the playfield.
// It is a pure function of the tick count, so runs stay reproducible.
void regress_scenario_input(void)
{
	static unsigned long tick = 0;
	const unsigned long phase = tick++ % 480;

	button[0] = true;
	button[1] = (tick / 30) % 2 == 0;
	button[2] = (tick / 45) % 2 == 0;
	button[3] = false;
	player[0].x += phase < 240 ? 1 : -1;
}

void regress_apply_loadout(void)
{
	if (regress_front_weapon < 0 && regress_front_power < 0)
		return;

	if (regress_front_weapon >= 0)
		player[0].items.weapon[FRONT_WEAPON].id = (JE_byte)regress_front_weapon;
	if (regress_front_power >= 0)
		player[0].items.weapon[FRONT_WEAPON].power = (JE_byte)regress_front_power;

	// The engine snapshots last_items for the pause/items screens; keep it
	// consistent with the override.
	player[0].last_items = player[0].items;
}

// Per-tick stick-trajectory log (--regress-stick-log).
static FILE *regress_stick_log = NULL;
static unsigned long regress_stick_tick = 0;

// True once the --regress-menu request has been handed to the level loop, so it
// fires on exactly one frame.
static bool regress_menu_taken = false;

// Snapshot requests (--regress-snapshot=FRAME:FILE), repeatable.  Fixed size:
// a run needs only a handful and parsing must not allocate per frame.
#define REGRESS_MAX_SNAPSHOTS 16

typedef struct
{
	unsigned long frame;
	const char *path;
} RegressSnapshot;

static RegressSnapshot regress_snapshots[REGRESS_MAX_SNAPSHOTS];
static int regress_snapshot_count = 0;

// 64-bit FNV-1a.
static const Uint64 fnv_offset_basis = UINT64_C(14695981039346656037);
static const Uint64 fnv_prime        = UINT64_C(1099511628211);

// Virtual frame-pacing clock, UQ22.10 milliseconds.
static Uint32 regress_clock = 0;

static FILE *regress_out = NULL;
static FILE *regress_state_out = NULL;
static unsigned long regress_frame = 0;

// Smoothness diagnostics: log the presented frame of the first few ticks whose
// interpolation showed non-monotonic motion.
static unsigned long regress_smooth_seen_events = 0;
static unsigned regress_smooth_log_count = 0;

// Gameplay-composition assertion (--regress-gameplay-check): while a level is
// being presented in Modern, every frame must drop the classic sidebar, and
// every frame in a level brightness *fade ramp* must mirror that brightness on
// the HUD (otherwise the panels flash at full brightness over the darkened
// playfield).  A colour override or a static brightness offset is not mirrored.
static unsigned long regress_gameplay_frames = 0;
static unsigned long regress_gameplay_missing = 0;
static char regress_gameplay_first[128] = "";
static unsigned long regress_gameplay_filter_frames = 0;
static unsigned long regress_gameplay_filter_missing = 0;
static char regress_gameplay_filter_first[128] = "";

// Dynamic fade/HUD interpolation check (--regress-smooth-effects-check): per
// presented Modern frame, probe the recorded HUD bars at alpha = 0.5, and the
// palette-fade counters are read from interp.c.
static unsigned long regress_bar_moved = 0;
static unsigned long regress_bar_unchanged = 0;
static unsigned long regress_bar_bad = 0;

static void regress_check_smooth_effects(void)
{
	if (!regress_smooth_effects_check)
		return;

	modern_hud_bar_interp_probe(&regress_bar_moved, &regress_bar_unchanged, &regress_bar_bad);
}

static void regress_check_gameplay_composition(void)
{
	if (!regress_gameplay_check || !modern_in_level_period() || !modern_hud_in_panels())
		return;

	regress_gameplay_frames++;
	if (!modern_last_frame_gameplay_panels())
	{
		if (regress_gameplay_missing == 0)
			snprintf(regress_gameplay_first, sizeof regress_gameplay_first,
			         "frame %lu: in-level frame used the full-frame composition "
			         "(gameplay=%d)", regress_frame, modern_last_frame_gameplay() ? 1 : 0);
		regress_gameplay_missing++;
	}

	// The engine darkens the playfield in place while a brightness ramp is
	// running (the level-start fade-in and filterFade events); the HUD panels
	// and message strip must mirror exactly that brightness ramp, or they show
	// at full brightness over a dark playfield (the level-start flash).  A
	// colour override or a static offset is deliberately NOT mirrored.
	const bool level_fade = levelBrightness != -99 && explosionTransparent &&
	                        filterFade && levelBrightnessChg != 0;
	if (level_fade)
	{
		regress_gameplay_filter_frames++;
		if (!modern_last_frame_hud_filtered())
		{
			if (regress_gameplay_filter_missing == 0)
				snprintf(regress_gameplay_filter_first, sizeof regress_gameplay_filter_first,
				         "frame %lu: brightness fade active (int=%d) but the HUD "
				         "did not follow it", regress_frame, levelBrightness);
			regress_gameplay_filter_missing++;
		}
	}
}

// Attract-demo HUD assertion (--regress-demo-hud-check): a played demo (or
// scenario) follows the active mode's HUD.  While the level is presented,
// Modern with side panels must compose the gameplay panels with both panels
// holding HUD pixels; Classic (and Modern without room for panels) must keep
// the original sidebar and never the panels.  A run that never saw an in-level
// frame fails, so the check cannot pass by observing nothing.
static unsigned long regress_demohud_frames = 0;
static unsigned long regress_demohud_bad = 0;
static bool regress_demohud_filled = false;  // a frame with both panels drawn was seen
static char regress_demohud_first[160] = "";
// "INSERT COIN" must not be an emitter.  The text is drawn over live objects,
// so the tags under it are not zero; what matters is that drawing the text adds
// none: pixels whose tag / light colour was clear before the text and is set
// after it.  mainint.c brackets the draw with the two probes below.
static unsigned long regress_coin_draws = 0;
static unsigned long regress_coin_tagged = 0;
static unsigned long regress_coin_lcol = 0;
static unsigned long regress_coin_layer_shot = 0;
static Uint8 regress_coin_tag0[12 * 160];
static Uint8 regress_coin_lcol0[12 * 160];
static Uint8 regress_coin_layer0[12 * 160];

void regress_coin_probe(bool after, int x0, int y0, int w)
{
	if (!regress_demo_hud_check || w > 160)
		return;

	int pitch = 0, tw = 0, th = 0;
	const Uint8 *tag = drawlist_tag_for_surface(VGAScreen, &pitch, &tw, &th);
	const Uint8 *lcol = drawlist_lightcol_for_surface(VGAScreen, NULL, NULL, NULL);
	int lw = 0, lh = 0, lpitch = 0;
	const Uint8 *layer = drawlist_layer_for_surface(VGAScreen, &lpitch, &lw, &lh);
	if (tag == NULL || lcol == NULL)
		return;

	for (int y = 0; y < 12; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t o = (size_t)(y0 + y) * pitch + (size_t)(x0 + x);
			const size_t i = (size_t)y * 160 + (size_t)x;
			const Uint8 lay = layer != NULL ? layer[(size_t)(y0 + y) * lpitch + (size_t)(x0 + x)] : 0;
			if (!after)
			{
				regress_coin_tag0[i] = tag[o];
				regress_coin_lcol0[i] = lcol[o];
				regress_coin_layer0[i] = lay;
			}
			else
			{
				if (regress_coin_tag0[i] == 0 && tag[o] != 0)
					regress_coin_tagged++;
				if (regress_coin_lcol0[i] == 0 && lcol[o] != 0)
					regress_coin_lcol++;
				if (lay == DL_LAYER_PLAYER_SHOT && regress_coin_layer0[i] != DL_LAYER_PLAYER_SHOT)
					regress_coin_layer_shot++;
			}
		}
	if (after)
		regress_coin_draws++;
}

static void regress_demohud_fail(const char *what)
{
	if (regress_demohud_bad == 0)
		snprintf(regress_demohud_first, sizeof regress_demohud_first, "frame %lu: %s",
		         regress_frame, what);
	regress_demohud_bad++;
}

static bool regress_surface_has_pixels(const SDL_Surface *surface)
{
	if (surface == NULL)
		return false;

	for (int y = 0; y < surface->h; ++y)
	{
		const Uint8 *row = (const Uint8 *)surface->pixels + (size_t)y * surface->pitch;
		for (int x = 0; x < surface->w; ++x)
			if (row[x] != 0)
				return true;
	}
	return false;
}

// `classic_surface` is the presented 320x200 frame of a non-Modern present, else NULL.
static void regress_check_demo_hud(const SDL_Surface *classic_surface)
{
	if (!regress_demo_hud_check || !playDemo || !modern_in_level_period())
		return;

	if (classic_surface == NULL && modern_hud_in_panels())
	{
		regress_demohud_frames++;
		if (!modern_last_frame_gameplay_panels())
			regress_demohud_fail("Modern demo frame without the side panels");
		else if (regress_surface_has_pixels(modern_hud_surface(0)) &&
		         regress_surface_has_pixels(modern_hud_surface(1)))
			regress_demohud_filled = true;
		else if (regress_demohud_filled)  // the level-intro fade-in precedes the first HUD
			regress_demohud_fail("Modern demo frame with an empty HUD panel");
		return;
	}

	// Classic frame, or Modern without panels: the original sidebar stays.
	regress_demohud_frames++;
	if (modern_last_frame_gameplay_panels())
		regress_demohud_fail("classic demo frame composed the Modern panels");
	else if (classic_surface != NULL)
	{
		bool sidebar = false;
		for (int y = 0; y < classic_surface->h && !sidebar; ++y)
		{
			const Uint8 *row = (const Uint8 *)classic_surface->pixels + (size_t)y * classic_surface->pitch;
			for (int x = 264; x < vga_width; ++x)
				if (row[x] != 0) { sidebar = true; break; }
		}
		if (!sidebar)
			regress_demohud_fail("classic demo frame without the classic sidebar");
	}
}

static void regress_note_smoothness(void)
{
	if (!drawlist_smoothness_enabled())
		return;

	const unsigned long events = drawlist_smoothness_events();
	if (events > regress_smooth_seen_events && regress_smooth_log_count < 8)
	{
		logError("Smoothness: %lu event(s) by presented frame %lu.",
		         events - regress_smooth_seen_events, regress_frame);
		regress_smooth_log_count++;
	}
	regress_smooth_seen_events = events;
}

bool regress_active(void)
{
	return regress_demo != 0 || regress_scenario_active() || regress_screen_active() || regress_script_active() ||
	       regress_flow_active();
}

bool regress_realtime_active(void)
{
	return regress_realtime != 0;
}

bool regress_scenario_active(void)
{
	// --regress-script uses the same episode/level but reaches the level through
	// the episode script, so it is not the synthetic-scenario path.
	return regress_scenario_episode != 0 && !regress_script;
}

bool regress_script_active(void)
{
	return regress_script && regress_scenario_episode != 0;
}

bool regress_screen_active(void)
{
	return regress_screen != NULL;
}

bool regress_stick_active(void)
{
	return regress_stick != 0;
}

void regress_stick_log_tick(int player_x, int player_y, int x_velocity, int y_velocity)
{
	if (regress_stick_log == NULL)
		return;

	++regress_stick_tick;
	fprintf(regress_stick_log, "%lu %d %d %d %d\n",
	        regress_stick_tick, player_x, player_y, x_velocity, y_velocity);
}

int regress_take_menu_request(void)
{
	if (regress_menu_kind == REGRESS_MENU_NONE || regress_menu_taken || regress_frames <= 0)
		return REGRESS_MENU_NONE;

	// Open on the run's last presented frame.  The in-level menus loop forever
	// on input, so the frame cap ends the run on this first menu frame.
	if (regress_frame + 1 < (unsigned long)regress_frames)
		return REGRESS_MENU_NONE;

	regress_menu_taken = true;
	if (regress_data_audit_root != NULL)
		logInfo("Menu coverage: in-level request %d taken.", regress_menu_kind);
	return regress_menu_kind;
}

bool regress_audio_active(void)
{
	return regress_audio != 0;
}

static bool arg_is_option(const char *arg, const char *option, size_t option_len)
{
	return strncmp(arg, option, option_len) == 0 &&
	       (arg[option_len] == '\0' || arg[option_len] == '=');
}

bool regress_scan_args(int argc, char *argv[])
{
	// The audit must start before provider validation and config loading.
	for (int i = 1; i < argc; ++i)
	{
		if (strncmp(argv[i], "--regress-handoff=", 18) == 0)
			regress_handoff = argv[i] + 18;
		if (strncmp(argv[i], "--regress-data-audit=", 21) == 0)
			regress_data_audit_root = argv[i] + 21;
	}
	static const char *const demo_option     = "--regress-demo";
	static const char *const scenario_option = "--regress-level";
	static const char *const audio_option    = "--regress-audio";
	static const char *const screen_option   = "--regress-screen";
	static const char *const script_option   = "--regress-script";
	static const char *const flow_option     = "--regress-flow";

	for (int i = 1; i < argc; ++i)
	{
		if (arg_is_option(argv[i], demo_option, strlen(demo_option)) ||
		    arg_is_option(argv[i], scenario_option, strlen(scenario_option)) ||
		    arg_is_option(argv[i], audio_option, strlen(audio_option)) ||
		    arg_is_option(argv[i], screen_option, strlen(screen_option)) ||
		    arg_is_option(argv[i], script_option, strlen(script_option)) ||
		    arg_is_option(argv[i], flow_option, strlen(flow_option)) ||
		    arg_is_option(argv[i], "--regress-crt-check", strlen("--regress-crt-check")) ||
		    arg_is_option(argv[i], "--regress-user-files", strlen("--regress-user-files")))
			return true;
	}

	regress_data_audit_root = regress_handoff = NULL;
	return false;
}

Uint32 regress_clock_ticks10bit(void)
{
	if (!regress_active() || regress_realtime_active())
		return (Uint32)SDL_GetTicks() << 10;

	return regress_clock;
}

void regress_clock_advance_to(Uint32 target)
{
	if (target > regress_clock)
		regress_clock = target;
}

static void hash_bytes(Uint64 *hash, const Uint8 *data, size_t size)
{
	for (size_t i = 0; i < size; ++i)
	{
		*hash ^= data[i];
		*hash *= fnv_prime;
	}
}

// Hash a scalar as explicit little-endian bytes of a fixed width, so the
// digest does not depend on the host's `long`/`int` widths or endianness.
// The widths match what the native-size hashing produced on the LP64
// baselines (macOS/Linux), so those baselines stay valid.
static void hash_u8(Uint64 *hash, Uint8 v)
{
	hash_bytes(hash, &v, 1);
}

static void hash_u16le(Uint64 *hash, Uint16 v)
{
	const Uint8 b[2] = { (Uint8)v, (Uint8)(v >> 8) };
	hash_bytes(hash, b, sizeof b);
}

static void hash_u32le(Uint64 *hash, Uint32 v)
{
	const Uint8 b[4] = { (Uint8)v, (Uint8)(v >> 8), (Uint8)(v >> 16), (Uint8)(v >> 24) };
	hash_bytes(hash, b, sizeof b);
}

static void hash_u64le(Uint64 *hash, Uint64 v)
{
	Uint8 b[8];
	for (int i = 0; i < 8; ++i)
		b[i] = (Uint8)(v >> (8 * i));
	hash_bytes(hash, b, sizeof b);
}

Uint64 regress_fnv1a(const void *data, size_t size)
{
	Uint64 hash = fnv_offset_basis;

	hash_bytes(&hash, data, size);

	return hash;
}

FILE *regress_output_file(void)
{
	return regress_out;
}

// Hash of the whole gameplay state, presentation-independent.  Deliberately
// skips the pointer fields of the enemy structs (their addresses depend on the
// process layout) and hashes everything else field by field so no struct
// padding can leak in.
static Uint64 regress_state_hash(void)
{
	Uint64 hash = fnv_offset_basis;

	hash_u64le(&hash, (Uint64)mt_rand_state_hash());

	// `power` is not part of the stock state hash.  With the bike active it is
	// the one new input, so it (and the bike state) is folded in only then,
	// which keeps every existing baseline byte-identical.
	if (bike_hash_active())
	{
		hash_u32le(&hash, (Uint32)power);
		hash_u32le(&hash, (Uint32)lastPower);
		hash_u64le(&hash, bike_state_digest());
	}

	for (int i = 0; i < 2; ++i)
	{
		const Player *p = &player[i];

		// Scalars are hashed as explicit little-endian fixed-width values so
		// the digest is identical on LP64 (macOS/Linux) and LLP64 (Windows,
		// where `unsigned long` is 4 bytes).  `cash` is an unsigned long that
		// fits in 32 bits, so zero-extending it to 64 bits reproduces the 8
		// bytes the LP64 baselines hash; every other scalar keeps its width.
		hash_u64le(&hash, (Uint64)p->cash);
		hash_bytes(&hash, (const Uint8 *)&p->items, sizeof p->items);
		hash_bytes(&hash, (const Uint8 *)&p->last_items, sizeof p->last_items);
		hash_u8(&hash, p->is_dragonwing ? 1 : 0);
		hash_u32le(&hash, (Uint32)p->shield_max);
		hash_u32le(&hash, (Uint32)p->initial_armor);
		hash_u32le(&hash, (Uint32)p->shot_hit_area_x);
		hash_u32le(&hash, (Uint32)p->shot_hit_area_y);
		hash_u8(&hash, p->is_alive ? 1 : 0);
		hash_u32le(&hash, (Uint32)p->invulnerable_ticks);
		hash_u32le(&hash, (Uint32)p->exploding_ticks);
		hash_u32le(&hash, (Uint32)p->shield);
		hash_u32le(&hash, (Uint32)p->armor);
		hash_u32le(&hash, (Uint32)p->weapon_mode);
		hash_u32le(&hash, (Uint32)p->superbombs);
		hash_u32le(&hash, (Uint32)p->purple_balls_needed);
		hash_u16le(&hash, p->mouseX);
		hash_u16le(&hash, p->mouseY);
		hash_u32le(&hash, (Uint32)p->x);
		hash_u32le(&hash, (Uint32)p->y);
		for (int j = 0; j < 20; ++j)
			hash_u32le(&hash, (Uint32)p->old_x[j]);
		for (int j = 0; j < 20; ++j)
			hash_u32le(&hash, (Uint32)p->old_y[j]);
		hash_u32le(&hash, (Uint32)p->x_velocity);
		hash_u32le(&hash, (Uint32)p->y_velocity);
		hash_u32le(&hash, (Uint32)p->x_friction_ticks);
		hash_u32le(&hash, (Uint32)p->y_friction_ticks);
		hash_u32le(&hash, (Uint32)p->delta_x_shot_move);
		hash_u32le(&hash, (Uint32)p->delta_y_shot_move);
		hash_u32le(&hash, (Uint32)p->last_x_shot_move);
		hash_u32le(&hash, (Uint32)p->last_y_shot_move);
		hash_u32le(&hash, (Uint32)p->last_x_explosion_follow);
		hash_u32le(&hash, (Uint32)p->last_y_explosion_follow);
		// sidekick/items are all fixed-width fields with no `long`, so their
		// raw bytes are the same on LP64 and LLP64.
		hash_bytes(&hash, (const Uint8 *)p->sidekick, sizeof p->sidekick);

		const Uint8 lives = (p->lives != NULL) ? *p->lives : 0;
		hash_u8(&hash, lives);
	}

	// Enemies: every field except the two pointers (sprite2s, enemydatofs),
	// whose addresses vary with the process layout.  The struct holds only
	// fixed-width fields plus those pointers, so the byte ranges (and the zero
	// padding) are the same on LP64 and LLP64.
	for (unsigned int i = 0; i < COUNTOF(enemy); ++i)
	{
		const Uint8 *base = (const Uint8 *)&enemy[i];
		hash_bytes(&hash, base, offsetof(struct JE_SingleEnemyType, sprite2s));
		hash_bytes(&hash, base + offsetof(struct JE_SingleEnemyType, exrev),
		                 offsetof(struct JE_SingleEnemyType, enemydatofs) - offsetof(struct JE_SingleEnemyType, exrev));
		hash_bytes(&hash, base + offsetof(struct JE_SingleEnemyType, edamaged),
		                 sizeof(struct JE_SingleEnemyType) - offsetof(struct JE_SingleEnemyType, edamaged));
	}
	hash_bytes(&hash, (const Uint8 *)enemyAvail, sizeof enemyAvail);
	hash_bytes(&hash, (const Uint8 *)enemyShot, sizeof enemyShot);
	hash_bytes(&hash, (const Uint8 *)enemyShotAvail, sizeof enemyShotAvail);
	hash_bytes(&hash, (const Uint8 *)playerShotData, sizeof playerShotData);
	hash_bytes(&hash, (const Uint8 *)shotAvail, sizeof shotAvail);
	hash_bytes(&hash, (const Uint8 *)boss_bar, sizeof boss_bar);
	hash_u16le(&hash, tempW);
	hash_u16le(&hash, eventLoc);
	hash_u16le(&hash, curLoc);
	hash_u8(&hash, levelTimer ? 1 : 0);
	hash_u16le(&hash, levelTimerCountdown);
	hash_bytes(&hash, (const Uint8 *)explosions, sizeof explosions);
	hash_bytes(&hash, (const Uint8 *)superpixels, sizeof superpixels);
	hash_bytes(&hash, (const Uint8 *)rep_explosions, sizeof rep_explosions);

	return hash;
}

// Metadata-only checks deliberately exclude progress from the game-state hash.
// Observer OFF/ON comparisons therefore still cover gameplay and the RNG.
static ModernProgressState progress_previous;
static bool progress_seen;
static unsigned long progress_samples, progress_attempts, progress_resets;
static unsigned long progress_advances, progress_events, progress_jumps;
static unsigned long progress_returns, progress_rewinds, progress_waits;
static unsigned long progress_successes, progress_blocked;
static unsigned long progress_boss_waits, progress_loop_waits, progress_shot_samples;
static unsigned long progress_early_ends, progress_boss_resumes, progress_loop_resumes;
static unsigned long progress_post_boss_advances, progress_supported_loop_samples;
static bool progress_boss_seen;

static void regress_check_progress(void)
{
	if (!regress_progress_check || regress_observer_off)
		return;
	const ModernProgressState *current = modern_progress_snapshot();
	if (current->attempt == 0)
		return;  // Classic and pre-level setup are inert.
	bool same = progress_seen && current->attempt == progress_previous.attempt;
	bool bounds = (current->percent >= 0 && current->percent <= 100) ||
	              (!current->supported && current->percent == -1);
	if (!bounds || ((current->boss_wait || current->loop_wait) && !current->paused) ||
	    (current->percent == 100 && !current->completed) ||
	    (current->completed && current->percent != 100) ||
	    (same && current->percent < progress_previous.percent) ||
	    (same && current->paused && !current->completed &&
	     current->percent != progress_previous.percent))
	{
		logError("Progress check FAILED: observer bounds, monotonicity, wait or success latch.");
		exit(EXIT_FAILURE);
	}
	if (!same)
	{
		++progress_attempts;
		progress_boss_seen = false;
		if (current->ticks == 0)
		{
			if (current->percent != (current->supported ? 0 : -1) || current->completed)
			{
				logError("Progress check FAILED: new attempt retained prior progress.");
				exit(EXIT_FAILURE);
			}
			++progress_resets;
		}
	}
	++progress_samples;
	if (same && progress_previous.boss_wait && !current->boss_wait && !current->ended)
		++progress_boss_resumes;
	if (same && progress_previous.loop_wait && !current->loop_wait && !current->ended)
		++progress_loop_resumes;
	if (current->boss_wait) progress_boss_seen = true;
	if (same && progress_boss_seen && !current->boss_wait && !current->completed &&
	    current->advances > progress_previous.advances)
		progress_post_boss_advances += current->advances - progress_previous.advances;
	progress_early_ends += current->early_ends - (same ? progress_previous.early_ends : 0);
	for (unsigned i = 0; i < COUNTOF(shotAvail); ++i)
		if (shotAvail[i] != 0)
		{
			++progress_shot_samples;
			break;
		}
	progress_advances += current->advances - (same ? progress_previous.advances : 0);
	progress_events += current->events - (same ? progress_previous.events : 0);
	progress_jumps += current->jumps - (same ? progress_previous.jumps : 0);
	progress_returns += current->returns - (same ? progress_previous.returns : 0);
	progress_rewinds += current->rewinds - (same ? progress_previous.rewinds : 0);
	if (current->paused) ++progress_waits;
	if (current->boss_wait) ++progress_boss_waits;
	if (current->loop_wait) ++progress_loop_waits;
	if (current->loop_wait && current->supported) ++progress_supported_loop_samples;
	if (current->completed) ++progress_successes;
	if (current->blocked) ++progress_blocked;
	progress_previous = *current;
	progress_seen = true;
}

// Writes the frame and/or state record(s) for the current frame and honors the
// scenario frame cap.  Either stream may be absent; at least one is open.
static void regress_emit_records(bool write_frame, Uint64 frame_hash)
{
	regress_check_progress();
	if (write_frame)
		fprintf(regress_out, "%lu %016" PRIx64 "\n", regress_frame, frame_hash);

	if (regress_state_out != NULL)
		fprintf(regress_state_out, "%lu %016" PRIx64 "\n", regress_frame, regress_state_hash());

	regress_frame++;

	if (regress_frames > 0 && regress_frame >= (unsigned long)regress_frames)
	{
		// Scenario length cap.  This is the last frame we want, so flush and
		// leave; there is no clean way to unwind the original in-level loop and
		// every frame we care about has already been captured.
		regress_finish();
		exit(EXIT_SUCCESS);
	}
}

bool regress_add_snapshot(unsigned long frame, const char *path)
{
	if (regress_snapshot_count >= REGRESS_MAX_SNAPSHOTS)
		return false;

	regress_snapshots[regress_snapshot_count].frame = frame;
	regress_snapshots[regress_snapshot_count].path = path;
	++regress_snapshot_count;
	return true;
}

bool regress_has_snapshots(void)
{
	return regress_snapshot_count > 0;
}

// Saves `surface` (8-bit) to every snapshot requested for the current frame,
// attaching the active palette so the BMP is viewable.
static void regress_save_snapshots_8bit(const SDL_Surface *surface)
{
	for (int i = 0; i < regress_snapshot_count; ++i)
	{
		if (regress_snapshots[i].frame != regress_frame)
			continue;

		SDL_Surface *tmp = SDL_CreateSurface(surface->w, surface->h, SDL_PIXELFORMAT_INDEX8);
		if (tmp == NULL)
		{
			logError("Failed to create snapshot surface: %s", SDL_GetError());
			continue;
		}

		const int row = MIN(surface->w, tmp->w);
		for (int y = 0; y < surface->h && y < tmp->h; ++y)
			memcpy((Uint8 *)tmp->pixels + (size_t)y * tmp->pitch,
			       (const Uint8 *)surface->pixels + (size_t)y * surface->pitch, (size_t)row);

		SDL_Palette *palette = SDL_CreatePalette(256);
		if (palette != NULL)
		{
			SDL_SetPaletteColors(palette, get_active_palette(), 0, 256);
			SDL_SetSurfacePalette(tmp, palette);
		}

		if (!SDL_SaveBMP(tmp, regress_snapshots[i].path))
			logError("Failed to save snapshot '%s': %s", regress_snapshots[i].path, SDL_GetError());
		else
			logInfo("Regression: saved snapshot frame %lu to '%s'.", regress_frame, regress_snapshots[i].path);

		if (palette != NULL)
			SDL_DestroyPalette(palette);
		SDL_DestroySurface(tmp);
	}
}

// Saves the current Modern canvas (XRGB8888) to every requested snapshot.
static void regress_save_snapshots_modern(const ModernFrame *frame)
{
	for (int i = 0; i < regress_snapshot_count; ++i)
	{
		if (regress_snapshots[i].frame != regress_frame)
			continue;

		SDL_Surface *tmp = SDL_CreateSurfaceFrom(frame->w, frame->h, SDL_PIXELFORMAT_XRGB8888,
		                                         frame->pixels, frame->pitch);
		if (tmp == NULL)
		{
			logError("Failed to wrap the modern canvas for a snapshot: %s", SDL_GetError());
			continue;
		}

		if (!SDL_SaveBMP(tmp, regress_snapshots[i].path))
			logError("Failed to save snapshot '%s': %s", regress_snapshots[i].path, SDL_GetError());
		else
			logInfo("Regression: saved snapshot frame %lu to '%s'.", regress_frame, regress_snapshots[i].path);

		SDL_DestroySurface(tmp);
	}
}

void regress_capture_frame(SDL_Surface *surface)
{
	if (surface == NULL)
		return;
	if (regress_out == NULL && regress_state_out == NULL && !regress_has_snapshots())
		return;

	const bool write_frame = regress_out != NULL;
	Uint64 hash = fnv_offset_basis;

	if (write_frame)
	{
		assert(SDL_BITSPERPIXEL(surface->format) == 8);

		// Hash only the visible 320 bytes of each row, honoring the pitch.
		const Uint8 *pixels = surface->pixels;
		const size_t row_size = MIN((size_t)surface->w, (size_t)vga_width);
		for (int y = 0; y < surface->h; ++y)
		{
			hash_bytes(&hash, pixels, row_size);
			pixels += surface->pitch;
		}

		// Hash the palette that is currently being presented.
		const SDL_Color *palette = get_active_palette();
		for (size_t i = 0; i < 256; ++i)
		{
			const Uint8 rgb[3] = { palette[i].r, palette[i].g, palette[i].b };
			hash_bytes(&hash, rgb, sizeof rgb);
		}
	}

	if (regress_has_snapshots())
		regress_save_snapshots_8bit(surface);

	regress_check_demo_hud(surface);
	regress_note_smoothness();
	regress_emit_records(write_frame, hash);
}

// --regress-layer-png=FRAME:FILE: writes the presented depth layer buffer of the
// requested presented frame as a false-colour PNG (local review only).
static void regress_capture_layer_png(void)
{
	if (regress_layer_png == NULL || regress_frame != regress_layer_png_frame)
		return;

	if (!modern_depth_save_png(regress_layer_png))
		logError("Failed to save the layer PNG '%s' (no gameplay frame was presented at frame %lu).",
		         regress_layer_png, regress_frame);
	else
		logInfo("Regression: saved layer PNG frame %lu to '%s'.", regress_frame, regress_layer_png);
}

void regress_capture_modern_frame(void)
{
	if (regress_out == NULL && regress_state_out == NULL && !regress_has_snapshots())
		return;

	const ModernFrame *frame = modern_current_frame();
	if (frame == NULL || frame->pixels == NULL)
		return;
	if (regress_screen != NULL)
		regress_screen_verify_mapping();

	const bool write_frame = regress_out != NULL;
	Uint64 hash = fnv_offset_basis;

	if (write_frame && regress_crt_mode < 0)
	{
		// Hash the visible XRGB bytes of each row, honoring the canvas pitch.
		// The palette is already applied to the canvas, so it is not hashed
		// separately.
		const Uint8 *pixels = (const Uint8 *)frame->pixels;
		const size_t row_size = (size_t)frame->w * sizeof(Uint32);
		for (int y = 0; y < frame->h; ++y)
		{
			hash_bytes(&hash, pixels, row_size);
			pixels += frame->pitch;
		}
	}

	if (regress_has_snapshots() && regress_crt_mode < 0)
		regress_save_snapshots_modern(frame);

	regress_check_gameplay_composition();
	regress_check_demo_hud(NULL);
	regress_check_smooth_effects();
	regress_note_smoothness();
	regress_capture_layer_png();
	if (regress_crt_mode < 0) regress_emit_records(write_frame, hash);
}

void regress_capture_crt_frame(const ModernFrame *frame)
{
	if (regress_crt_mode < 0) return;
	regress_capture_layer_png();
	Uint64 hash = fnv_offset_basis;
	for (int y = 0; y < frame->h; ++y)
		hash_bytes(&hash, (const Uint8 *)frame->pixels + (size_t)y * frame->pitch, (size_t)frame->w * 4);
	if (regress_frame == 0)
		logInfo("CRT coverage: mode=%s canvas=%dx200 output=%dx%d phase=0", crt_filter_names[regress_crt_mode],
		        modern_current_frame()->w, frame->w, frame->h);
	if (regress_has_snapshots()) regress_save_snapshots_modern(frame);
	regress_emit_records(regress_out != NULL, hash);
}

int regress_crt_window_w, regress_crt_window_h;
const char *regress_present_png;
unsigned long regress_present_png_frame;
void regress_capture_presented_frame(SDL_Renderer *renderer)
{
	if (!regress_active() || regress_present_png == NULL || regress_frame == 0 ||
	    regress_frame - 1 != regress_present_png_frame) return;
	SDL_Surface *surface = SDL_RenderReadPixels(renderer, NULL);
	if (surface == NULL || !SDL_SavePNG(surface, regress_present_png))
		logFatal("Failed to save presented PNG: %s", SDL_GetError());
	SDL_DestroySurface(surface);
	logInfo("CRT presented PNG: frame=%lu path=%s", regress_frame - 1, regress_present_png);
}

void regress_begin_scenario(void)
{
	assert(regress_scenario_active());

	// Mirror beginPlayDemo(): restart the RNG at its fixed seed and force the
	// difficulty, so scenario playback is as reproducible as demo playback.
	mt_srand(scenario_seed);
	difficultyLevel = DIFFICULTY_NORMAL;

	// Scenario mode intentionally cannot die before the smoothie event fires.
	// youAreCheating is the engine's own invincibility flag (the one the
	// F2+F3+F6 cheat toggles); it only suppresses player death, and only
	// scenario mode turns it on.  It is set here, not in regress_init(), because
	// main() resets it to false later; demo mode never reaches here.
	youAreCheating = true;

	// This selects tyrianN.lvl and loads the item tables (needed for ships[],
	// shields[] and the weapon fire code).
	JE_initEpisode(regress_scenario_episode);

	// Fixed level identity.  lvlFileNum is the 1-based index into tyrianN.lvl
	// (the same field the episode script's "L" line and the demo header set).
	// levelName/levelSong are arbitrary but must be constant.
	memset(levelName, 0, sizeof levelName);
	strncpy(levelName, "SCENARIO", sizeof levelName - 1);
	lvlFileNum = (JE_byte)regress_scenario_level;
	initial_episode_num = (JE_byte)regress_scenario_episode;
	levelSong = 1;

	// Fixed new-game loadout.  This keeps the frame hashes a function of the
	// engine and the level data only (no user config, no save file, no demo).
	player[0].items.ship = 1;                     // USP Talon
	player[0].items.generator = 2;                // Advanced MR-12
	player[0].items.shield = 4;                   // Gencore High Energy Shield
	player[0].items.weapon[FRONT_WEAPON].id = 1;  // Pulse Cannon
	player[0].items.weapon[FRONT_WEAPON].power = 1;
	player[0].items.weapon[REAR_WEAPON].id = 0;   // None
	player[0].items.weapon[REAR_WEAPON].power = 1;
	player[0].items.sidekick[LEFT_SIDEKICK] = 0;  // None
	player[0].items.sidekick[RIGHT_SIDEKICK] = 0;
	player[0].items.special = 0;                  // None
	player[0].items.sidekick_series = 0;
	player[0].items.sidekick_level = 0;
	player[0].items.super_arcade_mode = 0;

	// --regress-arcade turns the scenario into a 1-player arcade run (the same
	// `onePlayerAction` flag the 1-player arcade menu sets), so the arcade HUD
	// -- in particular the extra-lives row -- can be captured headless.
	if (regress_arcade)
	{
		onePlayerAction = true;
		player[0].items.weapon[FRONT_WEAPON].power = 6;  // extra lives (arcade)
		player[0].cash = 12345;
	}

	// --regress-players=2 turns the scenario into a deterministic two-player
	// run so the compact 2-player HUD can be captured headless.  Player 2 gets
	// a distinct loadout and some cash so the capture shows real data; player 1
	// gets extra lives (stored in the front weapon power field, as the engine
	// does) so the life icons are visible.
	if (regress_players == 2)
	{
		twoPlayerMode = true;

		player[0].items.weapon[FRONT_WEAPON].power = 6;
		player[0].items.weapon[REAR_WEAPON].id = 15;   // Vulcan Cannon
		player[0].items.weapon[REAR_WEAPON].power = 2;
		player[0].items.sidekick[LEFT_SIDEKICK] = 1;
		player[0].items.sidekick[RIGHT_SIDEKICK] = 2;
		player[0].weapon_mode = 1;
		player[0].cash = 2820;

		player[1].items = player[0].items;
		player[1].items.weapon[FRONT_WEAPON].id = 1;   // Pulse Cannon
		player[1].items.weapon[FRONT_WEAPON].power = 3;
		player[1].items.weapon[REAR_WEAPON].id = 15;   // Vulcan Cannon
		player[1].items.weapon[REAR_WEAPON].power = 2;
		player[1].items.sidekick[LEFT_SIDEKICK] = 2;
		player[1].items.sidekick[RIGHT_SIDEKICK] = 0;
		player[1].weapon_mode = 2;
		player[1].cash = 6789;
		player[1].last_items = player[1].items;
	}

	if (regress_loadout_new)
	{
		apply_new_items_loadout(&player[0].items);
		if (regress_players == 2)
			apply_new_items_loadout(&player[1].items);
	}

	if (regress_loadout_widest)
	{
		apply_widest_loadout(&player[0].items);
		if (regress_players == 2)
		{
			apply_widest_loadout(&player[1].items);
			player[1].last_items = player[1].items;
		}
	}

	regress_apply_loadout();  // --regress-front-weapon/-power (no-op without them)
	player[0].last_items = player[0].items;
}

void regress_init(void)
{
	if (regress_progress_check && regress_observer_off)
		logFatal("Progress check requires the observer enabled.");
	modern_progress_set_observer_enabled(!regress_observer_off);
	if (regress_flow_active())
		regress_flow_init();

	// Headless by default.  Respect a driver the caller set explicitly.  The
	// real-time pacing benchmark opens a real window on purpose.
	if (!regress_realtime_active() && SDL_getenv("SDL_VIDEO_DRIVER") == NULL)
		SDL_setenv_unsafe("SDL_VIDEO_DRIVER", "dummy", 1);
	if (SDL_getenv("SDL_AUDIO_DRIVER") == NULL)
		SDL_setenv_unsafe("SDL_AUDIO_DRIVER", "dummy", 1);

	// Regress playback needs neither audio nor joystick input, except the
	// offline audio regression, which drives the mixer directly.
	audio_disabled = !regress_audio_active();
	ignore_joystick = true;

	// No controller is opened and joystick hot-plug ADDED/REMOVED events are
	// ignored while ignore_joystick is set (see init_joysticks() and
	// joystick_device_added()/joystick_device_removed()).  handleSdlEvents()
	// additionally drops every input event in regress mode, so a device plugged
	// in mid-run cannot feed the game.
	//
	// Because that drop happens before any focus event is processed, redefine
	// the focused state directly: the release build auto-pauses while the window
	// is unfocused (see JE_main), and a regress run must never pause.
	windowHasFocus = true;

	// Pin every setting that can change the 8-bit framebuffer or the gameplay.
	// These mirror the defaults the engine uses when no config file is present,
	// but are forced so the user's config and save files cannot leak in.
	// processorTypeChoice is the requested detail level (the player's choice);
	// JE_applyProcessorType() below turns it into the effective processorType.
	// In the Modern presentation that pins the render detail to Pentium (4), or
	// keeps 6 while the SuperWild cheat is active, so a Modern case never
	// renders below Pentium; the Classic cases still sweep 1..6.  Classic
	// baselines are therefore unchanged.
	gameSpeed           = 4;                       // Normal
	processorTypeChoice = regress_detail;          // 1..6, sweepable
	gammaCorrection     = 0;                       // no gamma remap
	difficultyLevel     = DIFFICULTY_NORMAL;       // beginPlayDemo sets this too
	initialDifficulty   = DIFFICULTY_WIMP;         // fresh-run value used by scripts
	fullscreen_display  = -1;                      // windowed; no display needed
	youAreCheating      = false;
	fastPlay            = 0;
	pentiumMode         = false;
	starActive          = true;
	filterActive        = true;

	// Pin the presentation mode so the harness hashes the right buffer, without
	// the user's config leaking in (loadConfiguration() is skipped in regress
	// mode).  The Modern geometry is pinned too: the historic modern-* baselines
	// are 4:3 + original, and --regress-aspect opts into a wider canvas.
	presentation = (regress_modern || regress_realtime) ? PRESENTATION_MODERN : PRESENTATION_CLASSIC;
	if (regress_realtime_active())
		modern_aspect = regress_aspect >= 0 ? (ModernAspect)regress_aspect : MODERN_ASPECT_16_9;
	else
		modern_aspect = regress_aspect >= 0 ? (ModernAspect)regress_aspect : MODERN_ASPECT_4_3;
	modern_pixel_aspect = PIXEL_ASPECT_ORIGINAL;

	// VFX are pinned OFF for every existing baseline, so adding the particle
	// system does not change a single 8-bit frame; --regress-vfx opts a case
	// into a level (and the VFX cases run Modern 16:9, where they render).
	vfx_level = (regress_vfx >= 0) ? (VfxLevel)regress_vfx : VFX_OFF;

	// Record and replay-check every level tick.  This only observes: the draw
	// list is built from the existing drawing primitives and replayed into a
	// scratch surface; the real frame hash stream is unchanged.
	if (regress_replay_check)
	{
		drawlist_set_enabled(true);
		drawlist_set_check(true);
	}

	// Stage-3 proof: render each level tick's interpolated frame at alpha = 1
	// with the persistent renderer and compare it with the real frame.  Also
	// enable recording when a mid-tick snapshot is requested so there are two
	// lists to interpolate between.
	if (regress_interp_check || interp_regress_alpha_active())
		drawlist_set_enabled(true);
	if (regress_interp_check)
		drawlist_set_interp_check(true);

	// Stage-3 smoothness proof: re-derive the sub-frame positions for every
	// level tick and check they are monotonic.
	if (regress_interp_smoothness)
	{
		drawlist_set_enabled(true);
		drawlist_set_smoothness_check(true);
		drawlist_set_smoothness_alphas((unsigned)regress_smooth_alphas);
	}

	// Parallax guard: run the interpolated presentation every tick and require
	// it to leave the starfield and the background scroll untouched, so the
	// per-tick displacement is the same with the interpolation on or off.
	if (regress_parallax_check)
	{
		drawlist_set_enabled(true);
		drawlist_set_parallax_check(true);
	}

	// Depth layer buffer (stage 1): stamp it, and with the check also render the
	// interpolated frame at alpha = 1 every tick to compare the two buffers.
	// Neither touches game state; the flags only add the stamping.
	if (regress_layer_check)
	{
		drawlist_set_enabled(true);
		drawlist_set_layer_check(true);
	}
	if (regress_layer_check || regress_layer_png != NULL)
		modern_depth_set_requested(true);
	// The Depth setting is pinned Off for every existing case (the layer buffer
	// is then not stamped and the shadow pass does nothing), so the baselines
	// stay byte-for-byte unchanged; --regress-depth opts a run in.
	modern_depth_quality = regress_depth_quality >= 0 ? (ModernDepth)regress_depth_quality : MODERN_DEPTH_OFF;
	// The bike features are pinned off whatever the config says, so every
	// existing baseline stays unchanged; --regress-bike-feed turns master and the
	// generator on, --regress-bike-set then overrides any of it.
	bike_regress_pin(regress_bike_set);
	modern_held_set_check(regress_held_check);

	// Dynamic fade/HUD interpolation check: exercise the interpolated
	// presentation at a genuine mid-tick alpha and assert every interpolated
	// fade channel and HUD bar value stays between the two ticks.  The palette
	// fade path is inert in regress unless this (or --regress-interp-alpha)
	// asks for it.
	if (regress_smooth_effects_check)
	{
		drawlist_set_enabled(true);
		interp_set_regress_alpha(0.5);
		interp_set_fade_check(true);
		interp_fade_reset();
	}

	// Real-time pacing benchmark: log presented-fps statistics and exit after
	// the requested duration.
	if (regress_realtime_active())
		interp_bench_start(regress_bench_seconds);

	crt_filter_set_mode(regress_crt_mode >= 0 ? regress_crt_mode : CRT_FILTER_OFF);
	crt_filter_reset_phase();

	// Bloom and dynamic lighting are pinned OFF for every existing case so the
	// baselines stay byte-for-byte unchanged; --regress-lighting opts a run into
	// the new look (it sets both effects, the way the "Lighting" picker does),
	// and --regress-bloom is an advanced override of bloom only.  The user's
	// config cannot leak in because loadConfiguration() is skipped in regress
	// mode.
	if (regress_lighting_quality >= 0)
	{
		modern_bloom_quality = (ModernQuality)regress_lighting_quality;
		modern_lighting_quality = (ModernQuality)regress_lighting_quality;
	}
	else
	{
		modern_bloom_quality = regress_bloom_quality >= 0 ? (ModernQuality)regress_bloom_quality : MODERN_QUALITY_OFF;
		modern_lighting_quality = MODERN_QUALITY_OFF;
	}

	// --regress-seed pins the RNG for a regress run.  Demo playback and the
	// synthetic scenarios have their own fixed streams, but a --regress-script
	// run otherwise inherits main()'s time(NULL) seed, which makes it
	// irreproducible; the option lets such a run (e.g. a scripted death that
	// reaches the !playDemo GAME OVER) be a stable baseline.  Regress-only and
	// opt-in: it never affects a normal game.
	if (regress_seed_set)
		mt_srand(regress_seed);

	// Modern pins the effective detail to Pentium (4); 6 is kept for the
	// SuperWild cheat.  Log an explicit --regress-detail that the presentation
	// ignored, so the run log makes the override visible.
	if (presentation == PRESENTATION_MODERN && regress_detail != 4 && regress_detail != 6)
		logInfo("Modern presentation forces detail level 4 (requested %d).", regress_detail);

	JE_applyProcessorType();

	// Start the virtual clock at a fixed value; setFrameSpeed() re-anchors the
	// frame counters from it, so the absolute value is irrelevant.
	regress_clock = 0;
	regress_frame = 0;

	// A screen run always has a built-in frame cap: the screen functions loop
	// forever waiting for input, so the cap is what ends the run.
	if (regress_screen_active() && regress_frames == 0)
		regress_frames = REGRESS_SCREEN_FRAMES;

	// The episode script can block on an input-driven screen (the item screen)
	// once the WARNING text is done, so give --regress-script a default cap too.
	if (regress_script_active() && regress_frames == 0)
		regress_frames = REGRESS_SCREEN_FRAMES;

	if (regress_out_path == NULL && regress_state_out_path == NULL && !regress_has_snapshots() &&
	    !regress_realtime_active())
	{
		logFatal("--regress-demo/--regress-level/--regress-audio/--regress-screen require --regress-out=FILE, --regress-state-out=FILE or --regress-snapshot=FRAME:FILE.");
		exit(EXIT_FAILURE);
	}

	if (regress_audio_active() && regress_out_path == NULL)
	{
		// The audio baseline is the frame output stream; there is no state
		// stream for it.
		logFatal("--regress-audio requires --regress-out=FILE.");
		exit(EXIT_FAILURE);
	}

	if (regress_out_path != NULL)
	{
		regress_out = fopen(regress_out_path, "wb");
		if (regress_out == NULL)
		{
			logFatal("Failed to open regression output '%s'.", regress_out_path);
			exit(EXIT_FAILURE);
		}
	}

	if (regress_state_out_path != NULL)
	{
		regress_state_out = fopen(regress_state_out_path, "wb");
		if (regress_state_out == NULL)
		{
			logFatal("Failed to open regression state output '%s'.", regress_state_out_path);
			exit(EXIT_FAILURE);
		}
	}

	// --regress-stick installs one synthetic analog stick so JE_playerMovement
	// runs the whole stick path without hardware; --regress-stick-log records the
	// per-tick ship state for the Classic/Modern trajectory comparison.
	if (regress_stick_active())
	{
		joystick_inject_stick(regress_stick_x, regress_stick_y);

		if (regress_stick_log_path != NULL)
		{
			regress_stick_log = fopen(regress_stick_log_path, "wb");
			if (regress_stick_log == NULL)
			{
				logFatal("Failed to open stick log '%s'.", regress_stick_log_path);
				exit(EXIT_FAILURE);
			}
		}
	}

	// --regress-reverse-y forces the reverse-controls smoothie so the stick
	// inversion (Classic and Modern) can be exercised headless.  A level event
	// may clear it during level setup, so JE_playerMovement re-asserts it.
}

void regress_frame_reset(void)
{
	regress_frame = 0;
}

void regress_finish(void)
{
	bike_shutdown();

	if (regress_stick_log != NULL)
	{
		fclose(regress_stick_log);
		regress_stick_log = NULL;
		logInfo("Regression: wrote %lu stick ticks to '%s'.", regress_stick_tick, regress_stick_log_path);
	}

	if (regress_out == NULL && regress_state_out == NULL)
		return;

	bool close_ok = true;

	if (regress_out != NULL)
	{
		if (fclose(regress_out) != 0)
		{
			logError("Failed to close regression output '%s'.", regress_out_path);
			close_ok = false;
		}
		regress_out = NULL;
	}

	if (regress_state_out != NULL)
	{
		if (fclose(regress_state_out) != 0)
		{
			logError("Failed to close regression state output '%s'.", regress_state_out_path);
			close_ok = false;
		}
		regress_state_out = NULL;
	}

	if (!close_ok)
		return;

	if (regress_audio_active())
		logInfo("Regression: wrote audio baseline to '%s'.", regress_out_path);
	else
	{
		if (regress_out_path != NULL)
			logInfo("Regression: wrote %lu frames to '%s'.", regress_frame, regress_out_path);
		if (regress_state_out_path != NULL)
			logInfo("Regression: wrote %lu state records to '%s'.", regress_frame, regress_state_out_path);
	}

	if (regress_progress_check)
	{
		logInfo("Progress coverage: samples=%lu attempts=%lu reset_samples=%lu advances=%lu events=%lu jumps=%lu returns=%lu rewinds=%lu wait_samples=%lu success_samples=%lu blocked_samples=%lu boss_wait_samples=%lu loop_wait_samples=%lu shot_samples=%lu menu_requests=%d early_ends=%lu boss_resumes=%lu loop_resumes=%lu post_boss_advances=%lu supported_loop_samples=%lu final_percent=%d",
		        progress_samples, progress_attempts, progress_resets, progress_advances,
		        progress_events, progress_jumps, progress_returns, progress_rewinds,
		        progress_waits, progress_successes, progress_blocked, progress_boss_waits, progress_loop_waits, progress_shot_samples, regress_menu_taken ? 1 : 0, progress_early_ends, progress_boss_resumes,
		        progress_loop_resumes, progress_post_boss_advances, progress_supported_loop_samples, progress_previous.percent);
		if (regress_modern && progress_samples == 0)
			logFatal("Progress check FAILED: no observer attempt was sampled.");
	}

	if (regress_interp_check)
	{
		logInfo("Interp check: %lu level frames interpolated at alpha=1, %lu mismatched.",
		        drawlist_checked_frames(), drawlist_mismatched_frames());
		logInfo("Interp stats: %lu matched; snaps new=%lu jump=%lu sheet=%lu; overshoots=%lu.",
		        drawlist_interp_matched(), drawlist_interp_snap_new(), drawlist_interp_snap_jump(),
		        drawlist_interp_snap_sheet(), drawlist_interp_overshoots());
		if (drawlist_mismatched_frames() != 0)
		{
			logError("Interp check FAILED: %s", drawlist_first_mismatch());
			exit(EXIT_FAILURE);
		}
	}
	else if (regress_replay_check)
	{
		logInfo("Replay check: %lu level frames replayed, %lu mismatched.",
		        drawlist_checked_frames(), drawlist_mismatched_frames());
		if (drawlist_mismatched_frames() != 0)
		{
			logError("Replay check FAILED: %s", drawlist_first_mismatch());
			exit(EXIT_FAILURE);
		}
	}

	if (regress_interp_smoothness)
	{
		logInfo("Smoothness: %lu level ticks, %lu bg layer checks, %lu object checks.",
		        drawlist_smoothness_ticks(), drawlist_smoothness_bg_checks(),
		        drawlist_smoothness_object_checks());
		logInfo("Smoothness: horizontal events %lu, vertical events %lu, object events %lu (%lu event frames).",
		        drawlist_smoothness_horizontal_events(), drawlist_smoothness_vertical_events(),
		        drawlist_smoothness_object_events(), drawlist_smoothness_frames());
		if (drawlist_smoothness_events() != 0)
		{
			logError("Smoothness check FAILED: %lu non-monotonic motion events.",
			         drawlist_smoothness_events());
			exit(EXIT_FAILURE);
		}
	}

	if (regress_gameplay_check)
	{
		logInfo("Gameplay composition check: %lu in-level Modern frames, %lu without the panels.",
		        regress_gameplay_frames, regress_gameplay_missing);
		logInfo("Gameplay fade check: %lu frames in a brightness fade, %lu where the HUD did not follow.",
		        regress_gameplay_filter_frames, regress_gameplay_filter_missing);
		if (regress_gameplay_missing != 0)
		{
			logError("Gameplay composition check FAILED: %s", regress_gameplay_first);
			exit(EXIT_FAILURE);
		}
		if (regress_gameplay_filter_missing != 0)
		{
			logError("Gameplay fade check FAILED: %s", regress_gameplay_filter_first);
			exit(EXIT_FAILURE);
		}
	}

	if (regress_demo_hud_check)
	{
		logInfo("Demo HUD check: %lu in-level frames, %lu with the wrong HUD.",
		        regress_demohud_frames, regress_demohud_bad);
		if (regress_demohud_frames == 0 ||
		    (modern_hud_in_panels() && !regress_demohud_filled))
		{
			logError("Demo HUD check FAILED: no in-level frame with the HUD was presented");
			exit(EXIT_FAILURE);
		}
		if (regress_coin_draws != 0)
		{
			logInfo("Insert coin: draws=%lu tagged_px=%lu lightcol_px=%lu shot_layer_px=%lu",
			        regress_coin_draws, regress_coin_tagged, regress_coin_lcol,
			        regress_coin_layer_shot);
			if (regress_coin_tagged != 0 || regress_coin_lcol != 0 || regress_coin_layer_shot != 0)
			{
				logError("Demo HUD check FAILED: the INSERT COIN text is stamped as an emitter / shot");
				exit(EXIT_FAILURE);
			}
		}
		if (regress_demohud_bad != 0)
		{
			logError("Demo HUD check FAILED: %s", regress_demohud_first);
			exit(EXIT_FAILURE);
		}
	}

	if (regress_parallax_check)
	{
		logInfo("Parallax check: %lu level ticks, %lu presentation state mutations, %lu double updates, %lu advance mismatches.",
		        drawlist_parallax_ticks(), drawlist_parallax_mutations(),
		        drawlist_parallax_double_updates(), drawlist_parallax_advance_mismatches());
		if (drawlist_parallax_mutations() != 0 ||
		    drawlist_parallax_double_updates() != 0 ||
		    drawlist_parallax_advance_mismatches() != 0)
		{
			logError("Parallax check FAILED: the starfield/background moved beyond one tick.");
			exit(EXIT_FAILURE);
		}
	}

	if (regress_depth_quality >= 0)
		modern_depth_log_stats();

	if (regress_held_check)
		modern_held_log_stats();

	if (regress_layer_check)
	{
		const DrawlistLayerStats *st = drawlist_layer_stats();
		logInfo("Layer coverage: bg1=%lu starfield=%lu bg2=%lu bg2blend=%lu ground=%lu sky=%lu bg3=%lu top=%lu player=%lu sidekick=%lu shots=%lu enemyshots=%lu explosion=%lu superpixel=%lu hud=%lu other=%lu",
		        st->pixels[DL_LAYER_BG1], st->pixels[DL_LAYER_STARFIELD], st->pixels[DL_LAYER_BG2],
		        st->blend_pixels, st->pixels[DL_LAYER_GROUND_ENEMY], st->pixels[DL_LAYER_SKY_ENEMY],
		        st->pixels[DL_LAYER_BG3], st->pixels[DL_LAYER_TOP_ENEMY], st->pixels[DL_LAYER_PLAYER],
		        st->pixels[DL_LAYER_SIDEKICK], st->pixels[DL_LAYER_PLAYER_SHOT],
		        st->pixels[DL_LAYER_ENEMY_SHOT], st->pixels[DL_LAYER_EXPLOSION],
		        st->pixels[DL_LAYER_SUPERPIXEL], st->pixels[DL_LAYER_HUD], st->pixels[DL_LAYER_OTHER]);
		logInfo("Layer filters: lava=%lu water=%lu iced=%lu blur=%lu; rank orders=%lu.",
		        st->filters[DL_FILTER_LAVA], st->filters[DL_FILTER_WATER], st->filters[DL_FILTER_ICED],
		        st->filters[DL_FILTER_BLUR], st->rank_orders);
		logInfo("Layer present: frames=%lu interpolated=%lu flipped=%lu.",
		        modern_depth_presented_frames(), modern_depth_presented_interpolated(),
		        modern_depth_presented_flipped());
		if (st->ticks == 0 || st->mismatches != 0 || st->rank_bad != 0)
		{
			logError("Layer check FAIL: ticks=%lu interp_ticks=%lu mismatches=%lu rank_bad=%lu: %s",
			         st->ticks, st->interp_ticks, st->mismatches, st->rank_bad,
			         st->first[0] != '\0' ? st->first : "no level tick was checked");
			exit(EXIT_FAILURE);
		}
		logInfo("Layer check PASS: ticks=%lu interp_ticks=%lu mismatches=0 rank_bad=0.",
		        st->ticks, st->interp_ticks);
	}

	if (regress_smooth_effects_check)
	{
		logInfo("Smooth effects check: palette fade %lu frames, %lu channels, %lu bad; HUD bars %lu moved, %lu unchanged, %lu out of range.",
		        interp_fade_frames(), interp_fade_channels(), interp_fade_bad(),
		        regress_bar_moved, regress_bar_unchanged, regress_bar_bad);
		if (interp_fade_bad() != 0 || regress_bar_bad != 0)
		{
			logError("Smooth effects check FAILED: an interpolated fade/HUD value left the two ticks.");
			exit(EXIT_FAILURE);
		}
	}
}
