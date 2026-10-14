#include "input_probe.h"

#include "file.h"
#include "fonthand.h"
#include "logging.h"
#include "palette.h"
#include "sprite.h"
#include "video.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

bool input_probe_requested = false;

#define PROBE_LOG_NAME      "input-probe.log"
#define PROBE_DURATION_MS   (5u * 60u * 1000u)
#define PROBE_ESC_PRESSES   3
#define PROBE_ESC_WINDOW_MS 2000u
#define PROBE_SCREEN_LINES  12
#define PROBE_LINE_LEN      192
#define PROBE_FIELDS_LEN    160
#define PROBE_TEXT_MAX_W    312
#define PROBE_FRAME_MS      16u

typedef struct
{
	File log;
	bool log_open;
	bool quit;
	bool relative;  // what F2 last set; checked against SDL when the run ends
	unsigned long events;

	char screen[PROBE_SCREEN_LINES][PROBE_LINE_LEN];  // ring of the last event lines
	size_t screen_next;
	size_t screen_count;

	Uint64 esc[PROBE_ESC_PRESSES];  // ring of the last Escape press times (ms)
	size_t esc_next;
	size_t esc_count;

	// Motion summary: accumulated over the current second, then published.
	Uint64 summary_start;
	unsigned long motion_count;
	double motion_xrel;
	double motion_yrel;
	unsigned long shown_count;  // last full second, drawn in the status line
	double shown_xrel;
	double shown_yrel;
} ProbeState;

static ProbeState probe;

static const char *probeStr(const char *s)
{
	return s != NULL ? s : "?";
}

// Key and scancode names are empty for keys SDL does not name.
static const char *probeName(const char *s)
{
	return (s != NULL && s[0] != '\0') ? s : "-";
}

// Printable ASCII only.  '~' is the game's colour toggle in JE_outText, so it
// is replaced too.
static void probeSanitize(char *dst, size_t size, const char *src)
{
	size_t i = 0;

	for (; src[i] != '\0' && i + 1 < size; ++i)
	{
		const unsigned char c = (unsigned char)src[i];
		dst[i] = (c >= 32 && c < 127 && c != '~') ? (char)c : '?';
	}
	dst[i] = '\0';
}

// Draws text on the 320x200 surface, cut so it never runs past the right edge
// (the sprite blitter does not clip).
static void probeScreenText(int x, int y, const char *text, unsigned int colorbank, int brightness)
{
	char fit[PROBE_LINE_LEN];
	size_t len;

	probeSanitize(fit, sizeof fit, text);
	for (char *c = fit; *c != '\0'; ++c)
	{
		if (*c == '_')  // the tiny font has no underscore
			*c = ' ';
	}
	len = strlen(fit);
	while (len > 0 && JE_textWidth(fit, TINY_FONT) > PROBE_TEXT_MAX_W)
		fit[--len] = '\0';

	JE_outText(VGAScreen, x, y, fit, colorbank, brightness);
}

