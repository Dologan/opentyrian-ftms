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
// --regress-screen: render one non-gameplay screen in a deterministic state,
// present REGRESS_SCREEN_FRAMES (or --regress-frames) frames and exit.  This is
// the regress-only guard for the widescreen non-gameplay compositions: it draws
// the screens with the game's own functions, so a change to the compositor or to
// a screen's layout shows up as a frame-hash difference.
//
// The screens themselves are interactive loops that normally block on input.
// In screen mode hasInput() reports input as available (src/keyboard.c), so the
// loops keep redrawing, and the --regress-frames cap ends the run.  No real
// input ever reaches the menu logic (handleSdlEvents() drops it in regress
// mode), so the rendered screen does not advance.
#include "regress.h"
#include "bike_menu.h"

#include "config.h"
#include "episodes.h"
#include "fonthand.h"
#include "game_menu.h"
#include "game_schema.h"
#include "highscores.h"
#include "joystick.h"
#include "jukebox.h"
#include "keyboard.h"
#include "logging.h"
#include "mainint.h"
#include "menus.h"
#include "modern.h"
#include "mtrand.h"
#include "opentyr.h"
#include "palette.h"
#include "picload.h"
#include "sprite.h"
#include "tyrian2.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Check the real video inverse map against each split picked from a menu's
// rendered pixels.  The next modern_present_frame replaces this temporary
// output rectangle before any input is handled; the test draws nothing.
void regress_screen_verify_mapping(void)
{
	int left, right, insert_left, insert_right;
	if (!modern_frame_is_split(&left, &right, &insert_left, &insert_right))
		return;
	const ModernFrame *frame = modern_current_frame();
	static int checked_left = -1, checked_right = -1, checked_insert_left = -1, checked_insert_right = -1;
	if (left == checked_left && right == checked_right &&
	    insert_left == checked_insert_left && insert_right == checked_insert_right)
		return;
	const SDL_Rect rect = { 0, 0, frame->w * 3, frame->h * 3 };
	video_set_last_output_rect_split(&rect, frame->w, frame->h, left, right, insert_left, insert_right);
	static const int rows[] = { 10, 38, 62, 86, 110, 134, 158, 183 };
	for (size_t row = 0; row < COUNTOF(rows); ++row)
	{
		for (int x = 0; x < 320; ++x)
		{
			Sint32 mapped_x = x, mapped_y = rows[row];
			mapScreenPointToWindow(&mapped_x, &mapped_y);
			mapWindowPointToScreen(&mapped_x, &mapped_y);
			if (mapped_x != x || mapped_y != rows[row])
			{
				logFatal("Menu mouse mapping failed at (%d,%d), split %d/%d: got (%d,%d).", x, rows[row], left, right, mapped_x, mapped_y);
				exit(EXIT_FAILURE);
			}
		}
	}
	const int splits[] = { left, right }, inserts[] = { insert_left, insert_right };
	for (size_t i = 0; i < COUNTOF(splits); ++i)
	{
		if (inserts[i] == 0 || splits[i] == 0)
			continue;
		Sint32 x0 = splits[i] - 1, x1 = splits[i], y0 = 80, y1 = 80;
		mapScreenPointToWindow(&x0, &y0);
		mapScreenPointToWindow(&x1, &y1);
		x0 = (x0 + x1) / 2;
		mapWindowPointToScreen(&x0, &y0);
		if (x0 != splits[i] || y0 != 80)
		{
			logFatal("Inserted menu band does not map to split %d.", splits[i]);
			exit(EXIT_FAILURE);
		}
	}
	checked_left = left;
	checked_right = right;
	checked_insert_left = insert_left;
	checked_insert_right = insert_right;
	logInfo("Regression: menu inverse mouse map PASS (split %d/%d, inserted %d/%d).", left, right, insert_left, insert_right);
}

static void draw_test_glyph(SDL_Surface *surface, int x, unsigned int font, int mode)
{
	const unsigned int glyph = fontMap['A'];
	switch (mode)
	{
	case 0: blit_sprite(surface, x, 4, font, glyph); break;
	case 1: blit_sprite_blend(surface, x, 4, font, glyph); break;
	case 2: blit_sprite_hv_unsafe(surface, x, 4, font, glyph, 15, -3); break;
	case 3: blit_sprite_hv(surface, x, 4, font, glyph, 15, -3); break;
	case 4: blit_sprite_hv_blend(surface, x, 4, font, glyph, 15, -3); break;
	case 5: blit_sprite_dark(surface, x, 4, font, glyph, false); break;
	case 6: blit_sprite_dark(surface, x, 4, font, glyph, true); break;
	}
}

