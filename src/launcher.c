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
#include "bike_controls.h"
#include "launcher.h"

#include "file.h"
#include "installer.h"
#include "joystick.h"
#include "keyboard.h"
#include "launcher_art.h"
#include "logging.h"
#include "regress.h"
#include "opentyr.h"
#include "video.h"

#include <SDL3/SDL.h>

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// The launcher is drawn with the SDL renderer at the window's real pixel size,
// never through the 320x200 canvas, so the painted art stays sharp.  Everything
// is laid out on a 1920x1080 grid of "units" (each panel is 960x1080) and
// scaled by one rational factor, so the picture is the same at every size.
//
// Text is SDL's built-in 8x8 debug font at an integer scale, because it needs
// no game data (the game's own fonts live in the data files, and this screen
// must work before any data is chosen or exists), no extra library and no font
// file, and an integer scale keeps every glyph pixel a whole number of screen
// pixels, so it stays crisp on a Steam Deck.  The scale is picked per text
// block to be as large as fits, never below 1.
//
// Drawing uses only integer rectangles, lines and nearest/linear texture
// blits, so the same frame comes out of the software renderer bit for bit: the
// regress mode hashes it.
// ---------------------------------------------------------------------------

#define UNITS_W 1920
#define UNITS_H 1080
#define PANEL_W 960
#define GLYPH 8

typedef struct { Uint8 r, g, b; } Rgb;

static const Rgb colorWhite = { 236, 242, 252 };
static const Rgb colorMuted = { 168, 184, 208 };
static const Rgb colorBlack = { 0, 0, 0 };
static const Rgb colorPanelDark = { 5, 10, 22 };
static const Rgb colorPanelDark2000 = { 16, 6, 9 };
static const Rgb colorBox = { 7, 10, 18 };

static const Rgb panelColor[2] = { { 70, 170, 255 }, { 255, 100, 45 } };

// What detection found about the Tyrian 2000 data.
typedef struct
{
	GameDataStatus status;    // result of locating and validating the data
	char detail[256];         // why the data cannot be used ("" if not found)
	char suggested_dir[512];  // a folder named tyrian2000 beside the executable
} Launcher2000Data;

typedef struct
{
	SDL_Texture *panel[2];
	SDL_Texture *title[2];
} LauncherTextures;

typedef enum
{
	OVERLAY_NONE,
	OVERLAY_ABOUT,
	OVERLAY_MESSAGE,
	OVERLAY_INSTALL,    // how to install Tyrian 2000
	OVERLAY_PROGRESS,   // an install is running
	OVERLAY_RESULT,     // it succeeded or failed
	OVERLAY_MANUAL      // where to put the files when no file dialog exists
} LauncherOverlay;

typedef enum
{
	ACT_NONE,
	ACT_DOWNLOAD,
	ACT_ZIP,
	ACT_FOLDER,
	ACT_BACK,
	ACT_CANCEL,
	ACT_MENU,
	ACT_PLAY,
	ACT_SCAN
} LauncherAction;

#define DIALOG_BUTTONS 4

typedef struct
{
	GameVariant selected;
	int focus;              // 0: the panels, 1: the bottom bar
	int bar_selected;       // 0: About, 1: Exit
	bool installed2000;
	LauncherOverlay overlay;
	char message[600];
	char version[64];
	char data21[512];
	char data2000[512];
	char user_dir[512];
	int scroll_max;         // modal line limit from the current layout
	int scroll;             // first visible modal line
	int pulse;              // 0..255 glow phase; 0 in regress frames

	// The install dialogs.
	bool dialogs;           // the OS can show a file or folder picker
	bool fixed;             // regress: never ask the system about files
	bool skip_validate;     // headless flow: the synthetic data cannot pass the game's validation
	char title[48];
	char body[1600];
	int button_count, button_selected;
	LauncherAction button_action[DIALOG_BUTTONS];
	const char *button_label[DIALOG_BUTTONS];
	SDL_Rect button_rect[DIALOG_BUTTONS];   // where the last frame drew them (mouse)
	InstallerProgress progress;
	bool job;               // an install is running
	bool cancelling;
	LauncherAction job_kind;
	bool picker_wait;       // a file dialog is open
	LauncherAction picker_kind;
	char detail2000[256];   // why the data found could not be used
	char suggested_dir[512];
	char install_dir[INSTALLER_PATH_MAX];
	InstallerSuggestion found[4];
	int found_count;
} LauncherView;

typedef struct
{
	int width, height;
	SDL_Rect comp;          // the composition: at most 2:1, at least 16:10
	int num, den;           // pixels per unit = num / den
	SDL_Rect panel[2];      // the two halves of the composition
	int origin_x[2], origin_y;
} LauncherLayout;

// ---------------------------------------------------------------------------
// Layout

// Units to pixels, rounded to nearest.
#define U(l, v) (((v) * (l)->num * 2 + (l)->den) / (2 * (l)->den))

static void layoutCompute(LauncherLayout *l, int w, int h)
{
	l->width = w;
	l->height = h;

	int cw = w, ch = h;
	if (cw > 2 * ch)
		cw = 2 * ch;               // ultrawide: the art stops at 2:1, dark bands beyond
	if (cw * 10 < ch * 16)
		ch = cw * 10 / 16;         // narrower than 16:10: letterbox

	l->comp = (SDL_Rect) { (w - cw) / 2, (h - ch) / 2, cw, ch };

	if (ch * UNITS_W <= cw * UNITS_H)
	{
		l->num = ch;
		l->den = UNITS_H;
	}
	else
	{
		l->num = cw;
		l->den = UNITS_W;
	}

	l->panel[0] = (SDL_Rect) { l->comp.x, l->comp.y, cw / 2, ch };
	l->panel[1] = (SDL_Rect) { l->comp.x + cw / 2, l->comp.y, cw - cw / 2, ch };

	for (int i = 0; i < 2; ++i)
		l->origin_x[i] = l->panel[i].x + (l->panel[i].w - U(l, PANEL_W)) / 2;
	l->origin_y = l->comp.y + (ch - U(l, UNITS_H)) / 2;
}

static SDL_Rect panelRect(const LauncherLayout *l, int panel, int x, int y, int w, int h)
{
	return (SDL_Rect) { l->origin_x[panel] + U(l, x), l->origin_y + U(l, y), U(l, w), U(l, h) };
}

static SDL_Rect titleRect(const LauncherLayout *l, int panel)    { return panelRect(l, panel, 120, 24, 720, 288); }
static SDL_Rect featureRect(const LauncherLayout *l, int panel)  { return panelRect(l, panel, 60, 596, 840, 250); }
static SDL_Rect buttonRect(const LauncherLayout *l, int panel)   { return panelRect(l, panel, 60, 876, 840, 116); }

// The bottom bar is centred on the seam between the panels.
static SDL_Rect barButtonRect(const LauncherLayout *l, int which)
{
	const int cx = l->comp.x + l->comp.w / 2;
	const int w = U(l, 240);
	return (SDL_Rect) { which == 0 ? cx - U(l, 260) : cx + U(l, 20), l->origin_y + U(l, 1016), w, U(l, 48) };
}

static bool rectContains(const SDL_Rect *r, int x, int y)
{
	return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

// What is under a pixel: 0/1 a panel, 2/3 About/Exit, -1 nothing.
static int layoutHit(const LauncherLayout *l, int x, int y)
{
	for (int i = 0; i < 2; ++i)
	{
		const SDL_Rect bar = barButtonRect(l, i);
		if (rectContains(&bar, x, y))
			return 2 + i;
	}
	for (int i = 0; i < 2; ++i)
	{
		if (rectContains(&l->panel[i], x, y))
			return i;
	}
	return -1;
}

// ---------------------------------------------------------------------------
// Primitives

static void setColor(SDL_Renderer *r, Rgb c, int a)
{
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(r, c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 255 ? 255 : a));
}

static void fillRect(SDL_Renderer *r, int x, int y, int w, int h, Rgb c, int a)
{
	if (w <= 0 || h <= 0 || a <= 0)
		return;
	setColor(r, c, a);
	const SDL_FRect f = { (float)x, (float)y, (float)w, (float)h };
	SDL_RenderFillRect(r, &f);
}

static void fillBox(SDL_Renderer *r, SDL_Rect b, Rgb c, int a)
{
	fillRect(r, b.x, b.y, b.w, b.h, c, a);
}

static void strokeRect(SDL_Renderer *r, SDL_Rect b, int t, Rgb c, int a)
{
	if (t < 1)
		t = 1;
	fillRect(r, b.x, b.y, b.w, t, c, a);
	fillRect(r, b.x, b.y + b.h - t, b.w, t, c, a);
	fillRect(r, b.x, b.y + t, t, b.h - 2 * t, c, a);
	fillRect(r, b.x + b.w - t, b.y + t, t, b.h - 2 * t, c, a);
}

// Soft outward glow: `layers` frames of `step` pixels, fading with distance.
static void glowRect(SDL_Renderer *r, SDL_Rect b, int step, int layers, Rgb c, int peak)
{
	if (step < 1)
		step = 1;
	for (int i = 0; i < layers; ++i)
	{
		const int fade = layers - i;
		const int a = peak * fade * fade / (layers * layers);
		const SDL_Rect g = { b.x - (i + 1) * step, b.y - (i + 1) * step, b.w + 2 * (i + 1) * step, b.h + 2 * (i + 1) * step };
		strokeRect(r, g, step, c, a);
	}
}

// The same glow, running inward from the edge of `b`.
static void innerGlowRect(SDL_Renderer *r, SDL_Rect b, int step, int layers, Rgb c, int peak)
{
	if (step < 1)
		step = 1;
	for (int i = 0; i < layers; ++i)
	{
		const int fade = layers - i;
		const int a = peak * fade * fade / (layers * layers);
		const SDL_Rect g = { b.x + i * step, b.y + i * step, b.w - 2 * i * step, b.h - 2 * i * step };
		strokeRect(r, g, step, c, a);
	}
}

