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
#include "opentyr.h"

#include "bootstrap.h"
#include "config.h"
#include "crt_filter.h"
#include "demo.h"
#include "destruct.h"
#include "editship.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "game_data.h"
#include "gamepad_selftest.h"
#include "helptext.h"
#include "input_probe.h"
#include "installer.h"
#include "interp.h"
#include "joystick.h"
#include "jukebox.h"
#include "keyboard.h"
#include "launcher.h"
#include "logging.h"
#include "loudness.h"
#include "mainint.h"
#include "modern.h"
#include "modern_bloom.h"
#include "modern_depth.h"
#include "mouse.h"
#include "mtrand.h"
#include "network.h"
#include "nortsong.h"
#include "nortvars.h"
#include "opentyrian_version.h"
#include "palette.h"
#include "params.h"
#include "picload.h"
#include "regress.h"
#include "regress_flow.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"
#include "vfx.h"
#include "vga256d.h"
#include "video.h"
#include "xmas.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *opentyrian_str = "OpenTyrian";
const char *opentyrian_version = OPENTYRIAN_VERSION;

static size_t getDisplayPickerItemsCount(void)
{
	int display_count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&display_count);
	SDL_free(displays);

	return 1 + (size_t)display_count;
}

static const char *getDisplayPickerItem(size_t i, char *buffer, size_t bufferSize)
{
	if (i == 0)
		return "Window";

	snprintf(buffer, bufferSize, "Display %d", (int)i);
	return buffer;
}

static size_t getScalingModePickerItemsCount(void)
{
	return (size_t)ScalingMode_MAX;
}

static const char *getScalingModePickerItem(size_t i, char *buffer, size_t bufferSize)
{
	(void)buffer, (void)bufferSize;

	return scaling_mode_names[i];
}

static size_t getPresentationPickerItemsCount(void)
{
	return (size_t)PRESENTATION_MAX;
}

// The presentation/pixel-aspect names are canonical lowercase (cfg and CLI);
// the menu shows them capitalised like the other values ("Window", "None",
// "Integer", "On").  The aspect names keep their spelling ("auto" stays
// lowercase).
static const char *capitalized_name(const char *name, char *buffer, size_t bufferSize)
{
	size_t i = 0;
	for (; name[i] != '\0' && i + 1 < bufferSize; ++i)
		buffer[i] = i == 0 ? (char)toupper((unsigned char)name[i]) : name[i];
	buffer[i] = '\0';
	return buffer;
}

static const char *getPresentationPickerItem(size_t i, char *buffer, size_t bufferSize)
{
	return capitalized_name(presentation_names[i], buffer, bufferSize);
}

static size_t getAspectPickerItemsCount(void)
{
	return (size_t)MODERN_ASPECT_MAX;
}

static const char *getAspectPickerItem(size_t i, char *buffer, size_t bufferSize)
{
	(void)buffer, (void)bufferSize;

	return modern_aspect_names[i];
}

static size_t getPixelAspectPickerItemsCount(void)
{
	return (size_t)PIXEL_ASPECT_MAX;
}

static const char *getPixelAspectPickerItem(size_t i, char *buffer, size_t bufferSize)
{
	return capitalized_name(modern_pixel_aspect_names[i], buffer, bufferSize);
}

static size_t getSmoothMotionPickerItemsCount(void)
{
	return 2;
}

static const char *getSmoothMotionPickerItem(size_t i, char *buffer, size_t bufferSize)
{
	(void)buffer, (void)bufferSize;

	return i == 0 ? "On" : "Off";
}

static void drawCrtValue(SDL_Surface *surface, int x, int y, const char *text, Sint8 value)
{
	const Uint8 hue = 15;
	const int shadowDist = 2;
	drawFontHvShadow(surface, x, y, text, FONT_NORMAL, hue, value, false, shadowDist);
	const char *plus = strchr(text, '+');
	if (plus != NULL)
	{
		// The normal font's '+' is a blank 2x2 placeholder. Draw a 3x3 cross
		// inside its measured 3px advance; surrounding spaces remain intact.
		char prefix[32];
		size_t length = (size_t)(plus - text);
		assert(length < sizeof prefix);
		memcpy(prefix, text, length);
		prefix[length] = '\0';
		const int px = x + JE_textWidth(prefix, FONT_NORMAL);
		const int py = y + 3;
		static const int crossX[] = { 1, 0, 1, 2, 1 };
		static const int crossY[] = { 0, 1, 1, 1, 2 };

		// Reuse blit_sprite_hv/blit_sprite_dark's palette transforms (sprite.h),
		// as drawFontHvShadow does through drawFontHv/drawFontDark. The cross
		// uses a full-brightness source stroke and a destination-darkening shadow.
		const Uint8 color = sprite_hv_color(0x0f, hue, value);
		for (size_t i = 0; i < COUNTOF(crossX); ++i)
		{
			Uint8 *shadow = (Uint8 *)surface->pixels +
			                (py + crossY[i] + shadowDist) * surface->pitch + px + crossX[i] + shadowDist;
			*shadow = sprite_dark_color(*shadow, false);
		}
		for (size_t i = 0; i < COUNTOF(crossX); ++i)
		{
			Uint8 *pixel = (Uint8 *)surface->pixels +
			               (py + crossY[i]) * surface->pitch + px + crossX[i];
			*pixel = color;
		}
	}
}

static size_t getCrtPickerItemsCount(void)
{
	return CRT_FILTER_MODE_COUNT;
}

static const char *getCrtPickerItem(size_t i, char *buffer, size_t bufferSize)
{
	(void)buffer;
	(void)bufferSize;
	static const char *const labels[] = { "Off", "Scanlines", "NTSC", "Scanlines + NTSC" };
	// Measure with the loaded menu font, including its character spacing.
	return i == CRT_FILTER_BOTH && JE_textWidth(labels[i], FONT_NORMAL) > 95
	       ? "Scanl + NTSC" : labels[i];
}

static size_t getLightingPickerItemsCount(void)
{
	return (size_t)MODERN_QUALITY_MAX;
}

