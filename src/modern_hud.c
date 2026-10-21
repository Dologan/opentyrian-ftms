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
#include "modern_hud.h"

#include "bike.h"
#include "bike_touch.h"
#include "config.h"
#include "episodes.h"
#include "fonthand.h"
#include "helptext.h"
#include "mainint.h"
#include "modern.h"
#include "modern_progress.h"
#include "network.h"
#include "player.h"
#include "sprite.h"
#include "varz.h"
#include "vga256d.h"

#include <assert.h>
#include <string.h>

// Debug-only: a HUD primitive must not run past the bottom of the panel surface
// (the surfaces are deliberately wider than the visible panel, so only the
// height is checked).  Catches a layout row that would be clipped by the
// 200-row canvas; compiled out in release (NDEBUG).
#ifndef NDEBUG
#define HUD_ASSERT_FIT(hud_surf, hud_y, hud_h) \
	assert((hud_y) >= 0 && (hud_h) >= 0 && (hud_y) + (hud_h) <= (hud_surf)->h)
#else
#define HUD_ASSERT_FIT(hud_surf, hud_y, hud_h) ((void)0)
#endif

// ---------------------------------------------------------------------------
// Colours.  These are the same palette families the original sidebar uses for
// its shield (144) and armor (224) bars; everything else is text.
// ---------------------------------------------------------------------------
#define HUD_BAR_SHIELD 144
#define HUD_BAR_ARMOR  224
#define HUD_BAR_POWER  113
// The value fill ramps up the bar within its hue block (base + 2 at the bottom
// of the trough, base + 2 + HUD_BAR_RAMP at the top), like the original
// sidebar's JE_dBar3, whose palette index climbs one step every few rows.
#define HUD_BAR_RAMP 13
#define HUD_SEP_COLOR  16
#define HUD_LABEL_BANK 2
#define HUD_LABEL_DIM  3
#define HUD_VALUE_BANK 2
#define HUD_VALUE_BRIGHT 6
#define HUD_NUM_BANK   15
#define HUD_GAUGE_COLOR 112

// Content margin inside a panel.
#define HUD_MARGIN 2

// A panel is "roomy" once it is wide enough for full-word labels, wider bars
// and the generator name (16:9 gives 81/82 px, 21:9 more).  Below this it uses
// the compact layout (16:10 gives 60 px).
#define HUD_ROOMY_WIDTH 72

// Vertical vitals block (shield / armor / power reserve), shared by every
// layout.  Three columns filling bottom-up, the label above each bar and its
// numeric value below.  The 1P panel has room for 78 px bars; the 2P/16:10
// compact panel must also carry the weapon and sidekick icon+name+gauge rows,
// so its bars start at 26 px and grow (up to 39 px) by whatever the panel width
// frees at the bottom; see hud_cp_shift().
#define VIT_BAR_H_1P      78
#define VIT_BAR_H_COMPACT 26
#define VIT_BAR_H_COMPACT_MAX 39
#define VIT_COL_MAX_W     36  // keep the vital bars together on wide panels
#define VIT_LABEL_GAP     7   // label row to bar top
#define VIT_VALUE_GAP     1   // bar bottom to value row

// Layout slots, in logical pixels from the top of the 200-row panel canvas.
//
// The life and superbomb icons are 12x14 tiles; every row that follows one
// leaves a gap so they cannot touch it.  Text rows are 6 px tall.
//
// 1-player ship-status panel (right).  The original put shield, armor and power
// on the right sidebar, so they stay there, now as vertical bars; the playfield
// status block the Modern HUD used to carry (name, lives, cash, superbombs and
// the special-weapon icon) is grouped with them as "the ship".  The name is
// only drawn in multiplayer.  The special icon reserves a 28-row slot when one
// is held, otherwise the block starts near the top (and the special layout
// packs its rows a little tighter, since the 78 px bars leave no slack beside a
// held special).
//
// Only the upper block (down to the superbomb row) and the bottom stack
// (generator name over the cheat notice, anchored to HUD_BOTTOM_Y) have fixed
// rows; the vitals block is centred in the free space between them, so it
// stays put while lives, bombs or the cheat notice change.
#define ST_SPECIAL_Y      2
#define ST_NAME_HI_Y      31
#define ST_NAME_Y         6
#define ST_GEN_GAP        3   // generator name to the cheat notice
// Row steps of the upper block: name -> lives, lives -> cash, cash -> bombs.
// The name row only exists in 2P; a 1P game starts at the lives / cash row.
// The life and superbomb icons are 14 rows tall, so every step after one keeps
// a gap.  The lives row only exists in arcade / 2P.
#define ST_STEP_NAME      10
#define ST_STEP_LIVES     16
#define ST_STEP_CASH      11
#define ST_STEP_NAME_HI   8
#define ST_STEP_CASH_HI   9
// The first row of the upper block (the name in 2P); lives/cash/superbombs follow.

// 1-player armament panel (left).  Weapons, sidekicks and their gauges; the
// global boss bars and level timer sit at the bottom (they used to share the
// status panel, which no longer has the room for both them and the taller
// bars).
#define AR_FRONT_LABEL_Y  6
#define AR_FRONT_NAME_Y   15
#define AR_FRONT_PIPS_Y   25
#define AR_REAR_LABEL_Y   38
#define AR_REAR_NAME_Y    47
#define AR_REAR_PIPS_Y    57
#define AR_MODE_Y         66
#define AR_SK_L_LABEL_Y   82
#define AR_SK_L_ICON_Y    91
#define AR_SK_R_LABEL_Y   114
#define AR_SK_R_ICON_Y    123
#define AR_PROGRESS_LABEL_Y 142
#define AR_PROGRESS_BAR_Y   150
#define AR_BOSS_Y         158
#define AR_BOSS_STEP      9
#define AR_TIMER_LABEL_Y  176
#define AR_TIMER_VALUE_Y  184

// 2-player compact panel (one per player).  Both players' panels carry the same
// blocks (status, vertical vitals, weapons, sidekicks, bottom text); player 1
// (left) holds the timer, player 2 (right) the cheat notice, and boss bar b goes
// in panel b.  The rows are packed tighter than the 1P panel and the vertical
// bars are shorter so the base layout's sidekick icon+gauge blocks still fit;
// see VIT_BAR_H_COMPACT.
#define CP_NAME_Y         2
#define CP_LIVES_Y        9
#define CP_CASH_Y         24
#define CP_BOMBS_Y        31
#define CP_VIT_LABEL_Y    46
// Rows below the vitals, for the shortest (26 px) bars; hud_cp_shift() moves
// them down as the bars grow.
#define CP_FRONT_NAME_Y   88
#define CP_FRONT_PIPS_Y   95
#define CP_REAR_NAME_Y    102
#define CP_REAR_PIPS_Y    109  // rear firing mode shares this row
#define CP_SK_L_LABEL_Y   116
#define CP_SK_L_ICON_Y    123
#define CP_SK_R_LABEL_Y   141
#define CP_SK_R_ICON_Y    148
#define CP_BOSS_Y         166

// Lowest row (exclusive) a panel's text may use.  The cheat notice, the
// generator name and the 2P timer are anchored to it.
#define HUD_BOTTOM_Y      199
#define HUD_TIMER_GAP     4   // inline 2P timer: label to value

// ---------------------------------------------------------------------------
// Small helpers.
// ---------------------------------------------------------------------------

typedef struct
{
	SDL_Surface *surface;
	int w;
	bool right;   // right-align the content toward the outer screen edge
	bool roomy;
} HudPanel;

// Start x for a content block of the given width, honoring the panel's
// alignment.
static int hud_start(const HudPanel *p, int content_w)
{
	if (p->right)
	{
		int x = p->w - HUD_MARGIN - content_w;
		return x > HUD_MARGIN ? x : HUD_MARGIN;
	}
	return HUD_MARGIN;
}

