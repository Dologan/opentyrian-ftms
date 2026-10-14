/*
 * bike_effort.c - fixed-point effort engine.
 *
 * Port of Flywheel's game/src/core/effort/effort-engine.ts and
 * game/src/core/effort/filters.ts (Flywheel, MIT).  This is a
 * port: the TypeScript is the behavioural reference and the golden fixtures in
 * test/bike/golden (vendored from Flywheel's shared/conformance/effort) are
 * produced by running it.
 *
 * Arithmetic: values are Q16.16 in int32_t, time is microseconds, EMA state is
 * Q16, and the EMA coefficient is Q30.  Everything is integer, so the result is
 * bit-identical on every platform.  Signed multiply-shifts never use >> on a
 * negative value: they round the magnitude and reapply the sign.
 */
#include "bike_effort.h"

#include <stddef.h>

#define Q ((int64_t)BIKE_Q16_ONE)

/* ---- rounding helpers ---------------------------------------------------- */

/* round-half-away-from-zero of v / 2^sh, without shifting a negative value */
static int64_t round_shift(int64_t v, unsigned sh)
{
	uint64_t mag = (v < 0) ? (uint64_t)(-v) : (uint64_t)v;
	mag = (mag + ((uint64_t)1 << (sh - 1))) >> sh;
	return (v < 0) ? -(int64_t)mag : (int64_t)mag;
}

/* round-half-away-from-zero of n / d, d > 0 */
static int64_t round_div(int64_t n, int64_t d)
{
	uint64_t mag = (n < 0) ? (uint64_t)(-n) : (uint64_t)n;
	mag = (mag + (uint64_t)d / 2) / (uint64_t)d;
	return (n < 0) ? -(int64_t)mag : (int64_t)mag;
}

static uint32_t sat_add(uint32_t a, uint32_t b)
{
	return (a > UINT32_MAX - b) ? UINT32_MAX : a + b;
}

static bq16 to_bq16(int64_t v)
{
	if (v > INT32_MAX)
		return INT32_MAX;
	if (v < INT32_MIN)
		return INT32_MIN;
	return (bq16)v;
}

/* seconds in microseconds */
#define SEC(x) ((uint32_t)(x) * 1000000u)

/* ---- piecewise ------------------------------------------------------------ */

typedef struct { bq16 x, y; } Knot;

static bq16 piecewise(bq16 x, const Knot *k, size_t n)
{
	size_t i;

	if (x <= k[0].x)
		return k[0].y;
	if (x >= k[n - 1].x)
		return k[n - 1].y;
	for (i = 1; i < n; ++i)
	{
		if (x <= k[i].x)
		{
			int64_t num = (int64_t)(x - k[i - 1].x) * (k[i].y - k[i - 1].y);
			return to_bq16(k[i - 1].y + round_div(num, k[i].x - k[i - 1].x));
		}
	}
	return k[n - 1].y;
}

/* effortNorm: 0->.10, .40->.15, .75->.50, 1.10->1.0 */
bq16 bike_effort_norm(bq16 effort_pct)
{
	static const Knot knots[] = { { 0, 6554 }, { 26214, 9830 }, { 49152, 32768 }, { 72090, 65536 } };
	return piecewise(effort_pct, knots, 4);
}

/* cadence-only power guess (fraction of FTP), used when the bike reports no power */
static bq16 cadence_power_frac(bq16 cadence)
{
	static const Knot knots[] = {
		{ 30 * 65536, 0 }, { 60 * 65536, 29491 }, { 90 * 65536, 52429 }, { 110 * 65536, 65536 }
	};
	return piecewise(cadence, knots, 4);
}

/* ---- EMA ------------------------------------------------------------------ */

/* 1 - e^-x as Q30, x = dt/tau.  Taylor series to x^5 for x <= 0.25; a larger x is
 * halved until it fits and the result is composed back with a' = 2a - a^2. */
uint32_t bike_ema_alpha_q30(uint32_t dt_us, uint32_t tau_us)
{
	const int64_t one = (int64_t)1 << 30;
	uint64_t x;
	int64_t x1, x2, x3, x4, x5, a;
	unsigned k = 0;

	if (tau_us == 0)
		return (uint32_t)one;
	x = (((uint64_t)dt_us << 30) + tau_us / 2) / tau_us;
	while (x > ((uint64_t)1 << 28) && k < 40)
	{
		x >>= 1;
		++k;
	}
	x1 = (int64_t)x;
	x2 = round_shift(x1 * x1, 30);
	x3 = round_shift(x2 * x1, 30);
	x4 = round_shift(x3 * x1, 30);
	x5 = round_shift(x4 * x1, 30);
	a = x1 - x2 / 2 + x3 / 6 - x4 / 24 + x5 / 120;
	while (k-- > 0)
		a = 2 * a - round_shift(a * a, 30);
	if (a > one)
		a = one;
	if (a < 0)
		a = 0;
	return (uint32_t)a;
}