static const char *getLightingPickerItem(size_t i, char *buffer, size_t bufferSize)
{
	return capitalized_name(modern_quality_names[i], buffer, bufferSize);
}

static size_t getDepthPickerItemsCount(void)
{
	return (size_t)MODERN_DEPTH_MAX;
}

static const char *getDepthPickerItem(size_t i, char *buffer, size_t bufferSize)
{
	return capitalized_name(modern_depth_names[i], buffer, bufferSize);
}

static size_t getVfxPickerItemsCount(void)
{
	return (size_t)VFX_LEVEL_MAX;
}

static const char *getVfxPickerItem(size_t i, char *buffer, size_t bufferSize)
{
	return capitalized_name(vfx_level_names[i], buffer, bufferSize);
}

// Set by setupMenuStartAt() so the regress harness can open a submenu directly.
static int setup_menu_start = -1;

void setupMenuStartAt(int menu)
{
	setup_menu_start = menu;
}

void setupMenu(void)
{
	typedef enum
	{
		MENU_ITEM_NONE = 0,
		MENU_ITEM_DONE,
		MENU_ITEM_GRAPHICS,
		MENU_ITEM_SOUND,
		MENU_ITEM_JUKEBOX,
		MENU_ITEM_DESTRUCT,
		MENU_ITEM_DISPLAY,
		MENU_ITEM_SCALING_MODE,
		MENU_ITEM_PRESENTATION,
		MENU_ITEM_ASPECT,
		MENU_ITEM_PIXEL_ASPECT,
		MENU_ITEM_SMOOTH_MOTION,
		MENU_ITEM_CRT,
		MENU_ITEM_LIGHTING,
		MENU_ITEM_DEPTH,
		MENU_ITEM_VFX,
		MENU_ITEM_MUSIC_VOLUME,
		MENU_ITEM_SOUND_VOLUME,
	} MenuItemId;

	typedef enum
	{
		MENU_NONE = SETUP_MENU_NONE,
		MENU_SETUP = SETUP_MENU_SETUP,
		MENU_GRAPHICS = SETUP_MENU_GRAPHICS,
		MENU_SOUND = SETUP_MENU_SOUND,
	} MenuId;

	typedef struct
	{
		MenuItemId id;
		const char *name;
		const char *description;
		size_t (*getPickerItemsCount)(void);
		const char *(*getPickerItem)(size_t i, char *buffer, size_t bufferSize);
		bool requiresModern;  // greyed out and ignored in Classic
	} MenuItem;

	typedef struct
	{
		const char *header;
		const MenuItem items[12];
	} Menu;

	static const Menu menus[] = {
		[MENU_SETUP] = {
			.header = "Setup",
			.items = {
				{ MENU_ITEM_GRAPHICS, "Graphics...", "Change the graphics settings." },
				{ MENU_ITEM_SOUND, "Sound...", "Change the sound settings." },
				{ MENU_ITEM_JUKEBOX, "Jukebox", "Listen to the music of Tyrian." },
				// { MENU_ITEM_DESTRUCT, "Destruct", "Play a bonus mini-game." },
				{ MENU_ITEM_DONE, "Done", "Return to the main menu." },
				{ -1 }
			},
		},
		[MENU_GRAPHICS] = {
			.header = "Graphics",
			.items = {
				{ MENU_ITEM_DISPLAY, "Display:", "Change the display mode.", getDisplayPickerItemsCount, getDisplayPickerItem },
				{ MENU_ITEM_SCALING_MODE, "Scaling Mode:", "Change the scaling mode.", getScalingModePickerItemsCount, getScalingModePickerItem },
				{ MENU_ITEM_PRESENTATION, "Presentation:", "Change the presentation mode.", getPresentationPickerItemsCount, getPresentationPickerItem },
				{ MENU_ITEM_ASPECT, "Aspect:", "Change the Modern aspect ratio.", getAspectPickerItemsCount, getAspectPickerItem, true },
				{ MENU_ITEM_PIXEL_ASPECT, "Pixel Aspect:", "Change the pixel aspect.", getPixelAspectPickerItemsCount, getPixelAspectPickerItem },
				{ MENU_ITEM_SMOOTH_MOTION, "Smooth Motion:", "Present Modern gameplay at the display refresh.", getSmoothMotionPickerItemsCount, getSmoothMotionPickerItem, true },
				{ MENU_ITEM_CRT, "CRT Filter:", "Change the Modern CRT filter.", getCrtPickerItemsCount, getCrtPickerItem, true },
				{ MENU_ITEM_LIGHTING, "Lighting:", "Change the Modern bloom and lighting level.", getLightingPickerItemsCount, getLightingPickerItem, true },
				{ MENU_ITEM_DEPTH, "Depth:", "Change the Modern depth shadows.", getDepthPickerItemsCount, getDepthPickerItem, true },
				{ MENU_ITEM_VFX, "Effects:", "Change the Modern VFX level.", getVfxPickerItemsCount, getVfxPickerItem, true },
				{ MENU_ITEM_DONE, "Done", "Return to the previous menu." },
				{ -1 }
			},
		},
		[MENU_SOUND] = {
			.header = "Sound",
			.items = {
				{ MENU_ITEM_MUSIC_VOLUME, "Music Volume", "Change volume with the left/right arrow keys." },
				{ MENU_ITEM_SOUND_VOLUME, "Sound Volume", "Change volume with the left/right arrow keys." },
				{ MENU_ITEM_DONE, "Done", "Return to the previous menu." },
				{ -1 }
			},
		},
	};

	char buffer[100];

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	bool restart = true;

	MenuId menuParents[COUNTOF(menus)] = { MENU_NONE };
	size_t selectedMenuItemIndexes[COUNTOF(menus)] = { 0 };
	MenuId currentMenu = MENU_SETUP;
	if (setup_menu_start >= 0)
	{
		currentMenu = (MenuId)setup_menu_start;
		setup_menu_start = -1;
	}
	MenuItemId currentPicker = MENU_ITEM_NONE;
	size_t pickerSelectedIndex = 0;
	if (regress_screen != NULL && strcmp(regress_screen, "setup-crt-picker") == 0)
	{
		size_t visibleIndex = 0;
		for (const MenuItem *item = menus[MENU_GRAPHICS].items; item->id != (MenuItemId)-1; ++item)
		{
			const bool classicOnly = item->id == MENU_ITEM_SCALING_MODE || item->id == MENU_ITEM_PIXEL_ASPECT;
			if (classicOnly && presentation == PRESENTATION_MODERN)
				continue;
			if (item->id == MENU_ITEM_CRT)
			{
				selectedMenuItemIndexes[MENU_GRAPHICS] = visibleIndex;
				break;
			}
			++visibleIndex;
		}
		currentPicker = MENU_ITEM_CRT;
		pickerSelectedIndex = (size_t)crt_filter_mode();
	}
	if (regress_screen != NULL && strncmp(regress_screen, "setup", 5) == 0)
		logInfo("CRT menu coverage: full label=%d px, short label=%d px, value column=95 px, chosen=%s",
		        JE_textWidth("Scanlines + NTSC", FONT_NORMAL), JE_textWidth("Scanl + NTSC", FONT_NORMAL),
		        getCrtPickerItem(CRT_FILTER_BOTH, buffer, sizeof buffer));

	const int xCenter = 320 / 2;
	const int yMenuHeader = 4;
	const int xMenuItem = 45;
	const int xMenuItemName = xMenuItem;
	const int wMenuItemName = 135;
	const int xMenuItemValue = xMenuItemName + wMenuItemName;
	const int wMenuItemValue = 95;
	const int wMenuItem = wMenuItemName + wMenuItemValue;
	const int yMenuItems = 37;
	int dyMenuItems = 21;
	const int hMenuItem = 13;
	const int yMenuStatus = 190;
	// Keep at least the shorter menus' inter-row gap (21 - 13) between the last
	// row and the status line, so an eight-row menu (Graphics) does not touch it.
	const int yMenuItemsBottomGap = 8;

	for (; ; )
	{
		setFrameCount(1);

		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);
			fill_rectangle_wh(VGAScreen2, 0, 192, 320, 8, 0);
		}

		// Restore background.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		const Menu *menu = &menus[currentMenu];

		// Draw header.
		drawFontHvShadowAligned(VGAScreen, xCenter, yMenuHeader, menu->header, FONT_LARGE, ALIGN_CENTER, 15, -3, false, 2);

		int yPicker = 0;
		const int dyPickerItem = 15;
		const int dyPickerItemPadding = 2;
		const int hPickerItem = dyPickerItem - dyPickerItemPadding;

		size_t *const selectedMenuItemIndex = &selectedMenuItemIndexes[currentMenu];

		// Build the visible item list for this presentation.  Scaling Mode and
		// Pixel Aspect are Classic-only and are hidden (not greyed) in Modern,
		// where the user cannot pick a worse scaling.
		MenuItem visibleItems[COUNTOF(menu->items)];
		size_t menuItemsCount = 0;
		for (const MenuItem *item = menu->items; item->id != (MenuItemId)-1; ++item)
		{
			const bool classicOnly = item->id == MENU_ITEM_SCALING_MODE || item->id == MENU_ITEM_PIXEL_ASPECT;
			if (classicOnly && presentation == PRESENTATION_MODERN)
				continue;

			visibleItems[menuItemsCount++] = *item;
		}
		const MenuItem *const menuItems = visibleItems;

		// A presentation change can hide the selected row; clamp the selection.
		if (*selectedMenuItemIndex >= menuItemsCount)
			*selectedMenuItemIndex = menuItemsCount - 1;

		if (menuItemsCount > 1)
		{
			const int maxDy = (yMenuStatus - yMenuItemsBottomGap - hMenuItem - yMenuItems) / (int)(menuItemsCount - 1);
			if (dyMenuItems > maxDy)
				dyMenuItems = maxDy;
		}

		// Draw menu items.

		for (size_t i = 0; i < menuItemsCount; ++i)
		{
			const MenuItem *const menuItem = &menuItems[i];

			const int y = yMenuItems + dyMenuItems * i;

			const bool selected = i == *selectedMenuItemIndex;
			const bool disabled = (currentPicker != MENU_ITEM_NONE && !selected) ||
			                      (menuItem->requiresModern && presentation != PRESENTATION_MODERN);

			if (selected)
				yPicker = y;

			const char *const name = menuItem->name;

			drawFontHvShadow(VGAScreen, xMenuItemName, y, name, FONT_NORMAL, 15, -3 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);

			switch (menuItem->id)
			{
			case MENU_ITEM_DISPLAY:;
				const char *value = "Window";
				if (fullscreen_display >= 0)
				{
					snprintf(buffer, sizeof(buffer), "Display %d", fullscreen_display + 1);
					value = buffer;
				}

				drawFontHvShadow(VGAScreen, xMenuItemValue, y, value, FONT_NORMAL, 15, -3 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);
				break;

			case MENU_ITEM_SCALING_MODE:
				drawFontHvShadow(VGAScreen, xMenuItemValue, y, scaling_mode_names[scaling_mode], FONT_NORMAL, 15, -3 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);
				break;

			case MENU_ITEM_PRESENTATION:
				drawFontHvShadow(VGAScreen, xMenuItemValue, y, capitalized_name(presentation_names[presentation], buffer, sizeof buffer), FONT_NORMAL, 15, -3 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);
				break;

			case MENU_ITEM_ASPECT:
				drawFontHvShadow(VGAScreen, xMenuItemValue, y, modern_aspect_names[modern_aspect], FONT_NORMAL, 15, -3 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);
				break;

			case MENU_ITEM_PIXEL_ASPECT:
				drawFontHvShadow(VGAScreen, xMenuItemValue, y, capitalized_name(modern_pixel_aspect_names[modern_pixel_aspect], buffer, sizeof buffer), FONT_NORMAL, 15, -3 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);
				break;

			case MENU_ITEM_SMOOTH_MOTION:
				drawFontHvShadow(VGAScreen, xMenuItemValue, y, interp_smooth_motion ? "On" : "Off", FONT_NORMAL, 15, -3 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);
				break;

			case MENU_ITEM_CRT:
				drawCrtValue(VGAScreen, xMenuItemValue, y, getCrtPickerItem((size_t)crt_filter_mode(), buffer, sizeof buffer), (Sint8)(-3 + (selected ? 2 : 0) + (disabled ? -4 : 0)));
				break;

			case MENU_ITEM_LIGHTING:
				drawFontHvShadow(VGAScreen, xMenuItemValue, y, capitalized_name(modern_quality_names[modern_lighting_quality], buffer, sizeof buffer), FONT_NORMAL, 15, -3 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);
				break;

			case MENU_ITEM_DEPTH:
				drawFontHvShadow(VGAScreen, xMenuItemValue, y, capitalized_name(modern_depth_names[modern_depth_quality], buffer, sizeof buffer), FONT_NORMAL, 15, -3 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);
				break;

			case MENU_ITEM_VFX:
				drawFontHvShadow(VGAScreen, xMenuItemValue, y, capitalized_name(vfx_level_names[vfx_level], buffer, sizeof buffer), FONT_NORMAL, 15, -3 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);
				break;

			case MENU_ITEM_MUSIC_VOLUME:
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, music_disabled ? 170 : 174, (tyrMusicVolume + 4) / 8, 2, 10);
				JE_rectangle(VGAScreen, xMenuItemValue - 2, y - 2, xMenuItemValue + 96, y + 11, 242);
				break;

			case MENU_ITEM_SOUND_VOLUME:
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, samples_disabled ? 170 : 174, (fxVolume + 4) / 8, 2, 10);
				JE_rectangle(VGAScreen, xMenuItemValue - 2, y - 2, xMenuItemValue + 96, y + 11, 242);
				break;

			default:
				break;
			}
		}

		// Draw status text.
		JE_textShade(VGAScreen, xMenuItemName, yMenuStatus, menuItems[*selectedMenuItemIndex].description, 15, 4, PART_SHADE);

		// Draw picker box and items.

		if (currentPicker != MENU_ITEM_NONE)
		{
			const MenuItem *selectedMenuItem = &menuItems[*selectedMenuItemIndex];
			const size_t pickerItemsCount = selectedMenuItem->getPickerItemsCount();

			const int hPicker = dyPickerItem * pickerItemsCount - dyPickerItemPadding;
			yPicker = MIN(yPicker, 200 - 10 - (hPicker + 5 + 2));

			JE_rectangle(VGAScreen, xMenuItemValue - 5, yPicker- 3, xMenuItemValue + wMenuItemValue + 5 - 1, yPicker + hPicker + 3 - 1, 248);
			JE_rectangle(VGAScreen, xMenuItemValue - 4, yPicker- 4, xMenuItemValue + wMenuItemValue + 4 - 1, yPicker + hPicker + 4 - 1, 250);
			JE_rectangle(VGAScreen, xMenuItemValue - 3, yPicker- 5, xMenuItemValue + wMenuItemValue + 3 - 1, yPicker + hPicker + 5 - 1, 248);
			fill_rectangle_wh(VGAScreen, xMenuItemValue - 2, yPicker - 2, wMenuItemValue + 2 + 2, hPicker + 2 + 2, 224);

			for (size_t i = 0; i < pickerItemsCount; ++i)
			{
				const int y = yPicker + dyPickerItem * (int)i;

				const bool selected = i == pickerSelectedIndex;

				const char *value = selectedMenuItem->getPickerItem(i, buffer, sizeof buffer);

				if (currentPicker == MENU_ITEM_CRT)
					drawCrtValue(VGAScreen, xMenuItemValue, y, value, (Sint8)(-3 + (selected ? 2 : 0)));
				else
					drawFontHvShadow(VGAScreen, xMenuItemValue, y, value, FONT_NORMAL, 15, -3 + (selected ? 2 : 0), false, 2);
			}
		}

		if (restart)
		{
			mouseCursor = MOUSE_POINTER_NORMAL;

			fade_palette(colors, 10, 0, 255);

			restart = false;
		}

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		int oldFullscreenDisplay = fullscreen_display;
		while (true)
		{
			waitUntilElapsed();

			// If full-screen is toggled via keyboard shortcut then display
			// setting needs to be updated.
			if (fullscreen_display != oldFullscreenDisplay)
				break;

			if (hasInput(INPUT_ANY))
				break;

			setFrameCount(1);
		}

		if (currentPicker == MENU_ITEM_NONE)
		{
			// Handle menu item interaction.

			bool action = false;

			MouseInput mouseInput;
			KeyboardInput keyboardInput;

			if (mouseGetInput(INPUT_ANY, &mouseInput))
			{
				// Find menu item name or value that was hovered or clicked.
				if (mouseInput.x >= xMenuItem && mouseInput.x < xMenuItem + wMenuItem)
				{
					for (size_t i = 0; i < menuItemsCount; ++i)
					{
						const int yMenuItem = yMenuItems + dyMenuItems * i;
						if (mouseInput.y >= yMenuItem && mouseInput.y < yMenuItem + hMenuItem)
						{
							if (*selectedMenuItemIndex != i)
							{
								JE_playSampleNum(S_CURSOR);

								*selectedMenuItemIndex = i;
							}

							if (mouseInput.button == SDL_BUTTON_LEFT &&
							    mouseInput.y >= yMenuItem && mouseInput.y < yMenuItem + hMenuItem)
							{
								// Act on menu item via name.
								if (mouseInput.x >= xMenuItemName && mouseInput.x < xMenuItemName + wMenuItemName)
								{
									action = true;
								}

								// Act on menu item via value.
								else if (mouseInput.x >= xMenuItemValue && mouseInput.x < xMenuItemValue + wMenuItemValue)
								{
									switch (menuItems[*selectedMenuItemIndex].id)
									{
									case MENU_ITEM_DISPLAY:
									case MENU_ITEM_SCALING_MODE:
									case MENU_ITEM_PRESENTATION:
									case MENU_ITEM_ASPECT:
									case MENU_ITEM_PIXEL_ASPECT:
									case MENU_ITEM_SMOOTH_MOTION:
									case MENU_ITEM_CRT:
									case MENU_ITEM_LIGHTING:
									case MENU_ITEM_VFX:
									{
										action = true;
										break;
									}
									case MENU_ITEM_MUSIC_VOLUME:
									{
										JE_playSampleNum(S_CURSOR);

										int value = (mouseInput.x - xMenuItemValue) * 255 / (wMenuItemValue - 1);
										tyrMusicVolume = MIN(MAX(0, value), 255);

										set_volume(tyrMusicVolume, fxVolume);
										break;
									}
									case MENU_ITEM_SOUND_VOLUME:
									{
										int value = (mouseInput.x - xMenuItemValue) * 255 / (wMenuItemValue - 1);
										fxVolume = MIN(MAX(0, value), 255);

										set_volume(tyrMusicVolume, fxVolume);

										JE_playSampleNum(S_CURSOR);
										break;
									}
									default:
										break;
									}
								}
							}

							break;
						}
					}
				}

				if (mouseInput.button == SDL_BUTTON_RIGHT)
				{
					JE_playSampleNum(S_SPRING);

					currentMenu = menuParents[currentMenu];
				}
			}
			else if (keyboardGetInput(&keyboardInput))
			{
				switch (keyboardInput.scancode)
				{
				case SDL_SCANCODE_UP:
				{
					JE_playSampleNum(S_CURSOR);

					*selectedMenuItemIndex = *selectedMenuItemIndex == 0
						? menuItemsCount - 1
						: *selectedMenuItemIndex - 1;
					break;
				}
				case SDL_SCANCODE_DOWN:
				{
					JE_playSampleNum(S_CURSOR);

					*selectedMenuItemIndex = *selectedMenuItemIndex == menuItemsCount - 1
						? 0
						: *selectedMenuItemIndex + 1;
					break;
				}
				case SDL_SCANCODE_LEFT:
				{
					switch (menuItems[*selectedMenuItemIndex].id)
					{
					case MENU_ITEM_MUSIC_VOLUME:
					{
						JE_playSampleNum(S_CURSOR);

						JE_changeVolume(&tyrMusicVolume, -8, &fxVolume, 0);
						break;
					}
					case MENU_ITEM_SOUND_VOLUME:
					{
						JE_changeVolume(&tyrMusicVolume, 0, &fxVolume, -8);

						JE_playSampleNum(S_CURSOR);
						break;
					}
					default:
						break;
					}
					break;
				}
				case SDL_SCANCODE_RIGHT:
				{
					switch (menuItems[*selectedMenuItemIndex].id)
					{
					case MENU_ITEM_MUSIC_VOLUME:
					{
						JE_playSampleNum(S_CURSOR);

						JE_changeVolume(&tyrMusicVolume, 8, &fxVolume, 0);
						break;
					}
					case MENU_ITEM_SOUND_VOLUME:
					{
						JE_changeVolume(&tyrMusicVolume, 0, &fxVolume, 8);

						JE_playSampleNum(S_CURSOR);
						break;
					}
					default:
						break;
					}
					break;
				}
				case SDL_SCANCODE_SPACE:
				case SDL_SCANCODE_RETURN:
				{
					action = true;
					break;
				}
				case SDL_SCANCODE_ESCAPE:
				{
					JE_playSampleNum(S_SPRING);

					currentMenu = menuParents[currentMenu];
					break;
				}
				default:
					break;
				}
			}

			// A Modern-only setting is greyed out and ignored while Classic is
			// active, so its picker never opens.
			const MenuItem *const selectedMenuItem = &menuItems[*selectedMenuItemIndex];
			if (action && !(selectedMenuItem->requiresModern && presentation != PRESENTATION_MODERN))
			{
				const MenuItemId selectedMenuItemId = selectedMenuItem->id;

				switch (selectedMenuItemId)
				{
				case MENU_ITEM_DONE:
				{
					JE_playSampleNum(S_SELECT);

					currentMenu = menuParents[currentMenu];
					break;
				}
				case MENU_ITEM_GRAPHICS:
				{
					JE_playSampleNum(S_SELECT);

					menuParents[MENU_GRAPHICS] = currentMenu;
					currentMenu = MENU_GRAPHICS;
					selectedMenuItemIndexes[currentMenu] = 0;
					break;
				}
				case MENU_ITEM_SOUND:
				{
					JE_playSampleNum(S_SELECT);

					menuParents[MENU_SOUND] = currentMenu;
					currentMenu = MENU_SOUND;
					selectedMenuItemIndexes[currentMenu] = 0;
					break;
				}
				case MENU_ITEM_JUKEBOX:
				{
					JE_playSampleNum(S_SELECT);

					fade_black(10);

					jukebox();

					restart = true;
					break;
				}
				case MENU_ITEM_DESTRUCT:
				{
					JE_playSampleNum(S_SELECT);

					fade_black(10);

					JE_destructGame();

					restart = true;
					break;
				}
				case MENU_ITEM_DISPLAY:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)(fullscreen_display + 1);
					break;
				}
				case MENU_ITEM_SCALING_MODE:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = scaling_mode;
					break;
				}
				case MENU_ITEM_PRESENTATION:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)presentation;
					break;
				}
				case MENU_ITEM_ASPECT:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)modern_aspect;
					break;
				}
				case MENU_ITEM_PIXEL_ASPECT:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)modern_pixel_aspect;
					break;
				}
				case MENU_ITEM_SMOOTH_MOTION:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = interp_smooth_motion ? 0 : 1;
					break;
				}
				case MENU_ITEM_CRT:
					JE_playSampleNum(S_CLICK);
					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)crt_filter_mode();
					break;
				case MENU_ITEM_LIGHTING:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)modern_lighting_quality;
					break;
				}
				case MENU_ITEM_DEPTH:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)modern_depth_quality;
					break;
				}
				case MENU_ITEM_VFX:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)vfx_level;
					break;
				}
				case MENU_ITEM_MUSIC_VOLUME:
				{
					JE_playSampleNum(S_CLICK);

					music_disabled = !music_disabled;
					if (!music_disabled)
						restart_song();
					break;
				}
				case MENU_ITEM_SOUND_VOLUME:
				{
					samples_disabled = !samples_disabled;

					JE_playSampleNum(S_CLICK);
					break;
				}
				default:
					break;
				}
			}

			if (currentMenu == MENU_NONE)
			{
				fade_black(10);

				return;
			}
		}
		else
		{
			const MenuItem *selectedMenuItem = &menuItems[*selectedMenuItemIndex];

			// Handle picker interaction.

			bool action = false;

			MouseInput mouseInput;
			KeyboardInput keyboardInput;

			if (mouseGetInput(INPUT_ANY, &mouseInput))
			{
				const size_t pickerItemsCount = selectedMenuItem->getPickerItemsCount();

				// Find picker item that was hovered or clicked.
				if (mouseInput.x >= xMenuItemValue && mouseInput.x < xMenuItemValue + wMenuItemValue)
				{
					for (size_t i = 0; i < pickerItemsCount; ++i)
					{
						const int yPickerItem = yPicker + dyPickerItem * i;

						if (mouseInput.y >= yPickerItem && mouseInput.y < yPickerItem + hPickerItem)
						{
							if (pickerSelectedIndex != i)
							{
								JE_playSampleNum(S_CURSOR);

								pickerSelectedIndex = i;
							}

							// Act on picker item.
							if (mouseInput.button == SDL_BUTTON_LEFT &&
							    mouseInput.x >= xMenuItemValue && mouseInput.y < xMenuItemValue + wMenuItemName &&
							    mouseInput.y >= yPickerItem && mouseInput.y < yPickerItem + hPickerItem)
							{
								action = true;
							}
						}
					}
				}

				if (mouseInput.button == SDL_BUTTON_RIGHT)
				{
					JE_playSampleNum(S_SPRING);

					currentPicker = MENU_ITEM_NONE;
				}
			}
			else if (keyboardGetInput(&keyboardInput))
			{
				switch (keyboardInput.scancode)
				{
				case SDL_SCANCODE_UP:
				{
					JE_playSampleNum(S_CURSOR);

					const size_t pickerItemsCount = selectedMenuItem->getPickerItemsCount();

					pickerSelectedIndex = pickerSelectedIndex == 0
						? pickerItemsCount - 1
						: pickerSelectedIndex - 1;
					break;
				}
				case SDL_SCANCODE_DOWN:
				{
					JE_playSampleNum(S_CURSOR);

					const size_t pickerItemsCount = selectedMenuItem->getPickerItemsCount();

					pickerSelectedIndex = pickerSelectedIndex == pickerItemsCount - 1
						? 0
						: pickerSelectedIndex + 1;
					break;
				}
				case SDL_SCANCODE_SPACE:
				case SDL_SCANCODE_RETURN:
				{
					action = true;
					break;
				}
				case SDL_SCANCODE_ESCAPE:
				{
					JE_playSampleNum(S_SPRING);

					currentPicker = MENU_ITEM_NONE;
					break;
				}
				default:
					break;
				}
			}

			if (action)
			{
				JE_playSampleNum(S_CLICK);

				switch (selectedMenuItem->id)
				{
				case MENU_ITEM_DISPLAY:
				{
					if ((int)pickerSelectedIndex - 1 != fullscreen_display)
						reinit_fullscreen((int)pickerSelectedIndex - 1);
					break;
				}
				case MENU_ITEM_SCALING_MODE:
				{
					scaling_mode = pickerSelectedIndex;
					break;
				}
				case MENU_ITEM_PRESENTATION:
				{
					presentation = (Presentation)pickerSelectedIndex;
					// Modern pins the render detail to Pentium (or 6 while the
					// SuperWild cheat is active); Classic restores the player's
					// stored choice.  Re-derive the detail flags and the game
					// speed immediately, so the switch needs no restart.
					JE_applyProcessorType();
					JE_setNewGameSpeed();
					video_apply_display_settings();
					break;
				}
				case MENU_ITEM_ASPECT:
				{
					modern_aspect = (ModernAspect)pickerSelectedIndex;
					video_apply_display_settings();
					break;
				}
				case MENU_ITEM_PIXEL_ASPECT:
				{
					modern_pixel_aspect = (ModernPixelAspect)pickerSelectedIndex;
					video_apply_display_settings();
					break;
				}
				case MENU_ITEM_SMOOTH_MOTION:
				{
					interp_smooth_motion = pickerSelectedIndex == 0;
					break;
				}
				case MENU_ITEM_CRT:
					crt_filter_set_mode((int)pickerSelectedIndex);
					break;
				case MENU_ITEM_LIGHTING:
				{
					// One picker drives both effects.
					modern_bloom_quality = (ModernQuality)pickerSelectedIndex;
					modern_lighting_quality = (ModernQuality)pickerSelectedIndex;
					break;
				}
				case MENU_ITEM_DEPTH:
				{
					modern_depth_quality = (ModernDepth)pickerSelectedIndex;
					break;
				}
				case MENU_ITEM_VFX:
				{
					vfx_level = (VfxLevel)pickerSelectedIndex;
					break;
				}
				default:
					break;
				}

				currentPicker = MENU_ITEM_NONE;
			}
		}
	}
}

