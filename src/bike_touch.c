/*
 * bike_touch.c - touchscreen controls (T3b.3).  New code for the exercise-bike
 * integration; integer arithmetic only on everything that reaches game state.
 *
 * Fingers are tracked from SDL_EVENT_FINGER_* (normalized 0..1 coordinates,
 * converted to Q16 at the boundary) and mapped to game pixels with
 * mapWindowPointToScreen, the same chain the mouse uses.  A finger that goes
 * down on a side panel owns a tap zone; the first finger that goes down on the
 * playfield steers by its movement (no virtual stick).  The steering delta is
 * added to the mouseXC/YC path in mainint.c, so the original +-30 clamp and the
 * momentum rules still apply.
 *
 * Zones are the two side strips of the game frame, split into horizontal bands:
 * the Modern side panels in widescreen, the 24-px margin and 32-px sidebar of the
 * full frame in Classic and Modern 4:3 (small, but usable on a touchscreen).
 */
#include "bike_touch.h"

#include "bike.h"
#include "bike_controls.h"
#include "keyboard.h"
#include "logging.h"
#include "mainint.h"
#include "modern.h"
#include "opentyr.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_FINGERS = 4, MIN_HOLD_FRAMES = 4, TAP_MAX_MS = 400, TAP_MOVE_PX = 10, TICK_MS = 14,
       DRAG_GAIN = 1, FRAME_CLAMP = 30, BACKLOG_MAX = 120, LIVE_MS = 250 };

typedef enum { ACT_REAR, ACT_SK_L, ACT_SK_R, ACT_BOMB, ACT_AUTO, ACT_MENU, ACT_PAUSE } ZoneAct;

typedef struct { bool right; int y0, y1; const char *label; ZoneAct act; } ZoneDef;

static const ZoneDef zones[] =
{
	{ false,   0,  80, "REAR",  ACT_REAR  },
	{ false,  80, 140, "SK-L",  ACT_SK_L  },
	{ false, 140, 200, "SK-R",  ACT_SK_R  },
	{ true,    0,  44, "BOMB",  ACT_BOMB  },
	{ true,   44, 100, "AUTO",  ACT_AUTO  },
	{ true,  100, 150, "MENU",  ACT_MENU  },
	{ true,  150, 200, "PAUSE", ACT_PAUSE },
};
#define ZONE_COUNT ((int)(sizeof zones / sizeof zones[0]))

#define PLAYFIELD_LEFT_X  24
#define PLAYFIELD_W       264

typedef enum { ROLE_NONE, ROLE_STEER, ROLE_ZONE } Role;

typedef struct
{
	bool used;
	uint64_t id;
	Role role;
	int zone;
	int sx, sy, lx, ly; /* game px: where it went down, where it was last */
} Finger;

static Finger fingers[MAX_FINGERS];
static bool swallow_mouse;     /* touch-synthesized mouse events are dropped */
static uint32_t live_ms;       /* SDL_GetTicks() of the last level-loop pass */
static bool live_cleared;
static int pend_x, pend_y;     /* queued steering, in mouseXC units */
static int rem_x, rem_y;       /* sub-unit remainders of the sensitivity scale, in 1/100 */
static int hold[ZONE_COUNT];   /* min-hold frames left per zone */
static bool pend_auto, pend_menu, pend_pause;
static int gest_max;           /* most playfield fingers down in this gesture */
static bool gest_moved;
static uint32_t gest_t0;
static unsigned long steered_px, zone_taps, two_finger_taps, menu_opens, pauses, autos;

bool bike_touch_on(void)
{
	return bike_settings.master && bike_settings.touch;
}

bool bike_touch_play(void)
{
	return bike_touch_on() && bike_gate_open();
}

bool bike_touch_is_touch_mouse(SDL_MouseID which)
{
	return which == SDL_TOUCH_MOUSEID && bike_touch_on();
}

/* ---- geometry -------------------------------------------------------------- */