// Compare a partially visible glyph against a crop of the contained glyph,
// checking every pixel so row wrapping and writes outside the glyph fail.
static void verify_font_clipping(void)
{
	SDL_Surface *reference = SDL_CreateSurface(48, 32, SDL_PIXELFORMAT_INDEX8);
	SDL_Surface *edge = SDL_CreateSurface(48, 32, SDL_PIXELFORMAT_INDEX8);
	if (reference == NULL || edge == NULL)
		exit(EXIT_FAILURE);
	for (unsigned int font = FONT_SHAPES; font <= TINY_FONT; ++font)
	{
		const int width = sprite(font, fontMap['A'])->width;
		const int positions[] = { 46, -2, 48, -width };
		for (int mode = 0; mode < 7; ++mode)
		{
			SDL_FillSurfaceRect(reference, NULL, 0x85);
			draw_test_glyph(reference, 8, font, mode);
			for (size_t i = 0; i < COUNTOF(positions); ++i)
			{
				SDL_FillSurfaceRect(edge, NULL, 0x85);
				draw_test_glyph(edge, positions[i], font, mode);
				for (int y = 0; y < edge->h; ++y)
					for (int x = 0; x < edge->w; ++x)
					{
						const int column = x - positions[i];
						const Uint8 expected = column >= 0 && column < width
							? *((Uint8 *)reference->pixels + y * reference->pitch + 8 + column) : 0x85;
						if (*((Uint8 *)edge->pixels + y * edge->pitch + x) != expected)
						{
							logFatal("Font clipping failed: font %u mode %d at (%d,%d), origin %d.", font, mode, x, y, positions[i]);
							exit(EXIT_FAILURE);
						}
					}
			}
		}
	}
	SDL_DestroySurface(reference);
	SDL_DestroySurface(edge);
	logInfo("Regression: font clipping matches contained glyph crops (3 fonts, 7 modes, 4 edges).");
}

// Representative, deterministic save data for the high-score screen (the real
// save files are not loaded in regress mode).  Names and scores are owned by this
// file, in both variants: the fixtures carry no text from the game data.
static void seed_high_scores(void)
{
	static const struct
	{
		const char *name;
		JE_longint score;
		JE_byte difficulty;
	} entries[6] =
	{
		{ "JACK", 123456, 0 },
		{ "JILL",  98765, 1 },
		{ "ZAN",   55555, 2 },
		{ "ACE",   43210, 0 },
		{ "BOB",   21000, 1 },
		{ "KAT",    9999, 2 },
	};

	for (size_t i = 0; i < 6; ++i)
	{
		snprintf(saveFiles[i].highScoreName, sizeof saveFiles[i].highScoreName, "%s", entries[i].name);
		saveFiles[i].highScore1 = entries[i].score;
		saveFiles[i].highScoreDiff = entries[i].difficulty;
	}

	// Variants whose save has boards of its own (Tyrian 2000): every main board
	// gets three entries, in a different order per board so the sort is drawn.
	for (size_t board = 0; board < VARIANT_SCORE_BOARDS; ++board)
	{
		for (size_t entry = 0; entry < VARIANT_SCORE_ENTRIES; ++entry)
		{
			VariantHighScore *score = &variantHighScores[board][entry];
			const size_t pick = (board + entry * 2) % 6;

			snprintf(score->playerName, sizeof score->playerName, "%s %zu", entries[pick].name, board);
			score->score = entries[pick].score / (JE_longint)(1 + board % 3) + (JE_longint)board;
			score->difficulty = (JE_byte)((entries[pick].difficulty + board) % 6);
		}
	}
}

// A few populated save slots, so the load/save screen draws its per-slot name,
// level name and the episode text at x~297 (the element the S1 fallback
// analysis asks about) instead of only empty slots.
static void seed_save_slots(void)
{
	static const struct
	{
		const char *name;
		const char *level;
	} entries[3] =
	{
		{ "ALPHA", "TYRIAN 1" },
		{ "BETA",  "ASTEROID" },
		{ "GAMMA", "SAVARA" },
	};

	for (size_t i = 0; i < 3; ++i)
	{
		saveFiles[i].level = 1;
		saveFiles[i].episode = 1;
		snprintf(saveFiles[i].name, sizeof saveFiles[i].name, "%s", entries[i].name);
		snprintf(saveFiles[i].levelName, sizeof saveFiles[i].levelName, "%s", entries[i].level);
	}
}

