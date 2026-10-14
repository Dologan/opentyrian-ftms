/*
 * bike.c - exercise-bike integration: settings, sources, gate and frame tick.
 *
 * New code for the exercise-bike integration (see doc/ftms/design.md).  The
 * game core calls only bike_frame_tick() once per gameplay frame and the rules
 * hooks; with the master switch off nothing here changes game state.
 */
#include "bike.h"

#include "bike_feed.h"
#include "bike_ws.h"
#include "bike_rules.h"
#include "config.h"
#include "demo.h"
#include "logging.h"
#include "network.h"
#include "nortsong.h"
#include "params.h"
#include "regress.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BikeSettings bike_settings;
bool bike_settings_pinned = false;

static bool forced_off = false;
static bool level_active = false;
static bool gate_prev = false;

/* source state */
static BikeFeed feed;
static BikeFeedState feed_state;
static bool feed_loaded = false;
static BikeMailbox mailbox;

/* engine state */
static BikeEffortConfig eff_cfg;
static BikeEffortState eff_state;
static bool eff_ready = false;
static uint32_t eff_cfg_ftp = 0;
static BikeEffortSignals sig;
static BikeHud hud;

static uint64_t clock_us = 0;
static uint32_t tick_count = 0;
static uint32_t last_dt_us = 0;
static bool have_sample = false; /* latched once any sample has arrived */
static uint32_t last_seq = 0;
static uint64_t arrival_us = 0;
static bool link_up_now = false;

static FILE *log_file = NULL;
static const char *log_path = NULL;
static unsigned long log_lines = 0;

/* ---- settings ------------------------------------------------------------ */

void bike_settings_default(BikeSettings *s)
{
	memset(s, 0, sizeof *s);
	s->surge_superbomb = true; /* only matters once master, a source and the engine are on */
	s->autofire_key = SDL_SCANCODE_A;
	s->autofire_key2 = SDL_SCANCODE_APPLICATION;
	s->pointer_sens_pct = 100;
	s->ftp_w = 200;
	s->floor = 8;
	s->bias_pct = 50;
	s->gain_pct = 100;
	s->cap_pct = 150;
	strcpy(s->bridge_host, "127.0.0.1");
	s->bridge_port = 8787;
}

static bool parse_bool(const char *v, bool *out)
{
	if (strcmp(v, "1") == 0 || strcmp(v, "on") == 0 || strcmp(v, "true") == 0 || strcmp(v, "yes") == 0)
		*out = true;
	else if (strcmp(v, "0") == 0 || strcmp(v, "off") == 0 || strcmp(v, "false") == 0 || strcmp(v, "no") == 0)
		*out = false;
	else
		return false;
	return true;
}

static bool parse_uint(const char *v, unsigned lo, unsigned hi, unsigned *out)
{
	unsigned long n = 0;
	const char *p = v;

	if (*p == '\0')
		return false;
	for (; *p != '\0'; ++p)
	{
		if (*p < '0' || *p > '9')
			return false;
		n = n * 10 + (unsigned long)(*p - '0');
		if (n > 1000000)
			return false;
	}
	if (n < lo || n > hi)
		return false;
	*out = (unsigned)n;
	return true;
}

