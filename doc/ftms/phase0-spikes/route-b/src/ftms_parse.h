/*
 * ftms_parse.h - portable C99 decoders for Bluetooth FTMS / Cycling Power payloads.
 *
 * Port of flywheel bridge/src/flywheel_bridge/ble/{ftms,cps,control_point}.py.
 * No dependencies beyond <stdint.h>/<stddef.h>/<stdbool.h>.  No allocation, no
 * globals, thread-safe (pure functions).  Little-endian wire format throughout.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FTMS_PARSE_H
#define FTMS_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	FTMS_OK = 0,
	FTMS_ERR_SHORT = 1,   /* payload shorter than its flags require */
	FTMS_ERR_FORMAT = 2   /* wrong opcode / malformed */
} ftms_status;

/* ---- Indoor Bike Data (0x2AD2) -------------------------------------------------- */

/* Bits of ftms_bike_data.present: set when the field was in the packet. */
enum
{
	FTMS_HAS_SPEED          = 1u << 0,
	FTMS_HAS_AVG_SPEED      = 1u << 1,
	FTMS_HAS_CADENCE        = 1u << 2,
	FTMS_HAS_AVG_CADENCE    = 1u << 3,
	FTMS_HAS_DISTANCE       = 1u << 4,
	FTMS_HAS_RESISTANCE     = 1u << 5,
	FTMS_HAS_POWER          = 1u << 6,
	FTMS_HAS_AVG_POWER      = 1u << 7,
	FTMS_HAS_ENERGY         = 1u << 8,  /* total + per-hour + per-minute together */
	FTMS_HAS_HEART_RATE     = 1u << 9,
	FTMS_HAS_MET            = 1u << 10,
	FTMS_HAS_ELAPSED        = 1u << 11,
	FTMS_HAS_REMAINING      = 1u << 12
};

/* Fixed-point as on the wire, so there is no float rounding to disagree about. */
typedef struct
{
	uint32_t present;            /* FTMS_HAS_* mask; absent fields are 0 */
	bool     more_data;          /* flags bit 0 SET => speed absent, more follows */
	uint16_t speed_centi_kmh;    /* 0.01 km/h */
	uint16_t avg_speed_centi_kmh;
	uint16_t cadence_half_rpm;   /* 0.5 rpm */
	uint16_t avg_cadence_half_rpm;
	uint32_t distance_m;         /* uint24 */
	int16_t  resistance;         /* raw, unscaled */
	int16_t  power_w;
	int16_t  avg_power_w;
	uint16_t energy_total_kcal;
	uint16_t energy_per_hour_kcal;
	uint8_t  energy_per_min_kcal;
	uint8_t  heart_rate_bpm;
	uint8_t  met_tenths;         /* 0.1 MET */
	uint16_t elapsed_s;
	uint16_t remaining_s;
} ftms_bike_data;

/* Decode one notification strictly from its own flags (no cross-packet state).
 * Trailing bytes are ignored.  On error *out is zeroed. */
ftms_status ftms_parse_indoor_bike_data(const uint8_t *buf, size_t len, ftms_bike_data *out);

/* Convenience accessors (floating point only at the edge). */
static inline double ftms_speed_kmh(const ftms_bike_data *d)   { return d->speed_centi_kmh * 0.01; }
static inline double ftms_cadence_rpm(const ftms_bike_data *d) { return d->cadence_half_rpm * 0.5; }

/* ---- Cycling Power Measurement (0x2A63) fallback --------------------------------- */

enum
{
	CPS_HAS_CRANK  = 1u << 0,
	CPS_HAS_WHEEL  = 1u << 1,
	CPS_HAS_ENERGY = 1u << 2
};

typedef struct
{
	uint32_t present;            /* CPS_HAS_* */
	int16_t  power_w;            /* always present */
	uint16_t crank_revs;         /* cumulative, wraps at 65536 */
	uint16_t crank_event_time;   /* 1/1024 s, wraps */
	uint32_t wheel_revs;
	uint16_t wheel_event_time;   /* 1/2048 s */
	uint16_t energy_kj;
} cps_measurement;

ftms_status cps_parse_measurement(const uint8_t *buf, size_t len, cps_measurement *out);

/* Cadence from crank counters.  Zero-initialise (memset/={0}) before first use. */
typedef struct
{
	bool     have_prev;
	bool     have_rpm;
	uint16_t revs, time;
	double   changed_at_s;
	double   rpm;
} cps_crank_cadence;

#define CPS_CADENCE_STOP_AFTER_S 3.0
/* Returns true and sets *rpm when a cadence estimate is available. */
bool cps_crank_cadence_update(cps_crank_cadence *c, uint16_t revs, uint16_t event_time,
                              double now_s, double *rpm);

/* ---- Control Point (0x2AD9) ------------------------------------------------------ */

#define FTMS_CP_REQUEST_CONTROL   0x00
#define FTMS_CP_SET_TARGET_RESIST 0x04
#define FTMS_CP_START_OR_RESUME   0x07
#define FTMS_CP_RESPONSE_OPCODE   0x80

typedef struct
{
	uint8_t request_op_code;
	uint8_t result;              /* 1 = success */
} ftms_cp_response;

ftms_status ftms_parse_cp_response(const uint8_t *buf, size_t len, ftms_cp_response *out);
/* Encode Set Target Resistance (level in 0.1 steps, clamped 0..25.5); writes 2 bytes. */
void ftms_encode_set_target_resistance(double level, uint8_t out[2]);

/* ---- Fitness Machine Feature (0x2ACC) -------------------------------------------- */
ftms_status ftms_parse_feature(const uint8_t *buf, size_t len, uint32_t *machine, uint32_t *target);

#ifdef __cplusplus
}
#endif
#endif