/* v += round((x - v) * alpha) */
static bq16 ema_step(bq16 v, bq16 x, uint32_t dt_us, uint32_t tau_us)
{
	int64_t diff = (int64_t)x - v;
	uint32_t a = bike_ema_alpha_q30(dt_us, tau_us);
	return to_bq16(v + round_shift(diff * (int64_t)a, 30));
}

/* ---- config / init -------------------------------------------------------- */

void bike_effort_config_default(BikeEffortConfig *c, uint32_t ftp_w)
{
	c->ftp_w = ftp_w;
	c->moving_on_rpm = 25 * 65536;
	c->moving_off_rpm = 15 * 65536;
	c->moving_grace_us = 1500000u;
	c->silent_stop_us = SEC(3);
	c->start_confirm_us = 250000u;
	c->effort_tau_us = SEC(3);
	c->fast_tau_us = 1500000u;
	c->baseline_tau_us = SEC(75);
	c->baseline_floor_pct = 32768;
	c->surge_ratio = 78643;   /* 1.2 */
	c->surge_min_pct = 39322; /* 0.6 */
	c->surge_hold_us = SEC(2);
	c->surge_exit_us = SEC(1);
	c->surge_fill_us = SEC(8);
	c->surge_warmup_us = SEC(30);
	c->coast_max_rpm = 45 * 65536;
	c->coast_drop_frac = 22938; /* 0.35 */
	c->coast_window_us = SEC(5);
	c->coast_resume_samples = 3;
}

static bq16 baseline_floor(const BikeEffortConfig *c)
{
	return to_bq16((int64_t)c->ftp_w * c->baseline_floor_pct);
}

void bike_effort_init(BikeEffortState *s, const BikeEffortConfig *c)
{
	size_t i;
	unsigned char *p = (unsigned char *)s;

	for (i = 0; i < sizeof *s; ++i) /* deterministic padding */
		p[i] = 0;
	s->baseline_w = baseline_floor(c);
}

/* ---- moving / coast-down -------------------------------------------------- */

static void ring_push(BikeEffortState *s, const BikeEffortConfig *c, bq16 cadence)
{
	if (s->ring_count == BIKE_COAST_RING)
	{
		s->ring_start = (uint8_t)((s->ring_start + 1) % BIKE_COAST_RING);
		--s->ring_count;
	}
	s->ring[(s->ring_start + s->ring_count) % BIKE_COAST_RING].t_us = s->clock_us;
	s->ring[(s->ring_start + s->ring_count) % BIKE_COAST_RING].cadence = cadence;
	++s->ring_count;
	while (s->ring_count > 0 && s->clock_us - s->ring[s->ring_start].t_us > c->coast_window_us)
	{
		s->ring_start = (uint8_t)((s->ring_start + 1) % BIKE_COAST_RING);
		--s->ring_count;
	}
}

/* record a new sample; true if it looks like the pedals have stopped */
static bool update_coast(BikeEffortState *s, const BikeEffortConfig *c, bq16 cadence)
{
	bq16 max = 0;
	unsigned i;

	ring_push(s, c, cadence);
	for (i = 0; i < s->ring_count; ++i)
	{
		bq16 v = s->ring[(s->ring_start + i) % BIKE_COAST_RING].cadence;
		if (v > max)
			max = v;
	}
	return cadence < c->coast_max_rpm && max > 0 &&
	       (int64_t)cadence * Q <= (int64_t)max * (Q - c->coast_drop_frac);
}

static void moving_start(BikeEffortState *s, uint8_t *ev)
{
	s->moving = true;
	s->above_us = s->below_us = 0;
	s->coast_stopped = false;
	s->coast_resume_count = 0;
	*ev |= BIKE_EV_MOVING_START;
}

static void moving_stop(BikeEffortState *s, bool coast_down, uint8_t *ev)
{
	s->moving = false;
	s->below_us = s->above_us = 0;
	s->coast_stopped = coast_down;
	s->coast_resume_count = 0;
	*ev |= BIKE_EV_MOVING_STOP;
}