static const char *probeEventName(Uint32 type)
{
	switch (type)
	{
	case SDL_EVENT_QUIT:                     return "QUIT";
	case SDL_EVENT_KEY_DOWN:                 return "KEY_DOWN";
	case SDL_EVENT_KEY_UP:                   return "KEY_UP";
	case SDL_EVENT_TEXT_INPUT:               return "TEXT_INPUT";
	case SDL_EVENT_TEXT_EDITING:             return "TEXT_EDITING";
	case SDL_EVENT_KEYBOARD_ADDED:           return "KEYBOARD_ADDED";
	case SDL_EVENT_KEYBOARD_REMOVED:         return "KEYBOARD_REMOVED";
	case SDL_EVENT_KEYMAP_CHANGED:           return "KEYMAP_CHANGED";
	case SDL_EVENT_MOUSE_MOTION:             return "MOUSE_MOTION";
	case SDL_EVENT_MOUSE_BUTTON_DOWN:        return "MOUSE_BTN_DOWN";
	case SDL_EVENT_MOUSE_BUTTON_UP:          return "MOUSE_BTN_UP";
	case SDL_EVENT_MOUSE_WHEEL:              return "MOUSE_WHEEL";
	case SDL_EVENT_MOUSE_ADDED:              return "MOUSE_ADDED";
	case SDL_EVENT_MOUSE_REMOVED:            return "MOUSE_REMOVED";
	case SDL_EVENT_FINGER_DOWN:              return "FINGER_DOWN";
	case SDL_EVENT_FINGER_UP:                return "FINGER_UP";
	case SDL_EVENT_FINGER_MOTION:            return "FINGER_MOTION";
	case SDL_EVENT_JOYSTICK_AXIS_MOTION:     return "JOY_AXIS";
	case SDL_EVENT_JOYSTICK_BALL_MOTION:     return "JOY_BALL";
	case SDL_EVENT_JOYSTICK_HAT_MOTION:      return "JOY_HAT";
	case SDL_EVENT_JOYSTICK_BUTTON_DOWN:     return "JOY_BTN_DOWN";
	case SDL_EVENT_JOYSTICK_BUTTON_UP:       return "JOY_BTN_UP";
	case SDL_EVENT_JOYSTICK_ADDED:           return "JOY_ADDED";
	case SDL_EVENT_JOYSTICK_REMOVED:         return "JOY_REMOVED";
	case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE: return "JOY_UPDATE_DONE";
	case SDL_EVENT_JOYSTICK_BATTERY_UPDATED: return "JOY_BATTERY";
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:      return "PAD_AXIS";
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:      return "PAD_BTN_DOWN";
	case SDL_EVENT_GAMEPAD_BUTTON_UP:        return "PAD_BTN_UP";
	case SDL_EVENT_GAMEPAD_ADDED:            return "PAD_ADDED";
	case SDL_EVENT_GAMEPAD_REMOVED:          return "PAD_REMOVED";
	case SDL_EVENT_GAMEPAD_REMAPPED:         return "PAD_REMAPPED";
	case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:    return "PAD_TOUCH_DOWN";
	case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:  return "PAD_TOUCH_MOTION";
	case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:      return "PAD_TOUCH_UP";
	case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:    return "PAD_SENSOR";
	case SDL_EVENT_SENSOR_UPDATE:            return "SENSOR";
	default:
		break;
	}
	if (type >= SDL_EVENT_WINDOW_FIRST && type <= SDL_EVENT_WINDOW_LAST)
		return "WINDOW";
	return "OTHER";
}