int main(int argc, char *argv[])
{
	userFilesDisable();
	GameBootstrapOptions bootstrap;
	char bootstrap_error[256];
	if (!gameBootstrapParse(argc, argv, &bootstrap, bootstrap_error, sizeof bootstrap_error))
	{
		logError("%s", bootstrap_error);
		return EXIT_FAILURE;
	}
	if (launcherRegressRequested(argc, argv))
		return launcherRegressMain(argc, argv);  // one software frame; nothing else starts
	const bool launcher_flow = launcherFlowRequested(argc, argv);
	if (gameVariantSelect(bootstrap.variant) != GAME_VARIANT_OK)
	{
		logError("%s is not available yet.", gameVariantGet(bootstrap.variant)->display_name);
		return EXIT_FAILURE;
	}
	customDataDirPath = bootstrap.data_directory;
	bool regress = bootstrap.regress;
	bool selftest = bootstrap.selftest;
	const char *install_request = installerCliArgument(argc, argv);
	if (install_request == NULL && !launcher_flow && ((!regress && !selftest) || bootstrap.regress_user_root != NULL))
	{
		if (!userFilesEnable(bootstrap.regress_user_root))
		{
			logError("Failed to select user-file root.");
			return EXIT_FAILURE;
		}
	}

#ifndef NDEBUG
	SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_DEBUG);