// A dark-to-clear strip, `size` pixels deep, along one edge of `b`.
// side: 0 left, 1 right, 2 top, 3 bottom (the edge that is dark).
static void fadeEdge(SDL_Renderer *r, SDL_Rect b, int side, int size, Rgb c)
{
	for (int i = 0; i < size; ++i)
	{
		const int fade = size - i;
		const int a = 255 * fade * fade / (size * size);
		switch (side)
		{
		case 0: fillRect(r, b.x + i, b.y, 1, b.h, c, a); break;
		case 1: fillRect(r, b.x + b.w - 1 - i, b.y, 1, b.h, c, a); break;
		case 2: fillRect(r, b.x, b.y + i, b.w, 1, c, a); break;
		default: fillRect(r, b.x, b.y + b.h - 1 - i, b.w, 1, c, a); break;
		}
	}
}

// ---------------------------------------------------------------------------
// Text

static const char enDash[] = "\xE2\x80\x93";

static int utf8Count(const char *s, size_t len)
{
	int n = 0;
	for (size_t i = 0; i < len; ++i)
	{
		if (((unsigned char)s[i] & 0xC0) != 0x80)
			++n;
	}
	return n;
}

static int textChars(const char *s)
{
	return utf8Count(s, strlen(s));
}

// The largest integer scale for text of about `size` units whose `chars`
// characters still fit in `max_w` pixels.
static int textScale(const LauncherLayout *l, int size, int chars, int max_w)
{
	int s = (U(l, size) + GLYPH / 2) / GLYPH;
	if (s < 1)
		s = 1;
	if (s > 8)
		s = 8;
	while (s > 1 && chars * GLYPH * s > max_w)
		--s;
	return s;
}

static void textPass(SDL_Renderer *r, int x, int y, int s, Rgb c, int a, const char *text)
{
	x = x / s * s;
	y = y / s * s;

	const char *p = text;
	while (*p != '\0')
	{
		const char *q = p;
		while (*q != '\0' && strncmp(q, enDash, 3) != 0)
			++q;

		if (q > p)
		{
			char piece[256];
			size_t n = (size_t)(q - p);
			if (n > sizeof piece - 1)
				n = sizeof piece - 1;
			memcpy(piece, p, n);
			piece[n] = '\0';

			setColor(r, c, a);
			SDL_SetRenderScale(r, (float)s, (float)s);
			SDL_RenderDebugText(r, (float)(x / s), (float)(y / s), piece);
			SDL_SetRenderScale(r, 1.0f, 1.0f);
			x += utf8Count(piece, n) * GLYPH * s;
		}

		if (*q != '\0')
		{
			// The debug font has no en dash: draw one across the whole cell.
			fillRect(r, x + s, y + 3 * s, (GLYPH - 2) * s, s, c, a);
			x += GLYPH * s;
			q += 3;
		}
		p = q;
	}
}

static void drawText(SDL_Renderer *r, int x, int y, int s, Rgb c, int a, const char *text)
{
	textPass(r, x + s, y + s, s, colorBlack, a * 3 / 5, text);
	textPass(r, x, y, s, c, a, text);
}

static void drawTextCentered(SDL_Renderer *r, SDL_Rect box, int y, int s, Rgb c, int a, const char *text)
{
	drawText(r, box.x + (box.w - textChars(text) * GLYPH * s) / 2, y, s, c, a, text);
}

#define MAX_LINES 100
#define LINE_BYTES 256

// Word-wraps `text` to `max_chars` characters per line; long words (a path)
// are broken.  Returns the number of lines.
static int wrapText(const char *text, int max_chars, char lines[][LINE_BYTES], int max_lines)
{
	if (max_chars < 8)
		max_chars = 8;
	if (max_chars > 60)
		max_chars = 60;

	int n = 0;
	size_t bytes = 0;
	int chars = 0;
	char line[LINE_BYTES] = "";

#define COMMIT() do { if (n < max_lines) { memcpy(lines[n], line, bytes + 1); ++n; } bytes = 0; chars = 0; line[0] = '\0'; } while (0)

	const char *p = text;
	while (*p != '\0')
	{
		if (*p == '\n')
		{
			COMMIT();
			++p;
			continue;
		}

		const char *w = p;
		while (*w != '\0' && *w != ' ' && *w != '\n')
			++w;
		const int word_chars = utf8Count(p, (size_t)(w - p));

		if (chars > 0 && chars + 1 + word_chars > max_chars)
			COMMIT();

		if (word_chars > max_chars)
		{
			for (const char *c = p; c < w; )
			{
				const char *e = c + 1;
				while (e < w && ((unsigned char)*e & 0xC0) == 0x80)
					++e;
				if (chars == max_chars || bytes + (size_t)(e - c) >= LINE_BYTES - 1)
					COMMIT();
				memcpy(line + bytes, c, (size_t)(e - c));
				bytes += (size_t)(e - c);
				line[bytes] = '\0';
				++chars;
				c = e;
			}
		}
		else
		{
			if (chars > 0 && bytes + 1 < LINE_BYTES - 1)
			{
				line[bytes++] = ' ';
				++chars;
			}
			size_t len = (size_t)(w - p);
			if (bytes + len >= LINE_BYTES - 1)
				len = LINE_BYTES - 2 - bytes;
			memcpy(line + bytes, p, len);
			bytes += len;
			line[bytes] = '\0';
			chars += word_chars;
		}

		p = w;
		while (*p == ' ')
			++p;
	}
	if (chars > 0)
		COMMIT();

#undef COMMIT
	return n;
}

// ---------------------------------------------------------------------------
// Icons: drawn from rectangles, in a square of `s` pixels at (x, y).

typedef enum
{
	ICON_BOOK,
	ICON_MONITOR,
	ICON_GEM,
	ICON_SHIP,
	ICON_CLOCK
} IconKind;

static int isqrt(int v)
{
	int x = 0;
	while ((x + 1) * (x + 1) <= v)
		++x;
	return x;
}

static void drawIcon(SDL_Renderer *r, IconKind kind, int x, int y, int s, Rgb c)
{
	int t = s / 12;
	if (t < 1)
		t = 1;

	switch (kind)
	{
	case ICON_BOOK:
	{
		const int pw = s * 46 / 100, top = s * 16 / 100, ph = s * 68 / 100;
		fillRect(r, x, y + top, pw, ph, c, 235);
		fillRect(r, x + s - pw, y + top, pw, ph, c, 235);
		for (int i = 0; i < 3; ++i)
		{
			const int ly = y + top + ph * (2 + i * 3) / 12;
			fillRect(r, x + pw / 5, ly, pw * 3 / 5, t, colorBox, 210);
			fillRect(r, x + s - pw + pw / 5, ly, pw * 3 / 5, t, colorBox, 210);
		}
		break;
	}
	case ICON_MONITOR:
	{
		const SDL_Rect screen = { x, y + s * 14 / 100, s, s * 58 / 100 };
		strokeRect(r, screen, t + t / 2, c, 240);
		fillRect(r, x + screen.w / 4, y + screen.h * 3 / 10 + s * 14 / 100, screen.w / 2, t, c, 140);
		fillRect(r, x + s * 42 / 100, y + s * 72 / 100, s * 16 / 100, s * 12 / 100, c, 220);
		fillRect(r, x + s * 26 / 100, y + s * 84 / 100, s * 48 / 100, t, c, 240);
		break;
	}
	case ICON_GEM:
	{
		const int half = s / 2;
		for (int row = 0; row < s; ++row)
		{
			const int d = row < half ? row : s - 1 - row;
			const int inner = d - t * 2;
			const int wid = d + 1;
			fillRect(r, x + half - wid, y + row, 2 * wid, 1, c, 240);
			if (inner > 0)
				fillRect(r, x + half - inner, y + row, 2 * inner, 1, colorBox, 215);
		}
		break;
	}
	case ICON_SHIP:
	{
		// A fighter seen from above: tapered fuselage, swept wings, an engine.
		const int cx = x + s / 2;
		const int body = s / 8 > 1 ? s / 8 : 1;
		const int rows = s * 88 / 100;
		const int wing0 = s * 40 / 100, wing_rows = s * 44 / 100 > 1 ? s * 44 / 100 : 1;
		const int wing_t = s * 20 / 100 > 1 ? s * 20 / 100 : 1;
		for (int row = 0; row < rows; ++row)
		{
			int half = row < s / 5 ? row * body / (s / 5 + 1) : body;
			fillRect(r, cx - half, y + row, 2 * half + 1, 1, c, 240);
			if (row >= wing0 && row < wing0 + wing_rows)
			{
				const int out = body + (row - wing0) * (s / 2 - body) / wing_rows;
				const int in = out - wing_t > body ? out - wing_t : body;
				fillRect(r, cx - out, y + row, out - in + 1, 1, c, 240);
				fillRect(r, cx + in, y + row, out - in + 1, 1, c, 240);
			}
		}
		fillRect(r, cx - body / 2, y + rows, body, s - rows, colorWhite, 220);
		break;
	}
	case ICON_CLOCK:
	{
		const int radius = s / 2 - 1;
		const int cx = x + s / 2, cy = y + s / 2;
		const int inner = radius - t - t / 2;
		for (int dy = -radius; dy <= radius; ++dy)
		{
			const int xo = isqrt(radius * radius - dy * dy);
			if (dy > -inner && dy < inner)
			{
				const int xi = isqrt(inner * inner - dy * dy);
				fillRect(r, cx - xo, cy + dy, xo - xi, 1, c, 240);
				fillRect(r, cx + xi, cy + dy, xo - xi, 1, c, 240);
			}
			else
			{
				fillRect(r, cx - xo, cy + dy, 2 * xo + 1, 1, c, 240);
			}
		}
		fillRect(r, cx - t / 2, cy - inner * 3 / 4, t, inner * 3 / 4 + t / 2 + 1, c, 240);
		fillRect(r, cx - t / 2, cy - t / 2, inner * 2 / 3, t, c, 240);
		break;
	}
	}
}

// The PLAY triangle, or the INSTALL arrow, `s` pixels tall.
static void drawPlayGlyph(SDL_Renderer *r, int x, int y, int s, Rgb c, int a)
{
	for (int row = 0; row < s; ++row)
	{
		const int d = row < s / 2 ? row : s - 1 - row;
		fillRect(r, x, y + row, d * 16 / 10 + 1, 1, c, a);
	}
}