// The per-type fields after the name.  Unknown types print their raw number.
static void probeFields(const SDL_Event *ev, char *out, size_t size)
{
	char text[PROBE_LINE_LEN];

	switch (ev->type)
	{
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		snprintf(out, size, "sc=%d (%s) key=%s (0x%08lx) rep=%d mod=0x%04lx",
		         (int)ev->key.scancode, probeName(SDL_GetScancodeName(ev->key.scancode)),
		         probeName(SDL_GetKeyName(ev->key.key)), (unsigned long)ev->key.key,
		         ev->key.repeat ? 1 : 0, (unsigned long)ev->key.mod);
		break;

	case SDL_EVENT_TEXT_INPUT:
		probeSanitize(text, sizeof text, probeStr(ev->text.text));
		snprintf(out, size, "text=\"%s\" (non-ASCII shown as ?)", text);
		break;

	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		snprintf(out, size, "btn=%d clicks=%d x=%.1f y=%.1f mouse=%lu",
		         (int)ev->button.button, (int)ev->button.clicks,
		         (double)ev->button.x, (double)ev->button.y, (unsigned long)ev->button.which);
		break;

	case SDL_EVENT_MOUSE_MOTION:
		snprintf(out, size, "x=%.1f y=%.1f xrel=%.2f yrel=%.2f btns=0x%02lx mouse=%lu",
		         (double)ev->motion.x, (double)ev->motion.y,
		         (double)ev->motion.xrel, (double)ev->motion.yrel,
		         (unsigned long)ev->motion.state, (unsigned long)ev->motion.which);
		break;

	case SDL_EVENT_MOUSE_WHEEL:
		snprintf(out, size, "x=%.2f y=%.2f dir=%s mouse_xy=%.1f,%.1f mouse=%lu",
		         (double)ev->wheel.x, (double)ev->wheel.y,
		         ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? "flipped" : "normal",
		         (double)ev->wheel.mouse_x, (double)ev->wheel.mouse_y, (unsigned long)ev->wheel.which);
		break;

	case SDL_EVENT_FINGER_DOWN:
	case SDL_EVENT_FINGER_UP:
	case SDL_EVENT_FINGER_MOTION:
		snprintf(out, size, "finger=%lu x=%.4f y=%.4f dx=%.4f dy=%.4f p=%.3f touch=%lu",
		         (unsigned long)ev->tfinger.fingerID,
		         (double)ev->tfinger.x, (double)ev->tfinger.y,
		         (double)ev->tfinger.dx, (double)ev->tfinger.dy,
		         (double)ev->tfinger.pressure, (unsigned long)ev->tfinger.touchID);
		break;

	case SDL_EVENT_JOYSTICK_AXIS_MOTION:
		snprintf(out, size, "dev=%lu axis=%d val=%d", (unsigned long)ev->jaxis.which,
		         (int)ev->jaxis.axis, (int)ev->jaxis.value);
		break;

	case SDL_EVENT_JOYSTICK_BALL_MOTION:
		snprintf(out, size, "dev=%lu ball=%d rel=%d,%d", (unsigned long)ev->jball.which,
		         (int)ev->jball.ball, (int)ev->jball.xrel, (int)ev->jball.yrel);
		break;

	case SDL_EVENT_JOYSTICK_HAT_MOTION:
		snprintf(out, size, "dev=%lu hat=%d val=%d", (unsigned long)ev->jhat.which,
		         (int)ev->jhat.hat, (int)ev->jhat.value);
		break;

	case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
	case SDL_EVENT_JOYSTICK_BUTTON_UP:
		snprintf(out, size, "dev=%lu btn=%d", (unsigned long)ev->jbutton.which,
		         (int)ev->jbutton.button);
		break;

	case SDL_EVENT_JOYSTICK_ADDED:
	case SDL_EVENT_JOYSTICK_REMOVED:
	case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE:
		snprintf(out, size, "dev=%lu", (unsigned long)ev->jdevice.which);
		break;

	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		snprintf(out, size, "dev=%lu axis=%d (%s) val=%d", (unsigned long)ev->gaxis.which,
		         (int)ev->gaxis.axis, probeStr(SDL_GetGamepadStringForAxis((SDL_GamepadAxis)ev->gaxis.axis)),
		         (int)ev->gaxis.value);
		break;

	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		snprintf(out, size, "dev=%lu btn=%d (%s)", (unsigned long)ev->gbutton.which,
		         (int)ev->gbutton.button,
		         probeStr(SDL_GetGamepadStringForButton((SDL_GamepadButton)ev->gbutton.button)));
		break;

	case SDL_EVENT_GAMEPAD_ADDED:
	case SDL_EVENT_GAMEPAD_REMOVED:
	case SDL_EVENT_GAMEPAD_REMAPPED:
		snprintf(out, size, "dev=%lu", (unsigned long)ev->gdevice.which);
		break;

	case SDL_EVENT_KEYBOARD_ADDED:
	case SDL_EVENT_KEYBOARD_REMOVED:
		snprintf(out, size, "kbd=%lu", (unsigned long)ev->kdevice.which);
		break;

	case SDL_EVENT_MOUSE_ADDED:
	case SDL_EVENT_MOUSE_REMOVED:
		snprintf(out, size, "mouse=%lu", (unsigned long)ev->mdevice.which);
		break;

	case SDL_EVENT_QUIT:
		out[0] = '\0';
		break;

	default:
		if (ev->type >= SDL_EVENT_WINDOW_FIRST && ev->type <= SDL_EVENT_WINDOW_LAST)
			snprintf(out, size, "window=%lu data=%d,%d type=0x%04lx", (unsigned long)ev->window.windowID,
			         (int)ev->window.data1, (int)ev->window.data2, (unsigned long)ev->type);
		else
			snprintf(out, size, "type=0x%08lx", (unsigned long)ev->type);
		break;
	}
}