#endif

	mt_srand(time(NULL));

	logInfo("%s", "");
	logInfo("Welcome to... >> %s %s <<", opentyrian_str, opentyrian_version);
	logInfo("%s", "");
	logInfo("Copyright (C) The OpenTyrian Development Team");
	logInfo("%s", "");
	logInfo("This program comes with ABSOLUTELY NO WARRANTY.");
	logInfo("This is free software, and you are welcome to redistribute it");
	logInfo("under certain conditions.  See the file COPYING for details.");
	logInfo("%s", "");

	// macOS: a regress run must not become (or be brought to) the foreground,
	// or a stray osascript/System Events keystroke or a cursor warp from the
	// real desktop could focus it and inject input mid-run.  SDL ignores this
	// hint on other platforms.  The input events themselves are discarded in
	// handleSdlEvents() as well.
	if (regress)
	{
		SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
		// Let SIGINT/SIGTERM kill a regress run outright instead of becoming an
		// SDL quit event: the harness then reports the signal, rather than a
		// clean exit with a truncated output file.
		SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
	}

	if (!SDL_Init(0))
	{
		logFatal("Failed to initialize SDL: %s", SDL_GetError());
		return EXIT_FAILURE;
	}

	atexit(SDL_Quit);

	// Logs.  Steam hides the game's stdout in Game Mode, so on a Deck mirror
	// the log into the per-user directory automatically; --log-file=PATH
	// overrides that and works on every platform.  Both run before
	// loadConfiguration() so the whole startup is captured.
	const char *log_path = logFileFromArgs(argc, argv);
	char log_default_path[1024];
	log_default_path[0] = '\0';

	if (log_path == NULL && install_request == NULL && !launcher_flow && !regress && !selftest && steamDeck())
	{
		const char *user_dir = userDirGet();
		if (user_dir[0] != '\0' && userDirPrepare())
		{
			snprintf(log_default_path, sizeof(log_default_path), "%s/opentyrian.log", user_dir);
			log_path = log_default_path;
		}
	}

	if (log_path != NULL)
	{
		if (logOpenFile(log_path))
			logInfo("Logging to %s", log_path);
		else
			logWarn("Failed to open '%s' for logging.", log_path);
	}

	// The launcher's install flow with no window, for the tests.
	if (launcher_flow)
		return launcherFlowMain(argc, argv);

	// Headless data install for automation and tests: no video, config or saves.
	if (install_request != NULL)
		return installerRunCli(install_request, installerCliSpecArgument(argc, argv)) ? EXIT_SUCCESS : EXIT_FAILURE;

	// A normal start opens the launcher, which picks the variant; only an
	// explicit --variant (automation) and regress/selftest runs skip it.  The
	// saves belong to the variant, so with the launcher they load once it has
	// chosen, after the command line has been applied.
	// Help exits in JE_paramCheck(). Preserve its legacy config/save bootstrap
	// (also covered by the user-path guard) without opening a launcher.
	bool help_requested = false;
	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "--") == 0)
			break;
		if (strcmp(argv[i], "-h") == 0 ||
		    (strncmp(argv[i], "--", 2) == 0 && argv[i][2] != '\0' &&
		     strlen(argv[i] + 2) <= strlen("help") &&
		     strncmp("help", argv[i] + 2, strlen(argv[i] + 2)) == 0))
			help_requested = true;
	}
	const bool launcher = regress_handoff != NULL || (!regress && !selftest && !bootstrap.variant_explicit && !help_requested);
	bool video_ready = false;

	if (userFilesEnabled())
	{
		loadConfiguration();
		if (!launcher)
		{
			userPathsMigrateLegacy21();
			loadSaves();
		}
	}

	xmas = xmas_time();  // arg handler may override

	JE_paramCheck(argc, argv);
	if (regress_crt_check)
		return regress_crt_selfcheck();
	if (regress_depth_check)
		return regress_depth_selfcheck();

	if (input_probe_requested)
	{
		// Input diagnostic (T3b.0).  It needs the window, the joysticks and the
		// game's font (so the game data), but no launcher, title or menus.
		if (!gameDataPrepare())
			return EXIT_FAILURE;
		init_video();
		init_joysticks();
		loadPals();
		JE_loadMainShapeTables("tyrian.shp");

		input_probe_run();

		deinit_joysticks();
		deinit_video();
		return EXIT_SUCCESS;
	}

	if (launcher)
	{
		// The launcher is the first screen of the game: it needs the window,
		// the renderer and the controllers, but no game data.
		init_video();
		init_joysticks();
		video_ready = true;

		GameVariant choice = launcherLastVariant >= 0 ? (GameVariant)launcherLastVariant : VARIANT_TYRIAN21;
		const char *selection_error = NULL;
		for (;;)
		{
			if (!launcherChoose(choice, customDataDirPath, selection_error, &choice))
			{
				deinit_joysticks();
				deinit_video();
				return EXIT_SUCCESS;
			}
			if (gameVariantSelect(choice) == GAME_VARIANT_OK && gameDataPrepare())
				break;

			// No variant assets or saves have loaded yet. A late data failure
			// (for example a removed drive) can safely return to selection.
			selection_error = "The selected game data could not be validated. Check the data path and try again, or choose the other game. See the log for details.";
		}

		launcherLastVariant = (int)choice;
		if (userDirPrepare())
			saveConfiguration();

		video_apply_display_settings();  // the window goes back to the game's own shape

		userPathsMigrateLegacy21();
		loadSaves();
	}

	if (bootstrap.regress_user_files)
	{
		// Exercise the real serializers without data, video or gameplay.
		saveConfiguration();
		saveSaves();
		if (recordDemo)
		{
			beginRecordDemo();
			endRecordDemo();
		}
		return EXIT_SUCCESS;
	}

	// A --presentation on the command line overrides opentyrian.cfg, so
	// re-derive the effective detail after parsing it; loadConfiguration()
	// applied the config-file presentation.  Regress mode pins its own
	// presentation and detail in regress_init() instead.
	if (!regress && !selftest)
		JE_applyProcessorType();

	if (selftest)
	{
		int result = gamepad_selftest_run();

		return result;
	}

	if (regress)
	{
		// Apply the regress pins after JE_paramCheck() so they win over any
		// command-line option (including -x/-X, which set xmas).
		regress_init();
		xmas = regress_xmas != 0;  // Christmas only when a regress case asks for it
	}

	logInfo("Presentation mode: %s.", presentation_names[presentation]);

	if (presentation == PRESENTATION_MODERN)
	{
		logInfo("Modern geometry: aspect %s, pixel aspect %s.", modern_aspect_names[modern_aspect], modern_pixel_aspect_names[modern_pixel_aspect]);
		logInfo("Modern lighting: bloom %s, lighting %s, depth %s.", modern_quality_names[modern_bloom_quality], modern_quality_names[modern_lighting_quality], modern_depth_names[modern_depth_quality]);
	}

	if (!gameDataPrepare())
		return EXIT_FAILURE;

	if (regress_audio_active())
	{
		// Offline audio regression: no video, input or audio device needed.
		regress_audio_run();

		return EXIT_SUCCESS;
	}

	JE_scanForEpisodes();

	if (!video_ready)
		init_video();
	init_keyboard();
	if (!video_ready)
		init_joysticks();
	if (has_mouse)
		logInfo("Assuming mouse detected.");  // SDL can't tell us if there isn't one.

	if (xmas && (!dataFileExists("tyrianc.shp") || !dataFileExists("voicesc.snd")))
	{
		xmas = false;

		logWarn("Christmas is missing.");
	}

	loadPals();
	JE_loadMainShapeTables(xmas ? "tyrianc.shp" : "tyrian.shp");

	if (xmas && !regress_active() && !xmas_prompt())
	{
		xmas = false;

		free_main_shape_tables();
		JE_loadMainShapeTables("tyrian.shp");
	}

	/* Default Options */
	youAreCheating = false;
	smoothScroll = true;
	loadDestruct = false;

	if (!audio_disabled)
	{
		logInfo("Initializing SDL audio...");

		if (init_audio())
			loadSndFile(xmas);
	}
	else
	{
		logInfo("Audio is disabled.");
	}

	if (recordDemo)
		logInfo("Game will be recorded.");

	loadExtraShapes();  /*Editship*/

	JE_loadHelpText();

	if (isNetworkGame)
	{
#ifdef WITH_NETWORK
		if (network_init())
		{
			network_tyrian_halt(3, false);
		}
#else
		logFatal("OpenTyrian was compiled without networking support.");
		return EXIT_FAILURE;
#endif
	}

	if (regress_screen_active())
	{
		// Render one non-gameplay screen in a deterministic state, present
		// --regress-frames frames and exit (see src/regress_screen.c).  This
		// never returns.
		regress_screen_run();
		return EXIT_SUCCESS;
	}

	if (regress_flow_active())
	{
		// A scripted walk through the menus and levels, from the title screen on.
		// This never returns.
		regress_flow_run();
		return EXIT_SUCCESS;
	}

	if (regress_script_active())
	{
		// Start the requested level through the episode script, skipping intro,
		// title and menus.  This reaches the script's own screens -- the
		// WARNING text in particular -- which the demo/scenario paths jump over.
		// The --regress-frames cap ends the run from the first presented frame.
		JE_initPlayerData();

		// Regress-only loadout override (--regress-front-weapon/--regress-front-power)
		// so a chosen weapon can be captured firing headless.
		regress_apply_loadout();

		JE_initEpisode((JE_byte)regress_scenario_episode);
		initial_episode_num = (JE_byte)regress_scenario_episode;
		episodeNum = (JE_byte)regress_scenario_episode;
		mainLevel = (JE_byte)regress_scenario_level;
		playDemo = false;

		JE_main();

		regress_finish();

		return EXIT_SUCCESS;
	}

	if (regress_demo != 0 || regress_scenario_active())
	{
		// Replay the requested demo (or start the requested synthetic level)
		// directly, skipping intro, title and menus.
		if (regress_demo != 0)
			setDemoNumber(regress_demo);

		JE_initPlayerData();

		playDemo = true;

		JE_main();

		// JE_main() returns once playback ends (demo exhausted, scenario frame
		// cap reached, or level over).
		regress_finish();

		return EXIT_SUCCESS;
	}

	for (; ; )
	{
#ifdef NDEBUG
		if (!isNetworkGame && !stoppedDemo)
			intro_logos();
#endif

		JE_initPlayerData();
		JE_sortHighScores();

		playDemo = false;
		stoppedDemo = false;

		gameLoaded = false;
		jumpSection = false;

#ifdef WITH_NETWORK
		if (isNetworkGame)
		{
			networkStartScreen();
		}
		else
#endif
		{
			if (!titleScreen())
			{
				// Player quit from title screen.
				break;
			}
		}

		if (loadDestruct)
		{
			JE_destructGame();

			loadDestruct = false;
		}
		else
		{
			JE_main();

			if (trentWin)
			{
				// Player beat SuperTyrian.
				break;
			}
		}
	}

	JE_tyrianHalt(0);

	return 0;
}