static void drawInstallGlyph(SDL_Renderer *r, int x, int y, int s, Rgb c, int a)
{
	const int stem = s * 3 / 10;
	fillRect(r, x + (s - stem) / 2, y, stem, s * 5 / 10, c, a);
	const int head = s * 4 / 10 > 1 ? s * 4 / 10 : 1;
	for (int row = 0; row < head; ++row)
	{
		const int wid = s - row * s / head;
		fillRect(r, x + (s - wid) / 2, y + s * 5 / 10 + row, wid, 1, c, a);
	}
	fillRect(r, x, y + s * 95 / 100, s, s / 12 + 1, c, a);
}

// ---------------------------------------------------------------------------
// Drawing

typedef struct
{
	IconKind icon;
	const char *text;
} Feature;

// Only what exists.  The en dash is drawn by textPass().
static const Feature features[2][3] =
{
	{
		{ ICON_BOOK, "Episodes 1" "\xE2\x80\x93" "4" },
		{ ICON_MONITOR, "Modern widescreen & lighting" },
		{ ICON_GEM, "The original freeware classic" }
	},
	{
		{ ICON_BOOK, "Episodes 1" "\xE2\x80\x93" "5" },
		{ ICON_SHIP, "New ships & weapons" },
		{ ICON_CLOCK, "Timed Battle" }
	}
};

static void drawCover(SDL_Renderer *r, SDL_Texture *tex, SDL_Rect dst)
{
	float tw = 0, th = 0;
	if (tex == NULL || !SDL_GetTextureSize(tex, &tw, &th) || tw <= 0 || th <= 0)
		return;

	const float sx = (float)dst.w / tw, sy = (float)dst.h / th;
	const float scale = sx > sy ? sx : sy;
	const float sw = (float)dst.w / scale, sh = (float)dst.h / scale;
	const SDL_FRect src = { (tw - sw) / 2, (th - sh) / 4, sw, sh };
	const SDL_FRect d = { (float)dst.x, (float)dst.y, (float)dst.w, (float)dst.h };
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
	SDL_RenderTexture(r, tex, &src, &d);
}

static Rgb tint(Rgb c, int percent)
{
	return (Rgb) { (Uint8)(c.r * percent / 100), (Uint8)(c.g * percent / 100), (Uint8)(c.b * percent / 100) };
}

static void drawPanelUi(SDL_Renderer *r, const LauncherLayout *l, const LauncherTextures *tex,
                        const LauncherView *v, int i, int feature_scale)
{
	const bool selected = (int)v->selected == i;
	const Rgb c = panelColor[i];

	// Title.
	if (tex->title[i] != NULL)
	{
		const SDL_Rect t = titleRect(l, i);
		const SDL_FRect d = { (float)t.x, (float)t.y, (float)t.w, (float)t.h };
		SDL_SetTextureBlendMode(tex->title[i], SDL_BLENDMODE_BLEND);
		SDL_SetTextureAlphaMod(tex->title[i], selected ? 255 : 190);
		SDL_RenderTexture(r, tex->title[i], NULL, &d);
	}

	// Feature box.
	const SDL_Rect fb = featureRect(l, i);
	fillBox(r, fb, colorBox, selected ? 178 : 150);
	strokeRect(r, fb, U(l, 2) > 0 ? U(l, 2) : 1, c, selected ? 150 : 80);

	const int row_h = fb.h / 3;
	const int icon = U(l, 44);
	for (int k = 0; k < 3; ++k)
	{
		const int cy = fb.y + row_h * k + row_h / 2;
		drawIcon(r, features[i][k].icon, fb.x + U(l, 30), cy - icon / 2, icon, tint(c, selected ? 100 : 75));
		drawText(r, fb.x + U(l, 30) + icon + U(l, 26), cy - feature_scale * GLYPH / 2, feature_scale,
		         colorWhite, selected ? 255 : 200, features[i][k].text);
	}

	// Button.
	const SDL_Rect b = buttonRect(l, i);
	const bool install = i == 1 && !v->installed2000;
	const int edge = U(l, 3) > 0 ? U(l, 3) : 1;
	if (selected)
	{
		const int step = U(l, 4) > 0 ? U(l, 4) : 1;
		glowRect(r, b, step, 9, c, 150 + v->pulse * 90 / 255);
		fillBox(r, b, tint(c, 22), 235);
		strokeRect(r, b, edge, c, 255);
		strokeRect(r, (SDL_Rect) { b.x + edge * 2, b.y + edge * 2, b.w - edge * 4, b.h - edge * 4 }, 1, c, 110);
	}
	else
	{
		fillBox(r, b, colorBox, 205);
		strokeRect(r, b, edge > 1 ? edge - 1 : 1, c, 120);
	}

	const char *label = install ? "INSTALL" : "PLAY";
	const char *sub = i == 0 ? "TYRIAN 2.1 FREEWARE" : install ? "DATA NOT INSTALLED" : "TYRIAN 2000";
	const int label_scale = textScale(l, 40, textChars(label) + 3, b.w * 6 / 10);
	const int sub_scale = textScale(l, 24, textChars(sub), b.w * 8 / 10);
	const int gap = U(l, 10);
	const int block = label_scale * GLYPH + gap + sub_scale * GLYPH;
	const int top = b.y + (b.h - block) / 2;
	const int glyph = label_scale * GLYPH;
	const int label_w = textChars(label) * GLYPH * label_scale;
	const int total = glyph + label_scale * GLYPH + label_w;
	const int left = b.x + (b.w - total) / 2;
	const Rgb label_color = selected ? colorWhite : colorMuted;
	const int alpha = selected ? 255 : 190;

	if (install)
		drawInstallGlyph(r, left, top, glyph, c, alpha);
	else
		drawPlayGlyph(r, left + glyph / 6, top, glyph, c, alpha);
	drawText(r, left + glyph + label_scale * GLYPH, top, label_scale, label_color, alpha, label);
	drawTextCentered(r, b, top + label_scale * GLYPH + gap, sub_scale, selected ? c : colorMuted, selected ? 255 : 170, sub);
}

static void drawBottomBar(SDL_Renderer *r, const LauncherLayout *l, const LauncherView *v)
{
	const SDL_Rect strip = { l->comp.x, l->origin_y + U(l, 1004), l->comp.w, l->comp.h - (l->origin_y - l->comp.y) - U(l, 1004) };
	fillBox(r, strip, colorBlack, 150);
	fillRect(r, strip.x, strip.y, strip.w, 1, colorMuted, 60);

	static const char *const names[2] = { "ABOUT", "EXIT" };
	for (int k = 0; k < 2; ++k)
	{
		const SDL_Rect b = barButtonRect(l, k);
		const bool focused = v->focus == 1 && v->bar_selected == k && v->overlay == OVERLAY_NONE;
		fillBox(r, b, colorBox, focused ? 235 : 190);
		if (focused)
			glowRect(r, b, U(l, 3) > 0 ? U(l, 3) : 1, 5, colorWhite, 120);
		strokeRect(r, b, focused ? (U(l, 3) > 0 ? U(l, 3) : 1) : 1, focused ? colorWhite : colorMuted, focused ? 255 : 110);
		const int s = textScale(l, 26, textChars(names[k]), b.w * 8 / 10);
		drawTextCentered(r, b, b.y + (b.h - s * GLYPH) / 2, s, focused ? colorWhite : colorMuted, focused ? 255 : 210, names[k]);
	}

	// The version, discreet, on the left; a controls hint on the right if it fits.
	const int margin = U(l, 28);
	const SDL_Rect first = barButtonRect(l, 0), second = barButtonRect(l, 1);
	const int left_w = first.x - (l->comp.x + margin) - margin;
	const int vs = textScale(l, 20, textChars(v->version), left_w);
	drawText(r, l->comp.x + margin, strip.y + (strip.h - vs * GLYPH) / 2, vs, colorMuted, 150, v->version);

	static const char hint[] = "ENTER: PLAY   ESC: EXIT";
	const int right_x = second.x + second.w + margin;
	const int right_w = l->comp.x + l->comp.w - margin - right_x;
	const int hs = textScale(l, 20, textChars(hint), right_w);
	if (hs >= 2)
		drawText(r, l->comp.x + l->comp.w - margin - textChars(hint) * GLYPH * hs, strip.y + (strip.h - hs * GLYPH) / 2,
		         hs, colorMuted, 150, hint);
}

static void drawModal(SDL_Renderer *r, const LauncherLayout *l, LauncherView *v)
{
	const Rgb accent = panelColor[v->selected];
	fillBox(r, l->comp, colorBlack, 165);

	char lines[MAX_LINES][LINE_BYTES];
	char text[4096];
	const char *title;
	if (v->overlay == OVERLAY_ABOUT)
	{
		title = "ABOUT";
		snprintf(text, sizeof text,
		         "%s\n\n"
		         "Tyrian was made by Eclipse Software and published by Epic MegaGames in 1995. Tyrian 2.1 is freeware.\n\n"
		         "Based on OpenTyrian by the OpenTyrian Development Team, free software under the GNU GPL v2 or later. "
		         "Tyrian 2000 support follows OpenTyrian2000 (GPL v2). Nuked-OPL3 is under the LGPL v2.1 or later.\n\n"
		         "Tyrian 2.1 data: %s\n"
		         "Tyrian 2000 data: %s\n"
		         "Saves and settings: %s",
		         v->version, v->data21, v->data2000, v->user_dir);
	}
	else
	{
		title = "GAME DATA";
		snprintf(text, sizeof text, "%s", v->message);
	}

	const int box_w = U(l, 1360);
	const int pad = U(l, 40);
	const int body_size = 26;
	const int s = textScale(l, body_size, 1, 1 << 20);
	int max_chars = (box_w - 2 * pad) / (GLYPH * s);
	if (max_chars > 60)
		max_chars = 60;
	const int n = wrapText(text, max_chars, lines, MAX_LINES);

	const int line_h = s * GLYPH * 3 / 2;
	const int title_s = textScale(l, 40, textChars(title), box_w / 2);
	const int fixed_h = pad + title_s * GLYPH + pad / 2 + pad / 2 + s * GLYPH + pad;
	int visible = (l->comp.h - 2 * pad - fixed_h) / line_h;
	if (visible < 1)
		visible = 1;
	if (visible > n)
		visible = n;
	v->scroll_max = n - visible;
	if (v->scroll > v->scroll_max)
		v->scroll = v->scroll_max;
	const int first = v->scroll;
	const int box_h = fixed_h + visible * line_h;
	const SDL_Rect box = { l->comp.x + (l->comp.w - box_w) / 2, l->comp.y + (l->comp.h - box_h) / 2, box_w, box_h };
	glowRect(r, box, U(l, 4) > 0 ? U(l, 4) : 1, 6, accent, 110);
	fillBox(r, box, colorBox, 246);
	strokeRect(r, box, U(l, 3) > 0 ? U(l, 3) : 1, accent, 255);

	int y = box.y + pad;
	drawText(r, box.x + pad, y, title_s, accent, 255, title);
	y += title_s * GLYPH + pad / 2;
	for (int k = first; k < first + visible; ++k, y += line_h)
		drawText(r, box.x + pad, y, s, colorWhite, 240, lines[k]);
	drawText(r, box.x + pad, box.y + box.h - pad - s * GLYPH, s, colorMuted, 200, visible < n ? "Up/Down: scroll  Enter/click: close" : "Press Enter or click to close");
}

