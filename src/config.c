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
#include "config.h"

#include "backgrnd.h"
#include "crt_filter.h"
#include "episodes.h"
#include "file.h"
#include "game_rules.h"
#include "game_schema.h"
#include "helptext.h"
#include "interp.h"
#include "logging.h"
#include "loudness.h"
#include "memreader.h"
#include "memwriter.h"
#include "modern.h"
#include "modern_bloom.h"
#include "bike.h"
#include "modern_depth.h"
#include "mouse_buttons.h"
#include "mtrand.h"
#include "nortsong.h"
#include "opentyr.h"
#include "player.h"
#include "varz.h"
#include "vfx.h"
#include "video.h"

#define SAVE_FILES_SIZE (109 * SAVE_FILES_NUM)
#define SAVE_FILE_SIZE (SAVE_FILES_SIZE + 100)

// Bytes of one high-score entry of the Tyrian 2000 save suffix.
#define VARIANT_SCORE_TIMED_SIZE (4 + 1 + VARIANT_SCORE_NAME_MAX + 1)
#define VARIANT_SCORE_MAIN_SIZE  (4 + 4 + 1 + VARIANT_SCORE_NAME_MAX + 1)
#define VARIANT_SCORE_SUFFIX_MAX \
	(VARIANT_SCORE_TIMED_BOARDS * VARIANT_SCORE_ENTRIES * VARIANT_SCORE_TIMED_SIZE + \
	 (VARIANT_SCORE_BOARDS - VARIANT_SCORE_TIMED_BOARDS) * VARIANT_SCORE_ENTRIES * VARIANT_SCORE_MAIN_SIZE)
// The unknown field of a fresh main-game entry (the original's own placeholder).
#define VARIANT_SCORE_UNKNOWN_DEFAULT 0x12345678u

/* Configuration Load/Save handler */

static const Uint8 cryptKey[10] /* [1..10] */ =
{
	15, 50, 89, 240, 147, 34, 86, 9, 32, 208
};

const KeySettings defaultKeySettings =
{
	SDL_SCANCODE_UP,
	SDL_SCANCODE_DOWN,
	SDL_SCANCODE_LEFT,
	SDL_SCANCODE_RIGHT,
	SDL_SCANCODE_SPACE,
	SDL_SCANCODE_RETURN,
	SDL_SCANCODE_LCTRL,
	SDL_SCANCODE_LALT,
};

static const char *const keySettingNames[] =
{
	"up",
	"down",
	"left",
	"right",
	"fire",
	"change fire",
	"left sidekick",
	"right sidekick",
};

static const char defaultHighScoreNames[34][23] = /* [1..34] of string [22] */
{/*1P*/
/*TYR*/   "The Prime Chair", /*13*/
          "Transon Lohk",
          "Javi Onukala",
          "Mantori",
          "Nortaneous",
          "Dougan",
          "Reid",
          "General Zinglon",
          "Late Gyges Phildren",
          "Vykromod",
          "Beppo",
          "Borogar",
          "ShipMaster Carlos",

/*OTHER*/ "Jill", /*5*/
          "Darcy",
          "Jake Stone",
          "Malvineous Havershim",
          "Marta Louise Velasquez",

/*JAZZ*/  "Jazz Jackrabbit", /*3*/
          "Eva Earlong",
          "Devan Shell",

/*OMF*/   "Crystal Devroe", /*11*/
          "Steffan Tommas",
          "Milano Angston",
          "Christian",
          "Shirro",
          "Jean-Paul",
          "Ibrahim Hothe",
          "Angel",
          "Cossette Akira",
          "Raven",
          "Hans Kreissack",

/*DARE*/  "Tyler", /*2*/
          "Rennis the Rat Guard"
};

static const char defaultTeamNames[22][25] = /* [1..22] of string [24] */
{
	"Jackrabbits",
	"Team Tyrian",
	"The Elam Brothers",
	"Dare to Dream Team",
	"Pinball Freaks",
	"Extreme Pinball Freaks",
	"Team Vykromod",
	"Epic All-Stars",
	"Hans Keissack's WARriors",
	"Team Overkill",
	"Pied Pipers",
	"Gencore Growlers",
	"Microsol Masters",
	"Beta Warriors",
	"Team Loco",
	"The Shellians",
	"Jungle Jills",
	"Murderous Malvineous",
	"The Traffic Department",
	"Clan Mikal",
	"Clan Patrok",
	"Carlos' Crawlers"
};

static const JE_EditorItemAvailType initialEditorItemAvail =  // FKA initialItemAvail
{
	1,1,1,0,0,1,1,0,1,1,1,1,1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0, /* Front/Rear Weapons 1-38  */
	0,0,0,0,0,0,0,0,0,0,1,                                                           /* Fill                     */
	1,0,0,0,0,1,0,0,0,1,1,0,1,0,0,0,0,0,                                             /* Sidekicks          51-68 */
	0,0,0,0,0,0,0,0,0,0,0,                                                           /* Fill                     */
	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                                                   /* Special Weapons    81-93 */
	0,0,0,0,0                                                                        /* Fill                     */
};

/* Last 2 bytes = Word
 *
 * Max Value = 1680
 * X div  60 = Armor  (1-28)
 * X div 168 = Shield (1-12)
 * X div 280 = Engine (1-06)
 */

JE_boolean smoothies[9] = /* [1..9] */
{ 0, 0, 0, 0, 0, 0, 0, 0, 0 };

JE_byte starShowVGASpecialCode;

/* CubeData */
JE_word lastCubeMax, cubeMax;
JE_word cubeList[4]; /* [1..4] */

/* High-Score Stuff */
JE_boolean gameHasRepeated;  // can only get highscore on first play-through

/* Difficulty */
JE_shortint difficultyLevel, oldDifficultyLevel,
            initialDifficulty;  // can only get highscore on initial episode

/* Player Stuff */
uint    power, lastPower, powerAdd;
JE_byte shieldWait, shieldT;

JE_byte          shotRepeat[11], shotMultiPos[11];
JE_boolean       portConfigChange, portConfigDone;