static bool map_point(int32_t xq, int32_t yq, int *gx, int *gy)
{
	int w = 0, h = 0;
	Sint32 x, y;

	if (main_window == NULL || !SDL_GetWindowSize(main_window, &w, &h) || w <= 0 || h <= 0)
		return false;
	x = (Sint32)(((int64_t)xq * w) >> 16);
	y = (Sint32)(((int64_t)yq * h) >> 16);
	mapWindowPointToScreen(&x, &y);
	*gx = (int)x;
	*gy = (int)y;
	return true;
}

static int zone_at(int gx, int gy)
{
	/* Panel mode maps the mouse in playfield-local pixels (0..263, the panels
	 * lie left of 0 and right of 263); Classic and Modern 4:3 show the full
	 * 320-px frame, whose playfield is 24..287. */
	const int pw = modern_side_panel_width(); /* 0 without panels */
	const int lo = pw > 0 ? 0 : PLAYFIELD_LEFT_X;
	const int hi = lo + PLAYFIELD_W;
	const bool right = gx >= hi;
	int i;

	if (gx >= lo && gx < hi)
		return -1;
	if (pw > 0 ? (right ? gx >= hi + pw : gx < -pw)
	           : (right ? gx >= vga_width : gx < 0))
		return -1; /* the letterbox bars are not zones */
	if (gy < 0)
		gy = 0;
	for (i = 0; i < ZONE_COUNT; ++i)
		if (zones[i].right == right && gy >= zones[i].y0 && (gy < zones[i].y1 || zones[i].y1 >= 200))
			return i;
	return -1;
}

/* ---- finger tracking ------------------------------------------------------- */

static Finger *find_finger(uint64_t id)
{
	int i;

	for (i = 0; i < MAX_FINGERS; ++i)
		if (fingers[i].used && fingers[i].id == id)
			return &fingers[i];
	return NULL;
}

static int count_fingers(bool playfield_only)
{
	int i, n = 0;

	for (i = 0; i < MAX_FINGERS; ++i)
		if (fingers[i].used && (!playfield_only || fingers[i].role != ROLE_ZONE))
			++n;
	return n;
}

static bool steer_active(void)
{
	int i;

	for (i = 0; i < MAX_FINGERS; ++i)
		if (fingers[i].used && fingers[i].role == ROLE_STEER)
			return true;
	return false;
}

static void zone_press(int z)
{
	++zone_taps;
	switch (zones[z].act)
	{
		case ACT_AUTO:  pend_auto = true; break;
		case ACT_MENU:  pend_menu = true; break;
		case ACT_PAUSE: pend_pause = true; break;
		default: hold[z] = MIN_HOLD_FRAMES; break; /* held while touched, at least a few frames */
	}
}

static void finger_down(uint64_t id, int32_t xq, int32_t yq, uint32_t ms)
{
	Finger *f;
	int gx, gy, i;

	if (find_finger(id) != NULL || !map_point(xq, yq, &gx, &gy))
		return;
	for (i = 0, f = NULL; i < MAX_FINGERS; ++i)
		if (!fingers[i].used)
		{
			f = &fingers[i];
			break;
		}
	if (f == NULL)
		return;
	memset(f, 0, sizeof *f);
	f->used = true;
	f->id = id;
	f->sx = f->lx = gx;
	f->sy = f->ly = gy;
	f->zone = zone_at(gx, gy);
	if (f->zone >= 0)
	{
		f->role = ROLE_ZONE;
		zone_press(f->zone);
		return;
	}
	f->role = steer_active() ? ROLE_NONE : ROLE_STEER;
	if (count_fingers(true) == 1)
	{
		gest_t0 = ms;
		gest_max = 0;
		gest_moved = false;
	}
	if (count_fingers(true) > gest_max)
		gest_max = count_fingers(true);
}

static int scale_axis(int d, int *rem)
{
	const long v = (long)d * DRAG_GAIN * (long)bike_settings.pointer_sens_pct + *rem;

	*rem = (int)(v % 100);
	return (int)(v / 100);
}

