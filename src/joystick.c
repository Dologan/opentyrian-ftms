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
#include "joystick.h"

#include "config.h"
#include "config_file.h"
#include "keyboard.h"
#include "logging.h"
#include "network.h"
#include "nortsong.h"
#include "opentyr.h"
#include "regress.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int joystick_axis_threshold(int j, int value);
static int check_assigned(const Joystick *joy, const Joystick_assignment assignment[2]);

const char *assignment_to_code(const Joystick_assignment *assignment);
void code_to_assignment(Joystick_assignment *assignment, const char *buffer);

int joystick_repeat_delay = 300; // milliseconds, repeat delay for buttons
bool joydown = false;            // any joystick buttons down, updated by poll_joysticks()
bool ignore_joystick = false;

int joystick_deadzone_override = -1; // < 0: use the cfg; >= 0: force every joystick

int joysticks = 0;
Joystick *joystick = NULL;

static const int joystick_analog_max = 32767;
#define JOYSTICK_SUBPIXEL 1024                        // sub-pixel units per pixel
#define JOYSTICK_RESPONSE_KNEE (75 * JOYSTICK_SUBPIXEL / 100) // full speed at 75%

// A push whose cross-axis component is within this tangent of the main axis is
// snapped exactly onto that axis so it moves perfectly straight.  The value is
// tan(5 degrees) rounded down (5 degrees is ~1/9 of the 45 degree quadrant); a
// real Deck stick at full deflection reports a few degrees of cross-axis error
// (the reported cases are 0.5-3.5 degrees), while an intentional diagonal is
// 45 degrees, so the cone cannot swallow a deliberate diagonal.
#define JOYSTICK_SNAP_NUM 87
#define JOYSTICK_SNAP_DEN 1000

// eliminates axis movement below the threshold
int joystick_axis_threshold(int j, int value)
{
	assert(j < joysticks);
	
	bool negative = value < 0;
	if (negative)
		value = -value;
	
	if (value <= joystick[j].threshold * 1000)
		return 0;
	
	value -= joystick[j].threshold * 1000;
	
	return negative ? -value : value;
}

// converts joystick axis to sane Tyrian-usable value (based on sensitivity)
int joystick_axis_reduce(int j, int value)
{
	assert(j < joysticks);
	
	value = joystick_axis_threshold(j, value);
	
	if (value == 0)
		return 0;
	
	return value / (3000 - 200 * joystick[j].sensitivity);
}

// integer square root (floor), so the Modern curve needs no libm and stays
// deterministic for demos and network play
static unsigned int joystick_isqrt(unsigned long long value)
{
	unsigned long long x = value, result = 0, bit = 1ULL << 62;
	
	while (bit > x)
		bit >>= 2;
	
	while (bit != 0)
	{
		if (x >= result + bit)
		{
			x -= result + bit;
			result = (result >> 1) + bit;
		}
		else
		{
			result >>= 1;
		}
		bit >>= 2;
	}
	
	return (unsigned int)result;
}

// Modern response: radial dead zone, then a linear ramp from the dead zone to
// the 75% point, applied to the stick magnitude so a diagonal is no faster than
// an axis at the same deflection.  Returns the fraction in 1/1024 units.
int joystick_modern_response(int x, int y, int deadzone_percent)
{
	if (deadzone_percent < JOYSTICK_DEADZONE_MIN)
		deadzone_percent = JOYSTICK_DEADZONE_MIN;
	else if (deadzone_percent > JOYSTICK_DEADZONE_MAX)
		deadzone_percent = JOYSTICK_DEADZONE_MAX;
	
	const int deadzone = deadzone_percent * JOYSTICK_SUBPIXEL / 100;
	
	unsigned long long magnitude = joystick_isqrt((unsigned long long)((long long)x * x + (long long)y * y));
	if (magnitude == 0)
		return 0;
	
	// The stick vector is normalized by the axis range; a diagonal can exceed it,
	// so clamp the magnitude to 1 (full deflection).
	int m = (int)(magnitude * JOYSTICK_SUBPIXEL / joystick_analog_max);
	if (m > JOYSTICK_SUBPIXEL)
		m = JOYSTICK_SUBPIXEL;
	
	if (m <= deadzone)
		return 0;
	
	int s = (int)((long long)(m - deadzone) * JOYSTICK_SUBPIXEL / (JOYSTICK_RESPONSE_KNEE - deadzone));
	if (s > JOYSTICK_SUBPIXEL)
		s = JOYSTICK_SUBPIXEL;
	
	return s;
}

// The Modern ceiling is the joystick's current full-deflection analog speed: the
// original path adds joystick_axis_reduce() to mouseXC, clamps it to +/-30 and
// draws (mouseXC +/- 3) / 4 pixels per tick.  Reusing that formula is the proof
// that 100% of the curve is exactly as fast as the original -- never faster.
int joystick_modern_max_step(int j)
{
	assert(j < joysticks);
	
	int reduced = joystick_axis_reduce(j, joystick_analog_max);
	if (reduced > 30)
		reduced = 30;
	else if (reduced < 0)
		reduced = 0;
	
	return (reduced + 3) / 4;
}

