/*
 * bike_controls.c - auto-fire toggle with energy reserve, plus the injected
 * event script used by the regress harness.  New code for the exercise-bike
 * integration; integer arithmetic only.
 *
 * Toggle inputs (a small table so the defaults are easy to change while the
 * T3b.0 probe results are pending; none is a keySettings slot):
 *   - keyboard: bike_settings.autofire_key / autofire_key2 (defaults A and the
 *     Application/Menu key, which the remote's Menu button sends);
 *   - gamepad: the right-stick click, unassigned by default (Back is pause);
 *   - injected: --regress-bike-events.
 * The mouse right click is deliberately not bound: it is a configurable mouse
 * action in the stock game and T3b.2 owns its translation.
 *
 * The latch starts off at each launch and survives levels (design section 6).
 * Turning the "Auto-fire" setting off clears it.
 */
#include "bike_controls.h"

#include "bike.h"
#include "bike_touch.h"
#include "joystick.h"
#include "keyboard.h"
#include "mainint.h"
#include "mouse_buttons.h"
#include "logging.h"
#include "episodes.h"
#include "player.h"
#include "varz.h"
#include "config.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const SDL_GamepadButton pad_toggle_buttons[] = { SDL_GAMEPAD_BUTTON_RIGHT_STICK };

typedef enum { EV_TOGGLE, EV_ON, EV_OFF, EV_SET, EV_EQUIP, EV_FIRE_ON, EV_FIRE_OFF, EV_IMMORTAL,
		EV_MOUSE_MOVE, EV_MOUSE_DRIVE, EV_MOUSE_DOWN, EV_MOUSE_UP, EV_KEY_DOWN, EV_KEY_UP,
		EV_FINGER } EventKind;

typedef struct
{
	uint32_t tick;
	EventKind kind;
	char arg[96];
} InjectedEvent;

static bool latch = false;
static bool key_prev = false;
static bool ever_used = false;
static bool injected_fire = false;
static unsigned long fired_frames, held_frames, toggles;

/* air mouse / remote state (T3b.2) */
enum { MEDIA_PLAY_PAUSE, MEDIA_NEXT, MEDIA_PREV, MEDIA_COUNT };
static int inj_move_x, inj_move_y, inj_drive_x, inj_drive_y;
static int rem_x, rem_y; /* sub-unit remainders of the sensitivity scale, in 1/100 */
static uint8_t inj_buttons;
static bool inj_media[MEDIA_COUNT];
static bool right_armed, pause_armed;
static unsigned long moved_units, menu_opens, pauses, media_presses;

static InjectedEvent *events = NULL;
static size_t event_count = 0, event_next = 0;

bool bike_controls_remote(void)
{
	return bike_settings.master && bike_settings.air_mouse;
}

bool bike_controls_air_play(void)
{
	return bike_controls_remote() && bike_gate_open();
}

int bike_controls_remote_button_key(int sdl_button)
{
	if (!bike_controls_remote())
		return 0;
	if (sdl_button == SDL_BUTTON_LEFT)
		return SDL_SCANCODE_RETURN;
	if (sdl_button == SDL_BUTTON_RIGHT)
		return SDL_SCANCODE_ESCAPE;
	return -1;
}

static int scale_axis(int d, int *rem)
{
	const long v = (long)d * (long)bike_settings.pointer_sens_pct + *rem;

	*rem = (int)(v % 100);
	return (int)(v / 100);
}

void bike_controls_pointer(int *dx, int *dy)
{
	*dx += inj_move_x + inj_drive_x;
	*dy += inj_move_y + inj_drive_y;
	inj_move_x = inj_move_y = 0;
	if (bike_settings.pointer_sens_pct != 100)
	{
		*dx = scale_axis(*dx, &rem_x);
		*dy = scale_axis(*dy, &rem_y);
	}
	moved_units += (unsigned long)(abs(*dx) + abs(*dy));
}

void bike_controls_air_buttons(uint8_t buttons_down, bool button[4])
{
	const uint8_t down = (uint8_t)(buttons_down | inj_buttons);

	if (down & SDL_BUTTON_LMASK)
		button[0] = true;
	mouse_buttons_to_player((uint8_t)(down & SDL_BUTTON_MMASK), button);
}