// Copies `src` into `dst`, shortened with a trailing '.' only if characters had
// to be dropped to fit `max_w` at TINY_FONT size.  Trailing spaces are trimmed
// first: item names are fixed-width, space-padded strings, so otherwise a name
// that fits would still be measured (and marked) as too long.  Never overflows
// `dst`.
static void hud_truncate(char *dst, size_t dst_size, const char *src, int max_w)
{
	if (dst_size == 0)
		return;

	if (max_w < 0)
		max_w = 0;

	size_t n = strlen(src);
	while (n > 0 && src[n - 1] == ' ')
		--n;

	char full[128];
	size_t copy = MIN(n, dst_size - 1);
	copy = MIN(copy, sizeof full - 1);
	memcpy(full, src, copy);
	full[copy] = '\0';

	if (copy == 0)
	{
		dst[0] = '\0';
		return;
	}

	if (JE_textWidth(full, TINY_FONT) <= max_w)
	{
		memcpy(dst, full, copy + 1);
		return;
	}

	for (size_t len = copy; len > 0; --len)
	{
		char candidate[130];
		memcpy(candidate, full, len);
		candidate[len] = '.';
		candidate[len + 1] = '\0';
		if (JE_textWidth(candidate, TINY_FONT) <= max_w)
		{
			memcpy(dst, candidate, len + 2);
			return;
		}
	}
	dst[0] = '\0';
}

static void hud_text(SDL_Surface *surface, int x, int y, const char *s, unsigned int bank, int bright)
{
	HUD_ASSERT_FIT(surface, y, 6);
	JE_outText(surface, x, y, s, bank, bright);
}

// JE_outText() with blit_sprite_hv() (clamped) instead of
// blit_sprite_hv_unsafe().  Plain text is byte-identical; the difference only
// shows when a glyph nibble plus the '~' brightness boost would overflow the
// low nibble (see hud_draw_message_strip).  `~` toggles +4 exactly like
// JE_outText().
static void hud_text_clamped(SDL_Surface *surface, int x, int y, const char *s, unsigned int bank, int bright)
{
	HUD_ASSERT_FIT(surface, y, 6);

	int toggled = 0;

	for (int i = 0; s[i] != '\0'; ++i)
	{
		const int sprite_id = fontMap[(unsigned char)s[i]];

		switch (s[i])
		{
		case ' ':
			x += 6;
			break;

		case '~':
			toggled = (toggled == 0) ? 4 : 0;
			break;

		default:
			if (sprite_id != -1 && sprite_exists(TINY_FONT, sprite_id))
			{
				blit_sprite_hv(surface, x, y, TINY_FONT, sprite_id, bank, bright + toggled);
				x += sprite(TINY_FONT, sprite_id)->width + 1;
			}
			break;
		}
	}
}

// The longest of three label spellings that fits `max_w` at TINY_FONT.
static const char *hud_pick_label(const char *full, const char *mid, const char *short_, int max_w)
{
	if (JE_textWidth(full, TINY_FONT) <= max_w)
		return full;
	if (JE_textWidth(mid, TINY_FONT) <= max_w)
		return mid;
	return short_;
}

// --- dynamic bar interpolation (stage 4) ------------------------------------
//
// See modern.h: the vitals bars and the boss bars keep the geometry and the
// previous tick's value so the presentation can redraw them at a value between
// the two ticks.  The buffers are a fixed small array; the previous tick's
// records are rolled once per tick by modern_hud_begin_bars().
#define HUD_BAR_MAX 16

typedef enum
{
	HUD_BAR_VITALS = 0,
	HUD_BAR_BOSS,
} HudBarKind;

typedef struct
{
	HudBarKind kind;
	SDL_Surface *surface;
	int x, y, w, h;        // vitals rect
	int x1, y1, x2, y2;    // boss rect
	Uint8 base;            // vitals base colour
	Uint8 color;           // boss JE_barX colour (118 + flash)
	Uint32 value;          // this tick's value
	Uint32 max;            // vitals max (0 for a boss bar)
} HudBar;

static HudBar hud_bars[HUD_BAR_MAX];
static unsigned hud_bars_count = 0;
static HudBar hud_bars_prev[HUD_BAR_MAX];
static unsigned hud_bars_prev_count = 0;

static HudBar *hud_bar_record(void)
{
	if (hud_bars_count >= HUD_BAR_MAX)
		return NULL;
	return &hud_bars[hud_bars_count++];
}

static const HudBar *hud_bar_find_prev(const HudBar *cur)
{
	for (unsigned i = 0; i < hud_bars_prev_count; ++i)
	{
		const HudBar *p = &hud_bars_prev[i];

		if (p->kind != cur->kind || p->surface != cur->surface)
			continue;

		if (cur->kind == HUD_BAR_VITALS)
		{
			if (p->x == cur->x && p->y == cur->y && p->w == cur->w &&
			    p->h == cur->h && p->base == cur->base && p->max == cur->max)
				return p;
		}
		else
		{
			// The colour is the hit flash and changes every tick while the bar
			// blinks; the geometry is what identifies the bar, and the redraw
			// uses the current colour anyway.
			if (p->x1 == cur->x1 && p->y1 == cur->y1 &&
			    p->x2 == cur->x2 && p->y2 == cur->y2)
				return p;
		}
	}

	return NULL;
}

// a + (b - a) * alpha in 16.16; alpha = 65536 returns b exactly.
static Uint64 hud_lerp_fx(Uint32 a, Uint32 b, Uint32 alpha_fx16)
{
	const Sint64 d = (Sint64)b - (Sint64)a;
	return ((Sint64)a << 16) + ((d * (Sint64)alpha_fx16) >> 16);
}

// A procedural vertical bar with a 1px bevel, filled bottom-up and scaled to
// `max_value`.  The trough carries a light left / dark right edge; the value
// column is a palette ramp like the original sidebar bars (JE_dBar3): its
// brightness climbs as the column rises within the hue block, with a lit left
// edge and a bright meniscus at the top of the fill.  The ramp is anchored to
// the trough, so a given height keeps its colour as the bar drains, and the
// dark trough stays clearly distinct from even a sliver of value.
//
// `value_fx16` is the value in 16.16 fixed point, so an interpolated value
// gives sub-unit (and therefore sub-pixel) fill heights.  The tick draw passes
// the integer value shifted up, which reproduces the original bar exactly.
static void hud_vbar_draw(SDL_Surface *surface, int x, int y, int w, int h,
                          Uint64 value_fx16, uint max_value, Uint8 base)
{
	if (w < 3)
		w = 3;
	if (h < 4)
		h = 4;

	HUD_ASSERT_FIT(surface, y, h);

	// Top of the 16-colour hue block that holds `base`.  The power bar's base
	// (113) is not block-aligned, so base + 15 would spill into the next (blue)
	// block and paint a blue meniscus on a full bar.
	const int top = (int)base | 15;

	// Trough with a light left edge and a dark right edge, like the horizontal
	// bar's top/bottom rows.
	fill_rectangle_xy(surface, x, y, x + w - 1, y + h - 1, base);
	fill_rectangle_xy(surface, x, y, x, y + h - 1, (Uint8)(base + 2));
	fill_rectangle_xy(surface, x + w - 1, y, x + w - 1, y + h - 1, (Uint8)(base + 1));

	if (max_value == 0)
		max_value = 1;

	int fh = (int)((value_fx16 * (Uint64)(h - 2)) / ((Uint64)max_value << 16));
	if (fh > h - 2)
		fh = h - 2;

	const int span = h - 2;           // trough interior: the ramp's span
	const bool lit = (w >= 4);        // room for a lit column inside the fill

	for (int r = 0; r < fh; ++r)      // r == 0 at the bottom of the fill
	{
		const int ry = y + h - 2 - r;

		// Ramp up the trough within the hue block.
		int idx = (int)base + 2 + (r * HUD_BAR_RAMP) / span;
		// A brighter meniscus caps the fill, so even a sliver of value reads.
		if (r == fh - 1)
			idx = MIN(idx + 3, top);

		fill_rectangle_xy(surface, x + 1, ry, x + w - 2, ry, (Uint8)idx);

		if (lit)
			fill_rectangle_xy(surface, x + 1, ry, x + 1, ry, (Uint8)MIN(idx + 1, top));
	}
}