// Modern analog movement in whole pixels per tick.  The sub-pixel remainder is
// kept per device so a very slow stick still creeps instead of sticking, and the
// cursor is reset inside the dead zone so releasing the stick cannot leave a
// stale nudge.
//
// The curve's total desired speed is the legacy full-deflection steady state
// (max step + velocity cap) scaled by s, i.e. v = (max_step + 4) * s.
//   * v <= max_step: move with the sub-pixel step only (velocity target 0), so
//     a slow crawl is steady (no +/-1 momentum bursts);
//   * v >  max_step: step = max_step and momentum target = v - max_step.
// At s = 1 this is exactly the legacy full-deflection path (step 4 + target 4
// for the default sensitivity), so full deflection stays bit-identical.
//
// Both the step and the momentum target are projected per axis exactly like the
// stick vector (x/mag, y/mag, own sub-tick carry), so a cross-axis component
// inside/near the dead zone contributes proportionally (~0) instead of the full
// target.  A near-axis push is snapped onto the axis first so it is exactly
// straight; the snap is applied to the direction only, so the radial speed (s)
// is unchanged.
void joystick_analog_movement(int j, int *dx, int *dy,
                              int *velocity_target_x, int *velocity_target_y)
{
	assert(j < joysticks);
	
	*dx = 0;
	*dy = 0;
	*velocity_target_x = -1;
	*velocity_target_y = -1;
	
	const int x = joystick[j].x, y = joystick[j].y;
	const int s = joystick_modern_response(x, y, joystick[j].deadzone);
	
	if (s == 0)
	{
		joystick[j].analog_subpixel[0] = 0;
		joystick[j].analog_subpixel[1] = 0;
		joystick[j].velocity_target_frac[0] = 0;
		joystick[j].velocity_target_frac[1] = 0;
		return;
	}
	
	// Snap a near-axis push onto the axis: move perfectly straight.
	const int nax = x < 0 ? -x : x;
	const int nay = y < 0 ? -y : y;
	int sx = x, sy = y;
	if ((long long)nay * JOYSTICK_SNAP_DEN <= (long long)nax * JOYSTICK_SNAP_NUM)
		sy = 0;
	else if ((long long)nax * JOYSTICK_SNAP_DEN <= (long long)nay * JOYSTICK_SNAP_NUM)
		sx = 0;
	
	// Magnitude of the snapped direction; s still comes from the raw vector.
	const int snax = sx < 0 ? -sx : sx;
	const int snay = sy < 0 ? -sy : sy;
	const unsigned long long magnitude = joystick_isqrt((unsigned long long)((long long)sx * sx + (long long)sy * sy));
	if (magnitude == 0)
		return;
	
	const int max_step = joystick_modern_max_step(j);
	const int full_speed = max_step + JOYSTICK_MODERN_VELOCITY_MAX;
	
	// Desired total speed in 1/1024 px/tick, and the step/target split.
	const long long desired = (long long)s * full_speed;
	long long step_fixed, target_fixed;
	if (desired <= (long long)max_step * JOYSTICK_SUBPIXEL)
	{
		step_fixed = desired;
		target_fixed = 0;
	}
	else
	{
		step_fixed = (long long)max_step * JOYSTICK_SUBPIXEL;
		target_fixed = desired - step_fixed;
	}
	
	// Step: the same projection the stick vector uses, with a sub-pixel carry.
	joystick[j].analog_subpixel[0] += (int)(step_fixed * sx / (long long)magnitude);
	joystick[j].analog_subpixel[1] += (int)(step_fixed * sy / (long long)magnitude);
	
	*dx = joystick[j].analog_subpixel[0] / JOYSTICK_SUBPIXEL;
	*dy = joystick[j].analog_subpixel[1] / JOYSTICK_SUBPIXEL;
	
	joystick[j].analog_subpixel[0] -= *dx * JOYSTICK_SUBPIXEL;
	joystick[j].analog_subpixel[1] -= *dy * JOYSTICK_SUBPIXEL;
	
	// Momentum: project the target per axis, each with its own sub-tick carry so
	// the integer velocity can average a fractional target.  The carry persists
	// across ticks; it is only cleared in the step-only and dead-zone regimes.
	if (target_fixed > 0)
	{
		const long long tx_fixed = target_fixed * snax / (long long)magnitude;
		const long long ty_fixed = target_fixed * snay / (long long)magnitude;
		
		long long tx = tx_fixed / JOYSTICK_SUBPIXEL;
		joystick[j].velocity_target_frac[0] += (int)(tx_fixed % JOYSTICK_SUBPIXEL);
		if (joystick[j].velocity_target_frac[0] >= JOYSTICK_SUBPIXEL)
		{
			++tx;
			joystick[j].velocity_target_frac[0] -= JOYSTICK_SUBPIXEL;
		}
		
		long long ty = ty_fixed / JOYSTICK_SUBPIXEL;
		joystick[j].velocity_target_frac[1] += (int)(ty_fixed % JOYSTICK_SUBPIXEL);
		if (joystick[j].velocity_target_frac[1] >= JOYSTICK_SUBPIXEL)
		{
			++ty;
			joystick[j].velocity_target_frac[1] -= JOYSTICK_SUBPIXEL;
		}
		
		*velocity_target_x = (int)tx;
		*velocity_target_y = (int)ty;
	}
	else
	{
		// Step-only regime: hold the momentum at 0 (steady crawl).
		joystick[j].velocity_target_frac[0] = 0;
		joystick[j].velocity_target_frac[1] = 0;
		*velocity_target_x = 0;
		*velocity_target_y = 0;
	}
}