bool bike_settings_set(const char *key, const char *value)
{
	BikeSettings *s = &bike_settings;
	bool b;
	unsigned n;

	if (key == NULL || value == NULL)
		return false;

#define BOOL_KEY(name, field) \
	if (strcmp(key, name) == 0) { if (!parse_bool(value, &b)) return false; s->field = b; return true; }
	BOOL_KEY("enabled", master)
	BOOL_KEY("generator", generator)
	BOOL_KEY("hud", hud)
	BOOL_KEY("surge_superbomb", surge_superbomb)
	BOOL_KEY("surge_sidekick", surge_sidekick)
	BOOL_KEY("surge_special", surge_special)
	BOOL_KEY("surge_shield", surge_shield)
	BOOL_KEY("air_mouse", air_mouse)
	BOOL_KEY("pointer_smoothing", pointer_smoothing)
	BOOL_KEY("touch", touch)
	BOOL_KEY("remote_buttons", remote_buttons)
#undef BOOL_KEY

	if (strcmp(key, "autofire") == 0)
	{
		if (strcmp(value, "toggle") == 0)
			s->autofire = true;
		else if (strcmp(value, "off") == 0)
			s->autofire = false;
		else
			return false;
		return true;
	}
	if (strcmp(key, "autofire_reserve") == 0)
	{
		if (!parse_uint(value, 0, 50, &n) || (n != 0 && n != 25 && n != 50))
			return false;
		s->autofire_reserve_pct = (uint8_t)n;
		return true;
	}
	if (strcmp(key, "autofire_key") == 0 || strcmp(key, "autofire_key2") == 0)
	{
		SDL_Scancode sc = SDL_GetScancodeFromName(value);

		if (sc == SDL_SCANCODE_UNKNOWN)
			return false;
		if (key[12] == '2')
			s->autofire_key2 = (int)sc;
		else
			s->autofire_key = (int)sc;
		return true;
	}
	if (strcmp(key, "pointer_sensitivity") == 0)
	{
		if (!parse_uint(value, 25, 400, &n))
			return false;
		s->pointer_sens_pct = (uint16_t)n;
		return true;
	}
	if (strcmp(key, "ftp") == 0)
	{
		if (!parse_uint(value, 50, 500, &n))
			return false;
		s->ftp_w = (uint16_t)n;
		return true;
	}
	if (strcmp(key, "floor") == 0)
	{
		if (!parse_uint(value, 0, 64, &n))
			return false;
		s->floor = (uint8_t)n;
		return true;
	}
	if (strcmp(key, "bias") == 0)
	{
		if (!parse_uint(value, 0, 200, &n))
			return false;
		s->bias_pct = (uint16_t)n;
		return true;
	}
	if (strcmp(key, "strength") == 0)
	{
		if (!parse_uint(value, 0, 300, &n))
			return false;
		s->gain_pct = (uint16_t)n;
		return true;
	}
	if (strcmp(key, "cap") == 0)
	{
		if (!parse_uint(value, 50, 400, &n))
			return false;
		s->cap_pct = (uint16_t)n;
		return true;
	}
	if (strcmp(key, "bridge_host") == 0)
	{
		if (value[0] == '\0' || strlen(value) >= sizeof s->bridge_host)
			return false;
		memset(s->bridge_host, 0, sizeof s->bridge_host);
		strcpy(s->bridge_host, value);
		return true;
	}
	if (strcmp(key, "bridge_port") == 0)
	{
		if (!parse_uint(value, 1, 65535, &n))
			return false;
		s->bridge_port = (uint16_t)n;
		return true;
	}
	return false;
}

bool bike_settings_set_list(const char *list)
{
	char buf[512];
	char *p, *next;

	if (list == NULL || strlen(list) >= sizeof buf)
		return false;
	strcpy(buf, list);
	for (p = buf; p != NULL && *p != '\0'; p = next)
	{
		char *eq;

		next = strchr(p, ',');
		if (next != NULL)
			*next++ = '\0';
		eq = strchr(p, '=');
		if (eq == NULL)
		{
			logError("bike: '%s' is not KEY=VALUE", p);
			return false;
		}
		*eq = '\0';
		if (!bike_settings_set(p, eq + 1))
		{
			logError("bike: bad setting '%s=%s'", p, eq + 1);
			return false;
		}
	}
	return true;
}

/* ---- config ([ftms] section) --------------------------------------------- */

static const char *const config_keys[] = {
	"enabled", "generator", "hud", "surge_superbomb", "surge_sidekick", "surge_special", "surge_shield",
	"autofire", "autofire_reserve", "autofire_key", "autofire_key2", "air_mouse", "pointer_sensitivity",
	"pointer_smoothing", "touch", "remote_buttons", "ftp", "floor", "bias", "strength", "cap",
	"bridge_host", "bridge_port"
};

