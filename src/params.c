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
#include "params.h"

#include "arg_parse.h"
#include "backgrnd.h"
#include "bike.h"
#include "demo.h"
#include "episodes.h"
#include "file.h"
#include "game_schema.h"
#include "gamepad_selftest.h"
#include "interp.h"
#include "joystick.h"
#include "logging.h"
#include "loudness.h"
#include "modern.h"
#include "modern_depth.h"
#include "crt_filter.h"
#include "modern_bloom.h"
#include "network.h"
#include "opentyr.h"
#include "regress.h"
#include "regress_flow.h"
#include "regress_rules.h"
#include "vfx.h"
#include "xmas.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>

JE_boolean richMode = false, constantPlay = false, constantDie = false;

/* YKS: Note: LOOT cheat had non letters removed. */
const char pars[][9] = {
	"LOOT", "RECORD", "NOJOY", "CONSTANT", "DEATH", "NOSOUND", "NOXMAS", "YESXMAS"
};

const Options *JE_paramOptions(void)
{
	static const Options options[] =
	{
		{ 'h', 'h', "help",              false },
		
		{ 's', 's', "no-sound",          false },
		{ 'j', 'j', "no-joystick",       false },
		{ 'x', 'x', "no-xmas",           false },
		
		{ 't', 't', "data",              true },
		{ PARAM_VARIANT, 0, "variant",    true },
		{ PARAM_REGRESS_USER_ROOT, 0, "regress-user-root", true },
		{ PARAM_REGRESS_USER_FILES, 0, "regress-user-files", false },
		{ PARAM_INSTALL_2000, 0, "install-2000", true },
		{ PARAM_INSTALL_2000_SPEC, 0, "install-2000-spec", true },
		
		{ 'n', 'n', "net",               true },
		{ 256, 0,   "net-player-name",   true }, // TODO: no short codes because there should
		{ 257, 0,   "net-player-number", true }, //       be a menu for entering these in the future
		{ 'p', 'p', "net-port",          true },
		{ 'd', 'd', "net-delay",         true },
		
		{ 'X', 'X', "xmas",              false },
		{ 'c', 'c', "constant",          false },
		{ 'k', 'k', "death",             false },
		{ 'r', 'r', "record",            false },
		{ 'l', 'l', "loot",              false },
		
		{ 420, 0, "regress-progress-check", false },
		{ 421, 0, "regress-observer-off", false },
		{ 410, 0, "regress-crt", true },
		{ 411, 0, "regress-crt-check", false },
		{ 412, 0, "regress-crt-height", true },
		{ 413, 0, "regress-crt-window", true },
		{ 414, 0, "regress-present-png", true },
		{ 258, 0,   "regress-demo",      true },
		{ 259, 0,   "regress-out",       true },
		{ 260, 0,   "regress-detail",    true },
		{ 261, 0,   "regress-level",     true },
		{ 262, 0,   "regress-frames",    true },
		{ 263, 0,   "regress-audio",     false },
		{ 264, 0,   "presentation",      true },
		{ 265, 0,   "regress-modern",    false },
		{ 266, 0,   "aspect",            true },
		{ 267, 0,   "pixel-aspect",      true },
		{ 268, 0,   "regress-aspect",    true },
		{ 269, 0,   "selftest-gamepad",  false },
		{ 270, 0,   "regress-state-out", true },
		{ 271, 0,   "regress-snapshot",  true },
		{ 272, 0,   "regress-players",   true },
		{ 273, 0,   "regress-arcade",    false },
		{ 274, 0,   "regress-screen",    true },
		{ 275, 0,   "regress-replay-check", false },
		{ 276, 0,   "regress-interp-check", false },
		{ 277, 0,   "regress-interp-alpha", true },
		{ 278, 0,   "smooth-motion",     true },
		{ 279, 0,   "regress-realtime",  false },
		{ 280, 0,   "bench-seconds",     true },
		{ 281, 0,   "bloom",             true },
		{ 282, 0,   "lighting",          true },
		{ 283, 0,   "regress-bloom",     true },
		{ 284, 0,   "regress-lighting",  true },
		{ 285, 0,   "vfx",               true },
		{ 286, 0,   "regress-vfx",       true },
		{ 287, 0,   "regress-interp-smoothness", false },
		{ 288, 0,   "regress-smooth-alphas", true },
		{ 289, 0,   "regress-gameplay-check", false },
		{ 370, 0,   "regress-demo-hud-check", false },
		{ 371, 0,   "regress-items-new", false },
		{ 372, 0,   "regress-fire",       false },
		{ 290, 0,   "light-tag-stats",   false },
		{ 291, 0,   "light-threshold",   true },
		{ 292, 0,   "regress-script",    true },
		{ 296, 0,   "regress-parallax-check", false },
		{ 430, 0,   "regress-layer-check", false },
		{ 431, 0,   "regress-layer-png", true },
		{ 432, 0,   "regress-depth",     true },
		{ 433, 0,   "regress-depth-check", false },
		{ 434, 0,   "regress-held-check", false },
		{ 435, 0,   "regress-light-scale", true },
		{ 436, 0,   "regress-light-radius", true },
		{ 437, 0,   "regress-fog-strength", true },
		{ 297, 0,   "starfield-speed",   true },
		{ 298, 0,   "regress-seed",      true },
		{ 299, 0,   "regress-menu",      true },
		{ 310, 0,   "log-file",          true },
		{ 301, 0,   "regress-smooth-effects-check", false },
		{ 311, 0,   "regress-front-weapon", true },
		{ 312, 0,   "regress-front-power",  true },
		{ 320, 0,   "regress-rules",        true },
		{ 350, 0,   "regress-loadout",  true },
		{ 380, 0,   "regress-data-audit", true },
		{ 381, 0,   "regress-boss", false },
		{ 383, 0,   "regress-handoff", true },
		{ 382, 0,   "regress-gamepad", false },
		{ 360, 0,   "regress-flow",     true },
		{ 361, 0,   "regress-xmas",     false },
		
		{ 305, 0,   "deadzone",          true },
		
		{ 306, 0,   "regress-stick",     true },
		{ 307, 0,   "regress-stick-log", true },
		{ 308, 0,   "regress-reverse-y", false },
		
		{ 440, 0,   "regress-bike-feed", true },
		{ 441, 0,   "regress-bike-log",  true },
		{ 442, 0,   "regress-bike-set",  true },
		{ 450, 0,   "ftms-bridge",       true },
		{ 451, 0,   "ftms-ftp",          true },
		{ 452, 0,   "ftms-off",          false },
		
		{ 0, 0, NULL, false }
	};
	
	return options;
}