static bool media_down(int which)
{
	static const int keys[MEDIA_COUNT][2] =
	{
		{ SDL_SCANCODE_MEDIA_PLAY_PAUSE, SDL_SCANCODE_MEDIA_PLAY },
		{ SDL_SCANCODE_MEDIA_NEXT_TRACK, SDL_SCANCODE_MEDIA_NEXT_TRACK },
		{ SDL_SCANCODE_MEDIA_PREVIOUS_TRACK, SDL_SCANCODE_MEDIA_PREVIOUS_TRACK },
	};

	return inj_media[which] || keysactive[keys[which][0]] || keysactive[keys[which][1]] ||
	       (which == MEDIA_PLAY_PAUSE && keysactive[SDL_SCANCODE_MEDIA_PAUSE]);
}

void bike_controls_media_buttons(bool button[4])
{
	if (media_down(MEDIA_NEXT))
	{
		button[3] = true;
		++media_presses;
	}
	if (media_down(MEDIA_PREV))
	{
		button[1] = button[2] = true;
		++media_presses;
	}
}

/* Per level-loop pass: right click -> in-game menu, play/pause -> pause.  Each
 * fires on a press only after the input was seen released, so the click or key
 * that closes the menu or resumes the pause cannot reopen it. */
static void remote_frame(void)
{
	bool right, pp;

	if (!bike_controls_air_play())
	{
		right_armed = pause_armed = false;
		return;
	}
	right = ((mouseButtonsDown | inj_buttons) & SDL_BUTTON_RMASK) != 0;
	pp = media_down(MEDIA_PLAY_PAUSE);
	if (!right)
		right_armed = true;
	else if (right_armed)
	{
		right_armed = false;
		ingamemenu_pressed = true;
		++menu_opens;
	}
	if (!pp)
		pause_armed = true;
	else if (pause_armed)
	{
		pause_armed = false;
		pause_pressed = true;
		++pauses;
	}
}

static bool available(void)
{
	return bike_settings.autofire && bike_gate_open();
}

static bool toggle_input_down(void)
{
	bool down = false;
	const int keys[2] = { bike_settings.autofire_key, bike_settings.autofire_key2 };
	int i, j;

	for (i = 0; i < 2; ++i)
		if (keys[i] > 0 && keys[i] < SDL_SCANCODE_COUNT && keysactive[keys[i]])
			down = true;
	for (j = 0; j < joysticks; ++j)
		if (joystick[j].gamepad != NULL)
			for (i = 0; i < (int)(sizeof pad_toggle_buttons / sizeof pad_toggle_buttons[0]); ++i)
				if (SDL_GetGamepadButton(joystick[j].gamepad, pad_toggle_buttons[i]))
					down = true;
	return down;
}

void bike_controls_toggle(void)
{
	if (!available())
		return;
	latch = !latch;
	ever_used = true;
	++toggles;
}