static const char *onoff(bool v) { return v ? "on" : "off"; }

static void setting_text(const char *key, char *out, size_t cap)
{
	const BikeSettings *s = &bike_settings;
	unsigned num = 0;
	const char *str = NULL;

	if (strcmp(key, "enabled") == 0) str = onoff(s->master);
	else if (strcmp(key, "generator") == 0) str = onoff(s->generator);
	else if (strcmp(key, "hud") == 0) str = onoff(s->hud);
	else if (strcmp(key, "surge_superbomb") == 0) str = onoff(s->surge_superbomb);
	else if (strcmp(key, "surge_sidekick") == 0) str = onoff(s->surge_sidekick);
	else if (strcmp(key, "surge_special") == 0) str = onoff(s->surge_special);
	else if (strcmp(key, "surge_shield") == 0) str = onoff(s->surge_shield);
	else if (strcmp(key, "autofire") == 0) str = s->autofire ? "toggle" : "off";
	else if (strcmp(key, "autofire_reserve") == 0) num = s->autofire_reserve_pct;
	else if (strcmp(key, "autofire_key") == 0) str = SDL_GetScancodeName((SDL_Scancode)s->autofire_key);
	else if (strcmp(key, "autofire_key2") == 0) str = SDL_GetScancodeName((SDL_Scancode)s->autofire_key2);
	else if (strcmp(key, "air_mouse") == 0) str = onoff(s->air_mouse);
	else if (strcmp(key, "pointer_sensitivity") == 0) num = s->pointer_sens_pct;
	else if (strcmp(key, "pointer_smoothing") == 0) str = onoff(s->pointer_smoothing);
	else if (strcmp(key, "touch") == 0) str = onoff(s->touch);
	else if (strcmp(key, "remote_buttons") == 0) str = onoff(s->remote_buttons);
	else if (strcmp(key, "ftp") == 0) num = s->ftp_w;
	else if (strcmp(key, "floor") == 0) num = s->floor;
	else if (strcmp(key, "bias") == 0) num = s->bias_pct;
	else if (strcmp(key, "strength") == 0) num = s->gain_pct;
	else if (strcmp(key, "cap") == 0) num = s->cap_pct;
	else if (strcmp(key, "bridge_host") == 0) str = s->bridge_host;
	else if (strcmp(key, "bridge_port") == 0) num = s->bridge_port;

	if (str != NULL)
		snprintf(out, cap, "%s", str);
	else
		snprintf(out, cap, "%u", num);
}

void bike_config_load(void)
{
	const ConfigSection *section = config_find_section(&opentyrian_config, "ftms", NULL);
	size_t i;

	if (section == NULL)
		return;
	for (i = 0; i < sizeof config_keys / sizeof config_keys[0]; ++i)
	{
		const char *value;

		/* a missing key or an invalid value keeps the default */
		if (config_get_string_option(section, config_keys[i], &value) && !bike_settings_set(config_keys[i], value))
			logWarn("opentyrian.cfg [ftms]: ignoring invalid %s = %s", config_keys[i], value);
	}
}

void bike_config_save(void)
{
	ConfigSection *section;
	BikeSettings def;
	size_t i;

	if (bike_settings_pinned)
		return;

	section = config_find_section(&opentyrian_config, "ftms", NULL);
	bike_settings_default(&def);
	if (section == NULL && memcmp(&def, &bike_settings, sizeof def) == 0)
		return; /* a stock configuration leaves no [ftms] section behind */

	section = config_find_or_add_section(&opentyrian_config, "ftms", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE); /* out of memory */
	for (i = 0; i < sizeof config_keys / sizeof config_keys[0]; ++i)
	{
		char text[80];

		setting_text(config_keys[i], text, sizeof text);
		config_set_string_option(section, config_keys[i], text);
	}
}