// converts analog joystick axes to an angle
// returns false if axes are centered (there is no angle)
bool joystick_analog_angle(int j, float *angle)
{
	assert(j < joysticks);
	
	float x = joystick_axis_threshold(j, joystick[j].x), y = joystick_axis_threshold(j, joystick[j].y);
	
	if (x != 0)
	{
		*angle += atanf(-y / x);
		*angle += (x < 0) ? -M_PI_2 : M_PI_2;
		return true;
	}
	else if (y != 0)
	{
		*angle += y < 0 ? M_PI : 0;
		return true;
	}
	
	return false;
}

/* gives back value 0..joystick_analog_max indicating that one of the assigned
 * buttons has been pressed or that one of the assigned axes/hats has been moved
 * in the assigned direction
 */
static int check_assigned(const Joystick *joy, const Joystick_assignment assignment[2])
{
	int result = 0;
	
	for (int i = 0; i < 2; i++)
	{
		int temp = 0;
		
		switch (assignment[i].type)
		{
		case NONE:
			continue;
			
		case AXIS:
			temp = SDL_GetJoystickAxis(joy->handle, assignment[i].num);
			
			if (assignment[i].negative_axis)
				temp = -temp;
			break;
		
		case BUTTON:
			temp = SDL_GetJoystickButton(joy->handle, assignment[i].num) ? joystick_analog_max : 0;
			break;
		
		case HAT:
			temp = SDL_GetJoystickHat(joy->handle, assignment[i].num);
			
			if (assignment[i].x_axis)
				temp &= SDL_HAT_LEFT | SDL_HAT_RIGHT;
			else
				temp &= SDL_HAT_UP | SDL_HAT_DOWN;
			
			if (assignment[i].negative_axis)
				temp &= SDL_HAT_LEFT | SDL_HAT_UP;
			else
				temp &= SDL_HAT_RIGHT | SDL_HAT_DOWN;
			
			temp = temp ? joystick_analog_max : 0;
			break;
		
		case GAMEPAD_BUTTON:
			if (joy->gamepad == NULL)
				continue;
			
			temp = SDL_GetGamepadButton(joy->gamepad, (SDL_GamepadButton)assignment[i].num) ? joystick_analog_max : 0;
			break;
		
		case GAMEPAD_AXIS:
			if (joy->gamepad == NULL)
				continue;
			
			temp = SDL_GetGamepadAxis(joy->gamepad, (SDL_GamepadAxis)assignment[i].num);
			
			if (assignment[i].negative_axis)
				temp = -temp;
			break;
		}
		
		if (temp > result)
			result = temp;
	}
	
	return result;
}

// updates joystick state
void poll_joystick(int j)
{
	assert(j < joysticks);
	
	// A synthetic stick (--regress-stick) holds its raw axes directly and never
	// touches SDL; the harness reads them like a real device.
	if (joystick[j].injected)
		return;
	
	if (joystick[j].handle == NULL)
		return;
	
	SDL_UpdateJoysticks();
	
	// indicates that a direction/action was pressed since last poll
	joystick[j].input_pressed = false;
	
	// indicates that an direction/action has been held long enough to fake a repeat press
	bool repeat = joystick[j].joystick_delay < (Uint32)SDL_GetTicks();
	
	// update direction state
	for (uint d = 0; d < COUNTOF(joystick[j].direction); d++)
	{
		bool old = joystick[j].direction[d];
		
		joystick[j].analog_direction[d] = check_assigned(&joystick[j], joystick[j].assignment[d]);
		joystick[j].direction[d] = joystick[j].analog_direction[d] > (joystick_analog_max / 2);
		joydown |= joystick[j].direction[d];
		
		joystick[j].direction_pressed[d] = joystick[j].direction[d] && (!old || repeat);
		joystick[j].input_pressed |= joystick[j].direction_pressed[d];
	}
	
	joystick[j].x = -joystick[j].analog_direction[3] + joystick[j].analog_direction[1];
	joystick[j].y = -joystick[j].analog_direction[0] + joystick[j].analog_direction[2];
	
	// update action state
	for (uint d = 0; d < COUNTOF(joystick[j].action); d++)
	{
		bool old = joystick[j].action[d];
		
		joystick[j].action[d] = check_assigned(&joystick[j], joystick[j].assignment[d + COUNTOF(joystick[j].direction)]) > (joystick_analog_max / 2);
		joydown |= joystick[j].action[d];
		
		joystick[j].action_pressed[d] = joystick[j].action[d] && (!old || repeat);
		joystick[j].input_pressed |= joystick[j].action_pressed[d];
	}
	
	joystick[j].confirm = joystick[j].action[0] || joystick[j].action[4];
	joystick[j].cancel = joystick[j].action[1] || joystick[j].action[5];
	
	// if new input, reset press-repeat delay
	if (joystick[j].input_pressed)
		joystick[j].joystick_delay = (Uint32)SDL_GetTicks() + joystick_repeat_delay;
}

// updates all joystick states
void poll_joysticks(void)
{
	// Regression mode must never read a real controller, even one hot-plugged
	// mid-run.  init_joysticks() already leaves the device array empty via
	// ignore_joystick; this keeps the poll itself inert as well.
	if (regress_active())
		return;
	
	joydown = false;
	
	for (int j = 0; j < joysticks; j++)
		poll_joystick(j);
}

