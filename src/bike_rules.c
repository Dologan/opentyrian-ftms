/*
 * bike_rules.c - mapping C and the link-loss blend.  New code for the
 * exercise-bike integration; integer arithmetic only.
 */
#include "bike_rules.h"

#include "bike.h"

static uint32_t acc_q16;       /* exact remainder accumulator, 0..0xFFFF */
static bq16 blend_q16 = BIKE_Q16_ONE; /* 1 = stock refill */
static uint64_t down_us;       /* how long the link has been down */
static bool was_active;
static bq16 last_norm;
static int32_t last_income;
static unsigned last_add;

void bike_rules_level_start(void)
{
	acc_q16 = 0;
	last_income = 0;
	last_add = 0;
}

int32_t bike_income_q16(unsigned G, bq16 effort_norm, uint8_t floor, uint16_t bias_pct,
                        uint16_t gain_pct, uint16_t cap_pct)
{
	int64_t n = effort_norm < 0 ? 0 : (effort_norm > BIKE_Q16_ONE ? BIKE_Q16_ONE : effort_norm);
	/* G * (bias + gain*n) / 100, in Q16 */
	int64_t v = ((int64_t)G * ((int64_t)bias_pct * BIKE_Q16_ONE + (int64_t)gain_pct * n) + 50) / 100;
	int64_t hi = (int64_t)G * cap_pct * BIKE_Q16_ONE / 100;
	int64_t lo = (int64_t)floor * BIKE_Q16_ONE;

	if (v > hi)
		v = hi;
	if (v < lo)
		v = lo;
	return (int32_t)v;
}

void bike_rules_frame(const BikeEffortSignals *sig, bool link_up, bool active, uint32_t dt_us)
{
	const int32_t step = (int32_t)(((int64_t)(BIKE_Q16_ONE / 2) * dt_us) / 1000000); /* 1/2 per second */

	last_norm = sig->effort_norm;
	if (active && !was_active)
	{
		blend_q16 = BIKE_Q16_ONE; /* income ramps in and never jumps */
		down_us = 0;
	}
	was_active = active;
	if (!active)
		return;

	if (link_up)
	{
		down_us = 0;
		blend_q16 -= step;
		if (blend_q16 < 0)
			blend_q16 = 0;
	}
	else
	{
		down_us += dt_us;
		if (down_us >= BIKE_BLEND_DELAY_US)
		{
			blend_q16 += step;
			if (blend_q16 > BIKE_Q16_ONE)
				blend_q16 = BIKE_Q16_ONE;
		}
	}
}

unsigned bike_rules_power_add(unsigned stock_add)
{
	const int32_t income = bike_income_q16(stock_add, last_norm, bike_settings.floor, bike_settings.bias_pct,
	                                       bike_settings.gain_pct, bike_settings.cap_pct);
	const int64_t stock = (int64_t)stock_add * BIKE_Q16_ONE;
	/* out = income + (stock - income) * w, exact at w = 0 and w = 1 */
	const int64_t out = income + ((stock - income) * blend_q16) / BIKE_Q16_ONE;

	acc_q16 += (uint32_t)out;
	last_income = income;
	last_add = acc_q16 >> 16;
	acc_q16 &= 0xFFFF;
	return last_add;
}

bq16 bike_rules_blend(void) { return blend_q16; }
int32_t bike_rules_last_income(void) { return last_income; }
unsigned bike_rules_last_add(void) { return last_add; }

uint64_t bike_rules_digest(void)
{
	uint64_t h = 14695981039346656037ull;
	const uint32_t v[3] = { acc_q16, (uint32_t)blend_q16, (uint32_t)(down_us > 0xFFFFFFFFu ? 0xFFFFFFFFu : down_us) };
	int i, b;

	for (i = 0; i < 3; ++i)
		for (b = 0; b < 4; ++b)
		{
			h ^= (v[i] >> (8 * b)) & 0xFF;
			h *= 1099511628211ull;
		}
	h ^= was_active ? 1u : 0u;
	h *= 1099511628211ull;
	return h;
}