// The shield trough rotated clockwise: fill advances left-to-right, with
// the same anchored hue ramp, lit upper edge and bright leading meniscus.
// Progress deliberately has no interpolation record: the observer is the sole
// authority, and confirmed 100% must appear in the very next presentation.
static void hud_progress_bar(SDL_Surface *surface, int x, int y, int w, int h, int percent)
{
	assert(w >= 4 && h >= 3 && x >= 0 && x + w <= surface->w);
	HUD_ASSERT_FIT(surface, y, h);
	const int base = HUD_BAR_SHIELD;
	const int top = base | 15;
	fill_rectangle_xy(surface, x, y, x + w - 1, y + h - 1, base);
	fill_rectangle_xy(surface, x, y, x + w - 1, y, base + 2);
	fill_rectangle_xy(surface, x, y + h - 1, x + w - 1, y + h - 1, base + 1);
	const int span = w - 2;
	const int filled = span * percent / 100;
	for (int c = 0; c < filled; ++c)
	{
		int idx = base + 2 + c * HUD_BAR_RAMP / span;
		if (c == filled - 1)
			idx = MIN(idx + 3, top);
		fill_rectangle_xy(surface, x + 1 + c, y + 1, x + 1 + c, y + h - 2, (Uint8)idx);
		if (h >= 4)
			fill_rectangle_xy(surface, x + 1 + c, y + 1, x + 1 + c, y + 1, (Uint8)MIN(idx + 1, top));
	}
}

static void hud_progress_panel(void)
{
	SDL_Surface *surface = modern_hud_surface(0);
	const int avail = modern_side_panel_width() - 2 * HUD_MARGIN;
	// Clear only our reserved rows; neither the sidekick gauge (137..138)
	// nor either preexisting boss bar (158 onward) belongs to this block.
	fill_rectangle_xy(surface, HUD_MARGIN, AR_PROGRESS_LABEL_Y,
	                  HUD_MARGIN + avail - 1, AR_PROGRESS_BAR_Y + 5, 0);
	const int percent = modern_progress_percent();
	if (percent < 0)
		return;
	assert(percent <= 100);
	char value[8];
	snprintf(value, sizeof value, "%d%%", percent);
	const int lw = JE_textWidth("PROGRESS", TINY_FONT);
	const int vw = JE_textWidth(value, TINY_FONT);
	const int w = MIN(avail, 144);
	assert(lw <= w);
	hud_text(surface, HUD_MARGIN, AR_PROGRESS_LABEL_Y, "PROGRESS", HUD_LABEL_BANK, HUD_LABEL_DIM);
	if (lw + 3 + vw <= w)
	{
		hud_text(surface, HUD_MARGIN + w - vw, AR_PROGRESS_LABEL_Y, value, HUD_NUM_BANK, 1);
		hud_progress_bar(surface, HUD_MARGIN, AR_PROGRESS_BAR_Y, w, 5, percent);
	}
	else
	{
		// Narrow panels retain the full label; put the percentage beside the
		// shorter bar on the second row instead of truncating either text.
		assert(w - vw - 3 >= 4);
		hud_progress_bar(surface, HUD_MARGIN, AR_PROGRESS_BAR_Y, w - vw - 3, 5, percent);
		hud_text(surface, HUD_MARGIN + w - vw, AR_PROGRESS_BAR_Y, value, HUD_NUM_BANK, 1);
	}
}

static void hud_vbar(SDL_Surface *surface, int x, int y, int w, int h,
                     uint value, uint max_value, Uint8 base)
{
	hud_vbar_draw(surface, x, y, w, h, (Uint64)value << 16, max_value, base);

	HudBar *bar = hud_bar_record();
	if (bar != NULL)
	{
		bar->kind = HUD_BAR_VITALS;
		bar->surface = surface;
		bar->x = x;
		bar->y = y;
		bar->w = w < 3 ? 3 : w;
		bar->h = h < 4 ? 4 : h;
		bar->base = base;
		bar->value = value;
		bar->max = max_value;
	}
}

// The shield / armor / power-reserve block: three vertical bars side by side,
// each with its label above and its numeric value below.  `label_y` is the
// label row; the bars start VIT_LABEL_GAP below it and are `bar_h` tall.  The
// block is centered until capped, then follows the panel's outer alignment.
static void hud_draw_vitals(const HudPanel *p, int pi, int label_y, int bar_h)
{
	const uint values[3] = { player[pi].shield, player[pi].armor, MIN(power, 900u) };
	const uint maxima[3] = { MAX(player[pi].shield_max, 1u),
	                         MAX(player[pi].initial_armor, 1u), 900 };
	const Uint8 bases[3] = { HUD_BAR_SHIELD, HUD_BAR_ARMOR, HUD_BAR_POWER };
	static const char *const labels_full[3]  = { "SHIELD", "ARMOR", "POWER" };
	static const char *const labels_mid[3]   = { "SHLD",   "ARMR",  "PWR"   };
	static const char *const labels_short[3] = { "SH",     "AR",    "PW"    };

	const int avail = p->w - 2 * HUD_MARGIN;
	int col_w = MIN(avail / 3, VIT_COL_MAX_W);
	if (col_w < 12)
		col_w = 12;
	const int block_x = avail / 3 > VIT_COL_MAX_W
	                  ? hud_start(p, col_w * 3)
	                  : HUD_MARGIN + (avail - col_w * 3) / 2;

	const int bar_w = MIN(col_w - 4, 16);
	const int bar_y = label_y + VIT_LABEL_GAP;
	const int value_y = bar_y + bar_h + VIT_VALUE_GAP;

	for (int i = 0; i < 3; ++i)
	{
		const int col_x = block_x + i * col_w;

		const char *label = hud_pick_label(labels_full[i], labels_mid[i], labels_short[i], col_w);
		const int lx = col_x + (col_w - JE_textWidth(label, TINY_FONT)) / 2;
		hud_text(p->surface, lx, label_y, label, HUD_LABEL_BANK, HUD_LABEL_DIM);

		const int bx = col_x + (col_w - bar_w) / 2;
		hud_vbar(p->surface, bx, bar_y, bar_w, bar_h, values[i], maxima[i], bases[i]);

		char num[16];
		snprintf(num, sizeof num, "%u", values[i]);
		const int nx = col_x + (col_w - JE_textWidth(num, TINY_FONT)) / 2;
		hud_text(p->surface, nx, value_y, num, HUD_NUM_BANK, 1);
	}
}

// ---------------------------------------------------------------------------
// Individual elements.
// ---------------------------------------------------------------------------

// `reserve_corner` is true only in the compact 2-player layout, where the 1x
// special-weapon icon sits in the top-right corner and the name must not run
// under it.  In single player the special icon is the 2x2 one above the name,
// so the full panel width is available.
static void hud_draw_name(const HudPanel *p, int pi, int y, bool reserve_corner)
{
	char raw[64];

	if (isNetworkGame)
		snprintf(raw, sizeof raw, "%s", JE_getName(pi + 1));
	else
		snprintf(raw, sizeof raw, "%s", helpLabelText(pi == 0 ? GAME_LABEL_PLAYER_1 : GAME_LABEL_PLAYER_2));

	const int max_w = p->w - 2 * HUD_MARGIN -
	                  ((reserve_corner && player[pi].items.special > 0) ? 13 : 0);
	char buf[64];
	hud_truncate(buf, sizeof buf, raw, max_w);

	const int x = hud_start(p, JE_textWidth(buf, TINY_FONT));
	hud_text(p->surface, x, y, buf, HUD_VALUE_BANK, HUD_VALUE_BRIGHT);
}