static void apply_event(const InjectedEvent *e)
{
	switch (e->kind)
	{
	case EV_TOGGLE:
		bike_controls_toggle();
		break;
	case EV_ON:
		if (available() && !latch)
			bike_controls_toggle();
		break;
	case EV_OFF:
		if (available() && latch)
			bike_controls_toggle();
		break;
	case EV_MOUSE_MOVE:
	case EV_MOUSE_DRIVE:
	{
		int x = 0, y = 0;

		if (sscanf(e->arg, "%d,%d", &x, &y) != 2)
			break;
		if (e->kind == EV_MOUSE_MOVE)
		{
			inj_move_x += x;
			inj_move_y += y;
		}
		else
		{
			inj_drive_x = x;
			inj_drive_y = y;
		}
		break;
	}
	case EV_MOUSE_DOWN:
	case EV_MOUSE_UP:
	{
		const uint8_t m = e->arg[0] == 'r' ? SDL_BUTTON_RMASK : e->arg[0] == 'm' ? SDL_BUTTON_MMASK : SDL_BUTTON_LMASK;

		inj_buttons = e->kind == EV_MOUSE_DOWN ? (uint8_t)(inj_buttons | m) : (uint8_t)(inj_buttons & ~m);
		break;
	}
	case EV_KEY_DOWN:
	case EV_KEY_UP:
	{
		const int w = strcmp(e->arg, "next") == 0 ? MEDIA_NEXT : strcmp(e->arg, "prev") == 0 ? MEDIA_PREV : MEDIA_PLAY_PAUSE;

		inj_media[w] = e->kind == EV_KEY_DOWN;
		break;
	}
	case EV_FINGER: /* arg = "finger_xxx,id,x,y" (touch, T3b.3) */
	{
		const char *comma = strchr(e->arg, ',');
		char name[16];

		if (comma != NULL && (size_t)(comma - e->arg) < sizeof name)
		{
			memcpy(name, e->arg, (size_t)(comma - e->arg));
			name[comma - e->arg] = '\0';
			if (bike_touch_inject(name, comma + 1, e->tick))
				break;
		}
		logWarn("bike events: bad finger event '%s'", e->arg);
		break;
	}
	case EV_IMMORTAL: /* regress only: the engine's own invincibility flag, so the ship outlives the level */
		youAreCheating = true;
		break;
	case EV_FIRE_ON:
		injected_fire = true;
		break;
	case EV_FIRE_OFF:
		injected_fire = false;
		break;
	case EV_EQUIP: /* regress only: put gear on player 1 mid-run, then re-init the option state */
		if (strncmp(e->arg, "special=", 8) == 0)
			player[0].items.special = (JE_byte)atoi(e->arg + 8);
		else if (strncmp(e->arg, "sidekicks=", 10) == 0)
		{
			unsigned l = 0, r = 0;

			if (sscanf(e->arg + 10, "%u/%u", &l, &r) == 2)
			{
				player[0].items.sidekick[LEFT_SIDEKICK] = (JE_byte)l;
				player[0].items.sidekick[RIGHT_SIDEKICK] = (JE_byte)r;
			}
		}
		else if (strncmp(e->arg, "rear=", 5) == 0)
			player[0].items.weapon[REAR_WEAPON].id = (JE_word)atoi(e->arg + 5);
		else if (strncmp(e->arg, "shield=", 7) == 0)
		{
			/* shield item N, drained to 0 so the regen path has work to do */
			const unsigned id = (unsigned)atoi(e->arg + 7);

			if (id <= SHIELD_NUM)
			{
				player[0].items.shield = (JE_byte)id;
				shieldT = (JE_byte)(shields[id].tpwr * 20);
				player[0].shield = 0;
				player[0].shield_max = shields[id].mpwr * 2u;
			}
		}
		player[0].last_items = player[0].items;
		JE_drawOptions();
		break;
	case EV_SET:
	{
		char *eq, buf[sizeof e->arg];

		strcpy(buf, e->arg);
		eq = strchr(buf, '=');
		if (eq == NULL)
			break;
		*eq = '\0';
		if (!bike_settings_set(buf, eq + 1))
			logWarn("bike events: bad setting '%s'", e->arg);
		break;
	}
	}
}

void bike_controls_frame(void)
{
	const uint32_t tick = bike_tick_count();
	const bool down = toggle_input_down();

	/* a rising edge flips the latch; edges while unavailable are swallowed */
	if (down && !key_prev)
		bike_controls_toggle();
	key_prev = down;

	while (event_next < event_count && events[event_next].tick <= tick)
		apply_event(&events[event_next++]);

	remote_frame();
	bike_touch_frame();

	if (!bike_settings.autofire)
		latch = false;
}

/* pure: would auto-fire press fire now?  (latch on, available, above the reserve) */
static bool would_fire(unsigned power)
{
	if (!available() || !latch)
		return false;
	return bike_settings.autofire_reserve_pct == 0 ||
	       power >= (unsigned)bike_settings.autofire_reserve_pct * (BIKE_POWER_MAX / 100u);
}

/* the mainint.c hook: also counts what it did for the regress coverage line */
bool bike_controls_autofire(unsigned power)
{
	const bool fire = would_fire(power);

	if (fire)
		++fired_frames;
	else if (available() && latch)
		++held_frames;
	return fire;
}

bool bike_controls_injected_fire(void)
{
	return injected_fire && bike_gate_open();
}

bool bike_autofire_active(void)
{
	return available() && latch;
}

bool bike_autofire_held_back(void)
{
	return bike_autofire_active() && !would_fire((unsigned)power);
}

void bike_controls_log_coverage(void)
{
	logInfo("Bike autofire: toggles=%lu fired=%lu held=%lu", toggles, fired_frames, held_frames);
	logInfo("Bike air: moved=%lu menu_opens=%lu pauses=%lu media=%lu", moved_units, menu_opens, pauses, media_presses);
	bike_touch_log_coverage(); /* after the air line: check_bike.sh reads the first match of a key */
}