static uint64_t wall_us(void) { return SDL_GetTicksNS() / 1000; }

/* ---- sources ------------------------------------------------------------- */

bool bike_script_load(const char *spec)
{
	if (feed_loaded)
		bike_feed_free(&feed);
	feed_loaded = bike_feed_load(&feed, spec);
	if (!feed_loaded)
		logError("--regress-bike-feed: cannot load '%s'", spec);
	return feed_loaded;
}

void bike_force_off(void) { forced_off = true; }

BikeSource bike_source(void)
{
	if (feed_loaded)
		return BIKE_SOURCE_SCRIPT;
	if (bike_settings.master && !forced_off &&
	    (bike_settings.generator || bike_settings.surge_superbomb || bike_settings.surge_sidekick ||
	     bike_settings.surge_special || bike_settings.surge_shield || bike_settings.hud))
		return BIKE_SOURCE_BRIDGE;
	return BIKE_SOURCE_OFF;
}

void bike_regress_pin(const char *set_list)
{
	bike_settings_default(&bike_settings);
	bike_settings.master = false;
	forced_off = false;
	if (feed_loaded)
	{
		bike_settings.master = true;
		bike_settings.generator = true;
	}
	if (set_list != NULL && !bike_settings_set_list(set_list))
		exit(EXIT_FAILURE);
	bike_settings_pinned = true;
}

void bike_log_open(const char *path) { log_path = path; }

/* ---- lifecycle ----------------------------------------------------------- */

void bike_init(void)
{
	bike_settings_default(&bike_settings);
	memset(&mailbox, 0, sizeof mailbox);
}

void bike_shutdown(void)
{
	bike_ws_close();
	if (log_file != NULL)
	{
		fclose(log_file);
		log_file = NULL;
		logInfo("Regression: wrote %lu bike ticks to '%s'.", log_lines, log_path);
	}
	if (feed_loaded)
	{
		bike_feed_free(&feed);
		feed_loaded = false;
	}
}

/* The bridge connection is open only while the bike mode asks for the bridge,
 * never under regress.  Settings are read every call, so toggling the master
 * switch or changing the address takes effect on the next poll. */
void bike_service(void)
{
	static bool ws_open = false;
	static char ws_host[sizeof bike_settings.bridge_host];
	static uint16_t ws_port = 0;
	static bool ws_failed = false;
	const bool want = bike_source() == BIKE_SOURCE_BRIDGE && !regress_active();

	if (!want)
		ws_failed = false;
	if (ws_open && (!want || ws_port != bike_settings.bridge_port || strcmp(ws_host, bike_settings.bridge_host) != 0))
	{
		bike_ws_close();
		ws_open = false;
		memset(&mailbox, 0, sizeof mailbox); /* BIKE_LINK_OFF, no sample */
	}
	if (want && !ws_open)
	{
		memcpy(ws_host, bike_settings.bridge_host, sizeof ws_host);
		ws_port = bike_settings.bridge_port;
		if (ws_failed)
			return; /* SDL3_net is unavailable: try again only after the bridge was switched off */
		ws_open = bike_ws_open(ws_host, ws_port);
		ws_failed = !ws_open;
		if (!ws_open)
			return;
	}
	if (ws_open)
		bike_ws_poll(&mailbox, wall_us());
}

void bike_level_start(void)
{
	level_active = true;
	bike_rules_level_start();
}

void bike_level_end(void)
{
	level_active = false;
	gate_prev = false;
}

/* ---- gate ---------------------------------------------------------------- */

bool bike_gate_open(void)
{
	return bike_settings.master && !forced_off && level_active && !twoPlayerMode && !onePlayerAction &&
	       !isNetworkGame && !playDemo && !recordDemo && !galagaMode && !constantPlay;
}

bool bike_generator_on(void)
{
	return bike_gate_open() && bike_settings.generator && bike_source() != BIKE_SOURCE_OFF && have_sample;
}

/* ---- frame tick ---------------------------------------------------------- */