// sends SDL KEYDOWN and KEYUP events for a key
void push_key(SDL_Scancode key)
{
	SDL_Event e;
	
	memset(&e.key, 0, sizeof(e.key));
	
	e.key.scancode = key;
	
	e.key.down = true;
	e.type = SDL_EVENT_KEY_DOWN;
	SDL_PushEvent(&e);
	
	e.key.down = false;
	e.type = SDL_EVENT_KEY_UP;
	SDL_PushEvent(&e);
}

// helps us be lazy by pretending joysticks are a keyboard (useful for menus)
void push_joysticks_as_keyboard(void)
{
	const SDL_Scancode confirm = SDL_SCANCODE_RETURN, cancel = SDL_SCANCODE_ESCAPE;
	const SDL_Scancode direction[4] = { SDL_SCANCODE_UP, SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT };
	
	poll_joysticks();
	
	for (int j = 0; j < joysticks; j++)
	{
		if (!joystick[j].input_pressed)
			continue;
		
		if (joystick[j].confirm)
			push_key(confirm);
		if (joystick[j].cancel)
			push_key(cancel);
		
		for (uint d = 0; d < COUNTOF(joystick[j].direction_pressed); d++)
		{
			if (joystick[j].direction_pressed[d])
				push_key(direction[d]);
		}
	}
}

// opens a device, preferring the Gamepad API when SDL recognises it as a gamepad
static bool joystick_open_device(Joystick *joy, SDL_JoystickID id)
{
	memset(joy, 0, sizeof(*joy));
	joy->id = id;
	
	if (SDL_IsGamepad(id))
	{
		joy->gamepad = SDL_OpenGamepad(id);
		if (joy->gamepad == NULL)
			return false;
		
		joy->handle = SDL_GetGamepadJoystick(joy->gamepad);
		joy->is_gamepad = true;
	}
	else
	{
		joy->handle = SDL_OpenJoystick(id);
		if (joy->handle == NULL)
			return false;
	}
	
	return true;
}

static void joystick_close_device(Joystick *joy)
{
	if (joy->gamepad != NULL)
		SDL_CloseGamepad(joy->gamepad);
	else if (joy->handle != NULL)
		SDL_CloseJoystick(joy->handle);
	
	joy->gamepad = NULL;
	joy->handle = NULL;
	joy->is_gamepad = false;
}

static int joystick_find_id(SDL_JoystickID id)
{
	for (int j = 0; j < joysticks; j++)
	{
		if (joystick[j].id == id)
			return j;
	}
	
	return -1;
}

static void joystick_log_device(const Joystick *joy, const char *verb)
{
	if (joy->is_gamepad)
	{
		logInfo("Gamepad %s: %s", verb, SDL_GetGamepadName(joy->gamepad));
	}
	else
	{
		logInfo("Joystick %s: %s (%d axes, %d buttons, %d hats)",
			verb, SDL_GetJoystickName(joy->handle),
			SDL_GetNumJoystickAxes(joy->handle),
			SDL_GetNumJoystickButtons(joy->handle),
			SDL_GetNumJoystickHats(joy->handle));
	}
}

// keeps the per-player device selection within range after the joystick array changes
static void joystick_reindex_input_devices(int removed, int old_count)
{
	for (size_t i = 0; i < COUNTOF(inputDevice); ++i)
	{
		if (inputDevice[i] < 3)
			continue;
		
		int index = inputDevice[i] - 3;
		
		if (index == removed)
			inputDevice[i] = 1;  // the selected controller is gone; use the keyboard
		else if (index > removed)
			inputDevice[i]--;    // the array shifted down
		
		if (inputDevice[i] - 3 >= old_count - 1)
			inputDevice[i] = 1;
	}
}

// Installs one synthetic analog stick at a fixed raw axis position.  Used only
// by regress mode (--regress-stick): the harness never opens a real controller,
// yet JE_playerMovement then runs the whole analog path headless.  The raw x/y
// feed joystick_axis_reduce()/joystick_analog_movement() exactly like a device
// would, so Classic and Modern can be compared tick by tick.
void joystick_inject_stick(int x, int y)
{
	if (joysticks == 0)
	{
		joystick = malloc(sizeof(*joystick));
		if (joystick == NULL)
			return;
		memset(joystick, 0, sizeof(*joystick));
		joysticks = 1;
	}
	
	Joystick *const joy = &joystick[0];
	joy->injected = true;
	joy->is_gamepad = false;
	joy->handle = NULL;
	joy->gamepad = NULL;
	joy->analog = true;
	joy->sensitivity = 5;
	joy->threshold = 5;
	joy->deadzone = JOYSTICK_DEADZONE_DEFAULT;
	joy->x = x;
	joy->y = y;
	joy->analog_subpixel[0] = 0;
	joy->analog_subpixel[1] = 0;
	joy->velocity_target_frac[0] = 0;
	joy->velocity_target_frac[1] = 0;
}

// initializes SDL joystick system and loads assignments for joysticks found
void init_joysticks(void)
{
	if (ignore_joystick)
		return;
	
	// SDL_INIT_GAMEPAD implies SDL_INIT_JOYSTICK.
	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
	{
		logWarn("Failed to initialize SDL joystick: %s", SDL_GetError());
		ignore_joystick = true;
		return;
	}
	
	// Hot-plug detection needs joystick and gamepad events; motion events are
	// ignored by the input code, which reads the device state directly.
	SDL_SetJoystickEventsEnabled(true);
	SDL_SetGamepadEventsEnabled(true);
	
	int joystickCount = 0;
	SDL_JoystickID *joystickIds = SDL_GetJoysticks(&joystickCount);
	joysticks = 0;
	joystick = NULL;
	
	for (int i = 0; i < joystickCount; i++)
		joystick_device_added(joystickIds[i]);
	
	SDL_free(joystickIds);
	
	if (joysticks == 0)
		logInfo("No joysticks detected.");
}