static void hud_draw_lives(const HudPanel *p, int pi, int y)
{
	if (!(onePlayerAction || twoPlayerMode) || player[pi].lives == NULL)
		return;

	HUD_ASSERT_FIT(p->surface, y, 14);

	const uint extra_lives = *player[pi].lives - 1;

	int content_w;
	char count[16] = "";

	if (extra_lives >= 5)
	{
		snprintf(count, sizeof count, "%u", extra_lives);
		content_w = 12 + 2 + JE_textWidth(count, TINY_FONT);
	}
	else
	{
		content_w = (int)extra_lives * 12;
	}

	int x = hud_start(p, content_w);

	if (extra_lives >= 5)
	{
		blit_sprite2(p->surface, x, y, spriteSheet9, 285);
		hud_text(p->surface, x + 14, y + 3, count, HUD_NUM_BANK, 1);
	}
	else if (extra_lives >= 1)
	{
		for (uint i = 0; i < extra_lives; ++i)
		{
			blit_sprite2(p->surface, x, y, spriteSheet9, 285);
			x += 12;
		}
	}
}

static void hud_draw_cash(const HudPanel *p, int pi, int y)
{
	char buf[24];
	snprintf(buf, sizeof buf, "%lu", player[pi].cash);
	const int x = hud_start(p, JE_textWidth(buf, TINY_FONT));
	hud_text(p->surface, x, y, buf, HUD_VALUE_BANK, 4);
}

static void hud_draw_bombs(const HudPanel *p, int pi, int y)
{
	const uint bombs = player[pi].superbombs;
	if (bombs == 0)
		return;

	HUD_ASSERT_FIT(p->surface, y, 14);

	int x = hud_start(p, (int)bombs * 12);

	for (uint j = 0; j < bombs; ++j)
	{
		blit_sprite2(p->surface, x, y, spriteSheet9, 304);
		x += 12;
	}
}

static void hud_draw_special(const HudPanel *p, int pi, int y, bool big)
{
	if (player[pi].items.special == 0)
		return;

	const uint graphic = special[player[pi].items.special].itemgraphic;

	HUD_ASSERT_FIT(p->surface, y, big ? 28 : 14);

	if (big)
		blit_sprite2x2(p->surface, (p->w - 24) / 2, y, spriteSheet10, graphic);
	else
		blit_sprite2(p->surface, p->w - 1 - 12, y, spriteSheet10, graphic);
}

// Generator name (extra information: the original in-game HUD had no label,
// only the power-reserve bar).  Shown only in the roomy layouts; the power bar
// itself is always part of the vertical vitals block.
static void hud_draw_generator(const HudPanel *p, int pi, int y)
{
	char raw[64], buf[72];
	snprintf(raw, sizeof raw, "GEN %s", powerSys[player[pi].items.generator].name);
	hud_truncate(buf, sizeof buf, raw, p->w - 2 * HUD_MARGIN);
	hud_text(p->surface, hud_start(p, JE_textWidth(buf, TINY_FONT)), y, buf, HUD_LABEL_BANK, HUD_LABEL_DIM);
}

// ---------------------------------------------------------------------------
// Bike readout (exercise-bike integration, presentation only).  Drawn above the
// generator name of the 1-player status panel while the master switch and the
// Bike HUD setting are on (bike_hud_visible()): link dot + watts + cadence, an
// power gauge with the surge-start marker, an effort bar, a surge meter and an
// AUTO / SURGE row.  It reads bike_hud() and never writes game state, settings
// or the RNG.  Rows (relative to the top): gauge 1..3 with its marker 0..3,
// text 5..10, effort 13..15, surge meter 17..19, flags 21..26.  The gauge sits
// on top so that every row that was already there keeps its place.
// ---------------------------------------------------------------------------
#define BIKE_HUD_H 29   // rows reserved above the generator name (27 drawn, 2 px gap)

// The surge colour (palette block of the surge meter) and, for the start cue,
// the text colour it flashes in.
#define HUD_SURGE_BASE   112
#define HUD_SURGE_BANK   7
#define HUD_SURGE_BRIGHT 4
// Gauge: fill hue normally, and the greys used for the marker and for a lost link.
#define HUD_GAUGE_BASE      144
#define HUD_GAUGE_DIM_BASE  0
#define HUD_MARK_IDLE       11
#define HUD_MARK_READY      15

// Trough plus a value ramp within the hue block starting at `base`, like
// hud_progress_bar().  `frac` is Q16 (65536 == full).
static void hud_meter_bar(SDL_Surface *surface, int x, int y, int w, int h, int frac, int base)
{
	assert(w >= 4 && h >= 2 && x >= 0 && x + w <= surface->w);
	HUD_ASSERT_FIT(surface, y, h);
	const int top = base | 15;
	fill_rectangle_xy(surface, x, y, x + w - 1, y + h - 1, (Uint8)base);
	const int span = w - 2;
	const int filled = (int)(((int64_t)span * MAX(0, MIN(frac, 65536))) >> 16);
	for (int c = 0; c < filled; ++c)
	{
		int idx = base + 2 + c * HUD_BAR_RAMP / span;
		if (c == filled - 1)
			idx = MIN(idx + 3, top);
		fill_rectangle_xy(surface, x + 1 + c, y, x + 1 + c, y + h - 1, (Uint8)idx);
	}
}

// Power gauge (`y` is its top row; the tick rises one row above it): the
// smoothed power (fast_w) against a scale that ends past FTP and past the
// surge threshold, and a tick at the watts where a surge would start
// (bike_surge_threshold_w(), computed by the engine, not here).  The fill takes
// the surge colour while surging; the tick turns white and two pixels wide once
// the smoothed power has reached it (about to surge, or surging).
static void hud_draw_power_gauge(const HudPanel *p, const BikeHud *b, int y)
{
	const int avail = p->w - 2 * HUD_MARGIN;
	const int span = avail - 2;
	const bq16 scale = b->gauge_scale_w > 0 ? b->gauge_scale_w : 1;
	// No power reading: no fill and no threshold marker (the gauge is just the empty trough).
	const int frac = b->no_power ? 0 : (int)MAX(0, MIN((int64_t)b->fast_w * 65536 / scale, 65536));
	const int base = b->lost ? HUD_GAUGE_DIM_BASE : (b->surging ? HUD_SURGE_BASE : HUD_GAUGE_BASE);

	HUD_ASSERT_FIT(p->surface, y - 1, 4);
	hud_meter_bar(p->surface, HUD_MARGIN, y, avail, 3, frac, base);

	if (!b->has_threshold || b->no_power)
		return;
	const int64_t t = MAX(0, MIN((int64_t)b->threshold_w * 65536 / scale, 65536));
	const int mx = HUD_MARGIN + 1 + (int)((span - 1) * t >> 16);
	const bool ready = !b->lost && b->fast_w >= b->threshold_w;
	const int mx2 = ready ? MIN(mx + 1, HUD_MARGIN + avail - 2) : mx;
	fill_rectangle_xy(p->surface, ready ? MIN(mx, mx2 - 1) : mx, y - 1, mx2, y + 2,
	                  (Uint8)(ready ? HUD_MARK_READY : HUD_MARK_IDLE));
}