static void update_moving(BikeEffortState *s, const BikeEffortConfig *c, uint32_t dt,
                          bq16 cadence, bool stale, bool is_new, uint8_t *ev)
{
	bool coasting;

	if (stale)
		return; /* link down: hold the previous moving state */
	coasting = is_new && update_coast(s, c, cadence);
	if (s->moving)
	{
		if (coasting)
		{
			moving_stop(s, true, ev);
			return;
		}
		s->below_us = (cadence < c->moving_off_rpm) ? sat_add(s->below_us, dt) : 0;
		if (s->below_us >= c->moving_grace_us)
			moving_stop(s, false, ev);
	}
	else if (s->coast_stopped)
	{
		if (!is_new)
			return;
		if (cadence >= c->coast_max_rpm)
			moving_start(s, ev);
		else if (cadence >= c->moving_on_rpm && !coasting)
		{
			if (++s->coast_resume_count >= c->coast_resume_samples)
				moving_start(s, ev);
		}
		else
			s->coast_resume_count = 0;
	}
	else
	{
		s->above_us = (cadence >= c->moving_on_rpm) ? sat_add(s->above_us, dt) : 0;
		if (s->above_us >= c->start_confirm_us)
			moving_start(s, ev);
	}
}

/* ---- surge ---------------------------------------------------------------- */

/* clamp((ratio - 1) / (2 (surgeRatio - 1)), 0, 1) while moving */
static bq16 surge_intensity(const BikeEffortState *s, const BikeEffortConfig *c, bq16 ratio)
{
	int64_t span = 2 * ((int64_t)c->surge_ratio - Q), v;

	if (!s->moving || span <= 0)
		return 0;
	v = round_div(((int64_t)ratio - Q) * Q, span);
	return (bq16)(v < 0 ? 0 : v > Q ? Q : v);
}

static bq16 ratio_of(bq16 fast, bq16 base)
{
	if (base <= 0)
		return 0;
	return to_bq16(round_div((int64_t)fast * Q, base));
}

static void update_surge(BikeEffortState *s, const BikeEffortConfig *c, uint32_t dt, bq16 fast,
                         bq16 *ratio_out, bq16 *baseline_out, uint8_t *ev)
{
	const bq16 floor_w = baseline_floor(c);
	bool warmed, qualifies;
	bq16 baseline, ratio;
	const int64_t one30 = (int64_t)1 << 30;

	if (s->moving)
	{
		uint32_t tau, tau_eff;
		s->moving_time_us = sat_add(s->moving_time_us, dt);
		tau = sat_add(SEC(5), s->moving_time_us);
		if (tau > c->baseline_tau_us)
			tau = c->baseline_tau_us;
		/* dt/4 over tau == dt over 4*tau */
		tau_eff = s->surging ? tau * 4u : tau;
		s->baseline_w = ema_step(s->baseline_w, fast, dt, tau_eff);
	}
	warmed = s->moving_time_us >= c->surge_warmup_us;
	baseline = (s->baseline_w > floor_w) ? s->baseline_w : floor_w;
	ratio = ratio_of(fast, baseline);
	qualifies = warmed && s->moving && ratio >= c->surge_ratio &&
	            (int64_t)fast >= (int64_t)c->surge_min_pct * c->ftp_w;

	if (!s->surging)
	{
		s->surge_cand_us = qualifies ? sat_add(s->surge_cand_us, dt) : 0;
		if (s->surge_cand_us >= c->surge_hold_us)
		{
			s->surging = true;
			s->surge_exit_us = 0;
			*ev |= BIKE_EV_SURGE_START;
		}
	}
	else
	{
		bq16 exit_ratio = Q + (c->surge_ratio - Q) / 2;
		bool dropping = !s->moving || ratio < exit_ratio;
		s->surge_exit_us = dropping ? sat_add(s->surge_exit_us, dt) : 0;
		if (s->surge_exit_us >= c->surge_exit_us || !s->moving)
		{
			s->surging = false;
			s->surge_cand_us = 0;
			*ev |= BIKE_EV_SURGE_END;
		}
	}

	if (s->surging)
	{
		/* meter += dt/fill * (0.5 + intensity), in Q30 */
		int64_t gain = 32768 + (int64_t)surge_intensity(s, c, ratio);
		int64_t inc = round_div((int64_t)dt * gain * 16384, (int64_t)c->surge_fill_us);
		int64_t m = (int64_t)s->meter_q30 + inc;
		if (m >= one30)
		{
			m = 0;
			*ev |= BIKE_EV_OVERDRIVE;
		}
		s->meter_q30 = (uint32_t)m;
	}
	else
	{
		int64_t dec = round_div((int64_t)dt * one30, 30000000);
		int64_t m = (int64_t)s->meter_q30 - dec;
		s->meter_q30 = (uint32_t)(m < 0 ? 0 : m);
	}
	*ratio_out = ratio;
	*baseline_out = baseline;
}