// deinitializes SDL joystick system and saves joystick assignments
void deinit_joysticks(void)
{
	if (ignore_joystick)
	{
		// A regress run never opens a device, but --regress-stick installs a
		// synthetic one; release it here so it cannot leak.
		free(joystick);
		joystick = NULL;
		joysticks = 0;
		return;
	}
	
	for (int j = 0; j < joysticks; j++)
	{
		if (joystick[j].handle != NULL)
			save_joystick_assignments(&opentyrian_config, j);
		
		joystick_close_device(&joystick[j]);
	}
	
	free(joystick);
	joystick = NULL;
	joysticks = 0;
	
	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

// hot-plug: a device was connected
void joystick_device_added(SDL_JoystickID id)
{
	if (ignore_joystick)
		return;
	
	// A gamepad generates both a joystick and a gamepad added event.
	if (joystick_find_id(id) >= 0)
		return;
	
	Joystick *resized = realloc(joystick, (joysticks + 1) * sizeof(*joystick));
	if (resized == NULL)
	{
		logWarn("Failed to allocate joystick %u.", (unsigned)id);
		return;
	}
	joystick = resized;
	
	Joystick *const joy = &joystick[joysticks];
	if (!joystick_open_device(joy, id))
	{
		logWarn("Failed to open joystick %u: %s", (unsigned)id, SDL_GetError());
		return;
	}
	
	joysticks++;
	
	joystick_log_device(joy, "connected");
	
	if (!load_joystick_assignments(&opentyrian_config, joysticks - 1))
		reset_joystick_assignments(joysticks - 1);
}

// hot-plug: a device was disconnected
void joystick_device_removed(SDL_JoystickID id)
{
	if (ignore_joystick)
		return;
	
	// A gamepad generates both a joystick and a gamepad removed event.
	int index = joystick_find_id(id);
	if (index < 0)
		return;
	
	joystick_log_device(&joystick[index], "disconnected");
	
	if (joystick[index].handle != NULL)
		save_joystick_assignments(&opentyrian_config, index);
	
	joystick_close_device(&joystick[index]);
	
	for (int j = index; j < joysticks - 1; j++)
		joystick[j] = joystick[j + 1];
	
	joysticks--;
	
	joystick_reindex_input_devices(index, joysticks + 1);
	
	if (joysticks == 0)
	{
		free(joystick);
		joystick = NULL;
	}
	else
	{
		Joystick *resized = realloc(joystick, joysticks * sizeof(*joystick));
		if (resized != NULL)
			joystick = resized;
	}
}

static void set_assignment(Joystick_assignment *assignment, Joystick_assignment_types type, int num, bool negative_axis)
{
	assignment->type = type;
	assignment->num = num;
	assignment->x_axis = false;
	assignment->negative_axis = negative_axis;
}

void reset_joystick_assignments(int j)
{
	assert(j < joysticks);
	
	// clear assignments
	for (uint a = 0; a < COUNTOF(joystick[j].assignment); a++)
	{
		for (uint i = 0; i < COUNTOF(joystick[j].assignment[a]); i++)
			joystick[j].assignment[a][i].type = NONE;
	}
	
	if (joystick[j].is_gamepad)
	{
		// left stick and D-pad both drive movement
		set_assignment(&joystick[j].assignment[0][0], GAMEPAD_AXIS, SDL_GAMEPAD_AXIS_LEFTY, true);   // up
		set_assignment(&joystick[j].assignment[1][0], GAMEPAD_AXIS, SDL_GAMEPAD_AXIS_LEFTX, false);  // right
		set_assignment(&joystick[j].assignment[2][0], GAMEPAD_AXIS, SDL_GAMEPAD_AXIS_LEFTY, false);  // down
		set_assignment(&joystick[j].assignment[3][0], GAMEPAD_AXIS, SDL_GAMEPAD_AXIS_LEFTX, true);   // left
		
		set_assignment(&joystick[j].assignment[0][1], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_UP, false);
		set_assignment(&joystick[j].assignment[1][1], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
		set_assignment(&joystick[j].assignment[2][1], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
		set_assignment(&joystick[j].assignment[3][1], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_LEFT, false);
		
		set_assignment(&joystick[j].assignment[4][0], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_SOUTH, false);          // fire
		set_assignment(&joystick[j].assignment[5][0], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_EAST, false);           // change fire
		set_assignment(&joystick[j].assignment[6][0], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_NORTH, false);          // left sidekick
		set_assignment(&joystick[j].assignment[6][1], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);  // left sidekick (alt)
		set_assignment(&joystick[j].assignment[7][0], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_WEST, false);           // right sidekick
		set_assignment(&joystick[j].assignment[7][1], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false); // right sidekick (alt)
		set_assignment(&joystick[j].assignment[8][0], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_START, false);          // in-game menu
		set_assignment(&joystick[j].assignment[9][0], GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_BACK, false);           // pause
		
		joystick[j].analog = true;
	}
	else
	{
		// legacy defaults: first 2 axes, first hat, first 6 buttons
		for (uint a = 0; a < COUNTOF(joystick[j].assignment); a++)
		{
			if (a < 4)
			{
				if (SDL_GetNumJoystickAxes(joystick[j].handle) >= 2)
				{
					joystick[j].assignment[a][0].type = AXIS;
					joystick[j].assignment[a][0].num = (a + 1) % 2;
					joystick[j].assignment[a][0].negative_axis = (a == 0 || a == 3);
				}
				
				if (SDL_GetNumJoystickHats(joystick[j].handle) >= 1)
				{
					joystick[j].assignment[a][1].type = HAT;
					joystick[j].assignment[a][1].num = 0;
					joystick[j].assignment[a][1].x_axis = (a == 1 || a == 3);
					joystick[j].assignment[a][1].negative_axis = (a == 0 || a == 3);
				}
			}
			else
			{
				if (a - 4 < (unsigned)SDL_GetNumJoystickButtons(joystick[j].handle))
				{
					joystick[j].assignment[a][0].type = BUTTON;
					joystick[j].assignment[a][0].num = a - 4;
				}
			}
		}
		
		joystick[j].analog = false;
	}
	
	joystick[j].sensitivity = 5;
	joystick[j].threshold = 5;
	joystick[j].deadzone = joystick_deadzone_override >= 0
		? joystick_deadzone_override
		: JOYSTICK_DEADZONE_DEFAULT;
	joystick[j].analog_subpixel[0] = 0;
	joystick[j].analog_subpixel[1] = 0;
	joystick[j].velocity_target_frac[0] = 0;
	joystick[j].velocity_target_frac[1] = 0;
}

static const char* const assignment_names[] =
{
	"up",
	"right",
	"down",
	"left",
	"fire",
	"change fire",
	"left sidekick",
	"right sidekick",
	"menu",
	"pause",
};

bool load_joystick_assignments(Config *config, int j)
{
	ConfigSection *section = config_find_section(config, "joystick", SDL_GetJoystickName(joystick[j].handle));
	if (section == NULL)
		return false;
	
	if (!config_get_bool_option(section, "analog", &joystick[j].analog))
		joystick[j].analog = false;
	
	joystick[j].sensitivity = config_get_or_set_int_option(section, "sensitivity", 5);

	joystick[j].threshold = config_get_or_set_int_option(section, "threshold", 5);
	
	joystick[j].deadzone = config_get_or_set_int_option(section, "deadzone", JOYSTICK_DEADZONE_DEFAULT);
	if (joystick[j].deadzone < JOYSTICK_DEADZONE_MIN)
		joystick[j].deadzone = JOYSTICK_DEADZONE_MIN;
	else if (joystick[j].deadzone > JOYSTICK_DEADZONE_MAX)
		joystick[j].deadzone = JOYSTICK_DEADZONE_MAX;
	
	if (joystick_deadzone_override >= 0)
		joystick[j].deadzone = joystick_deadzone_override;
	
	for (size_t a = 0; a < COUNTOF(assignment_names); ++a)
	{
		for (unsigned int i = 0; i < COUNTOF(joystick[j].assignment[a]); ++i)
			joystick[j].assignment[a][i].type = NONE;
		
		ConfigOption *option = config_get_option(section, assignment_names[a]);
		if (option == NULL)
			continue;
		
		foreach_option_i_value(i, value, option)
		{
			if (i >= COUNTOF(joystick[j].assignment[a]))
				break;
			
			code_to_assignment(&joystick[j].assignment[a][i], value);
		}
	}
	
	return true;
}

bool save_joystick_assignments(Config *config, int j)
{
	ConfigSection *section = config_find_or_add_section(config, "joystick", SDL_GetJoystickName(joystick[j].handle));
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory
	
	config_set_bool_option(section, "analog", joystick[j].analog, NO_YES);
	
	config_set_int_option(section, "sensitivity", joystick[j].sensitivity);
	
	config_set_int_option(section, "threshold", joystick[j].threshold);
	
	config_set_int_option(section, "deadzone", joystick[j].deadzone);
	
	for (size_t a = 0; a < COUNTOF(assignment_names); ++a)
	{
		ConfigOption *option = config_set_option(section, assignment_names[a], NULL);
		if (option == NULL)
			exit(EXIT_FAILURE);  // out of memory
		
		option = config_set_value(option, NULL);
		if (option == NULL)
			exit(EXIT_FAILURE);  // out of memory

		for (size_t i = 0; i < COUNTOF(joystick[j].assignment[a]); ++i)
		{
			if (joystick[j].assignment[a][i].type == NONE)
				continue;
			
			option = config_add_value(option, assignment_to_code(&joystick[j].assignment[a][i]));
			if (option == NULL)
				exit(EXIT_FAILURE);  // out of memory
		}
	}
	
	return true;
}

// fills buffer with comma separated list of assigned joystick functions
void joystick_assignments_to_string(char *buffer, size_t buffer_len, const Joystick_assignment *assignments)
{
	if (buffer_len == 0)
		return;
	
	buffer[0] = '\0';
	
	bool comma = false;
	for (uint i = 0; i < COUNTOF(*joystick->assignment); ++i)
	{
		if (assignments[i].type == NONE)
			continue;
		
		int written = snprintf(buffer, buffer_len, "%s%s",
		                       comma ? ", " : "",
		                       assignment_to_code(&assignments[i]));
		if (written < 0 || (size_t)written >= buffer_len)
			break;
		
		buffer += written;
		buffer_len -= written;
		
		comma = true;
	}
}

// Display-only abbreviations; the persisted assignment codes stay unchanged.
void joystick_assignment_short_label(char *buffer, size_t buffer_len, const Joystick_assignment *assignment)
{
	static const char *const buttons[] = {
		"A", "B", "X", "Y", "BACK", "GUIDE", "START", "LS", "RS",
		"LB", "RB", "UP", "DOWN", "LEFT", "RIGHT"
	};
	static const char *const axes[] = { "LX", "LY", "RX", "RY", "LT", "RT" };
	const int n = assignment->num;
	const char sign = assignment->negative_axis ? '-' : '+';
	switch (assignment->type)
	{
	case GAMEPAD_BUTTON:
		if (n >= 0 && (size_t)n < COUNTOF(buttons))
			snprintf(buffer, buffer_len, "%s", buttons[n]);
		else
			snprintf(buffer, buffer_len, "GB%d", n);
		break;
	case GAMEPAD_AXIS:
		if (n >= 0 && (size_t)n < COUNTOF(axes))
			snprintf(buffer, buffer_len, "%s%c", axes[n], sign);
		else
			snprintf(buffer, buffer_len, "GA%d%c", n, sign);
		break;
	case BUTTON:
		snprintf(buffer, buffer_len, "B%u", (unsigned)n + 1u);
		break;
	case AXIS:
		snprintf(buffer, buffer_len, "AX%u%c", (unsigned)n + 1u, sign);
		break;
	case HAT:
		snprintf(buffer, buffer_len, "H%u%c%c", (unsigned)n + 1u, assignment->x_axis ? 'X' : 'Y', sign);
		break;
	case NONE:
		if (buffer_len > 0)
			buffer[0] = '\0';
		break;
	}
}

// reverse of assignment_to_code()
void code_to_assignment(Joystick_assignment *assignment, const char *buffer)
{
	memset(assignment, 0, sizeof(*assignment));
	
	char axis = 0, direction = 0, gamepad_name[32] = "";
	
	if (sscanf(buffer, " AX %d%c", &assignment->num, &direction) == 2)
	{
		assignment->type = AXIS;
		
		if (assignment->num == 0)
			assignment->type = NONE;
		else
			--assignment->num;
	}
	else if (sscanf(buffer, " BTN %d", &assignment->num) == 1)
	{
		assignment->type = BUTTON;
		
		if (assignment->num == 0)
			assignment->type = NONE;
		else
			--assignment->num;
	}
	else if (sscanf(buffer, " H %d%c%c", &assignment->num, &axis, &direction) == 3)
	{
		assignment->type = HAT;
		
		if (assignment->num == 0)
			assignment->type = NONE;
		else
			--assignment->num;
	}
	else if (sscanf(buffer, " GB %31s", gamepad_name) == 1)
	{
		SDL_GamepadButton button = SDL_GetGamepadButtonFromString(gamepad_name);
		if (button != SDL_GAMEPAD_BUTTON_INVALID)
		{
			assignment->type = GAMEPAD_BUTTON;
			assignment->num = button;
		}
	}
	else if (sscanf(buffer, " GA %31s", gamepad_name) == 1)
	{
		size_t len = strlen(gamepad_name);
		if (len > 0 && (gamepad_name[len - 1] == '+' || gamepad_name[len - 1] == '-'))
		{
			direction = gamepad_name[len - 1];
			gamepad_name[len - 1] = '\0';
		}
		
		SDL_GamepadAxis gamepad_axis = SDL_GetGamepadAxisFromString(gamepad_name);
		if (gamepad_axis != SDL_GAMEPAD_AXIS_INVALID)
		{
			assignment->type = GAMEPAD_AXIS;
			assignment->num = gamepad_axis;
		}
	}
	
	assignment->x_axis = (toupper((unsigned char)axis) == 'X');
	assignment->negative_axis = (toupper((unsigned char)direction) == '-');
}

/* gives the short identifier for a joystick assignment
 * 
 * gamepad assignments are named after the SDL button/axis (e.g. "GB a",
 * "GA lefty-"); legacy raw joystick assignments keep their number-based codes
 */
const char *assignment_to_code(const Joystick_assignment *assignment)
{
	static char name[24];
	
	switch (assignment->type)
	{
	case NONE:
		strcpy(name, "");
		break;
		
	case AXIS:
		snprintf(name, sizeof(name), "AX %d%c",
		         assignment->num + 1,
		         assignment->negative_axis ? '-' : '+');
		break;
		
	case BUTTON:
		snprintf(name, sizeof(name), "BTN %d",
		         assignment->num + 1);
		break;
		
	case HAT:
		snprintf(name, sizeof(name), "H %d%c%c",
		         assignment->num + 1,
		         assignment->x_axis ? 'X' : 'Y',
		         assignment->negative_axis ? '-' : '+');
		break;
		
	case GAMEPAD_BUTTON:
		snprintf(name, sizeof(name), "GB %s",
		         SDL_GetGamepadStringForButton((SDL_GamepadButton)assignment->num));
		break;
		
	case GAMEPAD_AXIS:
		snprintf(name, sizeof(name), "GA %s%c",
		         SDL_GetGamepadStringForAxis((SDL_GamepadAxis)assignment->num),
		         assignment->negative_axis ? '-' : '+');
		break;
	}
	
	return name;
}

// captures gamepad input (by button/axis name) for configuring assignments
static bool detect_gamepad_assignment(int j, Joystick_assignment *assignment)
{
	SDL_Gamepad *const gamepad = joystick[j].gamepad;
	if (gamepad == NULL)
		return false;
	
	bool button[SDL_GAMEPAD_BUTTON_COUNT];
	for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
		button[i] = SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)i);
	
	Sint16 axis[SDL_GAMEPAD_AXIS_COUNT];
	for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i)
		axis[i] = SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)i);
	
	bool detected = false;
	
	while (true)
	{
		setFrameCount(1);
		
		NETWORK_KEEP_ALIVE();
		
		delayUntilElapsed();
		
		handleSdlEvents();
		
		for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i)
		{
			Sint16 temp = SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)i);
			
			if (abs(temp - axis[i]) > joystick_analog_max * 2 / 3)
			{
				assignment->type = GAMEPAD_AXIS;
				assignment->num = i;
				assignment->negative_axis = temp < axis[i];
				detected = true;
				break;
			}
		}
		
		for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
		{
			bool new_button = SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)i);
			
			if (new_button == button[i])
				continue;
			
			if (!new_button) // button was released
			{
				button[i] = false;
			}
			else             // button was pressed
			{
				assignment->type = GAMEPAD_BUTTON;
				assignment->num = i;
				detected = true;
				break;
			}
		}
		
		if (detected || hasInput(INPUT_NO_MOTION))
			break;
	}
	
	return detected;
}