bool bike_controls_events_load(const char *path)
{
	FILE *f = fopen(path, "rb");
	char line[256];
	size_t cap = 0;
	uint32_t last = 0;

	if (f == NULL)
	{
		logError("--regress-bike-events: cannot open '%s'", path);
		return false;
	}
	free(events);
	events = NULL;
	event_count = event_next = 0;

	while (fgets(line, sizeof line, f) != NULL)
	{
		char *p = line, *act, *rest;
		unsigned long tick;
		InjectedEvent e;
		size_t n = strlen(line);

		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = '\0';
		while (*p == ' ' || *p == '\t')
			++p;
		if (*p == '\0' || *p == '#' || *p == 't') /* blank, comment, "tick,action" header */
			continue;
		tick = strtoul(p, &act, 10);
		if (act == p || *act != ',' || tick < last)
		{
			logError("--regress-bike-events: bad line '%s' (ticks must ascend)", line);
			fclose(f);
			return false;
		}
		++act;
		rest = strchr(act, ',');
		if (rest != NULL)
			*rest++ = '\0';
		memset(&e, 0, sizeof e);
		e.tick = (uint32_t)tick;
		if (strcmp(act, "autofire_toggle") == 0) e.kind = EV_TOGGLE;
		else if (strcmp(act, "autofire_on") == 0) e.kind = EV_ON;
		else if (strcmp(act, "autofire_off") == 0) e.kind = EV_OFF;
		else if (strcmp(act, "immortal") == 0) e.kind = EV_IMMORTAL;
		else if (strcmp(act, "fire_on") == 0) e.kind = EV_FIRE_ON;
		else if (strcmp(act, "fire_off") == 0) e.kind = EV_FIRE_OFF;
		else if (rest != NULL && strlen(rest) < sizeof e.arg &&
		         ((strcmp(act, "mouse_move") == 0 && (e.kind = EV_MOUSE_MOVE, true)) ||
		          (strcmp(act, "mouse_drive") == 0 && (e.kind = EV_MOUSE_DRIVE, true)) ||
		          (strcmp(act, "mouse_down") == 0 && (e.kind = EV_MOUSE_DOWN, true)) ||
		          (strcmp(act, "mouse_up") == 0 && (e.kind = EV_MOUSE_UP, true)) ||
		          (strcmp(act, "key_down") == 0 && (e.kind = EV_KEY_DOWN, true)) ||
		          (strcmp(act, "key_up") == 0 && (e.kind = EV_KEY_UP, true))))
			strcpy(e.arg, rest);
		else if (rest != NULL && strncmp(act, "finger_", 7) == 0 && strlen(act) < 13 && strlen(rest) + strlen(act) + 2 < sizeof e.arg)
		{
			e.kind = EV_FINGER;
			sprintf(e.arg, "%s,%s", act, rest);
		}
		else if ((strcmp(act, "set") == 0 || strcmp(act, "equip") == 0) && rest != NULL && strlen(rest) < sizeof e.arg)
		{
			e.kind = strcmp(act, "set") == 0 ? EV_SET : EV_EQUIP;
			strcpy(e.arg, rest);
		}
		else
		{
			logError("--regress-bike-events: unknown action in '%s'", line);
			fclose(f);
			return false;
		}
		last = e.tick;
		if (event_count == cap)
		{
			InjectedEvent *grown;

			cap = cap ? cap * 2 : 16;
			grown = realloc(events, cap * sizeof *events);
			if (grown == NULL)
			{
				fclose(f);
				return false;
			}
			events = grown;
		}
		events[event_count++] = e;
	}
	fclose(f);
	return true;
}

bool bike_controls_digest_active(void)
{
	return ever_used;
}

uint64_t bike_controls_digest(void)
{
	uint64_t h = 14695981039346656037ull;
	const uint32_t v[2] = { (latch ? 1u : 0u) | (key_prev ? 2u : 0u), (uint32_t)event_next };
	int i, b;

	for (i = 0; i < 2; ++i)
		for (b = 0; b < 4; ++b)
		{
			h ^= (v[i] >> (8 * b)) & 0xFF;
			h *= 1099511628211ull;
		}
	return h;
}

void bike_controls_shutdown(void)
{
	free(events);
	events = NULL;
	event_count = event_next = 0;
}