// Size in megabytes with one decimal, for the progress line.
static void formatMegabytes(char *out, size_t size, uint64_t bytes)
{
	const uint64_t tenths = bytes * 10 / (1024 * 1024);
	snprintf(out, size, "%" PRIu64 ".%" PRIu64 " MB", tenths / 10, tenths % 10);
}

// The install dialogs: a title, wrapped text, for a running install a progress
// bar, and a column of buttons.  The button rectangles are kept for the mouse.
static void drawDialog(SDL_Renderer *r, const LauncherLayout *l, LauncherView *v)
{
	const Rgb accent = panelColor[VARIANT_TYRIAN2000];
	const bool progress = v->overlay == OVERLAY_PROGRESS;
	fillBox(r, l->comp, colorBlack, 165);

	const int box_w = U(l, 1360);
	const int pad = U(l, 40);

	char text[1700];
	if (progress)
		snprintf(text, sizeof text, "%s", v->cancelling ? "Cancelling..." : v->progress.message);
	else
		snprintf(text, sizeof text, "%s", v->body);

	char lines[MAX_LINES][LINE_BYTES];
	int s = textScale(l, 26, 1, 1 << 20);
	int n, line_h, title_s, bar_h, button_h, gap, box_h;
	for (;;)
	{
		int max_chars = (box_w - 2 * pad) / (GLYPH * s);
		if (max_chars > 60)
			max_chars = 60;
		n = wrapText(text, max_chars, lines, MAX_LINES);
		line_h = s * GLYPH * 3 / 2;
		title_s = textScale(l, 40, textChars(v->title), box_w / 2);
		bar_h = s * GLYPH * 3 / 2;
		button_h = s * GLYPH * 5 / 2;
		gap = button_h / 4;
		box_h = pad + title_s * GLYPH + pad / 2 + n * line_h + pad / 2;
		if (progress)
			box_h += bar_h + line_h + pad / 2;
		if (v->button_count > 0)
			box_h += v->button_count * (button_h + gap) - gap + pad / 2;
		box_h += pad / 2;
		if (box_h <= l->comp.h || s == 1)
			break;
		--s;
	}

	const SDL_Rect box = { l->comp.x + (l->comp.w - box_w) / 2, l->comp.y + (l->comp.h - box_h) / 2, box_w, box_h };
	const int edge = U(l, 3) > 0 ? U(l, 3) : 1;
	glowRect(r, box, U(l, 4) > 0 ? U(l, 4) : 1, 6, accent, 110);
	fillBox(r, box, colorBox, 246);
	strokeRect(r, box, edge, accent, 255);

	int y = box.y + pad;
	drawText(r, box.x + pad, y, title_s, accent, 255, v->title);
	y += title_s * GLYPH + pad / 2;
	for (int k = 0; k < n; ++k, y += line_h)
		drawText(r, box.x + pad, y, s, colorWhite, 240, lines[k]);
	y += pad / 2;

	if (progress)
	{
		const InstallerProgress *p = &v->progress;
		const SDL_Rect bar = { box.x + pad, y, box_w - 2 * pad, bar_h };
		fillBox(r, bar, colorBlack, 220);
		if (p->bytesTotal > 0)
		{
			uint64_t done = p->bytesDone < p->bytesTotal ? p->bytesDone : p->bytesTotal;
			fillRect(r, bar.x + edge, bar.y + edge, (int)((uint64_t)(bar.w - 2 * edge) * done / p->bytesTotal),
			         bar.h - 2 * edge, accent, 230);
		}
		else
		{
			// No byte count in this step: a block sliding along the bar.
			const int seg = (bar.w - 2 * edge) / 5;
			const int at = (bar.w - 2 * edge - seg) * v->pulse / 255;
			fillRect(r, bar.x + edge + at, bar.y + edge, seg, bar.h - 2 * edge, accent, 230);
		}
		strokeRect(r, bar, edge, colorMuted, 200);
		y += bar_h + line_h / 4;

		char counts[80] = "";
		if (p->bytesTotal > 0)
		{
			char a[24], b[24];
			formatMegabytes(a, sizeof a, p->bytesDone < p->bytesTotal ? p->bytesDone : p->bytesTotal);
			formatMegabytes(b, sizeof b, p->bytesTotal);
			snprintf(counts, sizeof counts, "%s / %s   %d%%", a, b,
			         (int)(p->bytesDone < p->bytesTotal ? p->bytesDone * 100 / p->bytesTotal : 100));
		}
		drawText(r, box.x + pad, y, s, colorMuted, 230, counts);
		y += line_h + pad / 2;
	}

	for (int k = 0; k < v->button_count; ++k, y += button_h + gap)
	{
		const SDL_Rect b = { box.x + pad, y, box_w - 2 * pad, button_h };
		const bool focused = k == v->button_selected;
		v->button_rect[k] = b;
		if (focused)
		{
			glowRect(r, b, U(l, 3) > 0 ? U(l, 3) : 1, 5, accent, 130 + v->pulse * 60 / 255);
			fillBox(r, b, tint(accent, 22), 240);
			strokeRect(r, b, edge, accent, 255);
		}
		else
		{
			fillBox(r, b, colorBox, 205);
			strokeRect(r, b, 1, colorMuted, 120);
		}
		drawTextCentered(r, b, b.y + (b.h - s * GLYPH) / 2, s, focused ? colorWhite : colorMuted, focused ? 255 : 210,
		                 v->button_label[k]);
	}
}

static void launcherDraw(SDL_Renderer *r, const LauncherTextures *tex, LauncherView *v, int w, int h)
{
	// Reset the viewport after asynchronous window changes before the first
	// draw, rather than inheriting a renderer projection from the old size.
	SDL_SetRenderViewport(r, NULL);
	SDL_SetRenderScale(r, 1.0f, 1.0f);
	LauncherLayout l;
	layoutCompute(&l, w, h);

	// The whole output, in each panel's darkest colour, for the bands the art
	// does not cover (letterbox, ultrawide).
	fillRect(r, 0, 0, w / 2, h, colorPanelDark, 255);
	fillRect(r, w / 2, 0, w - w / 2, h, colorPanelDark2000, 255);

	for (int i = 0; i < 2; ++i)
	{
		drawCover(r, tex->panel[i], l.panel[i]);
		if ((int)v->selected != i)
			fillBox(r, l.panel[i], colorBlack, 120);
	}

	// Soften the edge of the art where a band starts.
	const int fade = U(&l, 150);
	if (l.comp.x > 0)
	{
		fadeEdge(r, l.panel[0], 0, fade, colorPanelDark);
		fadeEdge(r, l.panel[1], 1, fade, colorPanelDark2000);
	}
	if (l.comp.y > 0)
	{
		fadeEdge(r, l.panel[0], 2, fade, colorPanelDark);
		fadeEdge(r, l.panel[0], 3, fade, colorPanelDark);
		fadeEdge(r, l.panel[1], 2, fade, colorPanelDark2000);
		fadeEdge(r, l.panel[1], 3, fade, colorPanelDark2000);
	}

	// The selected panel gets a frame in its colour.
	{
		const SDL_Rect p = l.panel[v->selected];
		innerGlowRect(r, p, U(&l, 5) > 0 ? U(&l, 5) : 1, 8, panelColor[v->selected], 170);
	}

	// One text size for every feature line, so both panels match.
	int feature_scale = 8;
	{
		const SDL_Rect fb = featureRect(&l, 0);
		const int avail = fb.w - U(&l, 30) - U(&l, 44) - U(&l, 26) - U(&l, 20);
		for (int i = 0; i < 2; ++i)
		{
			for (int k = 0; k < 3; ++k)
			{
				const int s = textScale(&l, 32, textChars(features[i][k].text), avail);
				if (s < feature_scale)
					feature_scale = s;
			}
		}
	}

	for (int i = 0; i < 2; ++i)
		drawPanelUi(r, &l, tex, v, i, feature_scale);

	// The seam.
	fillRect(r, l.comp.x + l.comp.w / 2 - 1, l.comp.y, 2, l.comp.h, colorBlack, 200);

	drawBottomBar(r, &l, v);

	if (v->overlay == OVERLAY_ABOUT || v->overlay == OVERLAY_MESSAGE)
		drawModal(r, &l, v);
	else if (v->overlay != OVERLAY_NONE)
		drawDialog(r, &l, v);
}

// ---------------------------------------------------------------------------
// Data state

static void suggestedDir(char *out, size_t size)
{
	const char *base = SDL_GetBasePath();
	snprintf(out, size, "%styrian2000", base != NULL ? base : "");
}

static void detect2000(const char *data_override, Launcher2000Data *d)
{
	GameDataProvider *provider = NULL;
	GameDataError error = { GAME_DATA_OK, "", "" };
	const GameDataSearch search = { data_override, NULL, NULL };

	memset(d, 0, sizeof *d);
	suggestedDir(d->suggested_dir, sizeof d->suggested_dir);

	d->status = gameDataLocate(gameVariantGet(VARIANT_TYRIAN2000), &search, &provider, &error);
	if (d->status == GAME_DATA_OK)
		d->status = gameDataValidate(provider, &error);
	if (d->status != GAME_DATA_OK && d->status != GAME_DATA_NOT_FOUND)
		snprintf(d->detail, sizeof d->detail, "%s", error.detail);
	gameDataClose(provider);
}