// A screen is selected as NAME or NAME:key=value,key=value.  The keys are
// fixtures the descriptor may set, so one screen renders with different data:
//   ship=N   the player's ship type
//   shipgraphic=N   select a ship by its big illustration ID (45 / 46)
//   front=N, rear=N   the player's front / rear weapon port
//   twomode=1  a rear weapon that has two firing modes
//   mode=N   the rear weapon mode (1 or 2)
//   sel=N    the menu row to select (2 = first item)
//   page=N   the page (episode) the high-score screen opens on
//   cat=N    the upgrade category of the weapon simulator (3 front, 4 rear)
static int screen_param(const char *key, int fallback)
{
	const char *params = strchr(regress_screen, ':');
	const size_t key_len = strlen(key);

	while (params != NULL)
	{
		++params;
		if (strncmp(params, key, key_len) == 0 && params[key_len] == '=')
			return atoi(params + key_len + 1);
		params = strchr(params, ',');
	}
	return fallback;
}

// The screen's name without its fixture descriptor.
static bool screen_is(const char *name)
{
	const size_t len = strlen(name);
	return strncmp(regress_screen, name, len) == 0 && (regress_screen[len] == '\0' || regress_screen[len] == ':');
}

void regress_screen_run(void)
{
	// main() seeded the RNG from the wall clock; pin it so JE_initPlayerData()'s
	// random draws cannot leak into the rendered frames.
	mt_srand(32402394);

	// The title screen draws the build version; pin it so the title baselines
	// do not change on every commit.
	opentyrian_version = "regress";

	// Loads the item tables and the cube filename the shop screens read.
	JE_initEpisode(1);

	JE_initPlayerData();
	JE_sortHighScores();

	// A little cash so the upgrade/purchase list shows real costs and the
	// affordability shading is exercised.
	player[0].cash = 12345;

	seed_high_scores();
	highScoreSortAll();  // the game sorts after loading a save
	seed_save_slots();

	// The data-cube screens read cubeMax/cubeList (normally set from a save or
	// the level script).  load_cube() treats the entry as "skip this many marker
	// lines", so cube 1 (the first entry in cubetxt1.dat) is index 1.
	cubeMax = 1;
	cubeList[0] = 1;

	// Start this run's own frame timeline; snapshots count from 0.
	regress_frame_reset();

	const char *name = regress_screen;

	const int ship = screen_param("ship", 0);
	if (ship > 0)
		player[0].items.ship = ship;
	const int shipgraphic = screen_param("shipgraphic", 0);
	if (shipgraphic > 0)
	{
		bool found = false;
		for (unsigned int id = 1; id <= gameSchema()->ship_max; ++id)
		{
			if (ships[id].bigshipgraphic == shipgraphic)
			{
				player[0].items.ship = (JE_byte)id;
				found = true;
				logInfo("Regression: ship %u uses big illustration %d.", id, shipgraphic);
				break;
			}
		}
		if (!found)
		{
			logFatal("No ship has big illustration %d.", shipgraphic);
			exit(EXIT_FAILURE);
		}
	}

	const int front = screen_param("front", -1), rear = screen_param("rear", -1);
	if (front >= 0)
		player[0].items.weapon[FRONT_WEAPON].id = (JE_byte)front;
	if (rear >= 0)
		player[0].items.weapon[REAR_WEAPON].id = (JE_byte)rear;
	if (screen_param("twomode", 0))
	{
		// The first port with two firing modes (chosen from the data).
		for (unsigned int port = 1; port <= gameSchema()->port_max; ++port)
		{
			if (weaponPort[port].opnum == 2)
			{
				player[0].items.weapon[REAR_WEAPON].id = (JE_byte)port;
				break;
			}
		}
	}
	if (screen_param("mode", 0) > 0)
		player[0].weapon_mode = (JE_byte)screen_param("mode", 0);
	JE_itemScreenStartSelection(screen_param("sel", 0));
	if (front >= 0 || rear >= 0 || screen_param("twomode", 0) || ship > 0 || shipgraphic > 0)
		player[0].last_items = player[0].items;  // what the shop lists are built from

	if (screen_is("title"))
	{
		titleScreen();
	}
	else if (screen_is("gameplay-select"))
	{
		// The full game / arcade / (Timed Battle) / two-player choice.
		gameplaySelect();
	}
	else if (screen_is("episode-select"))
	{
		episodeSelect();
	}
	else if (screen_is("high-scores"))
	{
		JE_highScoreScreenAt((size_t)screen_param("page", 0));
	}
	else if (screen_is("game-menu"))
	{
		JE_itemScreenStartAt(MENU_FULL_GAME, 0);
		JE_itemScreen();
	}
	else if (screen_is("upgrade"))
	{
		JE_itemScreenStartAt(MENU_UPGRADES, 0);
		JE_itemScreen();
	}
	else if (screen_is("purchase"))
	{
		// Category 3 is "front weapon" (see JE_menuFunction); the purchase list
		// then holds the available front weapons and their costs.
		JE_itemScreenStartAt(MENU_UPGRADE_SUB, 3);
		JE_itemScreen();
	}
	else if (screen_is("shield"))
	{
		// Category 5 is "shield".  Its purchase list reaches x=310, which puts the
		// pic-1 widening split on the title box's right bevel (column 311), so this
		// guards the header rows of the widened box.
		JE_itemScreenStartAt(MENU_UPGRADE_SUB, 5);
		JE_itemScreen();
	}
	else if (screen_is("options"))
	{
		JE_itemScreenStartAt(MENU_OPTIONS, 0);
		JE_itemScreen();
	}
	else if (screen_is("options-limited"))
	{
		// The options menu that network games use (no save/load rows).
		JE_itemScreenStartAt(MENU_LIMITED_OPTIONS, 0);
		JE_itemScreen();
	}
	else if (screen_is("mouse"))
	{
		// Tyrian 2000's mouse settings menu.
		JE_itemScreenStartAt(MENU_MOUSE_CONFIG, 0);
		JE_itemScreen();
	}
	else if (screen_is("cube-list"))
	{
		JE_itemScreenStartAt(MENU_DATA_CUBES, 0);
		JE_itemScreen();
	}
	else if (screen_is("cube-reader"))
	{
		JE_itemScreenStartAt(MENU_DATA_CUBE_SUB, 0);
		JE_itemScreen();
	}
	else if (screen_is("keyboard") || screen_is("keyboard-long"))
	{
		if (screen_is("keyboard-long"))
			keySettings[0] = SDL_SCANCODE_KP_MEMSUBTRACT;
		JE_itemScreenStartAt(MENU_KEYBOARD_CONFIG, 0);
		JE_itemScreen();
	}
	else if (screen_is("joystick") || screen_is("joystick-multi"))
	{
		if (screen_is("joystick-multi"))
		{
			verify_font_clipping();
			// Reuse the synthetic stick: no hardware or persistent user config.
			joystick_inject_stick(0, 0);
			if (joysticks != 1)
				exit(EXIT_FAILURE);
			joystick[0].is_gamepad = true;
			reset_joystick_assignments(0);
			joystick[0].assignment[4][1] = (Joystick_assignment){ GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false, false };
			joystick[0].assignment[5][0] = (Joystick_assignment){ BUTTON, 0, false, false };
			joystick[0].assignment[5][1] = (Joystick_assignment){ BUTTON, 1, false, false };
			joystick[0].assignment[6][0] = (Joystick_assignment){ GAMEPAD_AXIS, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, false, false };
			joystick[0].assignment[6][1] = (Joystick_assignment){ HAT, 11, true, true };
			// Very large raw indices exercise the counted overflow fallback.
			joystick[0].assignment[7][0] = (Joystick_assignment){ HAT, 12344, false, true };
			joystick[0].assignment[7][1] = (Joystick_assignment){ AXIS, 12344, false, false };
		}
		JE_itemScreenStartAt(MENU_JOYSTICK_CONFIG, 0);
		JE_itemScreen();
	}
	else if (screen_is("load-save"))
	{
		// Menu 6 with performSave false is the load screen (its slot rows carry
		// the x~297 episode text the fallback analysis asks about).
		performSave = false;
		JE_itemScreenStartAt(MENU_LOAD_SAVE, 0);
		JE_itemScreen();
	}
	else if (screen_is("save"))
	{
		// Menu 6 with performSave true is the in-game save screen (same layout
		// as load-save, different header/action text).
		performSave = true;
		JE_itemScreenStartAt(MENU_LOAD_SAVE, 0);
		JE_itemScreen();
	}
	else if (screen_is("load"))
	{
		// The title-screen Load Game screen (pic 2, JE_loadScreen), reached from
		// the main menu; it does not use the item-screen frame.
		JE_loadScreen();
	}
	else if (screen_is("quit"))
	{
		// The quit confirmation dialog, drawn over the pic-1 frame.  In screen
		// mode JE_quitRequest() never accepts input, so it redraws the same
		// dialog every frame and the --regress-frames cap ends the run.
		JE_loadPic(VGAScreen, 1, false);
		set_palette(colors, 0, 255);
		JE_quitRequest();
	}
	else if (screen_is("solid"))
	{
		// A story picture (pic 5, flat black edges): the compositor uses the
		// solid edge fill.  This mirrors the level event that shows it.
		JE_loadPic(VGAScreen, 5, true);
		for (;;)
			JE_showVGA();
	}
	else if (screen_is("setup") || screen_is("setup-crt-picker"))
	{
		// The in-game Setup screen's Graphics submenu, which carries the
		// presentation/aspect/pixel-aspect/smooth-motion pickers.
		setupMenuStartAt(SETUP_MENU_GRAPHICS);
		setupMenu();
	}
	else if (screen_is("bike-menu") || screen_is("bike-menu-nav") || screen_is("bike-menu-edit") ||
	         screen_is("bike-menu-surge"))
	{
		// The Bike and Controls setup screen (bike_menu.c).  The -nav and -edit
		// variants queue a key script that walks to the FTP row and steps it, or
		// edits the bridge address, so the snapshot shows the result.  -surge walks
		// down to Surge Ratio, raises it twice (1.30x), then lowers Surge Min Power twice (70 %).
		if (screen_is("bike-menu-nav"))
			bike_menu_regress_keys("___]]]]]]]]_]]]]]]]]_]]]]");
		else if (screen_is("bike-menu-edit"))
			bike_menu_regress_keys("__#<<<<<<<<<<<<<<10.0.0.5:8788");
		else if (screen_is("bike-menu-surge"))
			bike_menu_regress_keys("___________]]_[[");
		setupMenuStartAt(SETUP_MENU_BIKE);
		setupMenu();
	}
	else if (screen_is("nav-map"))
	{
		// The short route and the palette are seeded inside JE_itemScreen()
		// (after its menu-choice and palette reset), so the frame matches the
		// real nav screen.
		JE_itemScreenStartAt(MENU_PLAY_NEXT_LEVEL, 0);
		JE_itemScreen();
	}
	else if (screen_is("ship-specs"))
	{
		// JE_doShipSpecs() is normally called from JE_itemScreen, which has
		// already loaded pic 1 and set its palette; do the same here.
		JE_loadPic(VGAScreen, 1, false);
		set_palette(colors, 0, 255);

		// In Classic JE_doShipSpecs() plays the zoom-in animation and presents
		// many frames; in Modern wide it draws one frame into the canvas-wide
		// scratch.  Loop until the frame cap exits.
		for (;;)
			JE_doShipSpecs();
	}
	else if (screen_is("jukebox"))
	{
		jukebox();
	}
	else if (screen_is("weapon-sim"))
	{
		// Category 3 (front weapon) opens the upgrade submenu with the weapon
		// simulator in the left window; cat=4 opens the rear weapon's.
		JE_itemScreenStartAt(MENU_UPGRADE_SUB, screen_param("cat", 3));
		JE_itemScreen();
	}
	else if (screen_is("credits"))
	{
		// The credits loop breaks on input; in regress mode input is reported
		// immediately, so hold it back and let the frame cap end the run on a
		// deterministic mid-scroll frame.
		JE_playCreditsRegressHold(true);
		JE_playCredits();
	}
	else
	{
		logFatal("Unknown --regress-screen '%s'.", name);
		exit(EXIT_FAILURE);
	}

	// Every screen above loops until the --regress-frames cap ends the run.
	logFatal("Regression screen '%s' returned before its frame cap.", name);
	exit(EXIT_FAILURE);
}