// Writes one line to stderr and to the log file (flushed, so it survives a crash).
static void probeLogLine(const char *line)
{
	fputs(line, stderr);
	fputc('\n', stderr);
	fflush(stderr);

	if (probe.log_open)
	{
		fileWrite(&probe.log, line, strlen(line));
		fileWrite(&probe.log, "\n", 1);
		fileFlush(&probe.log);
	}
}

static void probeRemember(const char *line)
{
	snprintf(probe.screen[probe.screen_next], PROBE_LINE_LEN, "%s", line);
	probe.screen_next = (probe.screen_next + 1) % PROBE_SCREEN_LINES;
	if (probe.screen_count < PROBE_SCREEN_LINES)
		++probe.screen_count;
}

// Escape counts only on a fresh press.  Three presses within the window quit;
// a single Escape (or the remote's Back, a different scancode) is just logged.
static void probeEscape(Uint64 now)
{
	probe.esc[probe.esc_next] = now;
	probe.esc_next = (probe.esc_next + 1) % PROBE_ESC_PRESSES;
	if (probe.esc_count < PROBE_ESC_PRESSES)
		++probe.esc_count;

	if (probe.esc_count >= PROBE_ESC_PRESSES)
	{
		// The slot at esc_next holds the oldest of the last three presses.
		if (now - probe.esc[probe.esc_next] <= PROBE_ESC_WINDOW_MS)
		{
			probeLogLine("input-probe: Escape x3 within 2 s, exiting");
			probe.quit = true;
		}
	}
}

static void probeToggleRelative(void)
{
	const bool next = !SDL_GetWindowRelativeMouseMode(main_window);

	SDL_SetWindowRelativeMouseMode(main_window, next);
	probe.relative = next;

	probeLogLine(next ? "input-probe: relative mouse mode ON (F2)" : "input-probe: relative mouse mode OFF (F2)");
}

static void probeEvent(const SDL_Event *ev, Uint64 now)
{
	char fields[PROBE_FIELDS_LEN];
	char line[PROBE_LINE_LEN];
	const unsigned long ms = (unsigned long)(ev->common.timestamp / 1000000u);

	probeFields(ev, fields, sizeof fields);
	snprintf(line, sizeof line, "%8lu ms %-14s %s", ms, probeEventName(ev->type), fields);
	probeLogLine(line);
	probeRemember(line);
	++probe.events;

	switch (ev->type)
	{
	case SDL_EVENT_QUIT:
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		probe.quit = true;
		break;

	case SDL_EVENT_MOUSE_MOTION:
		++probe.motion_count;
		probe.motion_xrel += ev->motion.xrel;
		probe.motion_yrel += ev->motion.yrel;
		break;

	case SDL_EVENT_KEY_DOWN:
		if (ev->key.repeat)
			break;
		if (ev->key.scancode == SDL_SCANCODE_ESCAPE)
			probeEscape(now);
		else if (ev->key.scancode == SDL_SCANCODE_F2)
			probeToggleRelative();
		break;

	default:
		break;
	}
}

// Publishes the motion totals once per second (only seconds with motion).
static void probeSummary(Uint64 now)
{
	char line[PROBE_LINE_LEN];

	if (now - probe.summary_start < 1000u)
		return;

	probe.shown_count = probe.motion_count;
	probe.shown_xrel = probe.motion_xrel;
	probe.shown_yrel = probe.motion_yrel;

	if (probe.motion_count > 0)
	{
		snprintf(line, sizeof line, "%8lu ms summary 1s: motion_events=%lu xrel_sum=%.1f yrel_sum=%.1f",
		         (unsigned long)now, probe.motion_count, probe.motion_xrel, probe.motion_yrel);
		probeLogLine(line);
	}

	probe.motion_count = 0;
	probe.motion_xrel = 0.0;
	probe.motion_yrel = 0.0;
	probe.summary_start = now;
}