void JE_paramCheck(int argc, char *argv[])
{
	const Options *options = JE_paramOptions();
	Option option;
	
	for (; ; )
	{
		option = parse_args(argc, (const char **)argv, options);
		
		if (option.value == NOT_OPTION)
			break;
		
		switch (option.value)
		{
		case 420:
			regress_progress_check = true;
			break;
		case 421:
			regress_observer_off = true;
			break;
		case 410:
			if (!crt_filter_set_by_name(option.arg)) logFatal("Unknown CRT filter '%s'.", option.arg);
			regress_crt_mode = crt_filter_mode();
			break;
		case 411:
			regress_crt_check = true;
			break;
		case 412:
			regress_crt_dst_h = atoi(option.arg);
			if (regress_crt_dst_h < 1) logFatal("CRT height must be positive.");
			break;
		case 413:
			if (sscanf(option.arg, "%dx%d", &regress_crt_window_w, &regress_crt_window_h) != 2 ||
			    regress_crt_window_w < 1 || regress_crt_window_h < 1)
				logFatal("CRT window must be positive WIDTHxHEIGHT.");
			break;
		case 414:
		{
			char *end;
			regress_present_png_frame = strtoul(option.arg, &end, 10);
			if (end == option.arg || *end != ':' || end[1] == '\0') logFatal("Presented PNG must be FRAME:FILE.");
			regress_present_png = end + 1;
			break;
		}
		case INVALID_OPTION:
		case AMBIGUOUS_OPTION:
		case OPTION_MISSING_ARG:
			logError("Try '%s --help' for more information.", argv[0]);
			exit(EXIT_FAILURE);
			break;
			
		case 'h':
			logInfo("Usage: %s [OPTION...]", argv[0]);
			logInfo("%s", "");
			logInfo("Options:");
			logInfo("  -h, --help                   Show help about options");
			logInfo("  -s, --no-sound               Disable audio");
			logInfo("  -j, --no-joystick            Disable joystick/gamepad input");
			logInfo("  -x, --no-xmas                Disable Christmas mode");
			logInfo("  -t, --data=DIR               Set Tyrian data directory");
			logInfo("  -n, --net=HOST[:PORT]        Start a networked game");
			logInfo("  --net-player-name=NAME       Set local player name in a networked game");
			logInfo("  --net-player-number=NUMBER   Set local player number in a networked game");
			logInfo("                               (1 or 2)");
			logInfo("  -p, --net-port=PORT          Set local port to bind (default is 1333)");
			logInfo("  -d, --net-delay=FRAMES       Set lag-compensation delay (default is 1)");
			logInfo("  --presentation=MODE          Set presentation mode: classic or modern");
			logInfo("  --aspect=RATIO               Modern aspect: 4:3, 16:10, 16:9, 21:9, 32:9, auto");
			logInfo("  --pixel-aspect=SHAPE         Classic pixel aspect: original (1.2) or square");
			logInfo("  --bloom=LEVEL                Modern bloom override: off, low or high");
			logInfo("  --lighting=LEVEL             Modern bloom + lighting: off, low or high (default low)");
			logInfo("  --variant=2.1|2000           Select variant for automation/testing (2000 unavailable)");
			logInfo("  --regress-user-root=DIR      Enable user files only in DIR for a regress run");
			logInfo("  --regress-user-files         Load/save configs and saves, then exit (requires DIR)");
			logInfo("  --install-2000=WHAT          Install the Tyrian 2000 data headless and exit (0 ok, 1 failed):");
			logInfo("                               download, a tyrian2000.zip, a folder, or detect");
			logInfo("  --install-2000-spec=FILE     Test only: synthetic archive size/SHA-256/manifest for --install-2000");
			logInfo("  --regress-demo=N             Replay recorded demo N (1-5) headless and exit");
			logInfo("  --regress-level=E:L          Start level L of episode E headless and exit");
			logInfo("  --regress-script=E:L         Start level L of episode E through the episode script");
			logInfo("                               (reaches script-driven screens such as WARNING)");
			logInfo("  --regress-seed=N             Pin the RNG seed for a reproducible regress run");
			logInfo("  --regress-frames=N           Cap a --regress-level run at N frames");
			logInfo("  --regress-out=FILE           Write per-frame hashes to FILE (regress modes)");
			logInfo("  --regress-state-out=FILE     Write per-frame game-state hashes to FILE");
			logInfo("  --regress-snapshot=F:FILE    Save the presented image of frame F to FILE (BMP)");
			logInfo("                               (repeatable; the Modern canvas with --regress-modern)");
			logInfo("  --regress-players=N          Start a --regress-level scenario with N players (1 or 2)");
			logInfo("  --regress-loadout=widest     Equip the items with the widest names (HUD fit check)");
			logInfo("  --regress-xmas               Run a regress case with Christmas mode on");
			logInfo("  --regress-data-audit=ROOT    Assert and log every resolved data-file open in ROOT");
			logInfo("  --regress-progress-check     Assert progress bounds, monotonicity and end causes");
			logInfo("  --regress-observer-off       Disable progress observer for state/RNG comparisons");
			logInfo("  --regress-boss               Accelerate events to a runtime boss, then run 60 ticks");
			logInfo("  --regress-gamepad            Route a flow through the controller menu adapter");
			logInfo("  --regress-handoff=VARIANT    Headless launcher choice and normal startup handoff");
			logInfo("  --regress-flow=NAME[:K=V,..] Play a scripted path through the real menus and levels");
			logInfo("                               (battle, episode, list-levels; see src/regress_flow.h)");
			logInfo("  --regress-arcade             Start a --regress-level scenario in 1-player arcade mode");
			logInfo("  --regress-screen=NAME        Render one non-gameplay screen headless and exit");
			logInfo("                               (title, episode-select, gameplay-select, high-scores,");
			logInfo("                               game-menu, upgrade, purchase, shield, options,");
			logInfo("                               options-limited, mouse, cube-list, cube-reader, keyboard,");
			logInfo("                               joystick, load-save, solid, setup, nav-map, ship-specs,");
			logInfo("                               jukebox, weapon-sim, credits); NAME:key=value,... sets");
			logInfo("                               fixtures (ship, shipgraphic, front, rear, twomode, mode,");
			logInfo("                               sel, cat, page)");
			logInfo("  --regress-replay-check       Record each level frame's draw list and replay it (proof)");
			logInfo("  --regress-interp-check       Render each level frame interpolated at alpha=1 and");
			logInfo("                               compare it byte for byte with the real frame (proof)");
			logInfo("  --regress-interp-alpha=A     Present the frame interpolated at alpha A (0..1)");
			logInfo("                               (with --regress-snapshot; Modern only)");
			logInfo("  --regress-interp-smoothness  Per level tick, check that every background layer and");
			logInfo("                               matched object moves monotonically across the sub-frames");
			logInfo("  --regress-smooth-alphas=N    Sub-frame samples for --regress-interp-smoothness (default 5)");
			logInfo("  --regress-gameplay-check     Assert every in-level Modern frame uses the gameplay");
			logInfo("                               composition (drops the classic sidebar)");
			logInfo("  --regress-demo-hud-check     Assert a played demo shows the active mode's HUD: the Modern");
			logInfo("                               side panels, or the classic sidebar in Classic");
			logInfo("  --regress-items-new        Equip a scenario with the variant's newest items (ship, Punch,");
			logInfo("                               chargeable sidekick)");
			logInfo("  --regress-fire               A --regress-level scenario fires and sweeps the ship");
			logInfo("  --regress-parallax-check     Per level tick, assert the interpolated presentation leaves");
			logInfo("                               the starfield/background scroll untouched (per-tick motion)");
			logInfo("  --regress-layer-check        Per level tick, stamp the depth layer buffer and assert the");
			logInfo("                               interpolated one (alpha = 1) equals the live tick's");
			logInfo("                               (needs --regress-modern)");
			logInfo("  --regress-layer-png=FRAME:FILE  Write the presented depth layers of that frame as a");
			logInfo("                               false-colour PNG (needs --regress-modern; Tyrian 2.1 only)");
			logInfo("  --regress-smooth-effects-check  Per level tick, assert the interpolated palette fade and");
			logInfo("                               HUD bars stay between the two ticks (needs --regress-modern)");
			logInfo("  --regress-menu=NAME          Open an in-level menu on the last presented frame:");
			logInfo("                               ingame (ESC), pause (P) or help (F1)");
			logInfo("                               (requires --regress-script and --regress-frames)");
			logInfo("  --starfield-speed=PERCENT    Background starfield speed as a percentage of the");
			logInfo("                               original rate (10-100, default 25); sub-pixel motion");
			logInfo("  --log-file=FILE              Mirror the log into FILE as well as stderr");
			logInfo("                               (default on Steam Deck: the user config directory)");
			logInfo("  --smooth-motion=on|off       Modern gameplay at the display refresh with interpolated");
			logInfo("                               motion (default on)");
			logInfo("  --regress-realtime           Replay a demo in a real window with the wall clock and log");
			logInfo("                               presented-fps statistics (uses --regress-demo)");
			logInfo("  --bench-seconds=N            Duration of --regress-realtime (default 20)");
			logInfo("  --vfx=LEVEL                  Modern VFX level: off, low or high (default low)");
			logInfo("  --regress-vfx=LEVEL          Pin the VFX level in a regress run (default off)");
			logInfo("  --regress-detail=M           Pin processor detail level M (1-6, default 2; with");
			logInfo("                               --regress-modern the engine forces Pentium 4, or 6 for");
			logInfo("                               the SuperWild cheat, and logs any override)");
			logInfo("  --regress-modern             Hash the Modern canvas in regress modes");
			logInfo("  --regress-bloom=LEVEL        Pin Modern bloom in regress modes (default off)");
			logInfo("  --regress-lighting=LEVEL     Pin Modern bloom + lighting in regress modes (default off)");
			logInfo("  --regress-depth=off|on        Pin the Modern Depth setting (soft shadows) in regress modes (default off)");
			logInfo("  --regress-depth-check        Run the synthetic depth-shadow fixture and exit");
			logInfo("  --regress-light-scale=PCT    Tuning: scale the light gain (100 = shipped)");
			logInfo("  --regress-light-radius=N     Tuning: force the light blur radius (quarter-res px, shipped 2)");
			logInfo("  --regress-fog-strength=Q8    Tuning: bg1 fog blend strength (Q8, shipped 30)");
			logInfo("  --regress-held-check         With --regress-menu: compare the held frame with the last live frame outside the overlay");
			logInfo("  --light-tag-stats            Count emitted playfield pixels per tag class and exit");
			logInfo("  --light-threshold=N          Debug: force both bloom/light thresholds to N");
			logInfo("  --regress-audio              Render the audio baselines to FILE and exit");
			logInfo("  --selftest-gamepad           Run the virtual-controller input self-test and exit");
			logInfo("  --deadzone=PERCENT           Modern analog stick dead zone, 0-20 (default 10)");
			logInfo("  --regress-stick=X,Y          Regress only: inject a synthetic analog stick at raw");
			logInfo("                               axis values X,Y and run the whole stick path headless");
			logInfo("  --regress-stick-log=FILE     Log the per-tick ship x/y and x/y velocity of a");
			logInfo("                               --regress-stick run to FILE");
			logInfo("  --regress-reverse-y          Regress only: force the reverse-controls smoothie on");
			logInfo("  --regress-bike-feed=FILE|const:W[,RPM]  Regress only: scripted bike feed (tick,watts,cadence,connected)");
			logInfo("  --regress-bike-log=FILE      Regress only: per-tick bike log");
			logInfo("  --regress-bike-set=K=V[,K=V...]  Regress only: pin [ftms] settings (enabled, generator, ftp, ...)");
			logInfo("  --ftms-bridge=HOST[:PORT]    Bike bridge address (default 127.0.0.1:8787)");
			logInfo("  --ftms-ftp=W                 Bike rider FTP in watts, 50-500");
			logInfo("  --ftms-off                   Force the bike features off for this run");
			logInfo("  --regress-front-weapon=N      Regress only: front weapon id (0-42) for --regress-script/--regress-level");
			logInfo("  --regress-front-power=N       Regress only: front weapon power (1-11) for --regress-script/--regress-level");
			logInfo("  --regress-rules=NAME          Code-owned gameplay fixture: events, spawn, sidekicks, twiddle, punch");
			exit(EXIT_SUCCESS);
			break;
			
		case 's':
			// Disables sound/music usage
			audio_disabled = true;
			break;
			
		case 'j':
			// Disables joystick detection
			ignore_joystick = true;
			break;
			
		case 'x':
			xmas = false;
			break;
			
		// Bootstrap owns the data directory and variant selection.
		case 't':
			// Already selected by gameBootstrapParse(), before user files.
		case PARAM_VARIANT:
		case PARAM_REGRESS_USER_ROOT:
		case PARAM_REGRESS_USER_FILES:
		// Run by main() before video init (installerRunCli).
		case PARAM_INSTALL_2000:
		case PARAM_INSTALL_2000_SPEC:
			break;
			
		case 'n':
			isNetworkGame = true;
			
			intptr_t temp = (intptr_t)strchr(option.arg, ':');
			if (temp)
			{
				temp -= (intptr_t)option.arg;
				
				int temp_port = atoi(&option.arg[temp + 1]);
				if (temp_port > 0 && temp_port < 49152)
					network_opponent_port = temp_port;
				else
				{
					logError("%s: invalid network port number", argv[0]);
					exit(EXIT_FAILURE);
				}
				
				network_opponent_host = malloc(temp + 1);
				SDL_strlcpy(network_opponent_host, option.arg, temp + 1);
			}
			else
			{
				network_opponent_host = malloc(strlen(option.arg) + 1);
				strcpy(network_opponent_host, option.arg);
			}
			break;
			
		case 256: // --net-player-name
			network_player_name = malloc(strlen(option.arg) + 1);
			strcpy(network_player_name, option.arg);
			break;
			
		case 257: // --net-player-number
		{
			int temp = atoi(option.arg);
			if (temp >= 1 && temp <= 2)
				thisPlayerNum = temp;
			else
			{
				logError("%s: invalid network player number", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'p':
		{
			int temp = atoi(option.arg);
			if (temp > 0 && temp < 49152)
				network_player_port = temp;
			else
			{
				logError("%s: invalid network port number", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'd':
		{
			int temp;
			if (sscanf(option.arg, "%d", &temp) == 1)
				network_delay = 1 + temp;
			else
			{
				logError("%s: invalid network delay value", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'X':
			xmas = true;
			break;
			
		case 'c':
			/* Constant play for testing purposes (C key activates invincibility)
			   This might be useful for publishers to see everything - especially
			   those who can't play it */
			constantPlay = true;
			break;
			
		case 'k':
			constantDie = true;
			break;
			
		case 'r':
			recordDemo = true;
			break;
			
		case 'l':
			// Gives you mucho bucks
			richMode = true;
			break;
			
		case 258: // --regress-demo
		{
			int temp = atoi(option.arg);
			if (temp >= 1 && temp <= 5)
				regress_demo = temp;
			else
			{
				logError("%s: regression demo number must be between 1 and 5", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 259: // --regress-out
			regress_out_path = option.arg;
			break;
		case 260: // --regress-detail
		{
			int temp = atoi(option.arg);
			if (temp >= 1 && temp <= 6)
				regress_detail = temp;
			else
			{
				logError("%s: regression detail level must be between 1 and 6", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 261: // --regress-level=EPISODE:LEVEL
		{
			int episode, level;
			if (sscanf(option.arg, "%d:%d", &episode, &level) == 2 &&
			    episode >= 1 && episode <= EPISODE_AVAILABLE &&
			    level >= 1)
			{
				regress_scenario_episode = episode;
				regress_scenario_level = level;
			}
			else
			{
				logError("%s: regression level must be EPISODE:LEVEL", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 262: // --regress-frames
		{
			int temp = atoi(option.arg);
			if (temp > 0)
				regress_frames = temp;
			else
			{
				logError("%s: regression frame cap must be positive", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 263: // --regress-audio
			regress_audio = 1;
			break;
		case 264: // --presentation=classic|modern
			if (!set_presentation_by_name(option.arg))
			{
				logError("%s: presentation mode must be 'classic' or 'modern'", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 265: // --regress-modern
			regress_modern = 1;
			break;
		case 266: // --aspect=4:3|16:10|16:9|21:9|32:9|auto
			if (!set_modern_aspect_by_name(option.arg))
			{
				logError("%s: aspect must be '4:3', '16:10', '16:9', '21:9', '32:9' or 'auto'", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 267: // --pixel-aspect=original|square
			if (!set_modern_pixel_aspect_by_name(option.arg))
			{
				logError("%s: pixel aspect must be 'original' or 'square'", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 268: // --regress-aspect=<aspect>
		{
			const ModernAspect before = modern_aspect;
			if (!set_modern_aspect_by_name(option.arg))
			{
				logError("%s: aspect must be '4:3', '16:10', '16:9', '21:9', '32:9' or 'auto'", argv[0]);
				exit(EXIT_FAILURE);
			}
			regress_aspect = modern_aspect;
			modern_aspect = before;  // applied by regress_init(), not now
			break;
		}
		case 269: // --selftest-gamepad
			selftest_gamepad = true;
			break;
		case 270: // --regress-state-out
			regress_state_out_path = option.arg;
			break;
		case 271: // --regress-snapshot=FRAME:FILE
		{
			char *end = NULL;
			const unsigned long frame = strtoul(option.arg, &end, 10);
			if (end == option.arg || *end != ':' || end[1] == '\0')
			{
				logError("%s: --regress-snapshot must be FRAME:FILE", argv[0]);
				exit(EXIT_FAILURE);
			}
			if (!regress_add_snapshot(frame, end + 1))
			{
				logError("%s: too many --regress-snapshot options (max 16)", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 272: // --regress-players
		{
			const int temp = atoi(option.arg);
			if (temp != 1 && temp != 2)
			{
				logError("%s: --regress-players must be 1 or 2", argv[0]);
				exit(EXIT_FAILURE);
			}
			regress_players = temp;
			break;
		}
		case 273: // --regress-arcade
			regress_arcade = 1;
			break;
		case 274: // --regress-screen=NAME
			regress_screen = option.arg;
			break;
		case 275: // --regress-replay-check
			regress_replay_check = 1;
			break;
		case 276: // --regress-interp-check
			regress_interp_check = 1;
			break;
		case 277: // --regress-interp-alpha=A
		{
			char *end = NULL;
			const double alpha = strtod(option.arg, &end);
			if (end == option.arg || *end != '\0' || alpha < 0.0 || alpha > 1.0)
			{
				logError("%s: --regress-interp-alpha must be a number between 0 and 1", argv[0]);
				exit(EXIT_FAILURE);
			}
			interp_set_regress_alpha(alpha);
			break;
		}
		case 278: // --smooth-motion=on|off
			if (!set_smooth_motion_by_name(option.arg))
			{
				logError("%s: --smooth-motion must be 'on' or 'off'", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 279: // --regress-realtime
			regress_realtime = 1;
			break;
		case 280: // --bench-seconds=N
		{
			const double seconds = atof(option.arg);
			if (seconds <= 0.0)
			{
				logError("%s: --bench-seconds must be positive", argv[0]);
				exit(EXIT_FAILURE);
			}
			regress_bench_seconds = seconds;
			break;
		}
		case 281: // --bloom=off|low|high (advanced override of bloom only)
			if (!set_modern_quality_by_name(option.arg, &modern_bloom_quality))
			{
				logError("%s: bloom must be 'off', 'low' or 'high'", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 282: // --lighting=off|low|high (sets bloom and lighting together)
		{
			ModernQuality quality = MODERN_QUALITY_LOW;
			if (!set_modern_quality_by_name(option.arg, &quality))
			{
				logError("%s: lighting must be 'off', 'low' or 'high'", argv[0]);
				exit(EXIT_FAILURE);
			}
			modern_bloom_quality = quality;
			modern_lighting_quality = quality;
			break;
		}
		case 283: // --regress-bloom=off|low|high
		{
			ModernQuality quality = MODERN_QUALITY_OFF;
			if (!set_modern_quality_by_name(option.arg, &quality))
			{
				logError("%s: regress bloom must be 'off', 'low' or 'high'", argv[0]);
				exit(EXIT_FAILURE);
			}
			regress_bloom_quality = (int)quality;
			break;
		}
		case 284: // --regress-lighting=off|low|high
		{
			ModernQuality quality = MODERN_QUALITY_OFF;
			if (!set_modern_quality_by_name(option.arg, &quality))
			{
				logError("%s: regress lighting must be 'off', 'low' or 'high'", argv[0]);
				exit(EXIT_FAILURE);
			}
			regress_lighting_quality = (int)quality;
			break;
		}
		case 285: // --vfx=off|low|medium|high
			if (!set_vfx_by_name(option.arg))
			{
				logError("%s: --vfx must be 'off', 'low' or 'high'", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 286: // --regress-vfx=off|low|medium|high
		{
			const VfxLevel before = vfx_level;
			if (!set_vfx_by_name(option.arg))
			{
				logError("%s: --regress-vfx must be 'off', 'low' or 'high'", argv[0]);
				exit(EXIT_FAILURE);
			}
			regress_vfx = vfx_level;
			vfx_level = before;  // applied by regress_init(), not now
			break;
		}
		case 287: // --regress-interp-smoothness
			regress_interp_smoothness = 1;
			break;
		case 288: // --regress-smooth-alphas=N
		{
			const int count = atoi(option.arg);
			if (count < 2 || count > 33)
			{
				logError("%s: --regress-smooth-alphas must be between 2 and 33", argv[0]);
				exit(EXIT_FAILURE);
			}
			regress_smooth_alphas = count;
			break;
		}
		case 289: // --regress-gameplay-check
			regress_gameplay_check = 1;
			break;
		case 370: // --regress-demo-hud-check
			regress_demo_hud_check = 1;
			break;
		case 371: // --regress-items-new
			regress_loadout_new = 1;
			break;
		case 372: // --regress-fire
			regress_fire = 1;
			break;
		case 290: // --light-tag-stats
			modern_bloom_set_stats(true);
			break;
		case 291: // --light-threshold=N (debug tuning override)
			modern_bloom_set_threshold(atoi(option.arg));
			break;

		case 292: // --regress-script=EPISODE:LEVEL
		{
			int episode, level;
			if (sscanf(option.arg, "%d:%d", &episode, &level) == 2 &&
			    episode >= 1 && episode <= EPISODE_AVAILABLE &&
			    level >= 1)
			{
				regress_scenario_episode = episode;
				regress_scenario_level = level;
				regress_script = 1;
			}
			else
			{
				logError("%s: --regress-script must be EPISODE:LEVEL", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}

		case 296: // --regress-parallax-check
			regress_parallax_check = 1;
			break;

		case 430: // --regress-layer-check
			regress_layer_check = 1;
			break;

		case 432: // --regress-depth=off|on (low/high aliases)
		{
			ModernDepth quality = MODERN_DEPTH_OFF;
			if (!set_modern_depth_by_name(option.arg, &quality))
			{
				logError("%s: regress depth must be 'off' or 'on' (legacy 'low'/'high' accepted)", argv[0]);
				exit(EXIT_FAILURE);
			}
			regress_depth_quality = (int)quality;
			break;
		}

		case 433: // --regress-depth-check
			regress_depth_check = true;
			break;

		case 435: // --regress-light-scale=PERCENT (tuning only)
			modern_bloom_set_light_scale(atoi(option.arg));
			break;

		case 436: // --regress-light-radius=N (tuning only)
			modern_bloom_set_light_radius(atoi(option.arg));
			break;

		case 437: // --regress-fog-strength=Q8 (tuning only)
			modern_depth_set_fog_strength(atoi(option.arg));
			break;

		case 434: // --regress-held-check
			regress_held_check = true;
			break;

		case 431: // --regress-layer-png=FRAME:FILE
		{
			char *end;
			regress_layer_png_frame = strtoul(option.arg, &end, 10);
			if (end == option.arg || *end != ':' || end[1] == '\0')
				logFatal("Layer PNG must be FRAME:FILE.");
			regress_layer_png = end + 1;
			break;
		}

		case 297: // --starfield-speed=PERCENT
		{
			const int percent = atoi(option.arg);
			if (percent < 10 || percent > 100)
			{
				logError("%s: --starfield-speed must be between 10 and 100", argv[0]);
				exit(EXIT_FAILURE);
			}
			starfield_set_speed_percent(percent);
			break;
		}

		case 298: // --regress-seed=N
			regress_seed = strtoul(option.arg, NULL, 0);
			regress_seed_set = 1;
			break;

		case 301: // --regress-smooth-effects-check
			regress_smooth_effects_check = 1;
			break;

		case 311: // --regress-front-weapon=N
		{
			const int id = atoi(option.arg);
			if (id < 0 || id > gameSchema()->port_max)
			{
				logError("%s: --regress-front-weapon must be between 0 and %d", argv[0], gameSchema()->port_max);
				exit(EXIT_FAILURE);
			}
			regress_front_weapon = id;
			break;
		}

		case 312: // --regress-front-power=N
		{
			const int power_level = atoi(option.arg);
			if (power_level < 1 || power_level > 11)
			{
				logError("%s: --regress-front-power must be between 1 and 11", argv[0]);
				exit(EXIT_FAILURE);
			}
			regress_front_power = power_level;
			break;
		}

		case 320: // --regress-rules=events|spawn|sidekicks|twiddle|punch
			regress_rule_fixture = option.arg;
			break;

		case 361: // --regress-xmas
			regress_xmas = 1;
			break;

		case 380:
			regress_data_audit_root = option.arg;
			break;
		case 381:
			regress_boss = 1;
			break;
		case 383:
			regress_handoff = option.arg;
			break;
		case 382:
			regress_flow_gamepad = 1;
			break;
		case 360: // --regress-flow=NAME[:key=value,...]
			regress_flow = option.arg;
			break;

		case 350: // --regress-loadout=widest
			if (strcmp(option.arg, "widest") != 0)
			{
				logError("%s: --regress-loadout must be widest", argv[0]);
				exit(EXIT_FAILURE);
			}
			regress_loadout_widest = 1;
			break;

		case 299: // --regress-menu=ingame|pause|help
			if (strcmp(option.arg, "ingame") == 0)
				regress_menu_kind = REGRESS_MENU_INGAME;
			else if (strcmp(option.arg, "pause") == 0)
				regress_menu_kind = REGRESS_MENU_PAUSE;
			else if (strcmp(option.arg, "help") == 0)
				regress_menu_kind = REGRESS_MENU_HELP;
			else
			{
				logError("%s: --regress-menu must be ingame, pause or help", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;

		case 310: // --log-file
			// logFileFromArgs() opens it before loadConfiguration(); this only
			// catches an abbreviated spelling (e.g. --log) and must not reopen
			// (which would truncate) an already-open log.
			if (!logFileIsOpen() && !logOpenFile(option.arg))
				logWarn("Failed to open '%s' for logging.", option.arg);
			break;

		case 305: // --deadzone=PERCENT
		{
			const int percent = atoi(option.arg);
			if (percent < JOYSTICK_DEADZONE_MIN || percent > JOYSTICK_DEADZONE_MAX)
			{
				logError("%s: --deadzone must be between %d and %d",
				         argv[0], JOYSTICK_DEADZONE_MIN, JOYSTICK_DEADZONE_MAX);
				exit(EXIT_FAILURE);
			}
			joystick_deadzone_override = percent;
			break;
		}

		case 306: // --regress-stick=X,Y
		{
			int x, y;
			if (sscanf(option.arg, "%d,%d", &x, &y) != 2 ||
			    x < -32768 || x > 32767 || y < -32768 || y > 32767)
			{
				logError("%s: --regress-stick must be X,Y with axis values in -32768..32767", argv[0]);
				exit(EXIT_FAILURE);
			}
			regress_stick = 1;
			regress_stick_x = x;
			regress_stick_y = y;
			break;
		}

		case 307: // --regress-stick-log=FILE
			regress_stick_log_path = option.arg;
			break;

		case 308: // --regress-reverse-y
			regress_reverse_y = 1;
			break;

		case 440: // --regress-bike-feed=FILE|const:W[,RPM]
			if (!bike_script_load(option.arg))
				exit(EXIT_FAILURE);
			break;

		case 441: // --regress-bike-log=FILE
			bike_log_open(option.arg);
			break;

		case 442: // --regress-bike-set=K=V[,K=V...]
			regress_bike_set = option.arg;
			break;

		case 450: // --ftms-bridge=HOST[:PORT]
		{
			char host[sizeof bike_settings.bridge_host + 8];
			char *colon;

			if (strlen(option.arg) >= sizeof host)
			{
				logError("%s: --ftms-bridge address is too long", argv[0]);
				exit(EXIT_FAILURE);
			}
			strcpy(host, option.arg);
			colon = strrchr(host, ':');
			if (colon != NULL)
			{
				*colon = '\0';
				if (!bike_settings_set("bridge_port", colon + 1))
				{
					logError("%s: --ftms-bridge port must be 1-65535", argv[0]);
					exit(EXIT_FAILURE);
				}
			}
			if (!bike_settings_set("bridge_host", host))
			{
				logError("%s: --ftms-bridge needs a host name (at most %d characters)", argv[0], (int)sizeof bike_settings.bridge_host - 1);
				exit(EXIT_FAILURE);
			}
			break;
		}

		case 451: // --ftms-ftp=W
			if (!bike_settings_set("ftp", option.arg))
			{
				logError("%s: --ftms-ftp must be 50-500 watts", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;

		case 452: // --ftms-off
			bike_force_off();
			break;

		default:
			assert(false);
			break;
		}
	}
	
	if (regress_crt_mode >= 0 && (!regress_active() || !regress_modern))
		logFatal("--regress-crt requires a Modern regression run.");
	if ((regress_crt_window_w > 0 || regress_present_png != NULL || regress_crt_dst_h > 0) && !regress_active())
		logFatal("CRT output controls are regress-only.");
	if (regress_present_png != NULL && gameVariantCurrent()->id != VARIANT_TYRIAN21)
		logFatal("Presented PNG capture is restricted to Tyrian 2.1.");

	if (regress_rule_fixture != NULL && !regress_scenario_active())
	{
		logError("%s: --regress-rules requires --regress-level", argv[0]);
		exit(EXIT_FAILURE);
	}

	if ((regress_demo != 0 || regress_scenario_episode != 0) && regress_audio)
	{
		logError("%s: --regress-audio cannot be combined with --regress-demo/--regress-level", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	if (regress_demo != 0 && regress_scenario_episode != 0)
	{
		logError("%s: --regress-demo and --regress-level are mutually exclusive", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (regress_realtime && regress_demo == 0 && regress_scenario_episode == 0)
	{
		logError("%s: --regress-realtime requires --regress-demo or --regress-level", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	if (regress_replay_check && regress_demo == 0 && regress_scenario_episode == 0)
	{
		logError("%s: --regress-replay-check requires --regress-demo or --regress-level", argv[0]);
		exit(EXIT_FAILURE);
	}

	if ((regress_interp_check || interp_regress_alpha_active()) &&
	    regress_demo == 0 && regress_scenario_episode == 0)
	{
		logError("%s: --regress-interp-check/--regress-interp-alpha require --regress-demo or --regress-level", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (regress_interp_smoothness && regress_demo == 0 && regress_scenario_episode == 0)
	{
		logError("%s: --regress-interp-smoothness requires --regress-demo or --regress-level", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (regress_demo_hud_check && regress_demo == 0 && regress_scenario_episode == 0)
	{
		logError("%s: --regress-demo-hud-check requires --regress-demo or --regress-level", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (regress_gameplay_check && !regress_modern)
	{
		logError("%s: --regress-gameplay-check requires --regress-modern", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (regress_parallax_check && regress_demo == 0 && regress_scenario_episode == 0)
	{
		logError("%s: --regress-parallax-check requires --regress-demo or --regress-level", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (regress_held_check && (!regress_modern || regress_menu_kind == REGRESS_MENU_NONE))
	{
		logError("%s: --regress-held-check requires --regress-modern and --regress-menu", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (regress_layer_check && (!regress_modern || (regress_demo == 0 && regress_scenario_episode == 0)))
	{
		logError("%s: --regress-layer-check requires --regress-modern and --regress-demo/--regress-level", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (regress_layer_png != NULL)
	{
		if (!regress_modern || (regress_demo == 0 && regress_scenario_episode == 0))
			logFatal("--regress-layer-png requires --regress-modern and --regress-demo/--regress-level.");
		if (gameVariantCurrent()->id != VARIANT_TYRIAN21)
			logFatal("Layer PNG capture is restricted to Tyrian 2.1.");
	}

	if (regress_smooth_effects_check && (!regress_modern || (regress_demo == 0 && regress_scenario_episode == 0)))
	{
		logError("%s: --regress-smooth-effects-check requires --regress-modern and --regress-demo/--regress-level", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (regress_menu_kind != REGRESS_MENU_NONE)
	{
		// The pause/menu/help screens only present on the real-gameplay
		// (!playDemo) path, which is what --regress-script reaches; and they
		// block on input, so the run needs a frame cap to end on the menu frame.
		if (!regress_script_active())
		{
			logError("%s: --regress-menu requires --regress-script", argv[0]);
			exit(EXIT_FAILURE);
		}
		if (regress_frames <= 0)
		{
			logError("%s: --regress-menu requires --regress-frames", argv[0]);
			exit(EXIT_FAILURE);
		}
	}
	
	// legacy parameter support
	for (int i = option.argn; i < argc; ++i)
	{
		for (uint j = 0; j < strlen(argv[i]); ++j)
			argv[i][j] = (char)toupper((unsigned char)argv[i][j]);
		
		for (uint j = 0; j < COUNTOF(pars); ++j)
		{
			if (strcmp(argv[i], pars[j]) == 0)
			{
				switch (j)
				{
				case 0:
					richMode = true;
					break;
				case 1:
					recordDemo = true;
					break;
				case 2:
					ignore_joystick = true;
					break;
				case 3:
					constantPlay = true;
					break;
				case 4:
					constantDie = true;
					break;
				case 5:
					audio_disabled = true;
					break;
				case 6:
					xmas = false;
					break;
				case 7:
					xmas = true;
					break;
					
				default:
					assert(false);
					break;
				}
			}
		}
	}
}
