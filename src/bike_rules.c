/*
 * bike_rules.c - mapping C and the link-loss blend.  New code for the
 * exercise-bike integration; integer arithmetic only.
 */
#include "bike_rules.h"

#include "bike.h"
#include "logging.h"
#include "player.h"
#include "varz.h"

static uint32_t acc_q16;       /* exact remainder accumulator, 0..0xFFFF */
static bq16 blend_q16 = BIKE_Q16_ONE; /* 1 = stock refill */
static uint64_t down_us;       /* how long the link has been down */
static bool was_active;
static bq16 last_norm;
static int32_t last_income;
static unsigned last_add;
static bool surging_now;
static uint64_t superbomb_cd_us; /* time until the next overdrive grant is allowed */
static unsigned long superbombs_granted, superbombs_cooled, shield_fast, sidekick_fast, special_extra;

void bike_rules_level_start(void)
{
	/* superbomb_cd_us is deliberately kept: the cooldown counts gameplay time across levels (a short
	 * level or a retry must not allow a grant more often than once per cooldown); new game resets it. */
	acc_q16 = 0;
	last_income = 0;
	last_add = 0;
}

void bike_rules_new_game(void)
{
	superbomb_cd_us = 0;
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
	surging_now = sig->surging && bike_gate_open() && bike_source() != BIKE_SOURCE_OFF;

	/* +1 superbomb on overdrive-ready, as the pickup does, capped at BIKE_SUPERBOMB_CAP and rate
	 * limited by a cooldown counted in gameplay time (dt), so it is frame-rate invariant. */
	superbomb_cd_us = superbomb_cd_us > dt_us ? superbomb_cd_us - dt_us : 0;
	if ((sig->events & BIKE_EV_OVERDRIVE) != 0 && bike_settings.surge_superbomb && bike_gate_open() &&
	    bike_source() != BIKE_SOURCE_OFF && player[0].superbombs < BIKE_SUPERBOMB_CAP)
	{
		if (superbomb_cd_us > 0)
			++superbombs_cooled;
		else
		{
			++player[0].superbombs;
			++superbombs_granted;
			superbomb_cd_us = (uint64_t)bike_settings.superbomb_cooldown_s * 1000000u;
		}
	}
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

/* 15 -> 8 frames as effort rises; the pool cost per shield point is untouched,
 * so a surge never makes regen worse than stock. */
unsigned bike_rules_shield_wait(unsigned stock_wait)
{
	int64_t cut;

	if (!surging_now || !bike_settings.surge_shield || stock_wait <= 8)
		return stock_wait;
	cut = ((int64_t)(stock_wait - 8) * (last_norm < 0 ? 0 : (last_norm > BIKE_Q16_ONE ? BIKE_Q16_ONE : last_norm)) +
	       BIKE_Q16_ONE / 2) / BIKE_Q16_ONE;
	if (cut > 0)
		++shield_fast;
	return stock_wait - (unsigned)cut;
}

unsigned bike_rules_sidekick_charge_ticks(unsigned stock)
{
	if (!surging_now || !bike_settings.surge_sidekick)
		return stock;
	++sidekick_fast;
	return stock / 2;
}

bool bike_rules_extra_special_tick(void)
{
	if (!surging_now || !bike_settings.surge_special)
		return false;
	++special_extra;
	return true;
}

void bike_rules_log_coverage(void)
{
	logInfo("Bike effects: superbombs=%lu shield_fast=%lu sidekick_fast=%lu special_extra=%lu superbombs_cooled=%lu", superbombs_granted,
	        shield_fast, sidekick_fast, special_extra,
	        superbombs_cooled);
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
	if (superbomb_cd_us > 0) /* folded only while cooling, so digests without superbomb grants are unchanged */
		for (b = 0; b < 8; ++b)
		{
			h ^= (superbomb_cd_us >> (8 * b)) & 0xFF;
			h *= 1099511628211ull;
		}
	return h;
}