/* Level Data */
char    lastLevelName[11], levelName[11]; /* string [10] */
JE_byte mainLevel, nextLevel, saveLevel;   /*Current Level #*/

/* Keyboard Junk */
KeySettings keySettings;

/* Configuration */
JE_shortint levelFilter, levelFilterNew, levelBrightness, levelBrightnessChg;
JE_boolean  filtrationAvail, filterActive, filterFade, filterFadeStart;

JE_boolean gameJustLoaded;

JE_boolean galagaMode;

JE_boolean extraGame;

JE_boolean twoPlayerMode, twoPlayerLinked, onePlayerAction, superTyrian;
JE_boolean trentWin = false;
JE_byte    superArcadeMode;
bool       timedBattleMode;
JE_byte    timeBattleSelection;

JE_byte    superArcadePowerUp;

JE_real linkGunDirec;
JE_byte inputDevice[2] = { 1, 2 }; // 0:any  1:keyboard  2:mouse  3+:joystick

JE_byte secretHint;
JE_byte background3over;
JE_byte background2over;
JE_byte gammaCorrection;
JE_boolean superPause = false;
JE_boolean explosionTransparent,
           youAreCheating,
           displayScore,
           background2, smoothScroll, wild, superWild, starActive,
           topEnemyOver,
           skyEnemyOverAll,
           background2notTransparent;

JE_byte    fastPlay;
JE_boolean pentiumMode;

/* Savegame files */
JE_byte    gameSpeed;
JE_byte    processorType;        /* effective level: 1=386 2=486 3=High 4=Pentium 5=Nonstandard VGA 6=SuperWild */
JE_byte    processorTypeChoice;  /* the player's stored choice (tyrian.cfg); Classic renders at it, Modern pins 4 (or 6) */

VariantHighScore variantHighScores[VARIANT_SCORE_BOARDS][VARIANT_SCORE_ENTRIES];
JE_SaveFilesType saveFiles; /*array[1..saveLevelnum] of savefiletype;*/

JE_EditorItemAvailType editorItemAvail;

JE_word editorLevel;   /*Initial value 800*/

Config opentyrian_config;  // implicitly initialized

// Fields of TYRIAN.CFG that are preserved for compatibility
static Uint8 inputDevice_ = 0;  // FKA inputDevice
static Uint8 jConfigure = 0;  // FKA NortSong.jConfigure
static Uint8 midiPort = 0;  // FKA NortSong.midiPort
static Uint8 soundEffects = 0;  // FKA NortSong.soundEffects
static Uint8 versionNum;   /* SW 1.0 and SW/Reg 1.1 = 0 or 1
                            * EA 1.2 = 2 */
static const Uint8 defaultJoyButtonAssign[4] = { 1, 4, 5, 5 };  // FKA Joystick.defaultJoyButtonAssign
static Uint8 joyButtonAssign[4] = { 0 };  // FKA Joystick.joyButtonAssign
static Uint8 inputDevice1 = 0;
static Uint8 inputDevice2 = 0;
static const Uint8 defaultDosKeySettings[8] = { 72, 80, 75, 77, 57, 28, 29, 56 };  // FKA defaultKeySettings
static Uint8 dosKeySettings[8] = { 0 };  // FKA keySettings

static const char *const opentyrianConfigFilename = "opentyrian.cfg";

// The variant the player last started from the launcher, preselected the next
// time it opens.  -1 until the launcher has chosen one or the config names it,
// so a config that never saw the launcher keeps no launcher section.
int launcherLastVariant = -1;