// Validates the data of `variant`; on failure copies the reason into `message`.
static bool validateVariant(GameVariant variant, const char *data_override, char *message, size_t size)
{
	GameDataProvider *provider = NULL;
	GameDataError error = { GAME_DATA_OK, "", "" };
	const GameDataSearch search = { data_override, NULL, NULL };

	GameDataStatus status = gameDataLocate(gameVariantGet(variant), &search, &provider, &error);
	if (status == GAME_DATA_OK)
		status = gameDataValidate(provider, &error);
	if (status != GAME_DATA_OK)
		snprintf(message, size, "%s%s%s", gameVariantGet(variant)->display_name,
		         ": ", error.detail[0] != '\0' ? error.detail : "The game data could not be used.");
	gameDataClose(provider);
	return status == GAME_DATA_OK;
}

static void dataDirectory(GameVariant variant, const char *data_override, char *out, size_t size)
{
	GameDataProvider *provider = NULL;
	GameDataError error = { GAME_DATA_OK, "", "" };
	const GameDataSearch search = { data_override, NULL, NULL };

	if (gameDataLocate(gameVariantGet(variant), &search, &provider, &error) == GAME_DATA_OK)
		snprintf(out, size, "%s", gameDataDirectory(provider));
	else
		snprintf(out, size, "not found");
	gameDataClose(provider);
}

// What the "message" regress frame shows: the plain manual-install hint.
static void installHintMessage(const char *detail, const char *dir, char *message, size_t size)
{
	if (detail[0] != '\0')
		snprintf(message, size, "The Tyrian 2000 data found could not be used. %s Place a valid copy in %s or set TYRIAN2000_DATA.",
		         detail, dir);
	else
		snprintf(message, size, "Tyrian 2000 data is not installed. Place it in %s or set TYRIAN2000_DATA.", dir);
}

static void refreshData(LauncherView *v, const char *data_override)
{
	Launcher2000Data d;
	detect2000(data_override, &d);
	v->installed2000 = d.status == GAME_DATA_OK;
	snprintf(v->detail2000, sizeof v->detail2000, "%s", d.detail);
	snprintf(v->suggested_dir, sizeof v->suggested_dir, "%s", d.suggested_dir);
	installerInstallDirectory(v->install_dir, sizeof v->install_dir);
	dataDirectory(VARIANT_TYRIAN21, data_override, v->data21, sizeof v->data21);
	if (v->installed2000)
		dataDirectory(VARIANT_TYRIAN2000, data_override, v->data2000, sizeof v->data2000);
	else
		snprintf(v->data2000, sizeof v->data2000, "not installed (looked in %s)", d.suggested_dir);
}

// ---------------------------------------------------------------------------
// Textures

static SDL_Texture *loadTexture(SDL_Renderer *r, const unsigned char *data, unsigned long size)
{
	SDL_IOStream *io = SDL_IOFromConstMem(data, (size_t)size);
	if (io == NULL)
		return NULL;
	SDL_Surface *surface = SDL_LoadPNG_IO(io, true);
	if (surface == NULL)
	{
		logError("Launcher art: %s", SDL_GetError());
		return NULL;
	}
	SDL_Texture *texture = SDL_CreateTextureFromSurface(r, surface);
	SDL_DestroySurface(surface);
	if (texture != NULL)
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
	return texture;
}

static void texturesLoad(SDL_Renderer *r, LauncherTextures *t)
{
	t->panel[0] = loadTexture(r, launcher_art_panel21, launcher_art_panel21_size);
	t->panel[1] = loadTexture(r, launcher_art_panel2000, launcher_art_panel2000_size);
	t->title[0] = loadTexture(r, launcher_art_title21, launcher_art_title21_size);
	t->title[1] = loadTexture(r, launcher_art_title2000, launcher_art_title2000_size);
}

static void texturesFree(LauncherTextures *t)
{
	for (int i = 0; i < 2; ++i)
	{
		SDL_DestroyTexture(t->panel[i]);
		SDL_DestroyTexture(t->title[i]);
		t->panel[i] = NULL;
		t->title[i] = NULL;
	}
}

// ---------------------------------------------------------------------------
// The screen

typedef enum
{
	STEP_CONTINUE,
	STEP_QUIT,
	STEP_CHOSEN
} LauncherStep;

static void showMessage(LauncherView *v, const char *text)
{
	snprintf(v->message, sizeof v->message, "%s", text);
	v->overlay = OVERLAY_MESSAGE;
	v->scroll = 0;
}

// ---------------------------------------------------------------------------
// The install flow.  Everything here runs on the UI thread; the installer's own
// worker thread is only ever reached through installerPoll()/installerCancel().
// The one thing that can arrive from another thread is a file dialog's
// callback, which writes a small result under a mutex (below) for the loop to
// pick up.

// The file dialogs answer asynchronously, maybe on another thread, maybe after
// the launcher has moved on or is gone.  The callback therefore touches only
// this static state: a generation number says whether anyone still waits for
// the answer, and the mutex is never destroyed, so a late callback is harmless.
typedef struct
{
	SDL_Mutex *lock;
	unsigned generation;
	bool ready;
	int outcome;                       // 0 chosen, 1 cancelled, 2 failed
	char path[INSTALLER_PATH_MAX];
	char error[256];
} PickerState;

static PickerState picker;

static void SDLCALL pickerCallback(void *userdata, const char *const *filelist, int filter)
{
	(void)filter;
	if (picker.lock == NULL)
		return;
	SDL_LockMutex(picker.lock);
	if ((unsigned)(uintptr_t)userdata == picker.generation)
	{
		picker.outcome = filelist == NULL ? 2 : filelist[0] == NULL ? 1 : 0;
		picker.path[0] = '\0';
		picker.error[0] = '\0';
		if (filelist == NULL)
			snprintf(picker.error, sizeof picker.error, "%s", SDL_GetError());
		else if (filelist[0] != NULL)
			snprintf(picker.path, sizeof picker.path, "%s", filelist[0]);
		picker.ready = true;
	}
	SDL_UnlockMutex(picker.lock);
}

// Starts waiting for a dialog answer; returns the token its callback must carry.
static unsigned pickerArm(LauncherView *v, LauncherAction kind)
{
	if (picker.lock == NULL)
		picker.lock = SDL_CreateMutex();
	unsigned generation = 0;
	if (picker.lock != NULL)
	{
		SDL_LockMutex(picker.lock);
		generation = ++picker.generation;
		picker.ready = false;
		SDL_UnlockMutex(picker.lock);
	}
	v->picker_wait = picker.lock != NULL;
	v->picker_kind = kind;
	return generation;
}

static void pickerDisarm(LauncherView *v)
{
	if (picker.lock != NULL)
	{
		SDL_LockMutex(picker.lock);
		++picker.generation;   // a late answer is ignored
		picker.ready = false;
		SDL_UnlockMutex(picker.lock);
	}
	v->picker_wait = false;
}

// File dialogs need a desktop portal or window system; Steam's Game Mode has
// neither, and it is gamepad-only anyway, so it never offers them.
static bool dialogsAvailable(void)
{
	const char *gamepadUi = getenv("SteamGamepadUI");
	const char *off = getenv("OPENTYRIAN_NO_DIALOGS");
	return !(gamepadUi != NULL && gamepadUi[0] != '\0' && strcmp(gamepadUi, "0") != 0) &&
	       !(off != NULL && off[0] != '\0' && strcmp(off, "0") != 0);
}

static void beginDialog(LauncherView *v, LauncherOverlay overlay, const char *title)
{
	v->overlay = overlay;
	snprintf(v->title, sizeof v->title, "%s", title);
	v->body[0] = '\0';
	v->button_count = 0;
	v->button_selected = 0;
}

static void addButton(LauncherView *v, LauncherAction action, const char *label)
{
	if (v->button_count >= DIALOG_BUTTONS)
		return;
	v->button_action[v->button_count] = action;
	v->button_label[v->button_count] = label;
	++v->button_count;
}

static void appendBody(LauncherView *v, const char *format, ...)
{
	const size_t used = strlen(v->body);
	va_list args;
	va_start(args, format);
	vsnprintf(v->body + used, sizeof v->body - used, format, args);
	va_end(args);
}

static void openInstallMenu(LauncherView *v, const char *note)
{
	beginDialog(v, OVERLAY_INSTALL, "INSTALL TYRIAN 2000");
	if (v->detail2000[0] != '\0')
		appendBody(v, "The data found could not be used: %s\n\n", v->detail2000);
	appendBody(v, "Source: www.camanis.net/tyrian/tyrian2000.zip, about 5 MB.\nInstalls to: %s", v->install_dir[0] != '\0' ? v->install_dir : "(no location on this system)");
	if (!v->dialogs)
		appendBody(v, "\n\nFile pickers are not available here. The zip and folder choices say where to put the files instead.");
	if (note != NULL && note[0] != '\0')
		appendBody(v, "\n\n%s", note);
	if (v->picker_wait)
		appendBody(v, "\n\nWaiting for the file dialog. Esc stops waiting.");
	addButton(v, ACT_DOWNLOAD, "Download from camanis.net (about 5 MB)");
	addButton(v, ACT_ZIP, "Install from a .zip file...");
	addButton(v, ACT_FOLDER, "Use an existing Tyrian 2000 folder...");
	addButton(v, ACT_BACK, "Back");
}

// The directory that holds the install location; a zip placed there is found.
static void parentOf(const char *path, char *out, size_t size)
{
	snprintf(out, size, "%s", path);
	char *cut = NULL;
	for (char *c = out; *c != '\0'; ++c)
		if (*c == '/' || *c == '\\')
			cut = c;
	if (cut != NULL)
		*cut = '\0';
	else
		out[0] = '\0';
}

static void openManual(LauncherView *v, const char *note)
{
	beginDialog(v, OVERLAY_MANUAL, "PLACE THE FILES");
	if (!v->fixed)
		v->found_count = installerSuggestFolders(v->found, (int)(sizeof v->found / sizeof v->found[0]));
	if (note != NULL && note[0] != '\0')
		appendBody(v, "%s\n\n", note);
	char parent[INSTALLER_PATH_MAX];
	parentOf(v->install_dir, parent, sizeof parent);
	appendBody(v, "Put the Tyrian 2000 files in one of these places. The launcher picks them up when you choose LOOK NOW, and when you come back to this window.\n\n"
	              "A folder with the game's files (a GOG copy works):\n%s\n\n"
	              "The file tyrian2000.zip in:\n%s\n\n"
	              "A folder named tyrian2000 next to the game:\n%s",
	           v->install_dir, parent, v->suggested_dir);
	for (int i = 0, shown = 0; i < v->found_count && shown < 2; ++i)
	{
		if (!v->found[i].hasData)
			continue;
		appendBody(v, shown == 0 ? "\n\nFound on this computer:\n%s" : "\n%s", v->found[i].path);
		++shown;
	}
	addButton(v, ACT_SCAN, "Look now");
	addButton(v, ACT_MENU, "Back");
}

