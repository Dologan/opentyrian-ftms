/*
 * bike_effort.h - fixed-point effort engine for the exercise-bike integration.
 *
 * Port of Flywheel's game/src/core/effort/effort-engine.ts and filters.ts
 * (Flywheel, GPL-2.0-or-later) to pure C99 integer arithmetic: Q16.16 values,
 * microsecond time, no floating point, so every platform produces identical
 * state.  Zones, streak, compliance, the power model and level inference of the
 * TypeScript engine are intentionally not ported.
 */
#ifndef BIKE_EFFORT_H
#define BIKE_EFFORT_H

#include <stdbool.h>
#include <stdint.h>

typedef int32_t bq16; /* Q16.16; 65536 == 1.0 */

#define BIKE_Q16_ONE 65536
#define BIKE_COAST_RING 64

enum
{
	BIKE_EV_MOVING_START = 1,
	BIKE_EV_MOVING_STOP = 2,
	BIKE_EV_SURGE_START = 4,
	BIKE_EV_SURGE_END = 8,
	BIKE_EV_OVERDRIVE = 16
};

typedef struct
{
	uint32_t ftp_w;
	bq16 moving_on_rpm, moving_off_rpm;                                /* 25, 15 */
	uint32_t moving_grace_us, silent_stop_us, start_confirm_us;        /* 1.5 s, 3 s, 0.25 s */
	uint32_t effort_tau_us, fast_tau_us, baseline_tau_us;              /* 3 s, 1.5 s, 75 s */
	bq16 baseline_floor_pct, surge_ratio, surge_min_pct;               /* .5, 1.2, .6 */
	uint32_t surge_hold_us, surge_exit_us, surge_fill_us, surge_warmup_us; /* 2, 1, 8, 30 s */
	bq16 coast_max_rpm, coast_drop_frac;                               /* 45, .35 */
	uint32_t coast_window_us;                                          /* 5 s */
	uint8_t coast_resume_samples;                                      /* 3 */
} BikeEffortConfig;

typedef struct
{
	bool has_sample, link_up, power_valid;
	uint32_t seq; /* a change of seq means a new sample */
	bq16 power_w, cadence_rpm;
	uint32_t age_us;
} BikeEffortInput;

typedef struct
{
	bool moving, stale, surging;
	bq16 power_w, cadence_rpm, effort_w, effort_pct, effort_norm, baseline_w, surge, surge_meter;
	uint8_t events;
} BikeEffortSignals;

typedef struct
{
	uint64_t t_us;
	bq16 cadence;
} BikeCoastEntry;

typedef struct BikeEffortState
{
	bq16 effort_w, fast_w, baseline_w; /* EMA state, Q16 watts */
	uint64_t clock_us;
	uint32_t moving_time_us; /* saturating */
	uint32_t below_us, above_us, surge_cand_us, surge_exit_us;
	uint32_t meter_q30; /* surge meter, 0..2^30 */
	uint32_t last_seq;
	uint8_t coast_resume_count;
	bool moving, surging, coast_stopped, has_last_seq;
	BikeCoastEntry ring[BIKE_COAST_RING];
	uint8_t ring_start, ring_count;
} BikeEffortState;

void bike_effort_config_default(BikeEffortConfig *c, uint32_t ftp_w);
void bike_effort_init(BikeEffortState *s, const BikeEffortConfig *c);
void bike_effort_step(BikeEffortState *s, const BikeEffortConfig *c, uint32_t dt_us,
                      const BikeEffortInput *in, BikeEffortSignals *out);
bq16 bike_effort_norm(bq16 effort_pct);                        /* knots (0,.10)(.40,.15)(.75,.50)(1.10,1.0) */
uint32_t bike_ema_alpha_q30(uint32_t dt_us, uint32_t tau_us);  /* 1-e^-x, Taylor to x^5, x=dt/tau<=0.25 */
uint64_t bike_effort_digest(const BikeEffortState *s);         /* FNV-1a over fixed-width LE fields */

#endif