// captures raw joystick input for configuring assignments
// returns false if non-joystick input was detected
// TODO: input from joystick other than the one being configured probably should not be ignored
static bool detect_legacy_assignment(int j, Joystick_assignment *assignment)
{
	// get initial joystick state to compare against to see if anything was pressed
	
	const int axes = SDL_GetNumJoystickAxes(joystick[j].handle);
	Sint16 *axis = malloc(axes * sizeof(*axis));
	for (int i = 0; i < axes; i++)
		axis[i] = SDL_GetJoystickAxis(joystick[j].handle, i);
	
	const int buttons = SDL_GetNumJoystickButtons(joystick[j].handle);
	Uint8 *button = malloc(buttons * sizeof(*button));
	for (int i = 0; i < buttons; i++)
		button[i] = SDL_GetJoystickButton(joystick[j].handle, i);
	
	const int hats = SDL_GetNumJoystickHats(joystick[j].handle);
	Uint8 *hat = malloc(hats * sizeof(*hat));
	for (int i = 0; i < hats; i++)
		hat[i] = SDL_GetJoystickHat(joystick[j].handle, i);
	
	bool detected = false;
	
	while (true)
	{
		setFrameCount(1);
		
		NETWORK_KEEP_ALIVE();

		delayUntilElapsed();

		handleSdlEvents();

		for (int i = 0; i < axes; ++i)
		{
			Sint16 temp = SDL_GetJoystickAxis(joystick[j].handle, i);
			
			if (abs(temp - axis[i]) > joystick_analog_max * 2 / 3)
			{
				assignment->type = AXIS;
				assignment->num = i;
				assignment->negative_axis = temp < axis[i];
				detected = true;
				break;
			}
		}
		
		for (int i = 0; i < buttons; ++i)
		{
			Uint8 new_button = SDL_GetJoystickButton(joystick[j].handle, i),
			      changed = button[i] ^ new_button;
			
			if (!changed)
				continue;
			
			if (new_button == 0) // button was released
			{
				button[i] = new_button;
			}
			else                 // button was pressed
			{
				assignment->type = BUTTON;
				assignment->num = i;
				detected = true;
				break;
			}
		}
		
		for (int i = 0; i < hats; ++i)
		{
			Uint8 new_hat = SDL_GetJoystickHat(joystick[j].handle, i),
			      changed = hat[i] ^ new_hat;
			
			if (!changed)
				continue;
			
			if ((new_hat & changed) == SDL_HAT_CENTERED) // hat was centered
			{
				hat[i] = new_hat;
			}
			else
			{
				assignment->type = HAT;
				assignment->num = i;
				assignment->x_axis = changed & (SDL_HAT_LEFT | SDL_HAT_RIGHT);
				assignment->negative_axis = changed & (SDL_HAT_LEFT | SDL_HAT_UP);
				detected = true;
			}
		}
		
		if (detected || hasInput(INPUT_NO_MOTION))
			break;
	}
	
	free(axis);
	free(button);
	free(hat);
	
	return detected;
}

// captures joystick/gamepad input for configuring assignments
// returns false if non-joystick input was detected
bool detect_joystick_assignment(int j, Joystick_assignment *assignment)
{
	if (joystick[j].is_gamepad)
		return detect_gamepad_assignment(j, assignment);
	
	return detect_legacy_assignment(j, assignment);
}

// compares relevant parts of joystick assignments for equality
bool joystick_assignment_cmp(const Joystick_assignment *a, const Joystick_assignment *b)
{
	if (a->type == b->type)
	{
		switch (a->type)
		{
		case NONE:
			return true;
		case AXIS:
			return (a->num == b->num) &&
			       (a->negative_axis == b->negative_axis);
		case BUTTON:
			return (a->num == b->num);
		case HAT:
			return (a->num == b->num) &&
			       (a->x_axis == b->x_axis) &&
			       (a->negative_axis == b->negative_axis);
		case GAMEPAD_BUTTON:
			return (a->num == b->num);
		case GAMEPAD_AXIS:
			return (a->num == b->num) &&
			       (a->negative_axis == b->negative_axis);
		}
	}
	
	return false;
}