void bike_frame_tick(void)
{
	BikeEffortInput in;
	const BikeSource source = bike_source();
	const bool gate = bike_gate_open();
	uint32_t frames = frameCountMax > 0 ? frameCountMax : 1;
	bool link_up = false, has_sample = false, pv = false;
	bq16 pw = 0, cad = 0;
	uint32_t seq = 0, age_us = 0;

	last_dt_us = (uint32_t)(((uint64_t)getFramePeriodTicks10() * frames * 1000) / 1024);
	clock_us += last_dt_us;

	if (bike_settings.ftp_w != eff_cfg_ftp || !eff_ready)
	{
		bike_effort_config_default(&eff_cfg, bike_settings.ftp_w);
		if (!eff_ready)
		{
			bike_effort_init(&eff_state, &eff_cfg);
			bike_feed_state_init(&feed_state);
		}
		eff_cfg_ftp = bike_settings.ftp_w;
		eff_ready = true;
	}

	if (source == BIKE_SOURCE_SCRIPT)
	{
		bike_feed_at(&feed, tick_count, &feed_state);
		link_up = feed_state.link_up;
		has_sample = feed_state.has_sample;
		pv = feed_state.power_valid;
		pw = feed_state.power_w;
		cad = feed_state.cadence_rpm;
		seq = feed_state.seq;
		if (has_sample && (!have_sample || seq != last_seq))
			arrival_us = clock_us;
		age_us = has_sample ? (uint32_t)(clock_us - arrival_us) : 0;
	}
	else if (source == BIKE_SOURCE_BRIDGE)
	{
		const bool connected = mailbox.link == BIKE_LINK_WAITING || mailbox.link == BIKE_LINK_STREAMING;
		uint64_t now = wall_us();

		has_sample = mailbox.has_sample;
		if (has_sample)
		{
			pv = mailbox.latest.power_valid;
			pw = mailbox.latest.power_w;
			cad = mailbox.latest.cadence_rpm;
			seq = mailbox.latest.seq;
			arrival_us = mailbox.latest.arrival_us;
			age_us = now > arrival_us ? (uint32_t)(now - arrival_us) : 0;
		}
		link_up = connected && (mailbox.link == BIKE_LINK_STREAMING || (has_sample && age_us < 3000000u));
	}

	if (has_sample)
	{
		have_sample = true;
		last_seq = seq;
	}

	in.has_sample = has_sample;
	in.link_up = link_up;
	in.power_valid = pv;
	in.seq = seq;
	in.power_w = pw;
	in.cadence_rpm = cad;
	in.age_us = age_us;
	link_up_now = link_up;

	if (source != BIKE_SOURCE_OFF)
		bike_effort_step(&eff_state, &eff_cfg, last_dt_us, &in, &sig);
	else
		memset(&sig, 0, sizeof sig);

	bike_rules_frame(&sig, link_up && have_sample, bike_generator_on(), last_dt_us);
	gate_prev = gate;

	memset(&hud, 0, sizeof hud);
	hud.link = source == BIKE_SOURCE_SCRIPT ? (link_up ? BIKE_LINK_STREAMING : BIKE_LINK_WAITING) : mailbox.link;
	hud.gate = gate;
	hud.generator_on = bike_generator_on();
	hud.lost = have_sample && !link_up;
	hud.watts = sig.power_w;
	hud.cadence = sig.cadence_rpm;
	hud.effort_norm = sig.effort_norm;
	hud.surge_meter = sig.surge_meter;
	hud.surging = sig.surging;

	++tick_count;
}

const BikeHud *bike_hud(void) { return &hud; }

/* ---- menu / HUD helpers --------------------------------------------------- */

static unsigned q16_round_uint(bq16 v)
{
	return v <= 0 ? 0u : (unsigned)(((int64_t)v + 32768) >> 16);
}