/* ---- step ----------------------------------------------------------------- */

void bike_effort_step(BikeEffortState *s, const BikeEffortConfig *c, uint32_t dt_us,
                      const BikeEffortInput *in, BikeEffortSignals *out)
{
	bool stale = !in->link_up;
	bool fresh = in->has_sample && in->link_up && in->age_us <= c->silent_stop_us;
	bool is_new;
	bq16 cadence = 0, power = 0, fast, ratio, baseline, pct;
	uint8_t ev = 0;

	if (fresh)
	{
		cadence = (in->cadence_rpm > 0) ? in->cadence_rpm : 0;
		if (in->power_valid)
			power = (in->power_w > 0) ? in->power_w : 0;
		else
			power = to_bq16((int64_t)c->ftp_w * cadence_power_frac(cadence));
	}

	s->clock_us += dt_us;
	is_new = fresh && (!s->has_last_seq || in->seq != s->last_seq);
	if (fresh)
	{
		s->last_seq = in->seq;
		s->has_last_seq = true;
	}
	update_moving(s, c, dt_us, cadence, stale, is_new, &ev);
	s->effort_w = ema_step(s->effort_w, power, dt_us, c->effort_tau_us);
	s->fast_w = ema_step(s->fast_w, power, dt_us, c->fast_tau_us);
	fast = s->fast_w;
	pct = to_bq16(round_div((int64_t)s->effort_w, (int64_t)c->ftp_w));
	update_surge(s, c, dt_us, fast, &ratio, &baseline, &ev);

	out->moving = s->moving;
	out->stale = stale;
	out->surging = s->surging;
	out->power_w = power;
	out->cadence_rpm = cadence;
	out->effort_w = s->effort_w;
	out->effort_pct = pct;
	out->effort_norm = s->moving ? bike_effort_norm(pct) : 0;
	out->baseline_w = baseline;
	out->surge = surge_intensity(s, c, ratio);
	out->surge_meter = to_bq16(round_shift((int64_t)s->meter_q30, 14));
	out->events = ev;
}

/* ---- digest --------------------------------------------------------------- */

static uint64_t fnv_u(uint64_t h, uint64_t v, unsigned bytes)
{
	unsigned i;
	for (i = 0; i < bytes; ++i)
	{
		h ^= (uint8_t)(v >> (8 * i));
		h *= 1099511628211ull;
	}
	return h;
}

uint64_t bike_effort_digest(const BikeEffortState *s)
{
	uint64_t h = 14695981039346656037ull;
	unsigned i;

	h = fnv_u(h, (uint32_t)s->effort_w, 4);
	h = fnv_u(h, (uint32_t)s->fast_w, 4);
	h = fnv_u(h, (uint32_t)s->baseline_w, 4);
	h = fnv_u(h, s->clock_us, 8);
	h = fnv_u(h, s->moving_time_us, 4);
	h = fnv_u(h, s->below_us, 4);
	h = fnv_u(h, s->above_us, 4);
	h = fnv_u(h, s->surge_cand_us, 4);
	h = fnv_u(h, s->surge_exit_us, 4);
	h = fnv_u(h, s->meter_q30, 4);
	h = fnv_u(h, s->last_seq, 4);
	h = fnv_u(h, s->coast_resume_count, 1);
	h = fnv_u(h, s->moving, 1);
	h = fnv_u(h, s->surging, 1);
	h = fnv_u(h, s->coast_stopped, 1);
	h = fnv_u(h, s->has_last_seq, 1);
	h = fnv_u(h, s->ring_count, 1);
	for (i = 0; i < s->ring_count; ++i)
	{
		const BikeCoastEntry *e = &s->ring[(s->ring_start + i) % BIKE_COAST_RING];
		h = fnv_u(h, e->t_us, 8);
		h = fnv_u(h, (uint32_t)e->cadence, 4);
	}
	return h;
}