static void hud_draw_bike(const HudPanel *p, int y)
{
	const BikeHud *b = bike_hud();
	const int avail = p->w - 2 * HUD_MARGIN;
	char w_text[16], line[40];
	bool held = false;

	if (b->no_power)
		snprintf(w_text, sizeof w_text, "no power");
	else
		snprintf(w_text, sizeof w_text, "%uW", (unsigned)((b->watts + 32768) >> 16));
	snprintf(line, sizeof line, "%s %uRPM", w_text, (unsigned)((b->cadence + 32768) >> 16));
	if (JE_textWidth(line, TINY_FONT) + 7 > avail)
		snprintf(line, sizeof line, "%s %u", w_text, (unsigned)((b->cadence + 32768) >> 16));
	if (JE_textWidth(line, TINY_FONT) + 7 > avail)
		snprintf(line, sizeof line, "%s", w_text);

	// Row 1: the power gauge with the surge marker (above the text, so the rows below keep their
	// place), then, 5 rows down, the link dot, watts and cadence (dim while the link is lost).
	hud_draw_power_gauge(p, b, y + 1);
	y += 5;
	const int content_w = 4 + 3 + JE_textWidth(line, TINY_FONT);
	const int x = hud_start(p, content_w);
	HUD_ASSERT_FIT(p->surface, y, 6);
	fill_rectangle_xy(p->surface, x, y + 1, x + 3, y + 4, (Uint8)bike_link_color(b->link, b->lost));
	// The surge-start cue (BIKE_SURGE_CUE_US) flashes the readout in the surge colour.
	const bool cue = b->cue_lit && !b->lost;
	hud_text(p->surface, x + 7, y, line, cue ? HUD_SURGE_BANK : HUD_VALUE_BANK,
	         cue ? HUD_SURGE_BRIGHT : (b->lost ? HUD_LABEL_DIM : HUD_VALUE_BRIGHT));

	// Rows 2 and 3: effort (green) and surge meter (red to yellow).
	hud_meter_bar(p->surface, HUD_MARGIN, y + 8, avail, 3, b->effort_norm, 192);
	hud_meter_bar(p->surface, HUD_MARGIN, y + 12, avail, 3, b->surge_meter, HUD_SURGE_BASE);

	// Row 4: AUTO (HOLD while held back) and SURGE (flashing during the cue).
	const bool autofire = bike_autofire_state(&held);
	char flags[24] = "";
	if (autofire)
		snprintf(flags, sizeof flags, "%s", held ? "AUTO HOLD" : "AUTO");
	if (b->surging)
		snprintf(flags + strlen(flags), sizeof flags - strlen(flags), "%sSURGE", flags[0] != '\0' ? " " : "");
	if (flags[0] != '\0')
	{
		char out[32];
		hud_truncate(out, sizeof out, flags, avail);
		hud_text(p->surface, hud_start(p, JE_textWidth(out, TINY_FONT)), y + 16, out,
		         cue ? HUD_SURGE_BANK : HUD_VALUE_BANK,
		         cue ? HUD_SURGE_BRIGHT : ((autofire && held) ? HUD_LABEL_DIM : HUD_VALUE_BRIGHT));
	}
}

// Weapon power pips (1..11), as the original sidebar's segments.
static void hud_draw_pips(const HudPanel *p, int pi, int port, int y)
{
	if (player[pi].items.weapon[port].id == 0)
		return;

	const uint power = player[pi].items.weapon[port].power;
	HUD_ASSERT_FIT(p->surface, y, 4);
	int x = hud_start(p, 22);

	for (uint j = 1; j <= power && j <= 11; ++j)
	{
		fill_rectangle_xy(p->surface, x, y, x + 1, y + 3, (Uint8)(115 + j));
		x += 2;
	}
}

static void hud_draw_weapon(const HudPanel *p, int pi, int port, int label_y, int name_y, int pips_y, int mode_y, bool mode_inline)
{
	const uint id = player[pi].items.weapon[port].id;
	const bool rear = (port == REAR_WEAPON);

	if (label_y >= 0)
	{
		const char *label = rear ? (p->roomy ? "REAR" : "R") : (p->roomy ? "FRONT" : "F");
		hud_text(p->surface, hud_start(p, JE_textWidth(label, TINY_FONT)), label_y, label, HUD_LABEL_BANK, HUD_LABEL_DIM);
	}

	char raw[64], buf[72];
	if (id == 0)
		snprintf(raw, sizeof raw, "%s", "NONE");
	else
		snprintf(raw, sizeof raw, "%s", weaponPort[id].name);

	hud_truncate(buf, sizeof buf, raw, p->w - 2 * HUD_MARGIN);
	hud_text(p->surface, hud_start(p, JE_textWidth(buf, TINY_FONT)), name_y, buf, HUD_VALUE_BANK, HUD_VALUE_BRIGHT);

	hud_draw_pips(p, pi, port, pips_y);

	// Rear firing mode: the original sidebar shows it with two port-config
	// buttons in single player.  It shares the pips row in the compact layout.
	if (rear && id != 0)
	{
		char mode[20], mout[24];
		snprintf(mode, sizeof mode, p->roomy ? "MODE %u" : "M%u", player[pi].weapon_mode);
		hud_truncate(mout, sizeof mout, mode, p->w - 2 * HUD_MARGIN);

		if (mode_inline)
		{
			int x = hud_start(p, 22 + 3 + JE_textWidth(mout, TINY_FONT)) + 22 + 3;
			hud_text(p->surface, x, pips_y, mout, HUD_LABEL_BANK, HUD_LABEL_DIM);
		}
		else if (mode_y >= 0)
		{
			hud_text(p->surface, hud_start(p, JE_textWidth(mout, TINY_FONT)), mode_y, mout, HUD_LABEL_BANK, HUD_LABEL_DIM);
		}
	}
}

// One sidekick: name, the OPTION_SHAPES mode icon (MONO/DUAL with its charge
// ticks, matching the original sidebar) and the segmented gauge.  `label_y` is
// the text row; `icon_y` is the icon top, with the gauge just below it.
static void hud_draw_sidekick(const HudPanel *p, int pi, int k, int label_y, int icon_y)
{
	const uint id = player[pi].items.sidekick[k];
	const char *which = (k == LEFT_SIDEKICK) ? (p->roomy ? "LEFT" : "L") : (p->roomy ? "RIGHT" : "R");

	char label[64], lout[72];
	if (id == 0)
		snprintf(label, sizeof label, "%s NONE", which);
	else
		snprintf(label, sizeof label, "%s %s", which, options[id].name);

	hud_truncate(lout, sizeof lout, label, p->w - 2 * HUD_MARGIN);
	hud_text(p->surface, hud_start(p, JE_textWidth(lout, TINY_FONT)), label_y, lout, HUD_LABEL_BANK, HUD_LABEL_DIM);

	if (id == 0)
		return;

	// Icon, matching the original sidebar (OPTION_SHAPES, icongr-1).
	const int icon_x = hud_start(p, 28);
	HUD_ASSERT_FIT(p->surface, icon_y, 14);
	if (options[id].icongr > 0)
		blit_sprite(p->surface, icon_x, icon_y, OPTION_SHAPES, options[id].icongr - 1);

	// Gauge: limited-ammo sidekicks show ammo/max; infinite-ammo ones show the
	// charge level (the original sidebar gauge becomes a discharge bar).
	const uint ammo_max = options[id].ammo;
	HUD_ASSERT_FIT(p->surface, icon_y + 14, 2);
	if (ammo_max > 0)
	{
		draw_segmented_gauge(p->surface, icon_x, icon_y + 14, HUD_GAUGE_COLOR, 2, 2,
		                     MAX(1, (uint)(ammo_max / 10)), (uint)player[pi].sidekick[k].ammo);
	}
	else
	{
		draw_segmented_gauge(p->surface, icon_x, icon_y + 14, HUD_GAUGE_COLOR, 2, 2, 1,
		                     player[pi].sidekick[k].charge);
	}

	// A numeric readout alongside the gauge: ammo/max (or charge/max) when the
	// panel is roomy, a short ammo form in the compact layout, "INF" for a
	// plain unlimited sidekick.  Compact charge-type sidekicks rely on the
	// gauge, exactly like the original sidebar.
	const uint pwr = options[id].pwr;
	char num[24];

	if (ammo_max > 0)
		snprintf(num, sizeof num, p->roomy ? "%d/%u" : "x%d", player[pi].sidekick[k].ammo, ammo_max);
	else if (pwr > 0)
		snprintf(num, sizeof num, "%u/%u", player[pi].sidekick[k].charge, pwr);
	else
		snprintf(num, sizeof num, "INF");

	const int num_w = JE_textWidth(num, TINY_FONT);
	const int num_x = p->right ? (icon_x - 2 - num_w) : (icon_x + 30);
	hud_text(p->surface, num_x, icon_y + 4, num, HUD_NUM_BANK, 1);
}