static void showFailure(LauncherView *v, const char *message, bool no_curl)
{
	beginDialog(v, OVERLAY_RESULT, no_curl ? "CANNOT DOWNLOAD" : "INSTALL FAILED");
	appendBody(v, "%s", message);
	if (no_curl)
	{
		appendBody(v, "\n\nYou can still install from a .zip file or an existing folder.");
		addButton(v, ACT_ZIP, "Install from a .zip file...");
		addButton(v, ACT_FOLDER, "Use an existing Tyrian 2000 folder...");
	}
	else
	{
		if (v->job_kind == ACT_DOWNLOAD)
			addButton(v, ACT_DOWNLOAD, "Try the download again");
		addButton(v, ACT_MENU, "Other ways to install");
	}
	addButton(v, ACT_BACK, "Back");
}

static void showSuccess(LauncherView *v, const char *path, bool non_canonical)
{
	beginDialog(v, OVERLAY_RESULT, "TYRIAN 2000 INSTALLED");
	appendBody(v, "The data is ready:\n%s", path);
	if (non_canonical)
		appendBody(v, "\n\nNote: these are not the original release's files, but they passed the checks.");
	addButton(v, ACT_PLAY, "PLAY");
	addButton(v, ACT_BACK, "Back");
}

static void showProgress(LauncherView *v)
{
	beginDialog(v, OVERLAY_PROGRESS, "INSTALLING");
	addButton(v, ACT_CANCEL, "Cancel");
}

// Starts an install job of the given kind (ACT_DOWNLOAD, ACT_ZIP or ACT_FOLDER).
static void installBegin(LauncherView *v, LauncherAction kind, const char *path)
{
	v->job_kind = kind;
	const bool started = kind == ACT_DOWNLOAD ? installerBeginDownload() :
	                     kind == ACT_ZIP ? installerBeginFromZip(path) : installerBeginFromFolder(path);
	if (!started)
	{
		showFailure(v, "The install could not be started. Another install may still be finishing; try again in a moment.", false);
		return;
	}
	v->job = true;
	v->cancelling = false;
	memset(&v->progress, 0, sizeof v->progress);
	installerPoll(&v->progress);
	showProgress(v);
}

// Called every frame: moves the screen along when the installer has finished.
static void installTick(LauncherView *v, const char *data_override)
{
	if (!v->job)
		return;
	if (installerPoll(&v->progress))
		return;

	v->job = false;
	v->cancelling = false;
	const InstallerProgress *p = &v->progress;
	switch (p->state)
	{
	case INSTALLER_DONE:
		if (v->skip_validate)
			v->installed2000 = true;
		else
			refreshData(v, data_override);
		if (v->installed2000)
			showSuccess(v, p->installedPath, p->nonCanonical);
		else
			showFailure(v, v->detail2000[0] != '\0' ? v->detail2000 : "The data was installed but the game could not use it.", false);
		break;
	case INSTALLER_CANCELLED:
		openInstallMenu(v, "Cancelled. Nothing was installed.");
		break;
	default:
		showFailure(v, p->message[0] != '\0' ? p->message : "The install failed.", p->error == INSTALLER_ERROR_NO_CURL);
		break;
	}
}

// LOOK NOW: whatever the player put in the documented places.
static void scanForFiles(LauncherView *v, const char *data_override)
{
	if (!v->skip_validate)
		refreshData(v, data_override);
	if (v->installed2000)
	{
		showSuccess(v, v->install_dir, false);
		return;
	}

	char parent[INSTALLER_PATH_MAX], candidate[INSTALLER_PATH_MAX];
	parentOf(v->install_dir, parent, sizeof parent);
	const char *base = SDL_GetBasePath();
	const char *downloads = v->fixed ? NULL : SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
	const char *const dirs[3] = { parent, base, downloads };
	for (int i = 0; i < 3; ++i)
	{
		SDL_PathInfo info;
		if (dirs[i] == NULL || dirs[i][0] == '\0')
			continue;
		snprintf(candidate, sizeof candidate, "%s%s%s", dirs[i], dirs[i][strlen(dirs[i]) - 1] == '/' ? "" : "/", INSTALLER_EXPECTED_FILENAME);
		if (SDL_GetPathInfo(candidate, &info) && info.type == SDL_PATHTYPE_FILE)
		{
			installBegin(v, ACT_ZIP, candidate);
			return;
		}
	}

	v->found_count = installerSuggestFolders(v->found, (int)(sizeof v->found / sizeof v->found[0]));
	for (int i = 0; i < v->found_count; ++i)
	{
		if (v->found[i].hasData)
		{
			installBegin(v, ACT_FOLDER, v->found[i].path);
			return;
		}
	}
	openManual(v, "Nothing found yet in those places.");
}

static void startPicker(LauncherView *v, LauncherAction kind)
{
	if (v->picker_wait)
		return;
	if (!v->dialogs)
	{
		openManual(v, "File pickers are not available here.");
		return;
	}
	const unsigned generation = pickerArm(v, kind);
	if (!v->picker_wait)
	{
		v->dialogs = false;
		openManual(v, "File pickers are not available here.");
		return;
	}
	if (kind == ACT_ZIP)
	{
		static const SDL_DialogFileFilter zips[] = { { "Zip archives", "zip" } };
		SDL_ShowOpenFileDialog(pickerCallback, (void *)(uintptr_t)generation, main_window, zips, 1,
		                       SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS), false);
	}
	else
		SDL_ShowOpenFolderDialog(pickerCallback, (void *)(uintptr_t)generation, main_window, SDL_GetUserFolder(SDL_FOLDER_HOME), false);
	openInstallMenu(v, NULL);
}

// Called every frame: takes a finished dialog's answer.
static void pickerTick(LauncherView *v)
{
	if (!v->picker_wait)
		return;
	bool got = false;
	int outcome = 1;
	char path[INSTALLER_PATH_MAX], error[256];
	path[0] = error[0] = '\0';
	SDL_LockMutex(picker.lock);
	if (picker.ready)
	{
		got = true;
		outcome = picker.outcome;
		snprintf(path, sizeof path, "%s", picker.path);
		snprintf(error, sizeof error, "%s", picker.error);
		picker.ready = false;
	}
	SDL_UnlockMutex(picker.lock);
	if (!got)
		return;

	v->picker_wait = false;
	if (outcome == 2)
	{
		v->dialogs = false;
		openManual(v, "The file dialog could not be opened here.");
		if (error[0] != '\0')
			logWarn("File dialog failed: %s", error);
	}
	else if (outcome == 1)
		openInstallMenu(v, NULL);
	else
		installBegin(v, v->picker_kind, path);
}

// What a button does; returns true when the player chose PLAY.
static bool confirmPanel(LauncherView *v, const char *data_override);

static bool runAction(LauncherView *v, LauncherAction action, const char *data_override)
{
	switch (action)
	{
	case ACT_DOWNLOAD:
		installBegin(v, ACT_DOWNLOAD, NULL);
		break;
	case ACT_ZIP:
	case ACT_FOLDER:
		startPicker(v, action);
		break;
	case ACT_SCAN:
		scanForFiles(v, data_override);
		break;
	case ACT_MENU:
		openInstallMenu(v, NULL);
		break;
	case ACT_CANCEL:
		if (v->job && !v->cancelling)
		{
			installerCancel();
			v->cancelling = true;
			v->button_count = 0;
		}
		break;
	case ACT_PLAY:
		v->overlay = OVERLAY_NONE;
		v->selected = VARIANT_TYRIAN2000;
		v->focus = 0;
		return confirmPanel(v, data_override);
	case ACT_BACK:
		v->overlay = OVERLAY_NONE;
		v->focus = 0;
		v->selected = VARIANT_TYRIAN2000;   // PLAY, if the data is there now, else INSTALL again
		break;
	case ACT_NONE:
		break;
	}
	return false;
}

// Esc / gamepad B in a dialog: one step back, or cancel a running install.
static void dialogBack(LauncherView *v, const char *data_override)
{
	switch (v->overlay)
	{
	case OVERLAY_PROGRESS:
		runAction(v, ACT_CANCEL, data_override);
		break;
	case OVERLAY_MANUAL:
		openInstallMenu(v, NULL);
		break;
	default:
		pickerDisarm(v);
		v->overlay = OVERLAY_NONE;
		break;
	}
}

// The player confirmed a panel.  Returns true if the variant can start.
static bool confirmPanel(LauncherView *v, const char *data_override)
{
	char message[600];

	if (v->selected == VARIANT_TYRIAN2000)
	{
		refreshData(v, data_override);
		if (!v->installed2000)
		{
			openInstallMenu(v, NULL);   // INSTALL: the install dialog
			return false;
		}
	}

	if (!validateVariant(v->selected, data_override, message, sizeof message))
	{
		refreshData(v, data_override);
		showMessage(v, message);
		return false;
	}
	return true;
}

static LauncherStep confirmFocus(LauncherView *v, const char *data_override)
{
	if (v->focus == 1)
	{
		if (v->bar_selected == 1)
			return STEP_QUIT;
		v->overlay = OVERLAY_ABOUT;
		v->scroll = 0;
		return STEP_CONTINUE;
	}
	return confirmPanel(v, data_override) ? STEP_CHOSEN : STEP_CONTINUE;
}