static void probeDraw(void)
{
	char text[PROBE_LINE_LEN];
	size_t start;

	JE_clr256(VGAScreen);

	probeScreenText(4, 2, "INPUT PROBE - Escape 3 times quits (auto-exit 5 min)", 15, 2);
	probeScreenText(4, 10, "Press every remote button: OK, Back, Home, Menu,", 14, 0);
	probeScreenText(4, 18, "media, volume, D-pad. Move the air mouse.", 14, 0);
	probeScreenText(4, 26, "Tap, drag and two-finger tap the touchscreen.", 14, 0);
	probeScreenText(4, 34, "F2 toggles relative mouse mode. Log: input-probe.log", 14, 0);

	snprintf(text, sizeof text, "relative=%s motion/s=%lu xrel=%.0f yrel=%.0f events=%lu",
	         probe.relative ? "on" : "off", probe.shown_count, probe.shown_xrel, probe.shown_yrel,
	         probe.events);
	probeScreenText(4, 46, text, 12, 3);
	probeScreenText(4, 56, "recent events (oldest first):", 15, 0);

	start = (probe.screen_next + PROBE_SCREEN_LINES - probe.screen_count) % PROBE_SCREEN_LINES;
	for (size_t i = 0; i < probe.screen_count; ++i)
		probeScreenText(4, 66 + (int)i * 11, probe.screen[(start + i) % PROBE_SCREEN_LINES], 11, 0);
}

void input_probe_run(void)
{
	const char *root;
	char line[PROBE_LINE_LEN];
	const int version = SDL_GetVersion();
	Uint64 start;

	memset(&probe, 0, sizeof probe);

	userDirPrepare();
	probe.log = userFileOpen(PROBE_LOG_NAME, "w");
	probe.log_open = !probe.log.error;
	root = userDirGet();
	if (probe.log_open)
		logInfo("input-probe: writing %s/%s", root[0] != '\0' ? root : ".", PROBE_LOG_NAME);
	else
		logWarn("input-probe: cannot open %s: %s", PROBE_LOG_NAME, fileGetError(&probe.log));

	snprintf(line, sizeof line, "input-probe start: platform=%s video=%s sdl=%d.%d.%d",
	         probeStr(SDL_GetPlatform()), probeStr(SDL_GetCurrentVideoDriver()),
	         SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version));
	probeLogLine(line);
	probeLogLine("input-probe: Escape x3 quits, F2 toggles relative mouse mode, 300 s limit");

	probe.relative = SDL_GetWindowRelativeMouseMode(main_window);
	SDL_StartTextInput(main_window);
	set_palette(palettes[0], 0, 255);

	start = SDL_GetTicks();
	probe.summary_start = start;

	while (!probe.quit)
	{
		SDL_Event ev;

		while (!probe.quit && SDL_PollEvent(&ev))
			probeEvent(&ev, SDL_GetTicks());

		const Uint64 now = SDL_GetTicks();
		if (probe.quit)
			break;
		if (now - start >= PROBE_DURATION_MS)
		{
			probeLogLine("input-probe: 5 minute limit reached, exiting");
			break;
		}

		probeSummary(now);
		probeDraw();
		JE_showVGA();
		SDL_Delay(PROBE_FRAME_MS);
	}

	snprintf(line, sizeof line, "input-probe end: %lu events", probe.events);
	probeLogLine(line);

	SDL_SetWindowRelativeMouseMode(main_window, false);
	SDL_StopTextInput(main_window);
	if (probe.log_open)
		fileClose(&probe.log);
	probe.log_open = false;
}