// The cheat notice uses as few lines as the panel width allows: one line in a
// wide panel, "Cheaters always" / "prosper." in a 16:9 one, one word per line in
// the narrowest.  Each line takes an 8-row step (6-row glyphs).
static const char *const hud_cheat_text[3][3] = {
	{ "Cheaters always prosper.", "", "" },
	{ "Cheaters always", "prosper.", "" },
	{ "Cheaters", "always", "prosper." },
};

static int hud_cheat_lines(int panel_w)
{
	const int avail = panel_w - 2 * HUD_MARGIN;

	for (int n = 1; n < 3; ++n)
	{
		int widest = 0;
		for (int l = 0; l < n; ++l)
			widest = MAX(widest, JE_textWidth(hud_cheat_text[n - 1][l], TINY_FONT));
		if (widest <= avail)
			return n;
	}
	return 3;
}

// Top row of the cheat notice: its last line ends on HUD_BOTTOM_Y.
static int hud_cheat_y(int panel_w)
{
	return HUD_BOTTOM_Y - (hud_cheat_lines(panel_w) * 8 - 2);
}

static void hud_draw_cheat(const HudPanel *p)
{
	if (!youAreCheating)
		return;

	const int lines = hud_cheat_lines(p->w);
	const int y = hud_cheat_y(p->w);

	HUD_ASSERT_FIT(p->surface, y + (lines - 1) * 8, 6);
	for (int l = 0; l < lines; ++l)
	{
		const char *text = hud_cheat_text[lines - 1][l];
		int x = (p->w - JE_textWidth(text, TINY_FONT)) / 2;
		if (x < HUD_MARGIN)
			x = HUD_MARGIN;
		hud_text(p->surface, x, y + l * 8, text, 3, 4);
	}
}

// The 2P level timer stacks its label over its value in a narrow panel; a wider
// one puts them side by side on one row, which frees 8 rows at the bottom.
// `sample` is the widest value the timer can show ("%.1f" of a 16-bit
// countdown / 100).
static bool hud_timer_inline(int panel_w)
{
	const int avail = panel_w - 2 * HUD_MARGIN;

	return JE_textWidth(helpLabelText(GAME_LABEL_TIMER), TINY_FONT) + HUD_TIMER_GAP +
	       JE_textWidth("655.3", SMALL_FONT_SHAPES) <= avail;
}

// How many rows the 2P compact panel's lower rows move down, i.e. how much
// taller the vitals bars get (26 -> 26 + shift, at most 39).  Everything below
// the bars (weapons, sidekicks) moves with them, so what limits it is the
// bottom of the panel: the boss bar (6 rows + 2) must clear the cheat notice
// (player 2's panel) and the timer (player 1's panel), both anchored to
// HUD_BOTTOM_Y.  Depends only on the panel width, so the layout is static.
static int hud_cp_shift(int panel_w)
{
	const int cheat_top = hud_cheat_y(panel_w);
	const int timer_top = HUD_BOTTOM_Y - (hud_timer_inline(panel_w) ? 8 : 16);
	const int boss_max = MIN(cheat_top, timer_top) - 8;
	const int shift = boss_max - CP_BOSS_Y;

	return MAX(0, MIN(shift, VIT_BAR_H_COMPACT_MAX - VIT_BAR_H_COMPACT));
}

// ---------------------------------------------------------------------------
// Layouts.
// ---------------------------------------------------------------------------

// 1-player ship-status panel (the right one): everything the original playfield
// HUD carried (special icon, optional multiplayer name, lives, cash, superbombs)
// grouped with the shield / armor / power vertical bars the original drew on
// the right sidebar, plus the generator name and the cheat notice.  Always
// player 0.
static void hud_draw_status_panel(const HudPanel *p)
{
	// Upper block.  With a special weapon the 2x2 icon takes a 28-row slot and
	// the rows below it pack tighter; without one they start near the top.  The
	// name row is only needed in multiplayer and is omitted in one-player games.
	const bool special = (player[0].items.special > 0);
	const int step_cash  = special ? ST_STEP_CASH_HI : ST_STEP_CASH;

	int y;
	if (special)
	{
		hud_draw_special(p, 0, ST_SPECIAL_Y, true);
		y = ST_NAME_HI_Y;
	}
	else
	{
		y = ST_NAME_Y;
	}

	if (twoPlayerMode)
	{
		hud_draw_name(p, 0, y, false);
		y += special ? ST_STEP_NAME_HI : ST_STEP_NAME;
	}

	if (onePlayerAction || twoPlayerMode)
	{
		// The life icon is a 12x14 tile: give the row 16 px so the icon and its
		// count cannot touch the cash row below (review fix 1).
		hud_draw_lives(p, 0, y);
		y += ST_STEP_LIVES;
	}

	hud_draw_cash(p, 0, y);
	y += step_cash;

	hud_draw_bombs(p, 0, y);
	const int upper_bottom = y + 14;   // the superbomb icons are 14 rows tall

	// Bottom stack: the generator name (roomy panels) over the cheat notice.
	const int gen_y = hud_cheat_y(p->w) - ST_GEN_GAP - 6;
	const bool bike = bike_hud_visible();
	const int bike_base = p->roomy ? gen_y : hud_cheat_y(p->w);
	const int lower_top = bike ? bike_base - BIKE_HUD_H : bike_base;

	// Shield, armor and power reserve: three vertical bars, bottom-up, the whole
	// block (label row + bars + value row) centred in the free space between the
	// two.
	const int vit_h = VIT_LABEL_GAP + VIT_BAR_H_1P + VIT_VALUE_GAP + 6;
	int label_y = upper_bottom + (lower_top - upper_bottom - vit_h) / 2;
	if (label_y < upper_bottom)
		label_y = upper_bottom;

	hud_draw_vitals(p, 0, label_y, VIT_BAR_H_1P);

	if (p->roomy)
		hud_draw_generator(p, 0, gen_y);

	if (bike)
		hud_draw_bike(p, bike_base - BIKE_HUD_H);

	hud_draw_cheat(p);
}

// 1-player armament panel (the left one): front/rear weapon name, power pips
// and rear firing mode, then both sidekicks.  The global boss bars and the
// level timer sit at the bottom, below the sidekicks.
static void hud_draw_armament_panel(const HudPanel *p)
{
	hud_draw_weapon(p, 0, FRONT_WEAPON, AR_FRONT_LABEL_Y, AR_FRONT_NAME_Y, AR_FRONT_PIPS_Y, -1, false);
	hud_draw_weapon(p, 0, REAR_WEAPON, AR_REAR_LABEL_Y, AR_REAR_NAME_Y, AR_REAR_PIPS_Y, AR_MODE_Y, false);

	hud_draw_sidekick(p, 0, LEFT_SIDEKICK, AR_SK_L_LABEL_Y, AR_SK_L_ICON_Y);
	hud_draw_sidekick(p, 0, RIGHT_SIDEKICK, AR_SK_R_LABEL_Y, AR_SK_R_ICON_Y);
}