static LauncherStep handleKey(LauncherView *v, const SDL_KeyboardEvent *key, const char *data_override)
{
	if (v->overlay >= OVERLAY_INSTALL)
	{
		const int n = v->button_count;
		if (key->scancode == SDL_SCANCODE_DOWN && n > 0)
			v->button_selected = (v->button_selected + 1) % n;
		else if (key->scancode == SDL_SCANCODE_UP && n > 0)
			v->button_selected = (v->button_selected + n - 1) % n;
		else if ((key->scancode == SDL_SCANCODE_RETURN || key->scancode == SDL_SCANCODE_KP_ENTER ||
		          key->scancode == SDL_SCANCODE_SPACE) && !key->repeat && n > 0)
			return runAction(v, v->button_action[v->button_selected], data_override) ? STEP_CHOSEN : STEP_CONTINUE;
		else if (key->scancode == SDL_SCANCODE_ESCAPE)
			dialogBack(v, data_override);
		return STEP_CONTINUE;
	}

	if (v->overlay != OVERLAY_NONE)
	{
		if (key->scancode == SDL_SCANCODE_DOWN && v->scroll < v->scroll_max)
			++v->scroll;
		else if (key->scancode == SDL_SCANCODE_UP && v->scroll > 0)
			--v->scroll;
		if (key->scancode == SDL_SCANCODE_ESCAPE || key->scancode == SDL_SCANCODE_RETURN ||
		    key->scancode == SDL_SCANCODE_KP_ENTER || key->scancode == SDL_SCANCODE_SPACE)
			v->overlay = OVERLAY_NONE;
		return STEP_CONTINUE;
	}

	switch (key->scancode)
	{
	case SDL_SCANCODE_LEFT:
		if (v->focus == 0)
			v->selected = VARIANT_TYRIAN21;
		else
			v->bar_selected = 0;
		break;
	case SDL_SCANCODE_RIGHT:
		if (v->focus == 0)
			v->selected = VARIANT_TYRIAN2000;
		else
			v->bar_selected = 1;
		break;
	case SDL_SCANCODE_DOWN:
		v->focus = 1;
		break;
	case SDL_SCANCODE_UP:
		v->focus = 0;
		break;
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER:
	case SDL_SCANCODE_SPACE:
		if (!key->repeat)
			return confirmFocus(v, data_override);
		break;
	case SDL_SCANCODE_ESCAPE:
		return STEP_QUIT;
	default:
		break;
	}
	return STEP_CONTINUE;
}

bool launcherChoose(GameVariant preselect, const char *data_override, const char *initial_error, GameVariant *out)
{
	if (regress_handoff != NULL)
	{
		GameVariant target;
		if (!gameVariantParse(regress_handoff, &target))
			return false;
		LauncherView fixture;
		memset(&fixture, 0, sizeof fixture);
		fixture.selected = preselect;
		logInfo("Launcher coverage: preselected %s.", gameVariantGet(preselect)->cli_name);
		SDL_KeyboardEvent key;
		if (!regress_gamepad_key(target == VARIANT_TYRIAN21 ? SDL_SCANCODE_LEFT : SDL_SCANCODE_RIGHT, &key) ||
		    handleKey(&fixture, &key, data_override) != STEP_CONTINUE || fixture.selected != target ||
		    !regress_gamepad_key(SDL_SCANCODE_RETURN, &key) ||
		    handleKey(&fixture, &key, data_override) != STEP_CHOSEN)
		{
			logError("Launcher coverage FAIL: gamepad selection.");
			exit(EXIT_FAILURE);
		}
		*out = fixture.selected;
		logInfo("Launcher coverage: gamepad chose %s.", gameVariantGet(*out)->cli_name);
		return true;
	}
	SDL_Renderer *r = video_renderer();
	LauncherTextures tex;
	LauncherView view;
	LauncherStep step = STEP_CONTINUE;

	memset(&tex, 0, sizeof tex);
	memset(&view, 0, sizeof view);
	texturesLoad(r, &tex);

	view.selected = preselect;
	snprintf(view.version, sizeof view.version, "Modern Tyrian %s", opentyrian_version);
	{
		const char *dir = userDirGet();
		snprintf(view.user_dir, sizeof view.user_dir, "%s", dir[0] != '\0' ? dir : "(current folder)");
	}
	view.dialogs = dialogsAvailable();
	refreshData(&view, data_override);
	if (initial_error != NULL)
		showMessage(&view, initial_error);

	video_fit_launcher_window();

	while (step == STEP_CONTINUE)
	{
		// The game's own controller layer turns the gamepad into key presses
		// (D-pad/stick to arrows, confirm to Enter, cancel to Esc) that arrive
		// below with real keys, so remapped buttons work here as in the menus.
		push_joysticks_as_keyboard();

		SDL_Event ev;
		bool got = SDL_WaitEventTimeout(&ev, 33);
		while (got && step == STEP_CONTINUE)
		{
			switch (ev.type)
			{
			case SDL_EVENT_QUIT:
				step = STEP_QUIT;
				break;

			case SDL_EVENT_WINDOW_RESIZED:
				video_on_win_resize();
				break;

			case SDL_EVENT_WINDOW_FOCUS_LOST:
				windowHasFocus = false;
				break;

			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				windowHasFocus = true;
				// The player may have put the data in place meanwhile.
				if (view.overlay == OVERLAY_MANUAL)
					scanForFiles(&view, data_override);
				else if (!view.job)
					refreshData(&view, data_override);
				break;

			case SDL_EVENT_JOYSTICK_ADDED:
				joystick_device_added(ev.jdevice.which);
				break;
			case SDL_EVENT_JOYSTICK_REMOVED:
				joystick_device_removed(ev.jdevice.which);
				break;
			case SDL_EVENT_GAMEPAD_ADDED:
				joystick_device_added(ev.gdevice.which);
				break;
			case SDL_EVENT_GAMEPAD_REMOVED:
				joystick_device_removed(ev.gdevice.which);
				break;

			case SDL_EVENT_KEY_DOWN:
				if ((ev.key.mod & SDL_KMOD_ALT) && ev.key.scancode == SDL_SCANCODE_RETURN)
					toggle_fullscreen();
				else
					step = handleKey(&view, &ev.key, data_override);
				break;

			case SDL_EVENT_MOUSE_MOTION:
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			{
				const bool click = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
				if (bike_controls_remote())
				{
					// Remote mode (air mouse): OK acts on the focused row, never on the
					// pointer position; Back steps out of a dialog but never quits;
					// drifting motion changes nothing.
					if (click && ev.button.button == SDL_BUTTON_LEFT)
					{
						SDL_KeyboardEvent kev;
						SDL_zero(kev);
						kev.scancode = SDL_SCANCODE_RETURN;
						step = handleKey(&view, &kev, data_override);
					}
					else if (click && ev.button.button == SDL_BUTTON_RIGHT)
					{
						if (view.overlay >= OVERLAY_INSTALL)
							dialogBack(&view, data_override);
						else
							view.overlay = OVERLAY_NONE;
					}
					break;
				}
				if (click ? ev.button.button != SDL_BUTTON_LEFT : (ev.motion.xrel == 0 && ev.motion.yrel == 0))
					break;  // a still pointer must not undo the keyboard's choice

				if (view.overlay >= OVERLAY_INSTALL)
				{
					float dx = click ? ev.button.x : ev.motion.x;
					float dy = click ? ev.button.y : ev.motion.y;
					SDL_RenderCoordinatesFromWindow(r, dx, dy, &dx, &dy);
					for (int k = 0; k < view.button_count; ++k)
					{
						if (!rectContains(&view.button_rect[k], (int)dx, (int)dy))
							continue;
						view.button_selected = k;
						if (click && runAction(&view, view.button_action[k], data_override))
							step = STEP_CHOSEN;
						break;
					}
					break;
				}
				if (click && view.overlay != OVERLAY_NONE)
				{
					view.overlay = OVERLAY_NONE;
					break;
				}
				if (view.overlay != OVERLAY_NONE)
					break;

				float fx = click ? ev.button.x : ev.motion.x;
				float fy = click ? ev.button.y : ev.motion.y;
				SDL_RenderCoordinatesFromWindow(r, fx, fy, &fx, &fy);

				int w = 0, h = 0;
				SDL_GetRenderOutputSize(r, &w, &h);
				LauncherLayout l;
				layoutCompute(&l, w, h);
				const int hit = layoutHit(&l, (int)fx, (int)fy);
				if (hit < 0)
					break;

				if (hit >= 2)
				{
					view.focus = 1;
					view.bar_selected = hit - 2;
				}
				else
				{
					view.focus = 0;
					view.selected = (GameVariant)hit;
				}
				if (click)
					step = confirmFocus(&view, data_override);
				break;
			}

			default:
				break;
			}
			got = step == STEP_CONTINUE && SDL_PollEvent(&ev);
		}

		pickerTick(&view);
		installTick(&view, data_override);

		// A slow triangle wave for the selected button's glow.
		{
			const int phase = (int)(SDL_GetTicks() % 2400);
			view.pulse = (phase < 1200 ? phase : 2400 - phase) * 255 / 1200;
		}

		int w = 0, h = 0;
		SDL_GetRenderOutputSize(r, &w, &h);
		launcherDraw(r, &tex, &view, w, h);
		SDL_RenderPresent(r);
	}

	// Leaving mid-install (quit, or a choice made meanwhile) stops the job and
	// removes its partial files before the process goes on.
	pickerDisarm(&view);
	installerShutdown();
	texturesFree(&tex);

	if (step == STEP_QUIT)
		return false;

	*out = view.selected;
	return true;
}

// ---------------------------------------------------------------------------
// --regress-launcher: one frame, in software, with fixed state.

static const char *regressArg(int argc, char *argv[], const char *name)
{
	const size_t n = strlen(name);
	for (int i = 1; i < argc; ++i)
	{
		if (strncmp(argv[i], name, n) == 0 && argv[i][n] == '=')
			return argv[i] + n + 1;
	}
	return NULL;
}

bool launcherRegressRequested(int argc, char *argv[])
{
	return regressArg(argc, argv, "--regress-launcher") != NULL;
}

