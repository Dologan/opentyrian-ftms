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
#include "joystick.h"
#include "keyboard.h"
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

typedef enum { EV_TOGGLE, EV_ON, EV_OFF, EV_SET, EV_EQUIP, EV_FIRE_ON, EV_FIRE_OFF, EV_IMMORTAL } EventKind;

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

static InjectedEvent *events = NULL;
static size_t event_count = 0, event_next = 0;

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
