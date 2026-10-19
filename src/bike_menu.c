/*
 * bike_menu.c - the "Bike and Controls" setup menu (T2.5).  See bike_menu.h and
 * doc/ftms/design.md section 3.
 *
 * New code for the exercise-bike integration.  The row list is longer than one
 * screen, so it scrolls; the live status line and the help line stay pinned
 * below it.  Fully usable with the arrow keys, Enter and Esc (the mouse works
 * too).  Left/right steppers accelerate while a key is held.
 */
#include "bike_menu.h"

#include "bike.h"
#include "font.h"
#include "fonthand.h"
#include "keyboard.h"
#include "loudness.h"
#include "mouse.h"
#include "nortsong.h"
#include "palette.h"
#include "picload.h"
#include "sprite.h"
#include "vga256d.h"
#include "video.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
	ROW_MASTER,
	ROW_POWER,
	ROW_BRIDGE,
	ROW_FTP,
	ROW_FTP_SRC,
	ROW_STRENGTH,
	ROW_FLOOR,
	ROW_SUPERBOMB,
	ROW_SIDEKICK,
	ROW_SPECIAL,
	ROW_SHIELD,
	ROW_AUTOFIRE,
	ROW_RESERVE,
	ROW_AIRMOUSE,
	ROW_REMOTE,
	ROW_SENS,
	ROW_TOUCH,
	ROW_HUD,
	ROW_DONE,
	ROW_COUNT
} RowId;

typedef struct
{
	const char *name;
	const char *help;
} RowInfo;

/* Names only use glyphs the game fonts have ('&' and '<' do not exist). */
static const RowInfo rows[ROW_COUNT] = {
	[ROW_MASTER]    = { "Bike and Controls:", "Master switch. Off keeps the game stock." },
	[ROW_POWER]     = { "Bike Power:", "Pedal power fills the generator." },
	[ROW_BRIDGE]    = { "Bridge:", "Flywheel bridge address. Enter to edit." },
	[ROW_FTP]       = { "FTP:", "Best even 8 minute power x 0.9. Hold left/right." },
	[ROW_FTP_SRC]   = { "FTP Source:", "Bridge uses your Flywheel FTP, Manual your own." },
	[ROW_STRENGTH]  = { "Strength:", "How hard riding pays. 100% is stock power." },
	[ROW_FLOOR]     = { "Floor:", "Power units per frame while coasting." },
	[ROW_SUPERBOMB] = { "Surge Superbomb:", "A superbomb for an overdrive surge." },
	[ROW_SIDEKICK]  = { "Surge Sidekick:", "Sidekicks charge faster while surging." },
	[ROW_SPECIAL]   = { "Surge Special:", "Special weapon recharges while surging." },
	[ROW_SHIELD]    = { "Surge Shield:", "Shields regenerate faster with effort." },
	[ROW_AUTOFIRE]  = { "Auto-fire:", "Toggle: a key switches auto-fire in play." },
	[ROW_RESERVE]   = { "Auto-fire Reserve:", "Hold auto-fire back below this power." },
	[ROW_AIRMOUSE]  = { "Air Mouse:", "Steer the ship with a pointer remote." },
	[ROW_REMOTE]    = { "Remote Buttons:", "Pointer OK and Back act as Enter and Esc." },
	[ROW_SENS]      = { "Pointer Speed:", "Pointer sensitivity for the air mouse." },
	[ROW_TOUCH]     = { "Touch Controls:", "Drag to steer, tap zones to fire." },
	[ROW_HUD]       = { "Bike HUD:", "Show watts, cadence and link in play." },
	[ROW_DONE]      = { "Done", "Return." },
};

enum
{
	FTP_MIN = 50, FTP_MAX = 500, FTP_UNIT = 5,
	STRENGTH_MAX = 300, STRENGTH_UNIT = 5,
	FLOOR_MAX = 64,
	SENS_MIN = 25, SENS_MAX = 400, SENS_UNIT = 5,
	ADDR_MAX = 70 /* 63 host characters, ':' and a 5 digit port */
};

static bool in_level_latched = false;
static char regress_keys[48];

bool bike_menu_in_level_latched(void) { return in_level_latched; }

void bike_menu_regress_keys(const char *keys)
{
	snprintf(regress_keys, sizeof regress_keys, "%s", keys != NULL ? keys : "");
}