// 2-player compact panel: one player's whole status + armament stacked.
// `with_cheat` is true for player 2's panel, which holds the global cheat
// notice (player 1's holds the timer).
static void hud_draw_compact_panel(const HudPanel *p, int pi, bool with_cheat)
{
	// Every row below the vitals moves down with the taller bars.
	const int sh = hud_cp_shift(p->w);

	hud_draw_special(p, pi, 3, false);
	hud_draw_name(p, pi, CP_NAME_Y, true);
	hud_draw_lives(p, pi, CP_LIVES_Y);
	hud_draw_cash(p, pi, CP_CASH_Y);
	hud_draw_bombs(p, pi, CP_BOMBS_Y);

	hud_draw_vitals(p, pi, CP_VIT_LABEL_Y, VIT_BAR_H_COMPACT + sh);

	hud_draw_weapon(p, pi, FRONT_WEAPON, -1, CP_FRONT_NAME_Y + sh, CP_FRONT_PIPS_Y + sh, -1, false);
	hud_draw_weapon(p, pi, REAR_WEAPON, -1, CP_REAR_NAME_Y + sh, CP_REAR_PIPS_Y + sh, -1, true);

	hud_draw_sidekick(p, pi, LEFT_SIDEKICK, CP_SK_L_LABEL_Y + sh, CP_SK_L_ICON_Y + sh);
	hud_draw_sidekick(p, pi, RIGHT_SIDEKICK, CP_SK_R_LABEL_Y + sh, CP_SK_R_ICON_Y + sh);

	if (with_cheat)
		hud_draw_cheat(p);
}

// Reproduces the `tempW` side effects of the original JE_inGameDisplays,
// which the parallax pan and the state hashes depend on.  No drawing.
static void hud_preserve_tempw(void)
{
	if (!(onePlayerAction || twoPlayerMode))
		return;

	char stemp[21];

	for (int temp = 0; temp < (onePlayerAction ? 1 : 2); temp++)
	{
		const uint extra_lives = *player[temp].lives - 1;

		tempW = (temp == 0) ? 30 : 270;

		if (extra_lives >= 5)
		{
			tempW = (temp == 0) ? 45 : 250;
		}
		else if (extra_lives >= 1)
		{
			for (uint i = 0; i < extra_lives; ++i)
				tempW += (temp == 0) ? 12 : -12;
		}

		if (isNetworkGame)
			snprintf(stemp, sizeof stemp, "%s", JE_getName(temp + 1));
		else
			snprintf(stemp, sizeof stemp, "%s", helpLabelText(temp == 0 ? GAME_LABEL_PLAYER_1 : GAME_LABEL_PLAYER_2));

		tempW = (temp == 0) ? 28 : (285 - JE_textWidth(stemp, TINY_FONT));
	}
}

// The 16-row message strip under the playfield.  It is an opaque info bar: the
// level name on the top line and the current in-game message (JE_drawTextWindow)
// on the bottom line.  Drawn into the strip surface; modern.c composites it.
static void hud_draw_message_strip(void)
{
	SDL_Surface *surface = modern_hud_message_surface();
	if (surface == NULL)
		return;

	memset(surface->pixels, 0, (size_t)surface->pitch * (size_t)surface->h);

	// Black bar with a separator line along its top edge.
	fill_rectangle_xy(surface, 0, 0, MODERN_PLAYFIELD_W - 1, surface->h - 1, 0);
	fill_rectangle_xy(surface, 0, 0, MODERN_PLAYFIELD_W - 1, 0, HUD_SEP_COLOR);

	int name_max = MODERN_PLAYFIELD_W - 4;
	const int percent = modern_progress_percent();
	if (twoPlayerMode && !galagaMode && percent >= 0)
	{
		assert(percent <= 100);
		char value[8];
		snprintf(value, sizeof value, "%d%%", percent);
		const int lw = JE_textWidth("PROGRESS", TINY_FONT);
		const int vw = JE_textWidth(value, TINY_FONT);
		const int bar_w = 36;
		const int value_w = JE_textWidth("100%", TINY_FONT);
		const int block_w = lw + 3 + value_w + 4 + bar_w;
		const int x = MODERN_PLAYFIELD_W - 2 - block_w;
		name_max = x - 4;
		hud_text(surface, x, 2, "PROGRESS", HUD_LABEL_BANK, HUD_LABEL_DIM);
		hud_text(surface, x + lw + 3 + value_w - vw, 2, value, HUD_NUM_BANK, 1);
		hud_progress_bar(surface, x + lw + 3 + value_w + 4, 2, bar_w, 5, percent);
	}
	char name[64];
	if (name_max < MODERN_PLAYFIELD_W - 4)
		hud_truncate(name, sizeof name, levelName, name_max);
	else
		snprintf(name, sizeof name, "%s", JE_textWidth(levelName, TINY_FONT) <= name_max ? levelName : "");
	if (name[0] != '\0')
		hud_text(surface, 2, 2, name, HUD_LABEL_BANK, HUD_VALUE_BRIGHT);

	const char *message = modern_message_text();
	if (message != NULL && message[0] != '\0')
	{
		char buf[64];
		hud_truncate(buf, sizeof buf, message, MODERN_PLAYFIELD_W - 4);

		int x = (MODERN_PLAYFIELD_W - JE_textWidth(buf, TINY_FONT)) / 2;
		if (x < 2)
			x = 2;

		// Event strings use '~' to toggle +4 (e.g. "~WARNING:~ Spikes
		// ahead!!").  JE_outText() draws with blit_sprite_hv_unsafe(), whose
		// low nibble is not clamped, so at this brightness the '~' peak wraps
		// into the colour block and corrupts the glyphs ("dark with strange
		// dots").  Draw the message with the clamped primitive instead; plain
		// messages are byte-identical to JE_outText().
		hud_text_clamped(surface, x, 9, buf, HUD_NUM_BANK, 5);
	}
}

// ---------------------------------------------------------------------------
// Public entry points.
// ---------------------------------------------------------------------------

// Called only on the gameplay composition path, including held warp/fade
// frames. Redrawing reads a snapshot; it never advances or resets the observer.
void modern_hud_draw_progress(void)
{
	if (!modern_hud_in_panels())
		return;
	if (!(twoPlayerMode && !galagaMode))
		hud_progress_panel();
	hud_draw_message_strip();
}

// Touch tap zones (exercise-bike integration, T3b.3; presentation only).  Drawn
// only while touch controls are on in single-player play: a dotted line where
// two zones meet and a small label at the inner edge of each zone, brighter
// while the zone is held (or auto-fire is on) and dim when unavailable.  Reads
// bike_touch_zone_info() and writes only the panel surfaces.
static void hud_draw_touch_zones(const HudPanel *left, const HudPanel *right)
{
	if (!bike_touch_zones_visible())
		return;

	for (int i = 0; i < bike_touch_zone_count(); ++i)
	{
		BikeTouchZone z;
		if (!bike_touch_zone_info(i, &z))
			continue;

		const HudPanel *p = z.right ? right : left;
		const int label_w = JE_textWidth(z.label, TINY_FONT);
		const int x = z.right ? HUD_MARGIN : p->w - HUD_MARGIN - label_w;
		const int y = z.y1 - 8;

		if (z.y0 > 0)
			for (int dx = 0; dx < p->w; dx += 2)
				fill_rectangle_xy(p->surface, dx, z.y0, dx, z.y0, HUD_SEP_COLOR);
		hud_text(p->surface, x, y, z.label, HUD_LABEL_BANK, z.dim ? HUD_LABEL_DIM : (z.lit ? HUD_VALUE_BRIGHT : 5));
	}
}