static void loadOpenTyrianConfig(void)
{
	// defaults
	fullscreen_display = -1;
	presentation = PRESENTATION_CLASSIC;
	modern_aspect = MODERN_ASPECT_4_3;
	modern_pixel_aspect = PIXEL_ASPECT_ORIGINAL;
	interp_smooth_motion = true;
	crt_filter_set_mode(CRT_FILTER_OFF);
	modern_bloom_quality = MODERN_QUALITY_LOW;
	modern_lighting_quality = MODERN_QUALITY_LOW;
	modern_depth_quality = MODERN_DEPTH_ON;
	vfx_level = VFX_LOW;
	starfield_set_speed_percent(25);
	memcpy(keySettings, defaultKeySettings, sizeof(keySettings));

	// Steam Deck first-run defaults.  A Deck is a 1280x800 16:10 handheld in a
	// fullscreen game-scope session, so start fullscreen with the Modern canvas
	// following the display ("auto" is exactly 16:10 at 1280x800 and still
	// right on a docked 16:9 TV).  Any stored opentyrian.cfg overrides all three
	// (it always writes fullscreen, presentation and aspect), so an existing
	// install is never changed, and nothing changes off the Deck.
	if (steamDeck())
	{
		fullscreen_display = 0;
		presentation = PRESENTATION_MODERN;
		modern_aspect = MODERN_ASPECT_AUTO;
	}
	
	Config *config = &opentyrian_config;

	File file = userFileOpen(opentyrianConfigFilename, "r");
	if (file.error)
	{
		logWarn("Failed to open '%s': %s", opentyrianConfigFilename, fileGetError(&file));

		return;
	}
	
	bool success = config_parse(config, file.f);
	file.error |= ferror(file.f) != 0;

	if (file.error)
		logError("Failed to read from '%s': %s", opentyrianConfigFilename, fileGetError(&file));
	else if (!success)
		logError("Failed to parse '%s'.", opentyrianConfigFilename);

	fileClose(&file);

	if (!success)
		return;
	
	ConfigSection *section;
	
	section = config_find_section(config, "video", NULL);
	if (section != NULL)
	{
		config_get_int_option(section, "fullscreen", &fullscreen_display);

		// An old "scaler" key is accepted and ignored: the software scalers were
		// removed and the GPU does the scaling now.

		const char *pixel_aspect_name;
		const bool have_pixel_aspect = config_get_string_option(section, "pixel_aspect", &pixel_aspect_name);
		if (have_pixel_aspect)
			set_modern_pixel_aspect_by_name(pixel_aspect_name);

		const char *scaling_mode_name;
		if (config_get_string_option(section, "scaling_mode", &scaling_mode_name))
		{
			if (!set_scaling_mode_by_name(scaling_mode_name))
			{
				// The pre-merge "Fit 8:5" and "Fit 4:3" modes became Fit.  They also
				// implied a pixel aspect; fill it in only when the cfg does not set
				// one explicitly (an explicit pixel_aspect wins).
				if (strcmp(scaling_mode_name, "Fit 8:5") == 0)
				{
					scaling_mode = SCALE_FIT;
					if (!have_pixel_aspect)
						modern_pixel_aspect = PIXEL_ASPECT_SQUARE;
				}
				else if (strcmp(scaling_mode_name, "Fit 4:3") == 0)
				{
					scaling_mode = SCALE_FIT;
					if (!have_pixel_aspect)
						modern_pixel_aspect = PIXEL_ASPECT_ORIGINAL;
				}
			}
		}

		const char *presentation_name;
		if (config_get_string_option(section, "presentation", &presentation_name))
			set_presentation_by_name(presentation_name);

		const char *aspect_name;
		if (config_get_string_option(section, "aspect", &aspect_name))
			set_modern_aspect_by_name(aspect_name);

		const char *smooth_motion_name;
		if (config_get_string_option(section, "smooth_motion", &smooth_motion_name))
			set_smooth_motion_by_name(smooth_motion_name);
		
		const char *crt_name;
		if (config_get_string_option(section, "crt_filter", &crt_name))
			crt_filter_set_by_name(crt_name);

		// One "lighting" key drives both bloom and dynamic lighting.  An old
		// "bloom" key is read only as a fallback and, for compatibility, sets
		// both effects too.
		ModernQuality quality = MODERN_QUALITY_LOW;
		const char *lighting_name;
		if (config_get_string_option(section, "lighting", &lighting_name) &&
		    set_modern_quality_by_name(lighting_name, &quality))
		{
			modern_bloom_quality = quality;
			modern_lighting_quality = quality;
		}
		else
		{
			const char *bloom_name;
			if (config_get_string_option(section, "bloom", &bloom_name) &&
			    set_modern_quality_by_name(bloom_name, &quality))
			{
				modern_bloom_quality = quality;
				modern_lighting_quality = quality;
			}
		}

		// Missing and unknown depth values keep the On default.
		const char *depth_name;
		if (config_get_string_option(section, "modern_depth", &depth_name))
		{
			ModernDepth depth = MODERN_DEPTH_ON;
			if (set_modern_depth_by_name(depth_name, &depth))
				modern_depth_quality = depth;
		}

		const char *vfx_name;
		if (config_get_string_option(section, "vfx", &vfx_name))
			set_vfx_by_name(vfx_name);

		int starfield_percent;
		if (config_get_int_option(section, "starfield_speed_percent", &starfield_percent))
			starfield_set_speed_percent(starfield_percent);
	}

	bike_config_load();

	section = config_find_section(config, "launcher", NULL);
	if (section != NULL)
	{
		const char *variant_name;
		GameVariant variant;
		if (config_get_string_option(section, "last_variant", &variant_name) &&
		    gameVariantParse(variant_name, &variant))
			launcherLastVariant = (int)variant;
	}

	section = config_find_section(config, "keyboard", NULL);
	if (section != NULL)
	{
		for (size_t i = 0; i < COUNTOF(keySettings); ++i)
		{
			const char *keyName;
			if (config_get_string_option(section, keySettingNames[i], &keyName))
			{
				SDL_Scancode scancode = SDL_GetScancodeFromName(keyName);
				if (scancode != SDL_SCANCODE_UNKNOWN)
					keySettings[i] = scancode;
			}
		}
	}

	// Mouse buttons (chosen in the Tyrian 2000 options menu).
	section = config_find_section(config, "mouse", NULL);
	if (section != NULL)
	{
		for (unsigned int i = 0; i < MOUSE_BUTTON_COUNT; ++i)
		{
			const char *actionName;
			if (config_get_string_option(section, mouse_button_config_key(i), &actionName))
				mouse_button_action_set_by_name(i, actionName);
		}
	}
}