static void finger_move(uint64_t id, int32_t xq, int32_t yq)
{
	Finger *f = find_finger(id);
	int gx, gy;

	if (f == NULL || !map_point(xq, yq, &gx, &gy))
		return;
	if (f->role != ROLE_ZONE)
	{
		if (abs(gx - f->sx) + abs(gy - f->sy) > TAP_MOVE_PX)
			gest_moved = true;
		if (f->role == ROLE_STEER)
		{
			pend_x += scale_axis(gx - f->lx, &rem_x);
			pend_y += scale_axis(gy - f->ly, &rem_y);
			steered_px += (unsigned long)(abs(gx - f->lx) + abs(gy - f->ly));
			if (pend_x > BACKLOG_MAX) pend_x = BACKLOG_MAX; else if (pend_x < -BACKLOG_MAX) pend_x = -BACKLOG_MAX;
			if (pend_y > BACKLOG_MAX) pend_y = BACKLOG_MAX; else if (pend_y < -BACKLOG_MAX) pend_y = -BACKLOG_MAX;
		}
	}
	f->lx = gx;
	f->ly = gy;
}

static void finger_up(uint64_t id, uint32_t ms, bool cancelled)
{
	Finger *f = find_finger(id);
	const Role role = f != NULL ? f->role : ROLE_ZONE;

	if (f == NULL)
		return;
	f->used = false;
	if (role != ROLE_ZONE && count_fingers(true) == 0)
	{
		/* all playfield fingers are up: was it a quick, still two-finger tap? */
		if (!cancelled && gest_max >= 2 && !gest_moved && ms - gest_t0 <= TAP_MAX_MS)
		{
			pend_menu = true;
			++two_finger_taps;
		}
		gest_max = 0;
	}
}

static void reset_state(void)
{
	memset(fingers, 0, sizeof fingers);
	memset(hold, 0, sizeof hold);
	pend_x = pend_y = rem_x = rem_y = 0;
	pend_auto = pend_menu = pend_pause = false;
	swallow_mouse = false;
	gest_max = 0;
}

/* ---- events ---------------------------------------------------------------- */

static bool real_play_live(void)
{
	return bike_touch_play() && !live_cleared && SDL_GetTicks() - live_ms <= LIVE_MS;
}

static int32_t to_q16(float v)
{
	if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
	return (int32_t)(v * 65536.0f);
}

bool bike_touch_filter(const SDL_Event *ev)
{
	switch (ev->type)
	{
		case SDL_EVENT_FINGER_DOWN:
		case SDL_EVENT_FINGER_MOTION:
		case SDL_EVENT_FINGER_UP:
		case SDL_EVENT_FINGER_CANCELED:
		{
			const uint32_t ms = (uint32_t)(ev->common.timestamp / 1000000u);
			const uint64_t id = (uint64_t)ev->tfinger.fingerID;

			if (!bike_touch_on())
				return false;
			if (ev->type == SDL_EVENT_FINGER_DOWN)
			{
				const bool play = real_play_live();

				swallow_mouse = (count_fingers(false) > 0 && swallow_mouse) || play;
				if (play)
					finger_down(id, to_q16(ev->tfinger.x), to_q16(ev->tfinger.y), ms);
			}
			else if (ev->type == SDL_EVENT_FINGER_MOTION)
				finger_move(id, to_q16(ev->tfinger.x), to_q16(ev->tfinger.y));
			else
				finger_up(id, ms, ev->type == SDL_EVENT_FINGER_CANCELED);
			return true;
		}
		case SDL_EVENT_MOUSE_MOTION:
			return swallow_mouse && ev->motion.which == SDL_TOUCH_MOUSEID && bike_touch_on();
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			return swallow_mouse && ev->button.which == SDL_TOUCH_MOUSEID && bike_touch_on();
		default:
			return false;
	}
}