int launcherRegressMain(int argc, char *argv[])
{
	const char *spec = regressArg(argc, argv, "--regress-launcher");
	int w = 0, h = 0, panel = 0;
	char data[16] = "", extra[16] = "";
	const int fields = sscanf(spec, "%dx%d,%15[^,],%d,%15s", &w, &h, data, &panel, extra);
	if (fields < 4 || w < 64 || h < 64 || w > 8192 || h > 8192 || (panel != 1 && panel != 2) ||
	    (strcmp(data, "installed") != 0 && strcmp(data, "missing") != 0) ||
	    (fields == 5 && strcmp(extra, "about") != 0 && strcmp(extra, "message") != 0 &&
	     strcmp(extra, "install") != 0 && strcmp(extra, "install-nodlg") != 0 && strcmp(extra, "progress") != 0 &&
	     strcmp(extra, "nocurl") != 0 && strcmp(extra, "success") != 0 && strcmp(extra, "manual") != 0 && strcmp(extra, "first-frame") != 0))
	{
		logError("Bad --regress-launcher; expected WxH,installed|missing,1|2[,about|message|install|install-nodlg|progress|nocurl|success|manual|first-frame].");
		return EXIT_FAILURE;
	}

	SDL_Surface *target = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
	SDL_Renderer *r = target != NULL ? SDL_CreateSoftwareRenderer(target) : NULL;
	if (r == NULL)
	{
		logError("Failed to create the software renderer: %s", SDL_GetError());
		return EXIT_FAILURE;
	}

	LauncherTextures tex;
	memset(&tex, 0, sizeof tex);
	texturesLoad(r, &tex);
	if (tex.panel[0] == NULL || tex.panel[1] == NULL || tex.title[0] == NULL || tex.title[1] == NULL)
	{
		logError("The embedded launcher art did not load.");
		return EXIT_FAILURE;
	}

	LauncherView view;
	memset(&view, 0, sizeof view);
	view.selected = panel == 1 ? VARIANT_TYRIAN21 : VARIANT_TYRIAN2000;
	view.installed2000 = strcmp(data, "installed") == 0;
	snprintf(view.version, sizeof view.version, "Modern Tyrian vTEST");
	snprintf(view.data21, sizeof view.data21, "data");
	snprintf(view.data2000, sizeof view.data2000, "%s", view.installed2000 ? "tyrian2000" : "not installed (looked in tyrian2000)");
	snprintf(view.user_dir, sizeof view.user_dir, "user");
	if (strcmp(extra, "about") == 0)
		view.overlay = OVERLAY_ABOUT;
	else if (strcmp(extra, "message") == 0)
	{
		installHintMessage("", "tyrian2000", view.message, sizeof view.message);
		view.overlay = OVERLAY_MESSAGE;
	}

	// The install screens, with fixed strings and fixed installer values: no
	// system query, no timing, no network.
	view.fixed = true;
	view.dialogs = strcmp(extra, "install-nodlg") != 0 && strcmp(extra, "manual") != 0;
	snprintf(view.install_dir, sizeof view.install_dir, "user/data-tyrian2000");
	snprintf(view.suggested_dir, sizeof view.suggested_dir, "tyrian2000");
	view.job_kind = ACT_DOWNLOAD;
	if (strcmp(extra, "install") == 0 || strcmp(extra, "install-nodlg") == 0)
		openInstallMenu(&view, NULL);
	else if (strcmp(extra, "progress") == 0)
	{
		view.progress.state = INSTALLER_DOWNLOADING;
		view.progress.bytesDone = 2306867;
		view.progress.bytesTotal = INSTALLER_EXPECTED_SIZE;
		snprintf(view.progress.message, sizeof view.progress.message, "Downloading the Tyrian 2000 data...");
		showProgress(&view);
	}
	else if (strcmp(extra, "nocurl") == 0)
		showFailure(&view, "The download needs the curl program, which was not found on this system. Install the data manually with a .zip file or an existing folder.", true);
	else if (strcmp(extra, "success") == 0)
		showSuccess(&view, "user/data-tyrian2000", false);
	else if (strcmp(extra, "manual") == 0)
		openManual(&view, "File pickers are not available here.");

	if (strcmp(extra, "first-frame") == 0)
	{
		// Model a renderer whose viewport still describes the pre-resize window.
		const SDL_Rect old_viewport = { 0, 0, 320, 200 };
		SDL_SetRenderViewport(r, &old_viewport);
	}
	SDL_GetRenderOutputSize(r, &w, &h);

	launcherDraw(r, &tex, &view, w, h);

	SDL_Surface *frame = SDL_RenderReadPixels(r, NULL);
	SDL_Surface *rgba = frame != NULL ? SDL_ConvertSurface(frame, SDL_PIXELFORMAT_RGBA32) : NULL;
	if (rgba == NULL)
	{
		logError("Failed to read the frame: %s", SDL_GetError());
		return EXIT_FAILURE;
	}

	if (strcmp(extra, "first-frame") == 0)
	{
		SDL_RenderPresent(r);
		SDL_GetRenderOutputSize(r, &w, &h);
		launcherDraw(r, &tex, &view, w, h);
		SDL_Surface *steady = SDL_RenderReadPixels(r, NULL);
		SDL_Surface *steady_rgba = steady != NULL ? SDL_ConvertSurface(steady, SDL_PIXELFORMAT_RGBA32) : NULL;
		bool equal = steady_rgba != NULL && steady_rgba->w == rgba->w && steady_rgba->h == rgba->h;
		for (int y = 0; equal && y < rgba->h; ++y)
			equal = memcmp((Uint8 *)rgba->pixels + y * rgba->pitch,
			               (Uint8 *)steady_rgba->pixels + y * steady_rgba->pitch, (size_t)rgba->w * 4) == 0;
		SDL_DestroySurface(steady_rgba);
		SDL_DestroySurface(steady);
		if (!equal)
		{
			logError("Launcher first frame differs from its settled layout.");
			return EXIT_FAILURE;
		}
	}

	// 64-bit FNV-1a over the RGB bytes, row by row.
	Uint64 hash = UINT64_C(14695981039346656037);
	for (int y = 0; y < rgba->h; ++y)
	{
		const Uint8 *row = (const Uint8 *)rgba->pixels + (size_t)y * (size_t)rgba->pitch;
		for (int x = 0; x < rgba->w; ++x)
		{
			for (int k = 0; k < 3; ++k)
			{
				hash ^= row[x * 4 + k];
				hash *= UINT64_C(1099511628211);
			}
		}
	}

	const char *png = regressArg(argc, argv, "--launcher-png");
	if (png != NULL && !SDL_SavePNG(rgba, png))
	{
		logError("Failed to save '%s': %s", png, SDL_GetError());
		return EXIT_FAILURE;
	}

	const char *out_path = regressArg(argc, argv, "--regress-out");
	FILE *out = out_path != NULL ? fopen(out_path, "wb") : stdout;
	if (out == NULL)
	{
		logError("Failed to open '%s'.", out_path);
		return EXIT_FAILURE;
	}
	fprintf(out, "launcher %s frame=%016" PRIx64 "\n", spec, (uint64_t)hash);
	if (out != stdout)
		fclose(out);

	SDL_DestroySurface(rgba);
	SDL_DestroySurface(frame);
	texturesFree(&tex);
	SDL_DestroyRenderer(r);
	SDL_DestroySurface(target);
	return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// --launcher-flow: the install controller with no window.

static const char *overlayName(LauncherOverlay o)
{
	switch (o)
	{
	case OVERLAY_NONE: return "none";
	case OVERLAY_ABOUT: return "about";
	case OVERLAY_MESSAGE: return "message";
	case OVERLAY_INSTALL: return "install";
	case OVERLAY_PROGRESS: return "progress";
	case OVERLAY_RESULT: return "result";
	case OVERLAY_MANUAL: return "manual";
	}
	return "?";
}

bool launcherFlowRequested(int argc, char *argv[])
{
	return regressArg(argc, argv, "--launcher-flow") != NULL;
}

int launcherFlowMain(int argc, char *argv[])
{
	const char *request = regressArg(argc, argv, "--launcher-flow");
	const char *spec = installerCliSpecArgument(argc, argv);
	if (spec != NULL && !installerLoadTestSpec(spec))
	{
		logError("launcher-flow: the test spec could not be read.");
		return EXIT_FAILURE;
	}

	static LauncherView view;
	memset(&view, 0, sizeof view);
	view.selected = VARIANT_TYRIAN2000;
	view.skip_validate = true;
	view.dialogs = false;
	installerInstallDirectory(view.install_dir, sizeof view.install_dir);
	snprintf(view.suggested_dir, sizeof view.suggested_dir, "tyrian2000");

	const char *arg = strchr(request, '=');
	arg = arg != NULL ? arg + 1 : "";
	bool installs = true;

	if (strcmp(request, "download") == 0)
		installBegin(&view, ACT_DOWNLOAD, NULL);
	else if (strncmp(request, "zip=", 4) == 0)
		installBegin(&view, ACT_ZIP, arg);
	else if (strncmp(request, "folder=", 7) == 0)
		installBegin(&view, ACT_FOLDER, arg);
	else if (strcmp(request, "scan") == 0)
		scanForFiles(&view, NULL);
	else if (strncmp(request, "picker-", 7) == 0)
	{
		// The file dialog's callback, fed the way SDL does: NULL on failure,
		// an empty list when cancelled, else the chosen path.
		const bool zip = strncmp(request, "picker-zip=", 11) == 0;
		const unsigned generation = pickerArm(&view, zip ? ACT_ZIP : ACT_FOLDER);
		const char *const chosen[2] = { arg, NULL };
		const char *const none[1] = { NULL };
		if (zip || strncmp(request, "picker-folder=", 14) == 0)
			pickerCallback((void *)(uintptr_t)generation, chosen, 0);
		else if (strcmp(request, "picker-cancel") == 0)
			pickerCallback((void *)(uintptr_t)generation, none, 0), installs = false;
		else if (strcmp(request, "picker-error") == 0)
			pickerCallback((void *)(uintptr_t)generation, NULL, 0), installs = false;
		else
		{
			logError("launcher-flow: unknown request '%s'.", request);
			return EXIT_FAILURE;
		}
		pickerTick(&view);
	}
	else
	{
		logError("launcher-flow: unknown request '%s'.", request);
		return EXIT_FAILURE;
	}

	const Uint64 start = SDL_GetTicks();
	while ((view.job || view.overlay == OVERLAY_PROGRESS) && SDL_GetTicks() - start < 120000)
	{
		installTick(&view, NULL);
		SDL_Delay(20);
	}
	installerShutdown();

	InstallerStatus status;
	installerDetect(&status);
	logInfo("launcher-flow: overlay=%s title=\"%s\" installed=%d", overlayName(view.overlay), view.title, status.installed ? 1 : 0);
	logInfo("launcher-flow: %s", view.overlay == OVERLAY_PROGRESS ? view.progress.message : view.body);
	return installs && !status.installed ? EXIT_FAILURE : EXIT_SUCCESS;
}