static void saveOpenTyrianConfig(void)
{
	Config *config = &opentyrian_config;
	
	ConfigSection *section;
	
	section = config_find_or_add_section(config, "video", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory
	
	config_set_int_option(section, "fullscreen", fullscreen_display);
	
	config_set_string_option(section, "scaling_mode", scaling_mode_names[scaling_mode]);

	config_set_string_option(section, "presentation", presentation_names[presentation]);

	config_set_string_option(section, "aspect", modern_aspect_names[modern_aspect]);

	config_set_string_option(section, "pixel_aspect", modern_pixel_aspect_names[modern_pixel_aspect]);
	config_set_string_option(section, "smooth_motion", interp_smooth_motion ? "on" : "off");
	config_set_string_option(section, "crt_filter", crt_filter_names[crt_filter_mode()]);
	config_set_string_option(section, "vfx", vfx_level_names[vfx_level]);

	config_set_int_option(section, "starfield_speed_percent", starfield_speed_percent);

	config_set_string_option(section, "lighting", modern_quality_names[modern_lighting_quality]);
	config_set_string_option(section, "modern_depth", modern_depth_names[modern_depth_quality]);

	bike_config_save();

	if (launcherLastVariant >= 0)
	{
		section = config_find_or_add_section(config, "launcher", NULL);
		if (section == NULL)
			exit(EXIT_FAILURE);  // out of memory

		config_set_string_option(section, "last_variant", gameVariantGet((GameVariant)launcherLastVariant)->cli_name);
	}

	section = config_find_or_add_section(config, "keyboard", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory

	for (size_t i = 0; i < COUNTOF(keySettings); ++i)
	{
		const char *keyName = SDL_GetScancodeName(keySettings[i]);
		if (keyName[0] == '\0')
			keyName = NULL;
		config_set_string_option(section, keySettingNames[i], keyName);
	}

	// Only what the player chose in the mouse menu is written, so a session that
	// never uses it writes the same file as before.
	for (unsigned int i = 0; i < MOUSE_BUTTON_COUNT; ++i)
	{
		const int action = mouse_button_config_action(i);
		if (action < 0)
			continue;

		section = config_find_or_add_section(config, "mouse", NULL);
		if (section == NULL)
			exit(EXIT_FAILURE);  // out of memory

		config_set_string_option(section, mouse_button_config_key(i), mouse_action_config_name((MouseAction)action));
	}

	File file = userFileOpen(opentyrianConfigFilename, "w");
	if (file.error)
	{
		logError("Failed to open '%s': %s", opentyrianConfigFilename, fileGetError(&file));

		return;
	}
	
	config_write(config, file.f);
	file.error |= ferror(file.f) != 0;

	fileFlush(&file);

	if (file.error)
		logError("Failed to write to '%s': %s", opentyrianConfigFilename, fileGetError(&file));

	fileClose(&file);
}

static void playeritems_to_pitems(JE_PItemsType pItems, PlayerItems *items, JE_byte initial_episode_num)
{
	pItems[0]  = items->weapon[FRONT_WEAPON].id;
	pItems[1]  = items->weapon[REAR_WEAPON].id;
	pItems[2]  = items->super_arcade_mode;
	pItems[3]  = items->sidekick[LEFT_SIDEKICK];
	pItems[4]  = items->sidekick[RIGHT_SIDEKICK];
	pItems[5]  = items->generator;
	pItems[6]  = items->sidekick_level;
	pItems[7]  = items->sidekick_series;
	pItems[8]  = initial_episode_num;
	pItems[9]  = items->shield;
	pItems[10] = items->special;
	pItems[11] = items->ship;
}

static void pitems_to_playeritems(PlayerItems *items, JE_PItemsType pItems, JE_byte *initial_episode_num)
{
	items->weapon[FRONT_WEAPON].id  = pItems[0];
	items->weapon[REAR_WEAPON].id   = pItems[1];
	items->super_arcade_mode        = pItems[2];
	items->sidekick[LEFT_SIDEKICK]  = pItems[3];
	items->sidekick[RIGHT_SIDEKICK] = pItems[4];
	items->generator                = pItems[5];
	items->sidekick_level           = pItems[6];
	items->sidekick_series          = pItems[7];
	if (initial_episode_num != NULL)
		*initial_episode_num        = pItems[8];
	items->shield                   = pItems[9];
	items->special                  = pItems[10];
	items->ship                     = pItems[11];
}

void JE_saveGame(JE_byte slot, const char *name)
{
	assert(strlen(name) >= 14);

	saveFiles[slot-1].initialDifficulty = initialDifficulty;
	saveFiles[slot-1].gameHasRepeated = gameHasRepeated;
	saveFiles[slot-1].level = saveLevel;
	
	if (superTyrian)
		player[0].items.super_arcade_mode = SA_SUPERTYRIAN;
	else if (superArcadeMode == SA_NONE && onePlayerAction)
		player[0].items.super_arcade_mode = SA_ARCADE;
	else
		player[0].items.super_arcade_mode = superArcadeMode;
	
	playeritems_to_pitems(saveFiles[slot-1].items, &player[0].items, initial_episode_num);
	
	if (twoPlayerMode)
		playeritems_to_pitems(saveFiles[slot-1].lastItems, &player[1].items, 0);
	else
		playeritems_to_pitems(saveFiles[slot-1].lastItems, &player[0].last_items, 0);
	
	saveFiles[slot-1].score  = player[0].cash;
	saveFiles[slot-1].score2 = player[1].cash;
	
	memcpy(&saveFiles[slot-1].levelName, &lastLevelName, sizeof(lastLevelName));
	saveFiles[slot-1].cubes  = lastCubeMax;

	if (strcmp(lastLevelName, "Completed") == 0)
	{
		temp = episodeNum - 1;
		if (temp < 1)
		{
			temp = EPISODE_AVAILABLE; /* JE: {Episodemax is 4 for completion purposes} */
		}
		saveFiles[slot-1].episode = temp;
	}
	else
	{
		saveFiles[slot-1].episode = episodeNum;
	}

	saveFiles[slot-1].difficulty = difficultyLevel;
	saveFiles[slot-1].secretHint = secretHint;
	saveFiles[slot-1].input1 = inputDevice[0];
	saveFiles[slot-1].input2 = inputDevice[1];

	memcpy(saveFiles[slot-1].name, name, 14);
	saveFiles[slot-1].name[14] = '\0';
	
	for (uint port = 0; port < 2; ++port)
	{
		// if two-player, use first player's front and second player's rear weapon
		saveFiles[slot-1].power[port] = player[twoPlayerMode ? port : 0].items.weapon[port].power;
	}
	
	saveSaves();
}

void JE_loadGame(JE_byte slot)
{
	superTyrian = false;
	onePlayerAction = false;
	twoPlayerMode = false;
	extraGame = false;
	galagaMode = false;

	initialDifficulty = saveFiles[slot-1].initialDifficulty;
	gameHasRepeated   = saveFiles[slot-1].gameHasRepeated;
	twoPlayerMode     = (slot-1) > 10;
	difficultyLevel   = saveFiles[slot-1].difficulty;
	
	pitems_to_playeritems(&player[0].items, saveFiles[slot-1].items, &initial_episode_num);
	
	superArcadeMode = player[0].items.super_arcade_mode;
	
	if (superArcadeMode == SA_SUPERTYRIAN)
		superTyrian = true;
	if (superArcadeMode != SA_NONE)
		onePlayerAction = true;
	if (superArcadeMode > gameRules()->arcade->ship_count)
		superArcadeMode = SA_NONE;
	timedBattleMode = false;
	
	if (twoPlayerMode)
	{
		onePlayerAction = false;
		
		pitems_to_playeritems(&player[1].items, saveFiles[slot-1].lastItems, NULL);
	}
	else
	{
		pitems_to_playeritems(&player[0].last_items, saveFiles[slot-1].lastItems, NULL);
	}

	/* Compatibility with old version */
	if (player[1].items.sidekick_level < 101)
	{
		player[1].items.sidekick_level = 101;
		player[1].items.sidekick_series = player[1].items.sidekick[LEFT_SIDEKICK];
	}
	
	player[0].cash = saveFiles[slot-1].score;
	player[1].cash = saveFiles[slot-1].score2;
	
	mainLevel   = saveFiles[slot-1].level;
	cubeMax     = saveFiles[slot-1].cubes;
	lastCubeMax = cubeMax;

	secretHint = saveFiles[slot-1].secretHint;
	inputDevice[0] = saveFiles[slot-1].input1;
	inputDevice[1] = saveFiles[slot-1].input2;

	for (uint port = 0; port < 2; ++port)
	{
		// if two-player, use first player's front and second player's rear weapon
		player[twoPlayerMode ? port : 0].items.weapon[port].power = saveFiles[slot-1].power[port];
	}
	
	int episode = saveFiles[slot-1].episode;

	memcpy(&levelName, &saveFiles[slot-1].levelName, sizeof(levelName));

	if (strcmp(levelName, "Completed") == 0)
	{
		if (episode == EPISODE_AVAILABLE)
			episode = 1;
		else if (episode < EPISODE_AVAILABLE)
			episode++;
		/* Increment episode.  Episode EPISODE_AVAILABLE goes to 1. */
	}

	JE_initEpisode(episode);
	saveLevel = mainLevel;
	memcpy(&lastLevelName, &levelName, sizeof(levelName));
}

void JE_initProcessorType(void)
{
	/* SYN: Originally this proc looked at your hardware specs and chose appropriate options. We don't care, so I'll just set
	   decent defaults here. */

	wild = false;
	superWild = false;
	smoothScroll = true;
	explosionTransparent = true;
	filtrationAvail = false;
	background2 = true;
	displayScore = true;

	switch (processorType)
	{
		case 1: /* 386 */
			background2 = false;
			displayScore = false;
			explosionTransparent = false;
			break;
		case 2: /* 486 - Default */
			break;
		case 3: /* High Detail */
			smoothScroll = false;
			break;
		case 4: /* Pentium */
			wild = true;
			filtrationAvail = true;
			break;
		case 5: /* Nonstandard VGA */
			smoothScroll = false;
			break;
		case 6: /* SuperWild */
			wild = true;
			superWild = true;
			filtrationAvail = true;
			break;
	}

	switch (gameSpeed)
	{
		case 1:  /* Slug Mode */
			fastPlay = 3;
			break;
		case 2:  /* Slower */
			fastPlay = 4;
			break;
		case 3: /* Slow */
			fastPlay = 5;
			break;
		case 4: /* Normal */
			fastPlay = 0;
			break;
		case 5: /* Pentium Hyper */
			fastPlay = 1;
			break;
	}

}

void JE_applyProcessorType(void)
{
	// Modern always renders at Pentium or above: the player's stored choice is
	// ignored there except for the SuperWild cheat (6), which must keep working.
	// The choice itself is preserved, so Classic still restores it and
	// tyrian.cfg still stores it.
	if (presentation == PRESENTATION_MODERN)
		processorType = (processorTypeChoice == 6) ? 6 : 4;
	else
		processorType = processorTypeChoice;

	JE_initProcessorType();
}

void JE_setNewGameSpeed(void)
{
	pentiumMode = false;

	Uint16 speed;
	switch (fastPlay)
	{
	default:
		assert(false);
		// fall through
	case 0:  // Normal
		speed = 0x4300;
		smoothScroll = true;
		frameCountMax = 2;
		break;
	case 1:  // Pentium Hyper
		speed = 0x3000;
		smoothScroll = true;
		frameCountMax = 2;
		break;
	case 2:
		speed = 0x2000;
		smoothScroll = false;
		frameCountMax = 2;
		break;
	case 3:  // Slug mode
		speed = 0x5300;
		smoothScroll = true;
		frameCountMax = 4;
		break;
	case 4:  // Slower
		speed = 0x4300;
		smoothScroll = true;
		frameCountMax = 3;
		break;
	case 5:  // Slow
		speed = 0x4300;
		smoothScroll = true;
		frameCountMax = 2;
		pentiumMode = true;
		break;
	}

	setFrameSpeed(speed);
	setFrameCount(frameCountMax);
}

static const char *const tyrianConfigFilename = "tyrian.cfg";

void loadConfiguration(void)
{
	bool invalid = false;

	File file = userFileOpen(tyrianConfigFilename, "rb");
	if (file.error)
	{
		logWarn("Failed to open '%s': %s", tyrianConfigFilename, fileGetError(&file));

		invalid = true;
	}
	else
	{
		Uint8 data[28];
		fileReadExactly(&file, data, sizeof data);

		invalid |= fileGetLength(&file) != sizeof data;

		invalid |= file.error;

		if (file.error)
			logError("Failed to read from '%s': %s", tyrianConfigFilename, fileGetError(&file));

		fileClose(&file);

		MemReader reader = { data, sizeof data, false };

		background2     = memReadBool(&reader);
		gameSpeed       = memReadU8(&reader);
		inputDevice_    = memReadU8(&reader);
		jConfigure      = memReadU8(&reader);
		versionNum      = memReadU8(&reader);
		processorTypeChoice = memReadU8(&reader);
		midiPort        = memReadU8(&reader);
		soundEffects    = memReadU8(&reader);
		gammaCorrection = memReadU8(&reader);
		difficultyLevel = memReadS8(&reader);
		memReadU8Array(&reader, joyButtonAssign, COUNTOF(joyButtonAssign));
		tyrMusicVolume  = memReadU16(&reader);
		fxVolume        = memReadU16(&reader);
		inputDevice1    = memReadU8(&reader);
		inputDevice2    = memReadU8(&reader);
		memReadU8Array(&reader, dosKeySettings, COUNTOF(dosKeySettings));

		assert(reader.size == 0 || reader.error);
		invalid |= reader.error;

		inputDevice_ = 0;
		if (jConfigure == 0)
			jConfigure = 1;
		// Game resets version number; ShipEdit doesn't.
		versionNum = 2;

		if (tyrMusicVolume > 255)
			tyrMusicVolume = 255;
		if (fxVolume > 255)
			fxVolume = 255;
	}

	if (invalid)
	{
		logWarn("'%s' is invalid or missing.", tyrianConfigFilename);
		
		background2 = true;
		gameSpeed = 4;
		inputDevice_ = 0;
		jConfigure = 0;
		versionNum = 2;
		processorTypeChoice = 4;  // fresh installs default to Pentium
		midiPort = 1;
		soundEffects = 1;
		gammaCorrection = 0;
		difficultyLevel = 0;
		memcpy(&joyButtonAssign, &defaultJoyButtonAssign, sizeof(joyButtonAssign));
		tyrMusicVolume = 223;
		fxVolume = 223;
		inputDevice1 = 0;
		inputDevice2 = 0;
		memcpy(&dosKeySettings, &defaultDosKeySettings, sizeof(dosKeySettings));
	}
	
	loadOpenTyrianConfig();

	set_volume(tyrMusicVolume, fxVolume);

	JE_applyProcessorType();
}

void saveConfiguration(void)
{
	if (!userFilesEnabled())
		return;

	Uint8 data[28];

	MemWriter writer = { data, sizeof data, false };

	memWriteBool(&writer,    background2);
	memWriteU8(&writer,      gameSpeed);
	memWriteU8(&writer,      inputDevice_);
	memWriteU8(&writer,      jConfigure);
	memWriteU8(&writer,      versionNum);
	memWriteU8(&writer,      processorTypeChoice);
	memWriteU8(&writer,      midiPort);
	memWriteU8(&writer,      soundEffects);
	memWriteU8(&writer,      gammaCorrection);
	memWriteS8(&writer,      difficultyLevel);
	memWriteU8Array(&writer, joyButtonAssign, COUNTOF(joyButtonAssign));
	memWriteU16(&writer,     tyrMusicVolume);
	memWriteU16(&writer,     fxVolume);
	memWriteU8(&writer,      inputDevice1);
	memWriteU8(&writer,      inputDevice2);
	memWriteU8Array(&writer, dosKeySettings, COUNTOF(dosKeySettings));

	assert(writer.size == 0 && !writer.error);

	File file = userFileOpen(tyrianConfigFilename, "wb");
	if (file.error)
	{
		logError("Failed to open '%s': %s", tyrianConfigFilename, fileGetError(&file));
	}
	else
	{
		fileWrite(&file, data, sizeof data);
		fileFlush(&file);

		if (file.error)
			logError("Failed to write to '%s': %s", tyrianConfigFilename, fileGetError(&file));

		fileClose(&file);
	}

	saveOpenTyrianConfig();
}

static const char *const tyrianSaveFilename = "tyrian.sav";

static bool decryptSaveData(Uint8 *data);

// Bytes of the unencrypted score boards after the encrypted prefix: none in
// Tyrian 2.1, 2,220 in Tyrian 2000.
static size_t saveSuffixSize(void)
{
	const GameDataSchema *schema = gameSchema();
	return (size_t)schema->save_suffix_boards_per *
	       ((size_t)schema->save_suffix_timed_boards * VARIANT_SCORE_TIMED_SIZE +
	        (size_t)schema->save_suffix_main_boards * VARIANT_SCORE_MAIN_SIZE);
}

// Parses the score boards of the save suffix.  Names longer than the field are
// clamped, and a difficulty that no board can hold makes the suffix invalid.
static bool readSaveSuffix(MemReader *reader)
{
	const GameDataSchema *schema = gameSchema();
	bool valid = true;

	for (size_t board = 0; board < (size_t)schema->save_suffix_timed_boards + schema->save_suffix_main_boards; ++board)
	{
		const bool timed = board < schema->save_suffix_timed_boards;

		for (size_t entry = 0; entry < schema->save_suffix_boards_per; ++entry)
		{
			VariantHighScore *score = &variantHighScores[board][entry];

			score->score = memReadS32(reader);
			score->unknown = timed ? 0 : memReadU32(reader);
			Uint8 nameLen = memReadU8(reader);
			memset(score->playerName, 0, sizeof score->playerName);
			memReadCharArray(reader, score->playerName, VARIANT_SCORE_NAME_MAX);
			if (nameLen > VARIANT_SCORE_NAME_MAX)
			{
				logWarn("Score board %u entry %u: name length %u is out of range; clamped.",
				        (unsigned)board, (unsigned)entry, (unsigned)nameLen);
				nameLen = VARIANT_SCORE_NAME_MAX;
			}
			score->playerName[nameLen] = '\0';
			score->difficulty = memReadU8(reader);
			if (score->difficulty > DIFFICULTY_10)
				valid = false;
		}
	}

	return valid && !reader->error && reader->size == 0;
}

static void writeSaveSuffix(MemWriter *writer)
{
	const GameDataSchema *schema = gameSchema();

	for (size_t board = 0; board < (size_t)schema->save_suffix_timed_boards + schema->save_suffix_main_boards; ++board)
	{
		const bool timed = board < schema->save_suffix_timed_boards;

		for (size_t entry = 0; entry < schema->save_suffix_boards_per; ++entry)
		{
			const VariantHighScore *score = &variantHighScores[board][entry];

			memWriteS32(writer, score->score);
			if (!timed)
				memWriteU32(writer, score->unknown);
			memWriteU8(writer, strlen(score->playerName));
			memWriteCharArray(writer, score->playerName, VARIANT_SCORE_NAME_MAX);
			memWriteU8(writer, score->difficulty);
		}
	}
}

void loadSaves(void)
{
	bool invalid = false;

	const size_t prefixSize = SAVE_FILE_SIZE + 4;
	const size_t suffixSize = saveSuffixSize();

	File file = userFileOpenKind(USER_FILE_VARIANT_SAVE, tyrianSaveFilename, "rb");
	if (file.error)
	{
		logWarn("Failed to open '%s': %s", tyrianSaveFilename, fileGetError(&file));

		invalid = true;
	}
	else
	{
		Uint8 data[SAVE_FILE_SIZE + 4 + VARIANT_SCORE_SUFFIX_MAX];

		// A variant with a score suffix requires the exact total length; the
		// suffix is not covered by the prefix's integrity bytes.
		if (suffixSize != 0 && (size_t)fileGetLength(&file) != prefixSize + suffixSize)
		{
			logError("'%s' must be %u bytes.", tyrianSaveFilename, (unsigned)(prefixSize + suffixSize));
			invalid = true;
		}
		else
		{
			fileReadExactly(&file, data, prefixSize + suffixSize);

			invalid |= file.error;

			if (file.error)
				logError("Failed to read from '%s': %s", tyrianSaveFilename, fileGetError(&file));
		}

		fileClose(&file);

		invalid |= !invalid && !decryptSaveData(data);

		MemReader reader = { data, prefixSize, false };

		if (!invalid)
		for (size_t i = 0; i < COUNTOF(saveFiles); ++i)
		{
			saveFiles[i].encode            = memReadU16(&reader);
			saveFiles[i].level             = memReadU16(&reader);
			memReadU8Array(&reader, saveFiles[i].items, COUNTOF(saveFiles[i].items));
			saveFiles[i].score             = memReadU32(&reader);
			saveFiles[i].score2            = memReadU32(&reader);
			Uint8 levelNameLen             = memReadU8(&reader);
			memReadCharArray(&reader, saveFiles[i].levelName, 9);
			saveFiles[i].levelName[MIN(levelNameLen, 9)] = '\0';
			memReadCharArray(&reader, saveFiles[i].name, 14);
			saveFiles[i].name[14] = '\0';
			saveFiles[i].cubes             = memReadU8(&reader);
			memReadU8Array(&reader, saveFiles[i].power, COUNTOF(saveFiles[i].power));
			saveFiles[i].episode           = memReadU8(&reader);
			memReadU8Array(&reader, saveFiles[i].lastItems, COUNTOF(saveFiles[i].lastItems));
			saveFiles[i].difficulty        = memReadU8(&reader);
			saveFiles[i].secretHint        = memReadU8(&reader);
			saveFiles[i].input1            = memReadU8(&reader);
			saveFiles[i].input2            = memReadU8(&reader);
			saveFiles[i].gameHasRepeated   = memReadBool(&reader);
			saveFiles[i].initialDifficulty = memReadU8(&reader);
			saveFiles[i].highScore1        = memReadS32(&reader);
			saveFiles[i].highScore2        = memReadS32(&reader);
			Uint8 highScoreNameLen         = memReadU8(&reader);
			memReadCharArray(&reader, saveFiles[i].highScoreName, 29);
			saveFiles[i].highScoreName[MIN(highScoreNameLen, 29)] = '\0';
			saveFiles[i].highScoreDiff     = memReadU8(&reader);
		}

		if (!invalid)
		{
			memReadU8Array(&reader, editorItemAvail, COUNTOF(editorItemAvail));

			editorLevel = ((Uint16)editorItemAvail[98] << 8) | editorItemAvail[99];

			assert(reader.size == 4 || reader.error);
			invalid |= reader.error;
		}

		if (!invalid && suffixSize != 0)
		{
			MemReader suffixReader = { data + prefixSize, suffixSize, false };
			invalid |= !readSaveSuffix(&suffixReader);
		}
	}

	if (invalid)
	{
		logWarn("'%s' is invalid or missing.", tyrianSaveFilename);

		// The default high-score names of some variants are data: they must be
		// loaded before any default score is generated.
		const GameStringSchema *strings = gameStrings();
		if (strings->default_names_from_data)
			helpTextEnsureLoaded();

		// (The casts only add const to the row type of the mutable HDT arrays.)
		const char (*highScoreNames)[23] = strings->default_names_from_data
			? (const char (*)[23])hdtHighScoreNames : defaultHighScoreNames;
		const size_t highScoreNameCount = strings->default_names_from_data
			? strings->high_score_names : COUNTOF(defaultHighScoreNames);
		const char (*teamNames)[25] = strings->default_names_from_data
			? (const char (*)[25])hdtTeamNames : defaultTeamNames;
		const size_t teamNameCount = strings->default_names_from_data
			? strings->team_names : COUNTOF(defaultTeamNames);

		memset(saveFiles, 0, sizeof(saveFiles));

		for (size_t i = 0; i < SAVE_FILES_NUM; ++i)
		{
			saveFiles[i].level = 0;

			for (size_t j = 0; j < 14; ++j)
				saveFiles[i].name[j] = ' ';
			saveFiles[i].name[14] = '\0';

			saveFiles[i].highScore1 = ((mt_rand() % 20) + 1) * 1000;
			if (i % 6 < 3)
			{
				saveFiles[i].highScore2 = 0;
				strcpy(saveFiles[i].highScoreName, highScoreNames[mt_rand() % highScoreNameCount]);
			}
			else
			{
				saveFiles[i].highScore2 = ((mt_rand() % 20) + 1) * 1000;
				strcpy(saveFiles[i].highScoreName, teamNames[mt_rand() % teamNameCount]);
			}
			saveFiles[i].highScoreDiff = 0;
		}

		// Score boards of the save suffix (variants that have one).  Every field
		// not drawn here, including the difficulty, starts at zero: no RNG draw
		// is added for them.
		const GameDataSchema *schema = gameSchema();
		memset(variantHighScores, 0, sizeof variantHighScores);
		for (size_t board = 0; board < (size_t)schema->save_suffix_timed_boards + schema->save_suffix_main_boards; ++board)
		{
			const bool timed = board < schema->save_suffix_timed_boards;

			for (size_t entry = 0; entry < schema->save_suffix_boards_per; ++entry)
			{
				VariantHighScore *score = &variantHighScores[board][entry];

				if (timed)
				{
					score->score = ((mt_rand() % 50) + 1) * 100;
					strcpy(score->playerName, highScoreNames[mt_rand() % highScoreNameCount]);
				}
				else
				{
					score->score = ((mt_rand() % 20) + 1) * 1000;
					score->unknown = VARIANT_SCORE_UNKNOWN_DEFAULT;
					if (board & 1)
						strcpy(score->playerName, teamNames[mt_rand() % teamNameCount]);
					else
						strcpy(score->playerName, highScoreNames[mt_rand() % highScoreNameCount]);
				}
			}
		}

		memcpy(editorItemAvail, initialEditorItemAvail, sizeof(editorItemAvail));

		editorLevel = 800;
	}
}

static void encryptSaveData(Uint8 *data);

void saveSaves(void)
{
	if (!userSavesWritable())
		return;

	const size_t suffixSize = saveSuffixSize();
	Uint8 data[SAVE_FILE_SIZE + 4 + VARIANT_SCORE_SUFFIX_MAX];

	MemWriter writer = { data, SAVE_FILE_SIZE + 4, false };

	for (size_t i = 0; i < COUNTOF(saveFiles); ++i)
	{
		memWriteU16(&writer,       saveFiles[i].encode);
		memWriteU16(&writer,       saveFiles[i].level);
		memWriteU8Array(&writer,   saveFiles[i].items, COUNTOF(saveFiles[i].items));
		memWriteU32(&writer,       saveFiles[i].score);
		memWriteU32(&writer,       saveFiles[i].score2);
		memWriteU8(&writer,        strlen(saveFiles[i].levelName));
		memWriteCharArray(&writer, saveFiles[i].levelName, 9);
		memWriteCharArray(&writer, saveFiles[i].name, 14);
		memWriteU8(&writer,        saveFiles[i].cubes);
		memWriteU8Array(&writer,   saveFiles[i].power, COUNTOF(saveFiles[i].power));
		memWriteU8(&writer,        saveFiles[i].episode);
		memWriteU8Array(&writer,   saveFiles[i].lastItems, COUNTOF(saveFiles[i].lastItems));
		memWriteU8(&writer,        saveFiles[i].difficulty);
		memWriteU8(&writer,        saveFiles[i].secretHint);
		memWriteU8(&writer,        saveFiles[i].input1);
		memWriteU8(&writer,        saveFiles[i].input2);
		memWriteBool(&writer,      saveFiles[i].gameHasRepeated);
		memWriteU8(&writer,        saveFiles[i].initialDifficulty);
		memWriteS32(&writer,       saveFiles[i].highScore1);
		memWriteS32(&writer,       saveFiles[i].highScore2);
		memWriteU8(&writer,        strlen(saveFiles[i].highScoreName));
		memWriteCharArray(&writer, saveFiles[i].highScoreName, 29);
		memWriteU8(&writer,        saveFiles[i].highScoreDiff);
	}

	editorItemAvail[98] = editorLevel >> 8;
	editorItemAvail[99] = editorLevel;

	memWriteU8Array(&writer, editorItemAvail, COUNTOF(editorItemAvail));

	assert(writer.size == 4 && !writer.error);

	encryptSaveData(data);

	// The score boards follow the encrypted prefix unencrypted.
	if (suffixSize != 0)
	{
		MemWriter suffixWriter = { data + SAVE_FILE_SIZE + 4, suffixSize, false };
		writeSaveSuffix(&suffixWriter);
		assert(suffixWriter.size == 0 && !suffixWriter.error);
	}

	File file = userFileOpenKind(USER_FILE_VARIANT_SAVE, tyrianSaveFilename, "wb");
	if (file.error)
	{
		logError("Failed to open '%s': %s", tyrianSaveFilename, fileGetError(&file));
	}
	else
	{
		fileWrite(&file, data, SAVE_FILE_SIZE + 4 + suffixSize);
		fileFlush(&file);

		if (file.error)
			logError("Failed to write to '%s': %s", tyrianSaveFilename, fileGetError(&file));

		fileClose(&file);
	}
}

void encryptSaveData(Uint8 *data)
{
	Uint8 y;

	y = 0;
	for (size_t i = 0; i < SAVE_FILE_SIZE; ++i)
		y += data[i];
	data[SAVE_FILE_SIZE] = y;

	y = 0;
	for (size_t i = 0; i < SAVE_FILE_SIZE; ++i)
		y -= data[i];
	data[SAVE_FILE_SIZE + 1] = y;

	y = 1;
	for (size_t i = 0; i < SAVE_FILE_SIZE; ++i)
		y = y * data[i] + 1;
	data[SAVE_FILE_SIZE + 2] = y;

	y = 0;
	for (size_t i = 0; i < SAVE_FILE_SIZE; ++i)
		y ^= data[i];
	data[SAVE_FILE_SIZE + 3] = y;

	for (size_t i = 0; i < SAVE_FILE_SIZE; ++i)
	{
		data[i] ^= cryptKey[(i + 1) % 10];
		if (i > 0)
			data[i] ^= data[i - 1];
	}
}

bool decryptSaveData(Uint8 *data)
{
	for (size_t i = SAVE_FILE_SIZE - 1; ; --i)
	{
		data[i] ^= cryptKey[(i + 1) % 10];
		if (i > 0)
			data[i] ^= data[i - 1];
		else
			break;
	}

	Uint8 y;

	y = 0;
	for (size_t i = 0; i < SAVE_FILE_SIZE; ++i)
		y += data[i];
	if (data[SAVE_FILE_SIZE] != y)
		return false;

	y = 0;
	for (size_t i = 0; i < SAVE_FILE_SIZE; ++i)
		y -= data[i];
	if (data[SAVE_FILE_SIZE + 1] != y)
		return false;

	y = 1;
	for (size_t i = 0; i < SAVE_FILE_SIZE; ++i)
		y = y * data[i] + 1;
	if (data[SAVE_FILE_SIZE + 2] != y)
		return false;

	y = 0;
	for (size_t i = 0; i < SAVE_FILE_SIZE; ++i)
		y ^= data[i];
	if (data[SAVE_FILE_SIZE + 3] != y)
		return false;

	return true;
}