const char *bike_link_name(BikeLink link)
{
	switch (link)
	{
		case BIKE_LINK_CONNECTING: return "Connecting";
		case BIKE_LINK_WAITING: return "Connected";
		case BIKE_LINK_STREAMING: return "Streaming";
		case BIKE_LINK_OFF:
		default: return "Off";
	}
}

void bike_status(BikeStatus *out)
{
	const BikeSource source = bike_source();

	memset(out, 0, sizeof *out);
	if (source == BIKE_SOURCE_BRIDGE)
	{
		const uint64_t now = wall_us();

		out->link = mailbox.link;
		out->has_sample = mailbox.has_sample;
		if (mailbox.has_sample)
		{
			out->power_valid = mailbox.latest.power_valid;
			out->cadence_valid = mailbox.latest.cadence_valid;
			out->watts = q16_round_uint(mailbox.latest.power_w);
			out->rpm = q16_round_uint(mailbox.latest.cadence_rpm);
			out->age_ms = now > mailbox.latest.arrival_us ? (uint32_t)((now - mailbox.latest.arrival_us) / 1000) : 0;
		}
	}
	else if (source == BIKE_SOURCE_SCRIPT)
	{
		out->link = hud.link;
		out->has_sample = have_sample;
		out->power_valid = out->cadence_valid = have_sample;
		out->watts = q16_round_uint(hud.watts);
		out->rpm = q16_round_uint(hud.cadence);
	}
}

unsigned bike_link_color(BikeLink link, bool lost)
{
	if (lost)
		return 116;
	switch (link)
	{
		case BIKE_LINK_STREAMING: return 202;
		case BIKE_LINK_WAITING: return 125;
		case BIKE_LINK_CONNECTING: return 120;
		case BIKE_LINK_OFF:
		default: return 8;
	}
}

bool bike_hud_visible(void)
{
	return bike_settings.master && bike_settings.hud && !forced_off && hud.gate;
}

/* STUB (T2.6): the auto-fire latch arrives with T3b.1.  Until then the HUD never
 * shows AUTO.  Replace this body; the HUD calls only this accessor. */
bool bike_autofire_state(bool *held)
{
	if (held != NULL)
		*held = false;
	return false;
}
uint32_t bike_tick_count(void) { return tick_count; }

/* ---- regress hash and log ------------------------------------------------- */

bool bike_hash_active(void) { return feed_loaded || bike_settings.master; }

uint64_t bike_state_digest(void)
{
	uint64_t h = 14695981039346656037ull;
	uint64_t v[8];
	int i, b;

	v[0] = bike_effort_digest(&eff_state);
	v[1] = bike_rules_digest();
	v[2] = clock_us;
	v[3] = tick_count;
	v[4] = feed_state.next;
	v[5] = feed_state.seq;
	v[6] = arrival_us;
	v[7] = (have_sample ? 1u : 0u) | (level_active ? 2u : 0u);
	for (i = 0; i < 8; ++i)
		for (b = 0; b < 8; ++b)
		{
			h ^= (v[i] >> (8 * b)) & 0xFF;
			h *= 1099511628211ull;
		}
	return h;
}

bool bike_log_active(void) { return log_path != NULL; }

void bike_log_refill(unsigned add, unsigned power_after)
{
	if (log_path == NULL)
		return;
	if (log_file == NULL)
	{
		log_file = fopen(log_path, "wb");
		if (log_file == NULL)
			logFatal("Failed to open bike log '%s'.", log_path);
	}
	/* tick link moving surging effort_w_q16 norm_q16 income_q16 add power w_q16 af_on af_held events */
	fprintf(log_file, "%lu %d %d %d %ld %ld %ld %u %u %ld %d %d %u\n", (unsigned long)(tick_count - 1),
	        (int)hud.link, sig.moving ? 1 : 0, sig.surging ? 1 : 0, (long)sig.effort_w, (long)sig.effort_norm,
	        (long)bike_rules_last_income(), add, power_after, (long)bike_rules_blend(), 0, 0,
	        (unsigned)sig.events);
	++log_lines;
}