enum { FTP_VALUE_W = 130, HELP_MAX_W = 300 }; /* the value column and the help line, in pixels */

/* The help line; the touch row warns when its fire companion is off (touch has
 * no fire zone: auto-fire is the way to shoot). */
static const char *row_help(RowId id)
{
	if (id == ROW_TOUCH && bike_settings.touch && !bike_settings.autofire)
		return "Touch fires via Auto-fire: set it to Toggle.";
	if (id == ROW_FTP || id == ROW_FTP_SRC)
	{
		static char help[64];
		BikeFtpInfo info;

		bike_ftp_info(&info);
		if (info.from_bridge && info.intensity)
		{
			/* "Easy 85 % of 230 W (Ann, test). Source Manual to edit."; shed the tail until it fits */
			char who[24];
			int pass;

			for (pass = 0; pass < 3; ++pass)
			{
				who[0] = '\0';
				if (pass == 0 && info.source[0] != '\0')
					snprintf(who, sizeof who, " (%.10s, %.6s)", info.rider, info.source);
				else if (pass <= 1)
					snprintf(who, sizeof who, " (%.10s)", info.rider);
				snprintf(help, sizeof help, "%.14s %u %% of %u W%s. Source Manual to edit.", info.preset,
				         info.percent, info.measured_w, who);
				if (JE_textWidth(help, TINY_FONT) <= HELP_MAX_W)
					break;
			}
			return help;
		}
		if (info.from_bridge)
		{
			snprintf(help, sizeof help, "From Flywheel: %.10s%s%.6s%s. Source Manual to edit.", info.rider,
			         info.source[0] != '\0' ? " (" : "", info.source, info.source[0] != '\0' ? ")" : "");
			return help;
		}
		if (info.pending)
			return "The bridge has a new FTP. It applies next level.";
	}
	return rows[id].help;
}

/* ---- row values ----------------------------------------------------------- */

static bool *bool_field(RowId id)
{
	BikeSettings *s = &bike_settings;

	switch (id)
	{
		case ROW_FTP_SRC: return &s->ftp_auto;
		case ROW_MASTER: return &s->master;
		case ROW_POWER: return &s->generator;
		case ROW_SUPERBOMB: return &s->surge_superbomb;
		case ROW_SIDEKICK: return &s->surge_sidekick;
		case ROW_SPECIAL: return &s->surge_special;
		case ROW_SHIELD: return &s->surge_shield;
		case ROW_AUTOFIRE: return &s->autofire;
		case ROW_AIRMOUSE: return &s->air_mouse;
		case ROW_REMOTE: return &s->remote_buttons;
		case ROW_TOUCH: return &s->touch;
		case ROW_HUD: return &s->hud;
		default: return NULL;
	}
}

static bool row_is_stepper(RowId id)
{
	return id == ROW_FTP || id == ROW_STRENGTH || id == ROW_FLOOR || id == ROW_SENS;
}

static void bridge_text(char *out, size_t cap)
{
	snprintf(out, cap, "%s:%u", bike_settings.bridge_host, (unsigned)bike_settings.bridge_port);
}

