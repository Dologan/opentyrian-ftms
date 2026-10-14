/*
 * bike.h - exercise-bike integration: settings, source selection, mailbox,
 * gate and the per-frame tick.  See doc/ftms/design.md.
 *
 * Everything that can reach game state is integer fixed point (bike_effort.h)
 * and a pure function of the frame tick under regress.  With the master switch
 * off (the default) none of this touches the game.
 */
#ifndef BIKE_H
#define BIKE_H

#include "bike_effort.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum { BIKE_SOURCE_OFF, BIKE_SOURCE_SCRIPT, BIKE_SOURCE_BRIDGE } BikeSource;
typedef enum { BIKE_LINK_OFF, BIKE_LINK_CONNECTING, BIKE_LINK_WAITING, BIKE_LINK_STREAMING } BikeLink;

typedef struct
{
	bool master, generator, hud;
	bool surge_superbomb, surge_sidekick, surge_special, surge_shield;
	bool autofire;
	uint8_t autofire_reserve_pct; /* 0|25|50 */
	int autofire_key, autofire_key2; /* SDL_Scancode values */
	bool air_mouse, pointer_smoothing, touch, remote_buttons;
	uint16_t pointer_sens_pct; /* 25..400 */
	uint16_t ftp_w; /* 50..500, default 200 */
	uint16_t superbomb_cooldown_s; /* 0..600, default 60 (BIKE_SUPERBOMB_COOLDOWN_S) */
	uint8_t floor; /* units per frame, default 8 */
	uint16_t bias_pct, gain_pct, cap_pct; /* 50, 100 ("Strength"), 150 */
	char bridge_host[64];
	uint16_t bridge_port; /* 127.0.0.1, 8787 */
} BikeSettings;

typedef struct
{
	uint32_t seq;
	bool power_valid, cadence_valid;
	bq16 power_w, cadence_rpm;
	uint64_t arrival_us;
} BikeSample;

typedef struct
{
	BikeLink link;
	bool has_sample;
	BikeSample latest;
} BikeMailbox;

typedef struct
{
	BikeLink link;
	bool gate, generator_on, lost;
	bq16 watts, cadence, effort_norm, surge_meter;
	bool surging, autofire_on, autofire_held;
} BikeHud;

extern BikeSettings bike_settings; /* edited by menus; read every frame, never cached */
extern bool bike_settings_pinned;  /* regress: bike_config_save() leaves [ftms] untouched */

void bike_settings_default(BikeSettings *s); /* all features off (superbomb sub-toggle on) */
/* The one parser used by the config file, the command line and --regress-bike-set.
 * Returns false (settings unchanged) for an unknown key or an invalid value. */
bool bike_settings_set(const char *key, const char *value);
/* "K=V,K=V,..." through bike_settings_set; logs the offending pair on failure. */
bool bike_settings_set_list(const char *list);

void bike_config_load(void); /* [ftms] section of opentyrian_config */
void bike_config_save(void);

bool bike_script_load(const char *spec); /* --regress-bike-feed: file or const:W[,RPM] */

void bike_init(void); /* defaults; call before the config is loaded */
void bike_shutdown(void);
void bike_service(void); /* transport poll; writes the mailbox only */

/* --ftms-off: forces the master switch off for this run without touching the setting. */
void bike_force_off(void);
/* --regress-bike-*: regress pins (settings, feed implication, log file). */
void bike_regress_pin(const char *set_list);
void bike_log_open(const char *path);
/* --ftms-test-level=E:L: local bridge integration check, not for players (tools/check_bike_bridge.sh). */
bool bike_test_level_set(const char *spec);
bool bike_test_level(int *episode, int *level);

void bike_level_start(void);
void bike_level_end(void);
void bike_frame_tick(void); /* exactly once per level-loop pass */

bool bike_gate_open(void);
bool bike_generator_on(void); /* gate && generator && source != OFF && a sample has arrived */
BikeSource bike_source(void);
const BikeHud *bike_hud(void);

/* Menus and HUD (presentation only, read-only). */
typedef struct
{
	BikeLink link;
	bool has_sample, power_valid, cadence_valid;
	unsigned watts, rpm; /* last values, rounded */
	uint32_t age_ms;     /* since the last sample arrived (0 without one) */
} BikeStatus;
void bike_status(BikeStatus *out);
const char *bike_link_name(BikeLink link); /* "Off", "Connecting", "Connected", "Streaming" */
/* The HUD readout is drawn only while the master switch and the HUD setting are on
 * and the gate was open at the last frame tick.  Reads settings live. */
bool bike_hud_visible(void);
/* Palette index for the link dot: green streaming, yellow connected, orange
 * connecting, red link lost (had data), grey off. */
unsigned bike_link_color(BikeLink link, bool lost);
/* Auto-fire state for the HUD (AUTO / HOLD).  STUB until T3b.1 lands: always false.
 * The auto-fire implementation replaces the body in bike.c (or bike_controls.c). */
bool bike_autofire_state(bool *held);

bool bike_hash_active(void);
uint64_t bike_state_digest(void);
uint32_t bike_tick_count(void);

/* Refill bookkeeping for the per-tick log (--regress-bike-log). */
bool bike_log_active(void);
void bike_log_refill(unsigned add, unsigned stock_add, unsigned power_after);

/* --ftms-log=FILE: per-second calibration CSV while the gate is open (T5.2).  Inactive unless given. */
void bike_calib_open(const char *path);
/* Player shot accounting for that CSV: tiny hook in player_shot_create, no behaviour change. */
void bike_note_shot(bool fired);

#endif