bool bike_touch_inject(const char *action, const char *arg, uint32_t tick)
{
	unsigned long id = 0;
	int x = 0, y = 0, n;
	const uint32_t ms = tick * TICK_MS;

	n = sscanf(arg, "%lu,%d,%d", &id, &x, &y);
	if (n < 1)
		return false;
	if (!bike_touch_play())
		return true;
	if (strcmp(action, "finger_up") == 0)
		finger_up(id, ms, false);
	else if (n == 3 && strcmp(action, "finger_down") == 0)
		finger_down(id, (int32_t)((int64_t)x * 65536 / 1000), (int32_t)((int64_t)y * 65536 / 1000), ms);
	else if (n == 3 && strcmp(action, "finger_move") == 0)
		finger_move(id, (int32_t)((int64_t)x * 65536 / 1000), (int32_t)((int64_t)y * 65536 / 1000));
	else
		return false;
	return true;
}

/* ---- per frame --------------------------------------------------------------- */

void bike_touch_frame(void)
{
	if (!bike_touch_play())
	{
		reset_state();
		return;
	}
	live_ms = SDL_GetTicks();
	live_cleared = false;
	if (pend_auto)
	{
		pend_auto = false;
		bike_controls_toggle();
		++autos;
	}
	if (pend_pause)
	{
		pend_pause = false;
		pause_pressed = true;
		live_cleared = true; /* the pause screen is modal: taps there are stock */
		++pauses;
	}
	if (pend_menu)
	{
		pend_menu = false;
		ingamemenu_pressed = true;
		live_cleared = true;
		++menu_opens;
	}
}

static bool zone_touched(int z)
{
	int i;

	for (i = 0; i < MAX_FINGERS; ++i)
		if (fingers[i].used && fingers[i].role == ROLE_ZONE && fingers[i].zone == z)
			return true;
	return false;
}

void bike_touch_input(int *dx, int *dy, bool button[4])
{
	int tx, ty, z;

	if (!bike_touch_play())
		return;
	tx = pend_x > FRAME_CLAMP ? FRAME_CLAMP : pend_x < -FRAME_CLAMP ? -FRAME_CLAMP : pend_x;
	ty = pend_y > FRAME_CLAMP ? FRAME_CLAMP : pend_y < -FRAME_CLAMP ? -FRAME_CLAMP : pend_y;
	pend_x -= tx;
	pend_y -= ty;
	*dx += tx;
	*dy += ty;

	for (z = 0; z < ZONE_COUNT; ++z)
	{
		if (!zone_touched(z) && hold[z] <= 0)
			continue;
		if (hold[z] > 0)
			--hold[z];
		switch (zones[z].act)
		{
			case ACT_REAR:  button[3] = true; break;
			case ACT_SK_L:  button[1] = true; break;
			case ACT_SK_R:  button[2] = true; break;
			case ACT_BOMB:  button[1] = button[2] = true; break; /* both sidekicks: stock superbomb */
			default: break;
		}
	}
}

/* ---- HUD ------------------------------------------------------------------- */

int bike_touch_zone_count(void)
{
	return ZONE_COUNT;
}

bool bike_touch_zones_visible(void)
{
	return bike_touch_play();
}

bool bike_touch_zone_info(int index, BikeTouchZone *out)
{
	if (index < 0 || index >= ZONE_COUNT)
		return false;
	out->right = zones[index].right;
	out->y0 = zones[index].y0;
	out->y1 = zones[index].y1;
	out->label = zones[index].label;
	out->lit = zone_touched(index) || hold[index] > 0;
	out->dim = false;
	if (zones[index].act == ACT_AUTO)
	{
		out->lit = bike_autofire_active();
		out->dim = !bike_settings.autofire; /* the companion setting is Off */
	}
	return true;
}

void bike_touch_log_coverage(void)
{
	logInfo("Bike touch: steered=%lu zone_taps=%lu two_finger=%lu tmenus=%lu tpauses=%lu tautos=%lu",
	        steered_px, zone_taps, two_finger_taps, menu_opens, pauses, autos);
}