static void row_value(RowId id, char *out, size_t cap)
{
	const BikeSettings *s = &bike_settings;
	const bool *b = bool_field(id);

	out[0] = '\0';
	if (id == ROW_FTP_SRC)
		snprintf(out, cap, "%s", s->ftp_auto ? "Bridge" : "Manual");
	else if (id == ROW_AUTOFIRE)
		snprintf(out, cap, "%s", s->autofire ? "Toggle" : "Off");
	else if (b != NULL)
		snprintf(out, cap, "%s", *b ? "On" : "Off");
	else if (id == ROW_BRIDGE)
		bridge_text(out, cap);
	else if (id == ROW_FTP)
	{
		BikeFtpInfo info;

		bike_ftp_info(&info);
		if (info.from_bridge && info.intensity)
		{
			char name[32];

			/* "196 W Easy 85 % of 230 W", else "196 W Easy 85 %" with the name cut to fit.  (The
			 * large font draws no visible parentheses, so the text has none.) */
			snprintf(out, cap, "%u W %.14s %u %% of %u W", info.watts, info.preset, info.percent, info.measured_w);
			snprintf(name, sizeof name, "%.14s", info.preset);
			while (JE_textWidth(out, FONT_NORMAL) > FTP_VALUE_W)
			{
				size_t n = strlen(name);

				snprintf(out, cap, "%u W %s %u %%", info.watts, name, info.percent);
				if (JE_textWidth(out, FONT_NORMAL) <= FTP_VALUE_W || n <= 1)
					break;
				name[n - 1] = '\0';
			}
		}
		else if (info.from_bridge)
			snprintf(out, cap, "%u W %.10s", info.watts, info.rider);
		else
			snprintf(out, cap, "%u W", (unsigned)s->ftp_w);
	}
	else if (id == ROW_STRENGTH)
		snprintf(out, cap, "%u%%", (unsigned)s->gain_pct);
	else if (id == ROW_FLOOR)
		snprintf(out, cap, "%u", (unsigned)s->floor);
	else if (id == ROW_RESERVE)
		snprintf(out, cap, s->autofire_reserve_pct == 0 ? "Off" : "%u%%", (unsigned)s->autofire_reserve_pct);
	else if (id == ROW_SENS)
		snprintf(out, cap, "%u%%", (unsigned)s->pointer_sens_pct);
}

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* dir is -1 or +1, mult scales a stepper's unit (hold-to-accelerate). */
static void row_adjust(RowId id, int dir, int mult)
{
	BikeSettings *s = &bike_settings;
	bool *b = bool_field(id);

	if (b != NULL)
		*b = !*b;
	else if (id == ROW_FTP)
	{
		BikeFtpInfo info;

		bike_ftp_info(&info);
		if (!info.from_bridge) /* the bridge's value is not edited here: switch the source to Manual */
			s->ftp_w = (uint16_t)clamp_int((int)s->ftp_w + dir * FTP_UNIT * mult, FTP_MIN, FTP_MAX);
	}
	else if (id == ROW_STRENGTH)
		s->gain_pct = (uint16_t)clamp_int((int)s->gain_pct + dir * STRENGTH_UNIT * mult, 0, STRENGTH_MAX);
	else if (id == ROW_FLOOR)
		s->floor = (uint8_t)clamp_int((int)s->floor + dir * (mult > 4 ? 4 : mult), 0, FLOOR_MAX);
	else if (id == ROW_SENS)
		s->pointer_sens_pct = (uint16_t)clamp_int((int)s->pointer_sens_pct + dir * SENS_UNIT * mult, SENS_MIN, SENS_MAX);
	else if (id == ROW_RESERVE)
	{
		static const uint8_t steps[3] = { 0, 25, 50 };
		int i = s->autofire_reserve_pct == 25 ? 1 : (s->autofire_reserve_pct == 50 ? 2 : 0);

		i = (i + dir + 3) % 3;
		s->autofire_reserve_pct = steps[i];
	}
}

/* Parses "host", "host:port" into the settings; false leaves them untouched. */
static bool bridge_commit(const char *text)
{
	char host[sizeof bike_settings.bridge_host];
	const char *colon = strrchr(text, ':');
	size_t hlen = colon != NULL ? (size_t)(colon - text) : strlen(text);
	char port[8];

	if (hlen == 0 || hlen >= sizeof host)
		return false;
	memcpy(host, text, hlen);
	host[hlen] = '\0';
	if (colon != NULL)
	{
		if (colon[1] == '\0' || strlen(colon + 1) >= sizeof port)
			return false;
		strcpy(port, colon + 1);
	}
	else
		strcpy(port, "8585");

	{
		BikeSettings saved = bike_settings;

		if (!bike_settings_set("bridge_host", host) || !bike_settings_set("bridge_port", port))
		{
			bike_settings = saved;
			return false;
		}
	}
	return true;
}

/* ---- drawing -------------------------------------------------------------- */

static void draw_arrow(SDL_Surface *s, int cx, int y, bool up)
{
	int i;

	for (i = 0; i < 4; ++i)
	{
		const int w = up ? i : 3 - i;

		fill_rectangle_xy(s, cx - w, y + i, cx + w, y + i, 14);
	}
}