void modern_hud_draw(void)
{
	if (!modern_hud_in_panels())
		return;

	hud_preserve_tempw();

	const int w = modern_side_panel_width();
	const bool two = (twoPlayerMode && !galagaMode);

	if (two)
	{
		HudPanel p1 = { modern_hud_surface(0), w, false, w >= HUD_ROOMY_WIDTH };
		HudPanel p2 = { modern_hud_surface(1), w, true,  w >= HUD_ROOMY_WIDTH };

		// Player 2's panel carries the global cheat notice; player 1's the
		// timer (see hud_draw_compact_panel / modern_hud_draw_timer).
		hud_draw_compact_panel(&p1, 0, false);
		hud_draw_compact_panel(&p2, 1, true);
	}
	else
	{
		// 1 player: armament on the left, the ship status/vitals panel on the
		// right (where the original sidebar kept shield/armor/power), hugging
		// the outer screen edges.
		HudPanel left  = { modern_hud_surface(0), w, false, w >= HUD_ROOMY_WIDTH };
		HudPanel right = { modern_hud_surface(1), w, true,  w >= HUD_ROOMY_WIDTH };

		hud_draw_armament_panel(&left);
		hud_draw_status_panel(&right);
		hud_draw_touch_zones(&left, &right);
	}

	hud_draw_message_strip();
}

// The intro has no gameplay tick yet, so only the message strip (bar + level
// name) is drawn; the panels stay cleared.  modern_hud_draw() draws the same
// strip on the first gameplay frame, so the level name simply carries over.
void modern_hud_show_intro(void)
{
	if (!modern_hud_in_panels())
		return;

	hud_draw_message_strip();
}

void modern_hud_draw_timer(const char *label, const char *value, int brightness)
{
	if (!modern_hud_in_panels())
		return;

	SDL_Surface *surface = modern_hud_surface(0);
	const int w = modern_side_panel_width();
	const bool two = (twoPlayerMode && !galagaMode);

	if (two && hud_timer_inline(w))
	{
		// One row: label, then value, centred as a pair on the bottom rows.
		const int lw = JE_textWidth(label, TINY_FONT);
		const int vw = JE_textWidth(value, SMALL_FONT_SHAPES);
		int x = (w - (lw + HUD_TIMER_GAP + vw)) / 2;
		if (x < HUD_MARGIN)
			x = HUD_MARGIN;
		const int value_y = HUD_BOTTOM_Y - 8;
		HUD_ASSERT_FIT(surface, value_y, 8);
		JE_textShade(surface, x, value_y + 1, label, 7, brightness, FULL_SHADE);
		JE_dString(surface, x + lw + HUD_TIMER_GAP, value_y, value, SMALL_FONT_SHAPES);
		return;
	}

	// Stacked, the 2P timer's value ends on HUD_BOTTOM_Y (label 6 rows, gap 2,
	// value 8 rows).
	const int label_y = two ? HUD_BOTTOM_Y - 16 : AR_TIMER_LABEL_Y;
	const int value_y = two ? HUD_BOTTOM_Y - 8 : AR_TIMER_VALUE_Y;

	int x = (w - JE_textWidth(label, TINY_FONT)) / 2;
	if (x < HUD_MARGIN)
		x = HUD_MARGIN;
	HUD_ASSERT_FIT(surface, label_y, 8);
	JE_textShade(surface, x, label_y, label, 7, brightness, FULL_SHADE);

	x = (w - JE_textWidth(value, SMALL_FONT_SHAPES)) / 2;
	if (x < HUD_MARGIN)
		x = HUD_MARGIN;
	HUD_ASSERT_FIT(surface, value_y, 8);
	JE_dString(surface, x, value_y, value, SMALL_FONT_SHAPES);
}

// --- dynamic bar interpolation (stage 4) ------------------------------------

void modern_hud_begin_bars(void)
{
	hud_bars_prev_count = hud_bars_count;
	if (hud_bars_count > 0)
		memcpy(hud_bars_prev, hud_bars, sizeof(HudBar) * hud_bars_count);
	hud_bars_count = 0;
}

// Boss bar half: reproduces JE_barX() (tyrian2.c) on the panel surface, with a
// 16.16 value so the inner fill can sit between two ticks' armour values.
static void hud_boss_bar_draw(SDL_Surface *surface, int x1, int y1, int x2, int y2,
                              Uint8 color, Uint64 value_fx16)
{
	const int cx = (x1 + x2) / 2;
	const int lo = (int)(value_fx16 / (10ull << 16));                  // armor / 10
	const int hi = (int)((value_fx16 + (5ull << 16)) / (10ull << 16)); // (armor + 5) / 10

	// Trough (JE_barX colour 115).
	fill_rectangle_xy(surface, x1, y1, x2, y1, 116);
	fill_rectangle_xy(surface, x1, y1 + 1, x2, y2 - 1, 115);
	fill_rectangle_xy(surface, x1, y2, x2, y2, 114);

	// Value (JE_barX colour `color` = 118 + the bar's flash colour).
	fill_rectangle_xy(surface, cx - lo, y1, cx + hi, y1, (Uint8)(color + 1));
	fill_rectangle_xy(surface, cx - lo, y1 + 1, cx + hi, y2 - 1, color);
	fill_rectangle_xy(surface, cx - lo, y2, cx + hi, y2, (Uint8)(color - 1));
}

void modern_hud_record_boss_bar(SDL_Surface *surface, int x1, int y1, int x2, int y2,
                                Uint8 color, Uint32 value)
{
	if (!modern_hud_in_panels())
		return;

	HudBar *bar = hud_bar_record();
	if (bar != NULL)
	{
		bar->kind = HUD_BAR_BOSS;
		bar->surface = surface;
		bar->x1 = x1;
		bar->y1 = y1;
		bar->x2 = x2;
		bar->y2 = y2;
		bar->color = color;
		bar->value = value;
		bar->max = 0;
	}
}

unsigned modern_hud_draw_interpolated_bars(Uint32 alpha_fx16)
{
	if (!modern_hud_in_panels())
		return 0;

	if (alpha_fx16 > 65536)
		alpha_fx16 = 65536;

	unsigned drawn = 0;

	for (unsigned i = 0; i < hud_bars_count; ++i)
	{
		const HudBar *c = &hud_bars[i];
		const HudBar *p = hud_bar_find_prev(c);
		const Uint32 from = (p != NULL) ? p->value : c->value;
		const Uint64 fx = hud_lerp_fx(from, c->value, alpha_fx16);

		if (c->kind == HUD_BAR_VITALS)
			hud_vbar_draw(c->surface, c->x, c->y, c->w, c->h, fx, c->max, c->base);
		else
			hud_boss_bar_draw(c->surface, c->x1, c->y1, c->x2, c->y2, c->color, fx);

		drawn++;
	}

	return drawn;
}

void modern_hud_bar_interp_probe(unsigned long *moved, unsigned long *unchanged,
                                 unsigned long *out_of_range)
{
	for (unsigned i = 0; i < hud_bars_count; ++i)
	{
		const HudBar *c = &hud_bars[i];
		const HudBar *p = hud_bar_find_prev(c);

		if (p == NULL)
		{
			(*unchanged)++;
			continue;
		}

		const Uint32 lo = MIN(p->value, c->value);
		const Uint32 hi = MAX(p->value, c->value);

		// Fixed-point midpoint, then the integer value the presentation uses.
		const Uint64 fx = hud_lerp_fx(p->value, c->value, 32768);
		const Uint32 mid = (Uint32)(fx >> 16);

		if (mid < lo || mid > hi)
		{
			(*out_of_range)++;
		}
		else if (hi - lo >= 2 && mid > lo && mid < hi)
		{
			(*moved)++;
		}
		else
		{
			(*unchanged)++;
		}
	}
}

bool modern_hud_boss_target(int bar, bool two_player, SDL_Surface **surface, int *cx, int *y, int *half_width)
{
	if (!modern_hud_in_panels())
		return false;

	const int w = modern_side_panel_width();

	if (two_player)
	{
		*surface = modern_hud_surface(bar);
		*y = CP_BOSS_Y + hud_cp_shift(w);
	}
	else
	{
		*surface = modern_hud_surface(0);
		*y = AR_BOSS_Y + bar * AR_BOSS_STEP;
	}

	*cx = w / 2;
	*half_width = MIN(25, (w - 2) / 2);
	return true;
}