static void status_line(char *out, size_t cap, BikeLink *link_out)
{
	BikeStatus st;

	bike_status(&st);
	*link_out = st.link;
	if (!bike_settings.master)
		snprintf(out, cap, "Off");
	else if (st.has_sample)
	{
		char w[16] = "-- W", r[16] = "-- rpm";

		if (st.power_valid)
			snprintf(w, sizeof w, "%u W", st.watts);
		if (st.cadence_valid)
			snprintf(r, sizeof r, "%u rpm", st.rpm);
		snprintf(out, cap, "%s  %s  %s  %u.%u s", bike_link_name(st.link), w, r,
		         (unsigned)(st.age_ms / 1000), (unsigned)((st.age_ms % 1000) / 100));
	}
	else
		snprintf(out, cap, "%s  no data yet", bike_link_name(st.link));
}

/* ---- the menu ------------------------------------------------------------- */

void bike_menu(bool in_level)
{
	enum { VISIBLE = 9, X_NAME = 24, W_NAME = 140, X_VALUE = X_NAME + W_NAME, W_VALUE = FTP_VALUE_W, DY = 14,
	       H_ROW = 13, Y_HEADER = 4, Y_ROWS = 34, Y_STATUS = 172, Y_HELP = 188 };
	SDL_Surface *saved1 = NULL, *saved2 = NULL;
	size_t selected = 0, top = 0;
	bool restart = true, done = false, editing = false;
	char edit[ADDR_MAX + 1] = "";
	size_t edit_len = 0;
	const char *edit_error = NULL;
	int streak = 0, last_dir = 0;
	Uint32 last_step = 0;
	const int y_header = in_level ? 8 : Y_HEADER;
	const int y_rows = in_level ? 38 : Y_ROWS;
	const char *rk;

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1'); /* mouse pointer sprites */

	if (in_level)
	{
		in_level_latched = true;
		saved1 = SDL_DuplicateSurface(VGAScreen);
		saved2 = SDL_DuplicateSurface(VGAScreen2);
		if (saved1 == NULL || saved2 == NULL)
		{
			SDL_DestroySurface(saved1);
			SDL_DestroySurface(saved2);
			return;
		}
	}


	while (!done)
	{
		const BikeSettings *s = &bike_settings;
		char value[ADDR_MAX + 16], status[96];
		BikeLink link;
		size_t i;

		setFrameCount(1);

		if (restart)
		{
			if (in_level)
			{
				/* VGAScreen2 holds the frozen frame without the caller's item text. */
				memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);
				for (i = 0; i < 4; ++i)
					JE_barShade(VGAScreen, 3, 3, 316, 196);
				memcpy(VGAScreen2->pixels, VGAScreen->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);
			}
			else
			{
				JE_loadPic(VGAScreen2, 2, false);
				fill_rectangle_wh(VGAScreen2, 0, 192, 320, 8, 0);
			}
		}

		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		drawFontHvShadowAligned(VGAScreen, 160, y_header, "Bike and Controls", FONT_LARGE, ALIGN_CENTER, 15, -3, false, 2);

		/* keep the selection inside the window */
		if (selected < top)
			top = selected;
		if (selected >= top + VISIBLE)
			top = selected - VISIBLE + 1;

		for (i = 0; i < VISIBLE && top + i < ROW_COUNT; ++i)
		{
			const RowId id = (RowId)(top + i);
			const int y = y_rows + DY * (int)i;
			const bool sel = id == selected;
			const bool dim = !s->master && id != ROW_MASTER && id != ROW_DONE;
			const Sint8 bright = (Sint8)(-3 + (sel ? 2 : 0) + (dim ? -3 : 0));

			drawFontHvShadow(VGAScreen, X_NAME, y, rows[id].name, FONT_NORMAL, 15, bright, false, 2);
			if (id == ROW_DONE)
				continue;

			if (editing && id == ROW_BRIDGE)
			{
				const char *shown = edit;

				while (JE_textWidth(shown, FONT_NORMAL) > W_VALUE - 10 && *shown != '\0')
					++shown; /* show the tail while typing */
				fill_rectangle_wh(VGAScreen, X_VALUE - 3, y - 2, W_VALUE + 4, H_ROW + 3, 224);
				JE_rectangle(VGAScreen, X_VALUE - 3, y - 2, X_VALUE + W_VALUE, y + H_ROW, 248);
				drawFontHvShadow(VGAScreen, X_VALUE, y, shown, FONT_NORMAL, 15, -1, false, 2);
				if ((SDL_GetTicks() / 400) % 2 == 0)
				{
					const int cx = X_VALUE + JE_textWidth(shown, FONT_NORMAL) + 1;

					fill_rectangle_xy(VGAScreen, cx, y, cx + 1, y + 9, 15);
				}
				continue;
			}

			row_value(id, value, sizeof value);
			while (JE_textWidth(value, FONT_NORMAL) > W_VALUE && strlen(value) > 1)
				value[strlen(value) - 1] = '\0';
			drawFontHvShadow(VGAScreen, X_VALUE, y, value, FONT_NORMAL, 15, bright, false, 2);
		}

		if (top > 0)
			draw_arrow(VGAScreen, 300, y_rows - 4, true);
		if (top + VISIBLE < ROW_COUNT)
			draw_arrow(VGAScreen, 300, y_rows + DY * VISIBLE - 2, false);

		/* pinned live status and help */
		status_line(status, sizeof status, &link);
		{
			const Uint8 dot = (Uint8)bike_link_color(link, false);

			fill_rectangle_xy(VGAScreen, X_NAME, Y_STATUS + 2, X_NAME + 4, Y_STATUS + 6, dot);
			JE_textShade(VGAScreen, X_NAME + 9, Y_STATUS, "Status:", 15, 4, PART_SHADE);
			JE_textShade(VGAScreen, X_NAME + 9 + JE_textWidth("Status: ", TINY_FONT), Y_STATUS, status, 15, 2, PART_SHADE);
		}
		JE_textShade(VGAScreen, X_NAME, Y_HELP,
		             edit_error != NULL ? edit_error : (editing ? "Type the address. Enter saves, Esc cancels." : row_help((RowId)selected)),
		             15, edit_error != NULL ? 6 : 4, PART_SHADE);

		if (restart)
		{
			mouseCursor = MOUSE_POINTER_NORMAL;
			if (!in_level)
				fade_palette(colors, 10, 0, 255); /* clears the key queue */
			restart = false;

			/* Regress: a queued key script for this call (fills the key queue at once). */
			for (rk = regress_keys; *rk != '\0'; ++rk)
			{
				KeyboardInput in;

				memset(&in, 0, sizeof in);
				in.scancode = (Uint16)-1;
				in.sym = (SDL_Keycode)-1;
				switch (*rk)
				{
					case '^': in.scancode = SDL_SCANCODE_UP; in.sym = SDLK_UP; break;
					case '_': in.scancode = SDL_SCANCODE_DOWN; in.sym = SDLK_DOWN; break;
					case '[': in.scancode = SDL_SCANCODE_LEFT; in.sym = SDLK_LEFT; break;
					case ']': in.scancode = SDL_SCANCODE_RIGHT; in.sym = SDLK_RIGHT; break;
					case '#': in.scancode = SDL_SCANCODE_RETURN; in.sym = SDLK_RETURN; break;
					case '!': in.scancode = SDL_SCANCODE_ESCAPE; in.sym = SDLK_ESCAPE; break;
					case '<': in.scancode = SDL_SCANCODE_BACKSPACE; in.sym = SDLK_BACKSPACE; break;
					default: in.ch = (Uint8)*rk; break; /* text */
				}
				keyboardPushInput(&in);
			}
			regress_keys[0] = '\0';
		}

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		waitUntilElapsed();

		/* ---- input ---- */
		{
			MouseInput mi;
			KeyboardInput ki;

			if (!editing && mouseGetInput(INPUT_ANY, &mi))
			{
				if (mi.x >= X_NAME && mi.x < X_VALUE + W_VALUE)
				{
					for (i = 0; i < VISIBLE && top + i < ROW_COUNT; ++i)
					{
						const int y = y_rows + DY * (int)i;

						if (mi.y >= y && mi.y < y + H_ROW)
						{
							if (selected != top + i)
							{
								JE_playSampleNum(S_CURSOR);
								selected = top + i;
							}
							if (mi.button == SDL_BUTTON_LEFT)
							{
								const RowId id = (RowId)selected;

								if (id == ROW_DONE)
									done = true;
								else if (id == ROW_BRIDGE)
								{
									bridge_text(edit, sizeof edit);
									edit_len = strlen(edit);
									editing = true;
									edit_error = NULL;
									SDL_StartTextInput(main_window);
								}
								else if (row_is_stepper(id) && mi.x >= X_VALUE)
									row_adjust(id, mi.x < X_VALUE + W_VALUE / 2 ? -1 : 1, 1);
								else if (!row_is_stepper(id))
									row_adjust(id, 1, 1);
								JE_playSampleNum(S_CLICK);
							}
							break;
						}
					}
				}
				if (mi.button == SDL_BUTTON_RIGHT)
				{
					JE_playSampleNum(S_SPRING);
					done = true;
				}
			}

			while (!done && keyboardGetInput(&ki))
			{
				if (editing)
				{
					if (ki.scancode == SDL_SCANCODE_ESCAPE)
					{
						editing = false;
						edit_error = NULL;
						SDL_StopTextInput(main_window);
						JE_playSampleNum(S_SPRING);
					}
					else if (ki.scancode == SDL_SCANCODE_RETURN)
					{
						if (bridge_commit(edit))
						{
							editing = false;
							edit_error = NULL;
							SDL_StopTextInput(main_window);
							JE_playSampleNum(S_SELECT);
						}
						else
						{
							edit_error = "Not a valid host:port.";
							JE_playSampleNum(S_CLICK);
						}
					}
					else if (ki.scancode == SDL_SCANCODE_BACKSPACE || ki.scancode == SDL_SCANCODE_DELETE)
					{
						if (edit_len > 0)
							edit[--edit_len] = '\0';
						edit_error = NULL;
					}
					else if (ki.ch != 0)
					{
						const char c = (char)ki.ch;
						const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
						                c == '.' || c == '-' || c == ':';

						if (ok && edit_len < ADDR_MAX)
						{
							edit[edit_len++] = c;
							edit[edit_len] = '\0';
							edit_error = NULL;
						}
					}
					continue;
				}

				switch (ki.scancode)
				{
					case SDL_SCANCODE_UP:
						JE_playSampleNum(S_CURSOR);
						selected = selected == 0 ? ROW_COUNT - 1 : selected - 1;
						break;
					case SDL_SCANCODE_DOWN:
						JE_playSampleNum(S_CURSOR);
						selected = selected == ROW_COUNT - 1 ? 0 : selected + 1;
						break;
					case SDL_SCANCODE_LEFT:
					case SDL_SCANCODE_RIGHT:
					{
						const int dir = ki.scancode == SDL_SCANCODE_LEFT ? -1 : 1;
						const RowId id = (RowId)selected;
						const Uint32 now = SDL_GetTicks();
						int mult = 1;

						/* A held key repeats; each repeat raises the step. */
						if (dir == last_dir && keysactive[ki.scancode] && now - last_step <= 350)
							++streak;
						else
							streak = 0;
						last_dir = dir;
						last_step = now;
						if (streak >= 24)
							mult = 10;
						else if (streak >= 12)
							mult = 4;
						else if (streak >= 5)
							mult = 2;

						if (id != ROW_DONE && id != ROW_BRIDGE)
						{
							JE_playSampleNum(S_CURSOR);
							row_adjust(id, dir, row_is_stepper(id) ? mult : 1);
						}
						break;
					}
					case SDL_SCANCODE_SPACE:
					case SDL_SCANCODE_RETURN:
					{
						const RowId id = (RowId)selected;

						if (id == ROW_DONE)
						{
							JE_playSampleNum(S_SELECT);
							done = true;
						}
						else if (id == ROW_BRIDGE)
						{
							JE_playSampleNum(S_CLICK);
							bridge_text(edit, sizeof edit);
							edit_len = strlen(edit);
							editing = true;
							edit_error = NULL;
							SDL_StartTextInput(main_window);
						}
						else if (!row_is_stepper(id))
						{
							JE_playSampleNum(S_CLICK);
							row_adjust(id, 1, 1);
						}
						break;
					}
					case SDL_SCANCODE_ESCAPE:
						JE_playSampleNum(S_SPRING);
						done = true;
						break;
					default:
						break;
				}
			}

			if (!keysactive[SDL_SCANCODE_LEFT] && !keysactive[SDL_SCANCODE_RIGHT])
				streak = 0;
		}
	}

	if (editing)
		SDL_StopTextInput(main_window);

	if (in_level)
	{
		memcpy(VGAScreen2->pixels, saved2->pixels, (size_t)VGAScreen2->pitch * VGAScreen2->h);
		memcpy(VGAScreen->pixels, saved1->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);
		SDL_DestroySurface(saved1);
		SDL_DestroySurface(saved2);
	}
}
