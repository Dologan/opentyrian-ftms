/* ftms_parse.c - see ftms_parse.h.  SPDX-License-Identifier: GPL-2.0-or-later */
#include "ftms_parse.h"

#include <string.h>

typedef struct
{
	const uint8_t *p;
	size_t len, off;
	bool bad;
} reader;

static const uint8_t *take(reader *r, size_t n)
{
	const uint8_t *at;
	if (r->bad || r->len - r->off < n) { r->bad = true; return NULL; }
	at = r->p + r->off;
	r->off += n;
	return at;
}
static uint16_t rd_u16(reader *r)
{
	const uint8_t *b = take(r, 2);
	return b ? (uint16_t)(b[0] | (b[1] << 8)) : 0;
}
static uint8_t rd_u8(reader *r)
{
	const uint8_t *b = take(r, 1);
	return b ? b[0] : 0;
}
static uint32_t rd_u24(reader *r)
{
	const uint8_t *b = take(r, 3);
	return b ? (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) : 0;
}
static uint32_t rd_u32(reader *r)
{
	const uint8_t *b = take(r, 4);
	return b ? (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24) : 0;
}
static int16_t rd_s16(reader *r)
{
	uint16_t u = rd_u16(r);
	return u >= 0x8000u ? (int16_t)(-(int)(0x10000u - u)) : (int16_t)u;  /* no UB, no impl-defined cast */
}
static void skip(reader *r, size_t n) { (void)take(r, n); }

ftms_status ftms_parse_indoor_bike_data(const uint8_t *buf, size_t len, ftms_bike_data *out)
{
	reader r;
	uint16_t flags;
	ftms_bike_data d;

	memset(&d, 0, sizeof d);
	memset(out, 0, sizeof *out);
	r.p = buf; r.len = len; r.off = 0; r.bad = false;
	if (buf == NULL || len < 2)
		return FTMS_ERR_SHORT;

	flags = rd_u16(&r);
	d.more_data = (flags & 0x0001) != 0;

	/* Bit 0 is inverted: speed is present when it is CLEAR. */
	if (!d.more_data) { d.speed_centi_kmh = rd_u16(&r); d.present |= FTMS_HAS_SPEED; }
	if (flags & 0x0002) { d.avg_speed_centi_kmh = rd_u16(&r); d.present |= FTMS_HAS_AVG_SPEED; }
	if (flags & 0x0004) { d.cadence_half_rpm = rd_u16(&r); d.present |= FTMS_HAS_CADENCE; }
	if (flags & 0x0008) { d.avg_cadence_half_rpm = rd_u16(&r); d.present |= FTMS_HAS_AVG_CADENCE; }
	if (flags & 0x0010) { d.distance_m = rd_u24(&r); d.present |= FTMS_HAS_DISTANCE; }
	if (flags & 0x0020) { d.resistance = rd_s16(&r); d.present |= FTMS_HAS_RESISTANCE; }
	if (flags & 0x0040) { d.power_w = rd_s16(&r); d.present |= FTMS_HAS_POWER; }
	if (flags & 0x0080) { d.avg_power_w = rd_s16(&r); d.present |= FTMS_HAS_AVG_POWER; }
	if (flags & 0x0100)
	{
		d.energy_total_kcal = rd_u16(&r);
		d.energy_per_hour_kcal = rd_u16(&r);
		d.energy_per_min_kcal = rd_u8(&r);
		d.present |= FTMS_HAS_ENERGY;
	}
	if (flags & 0x0200) { d.heart_rate_bpm = rd_u8(&r); d.present |= FTMS_HAS_HEART_RATE; }
	if (flags & 0x0400) { d.met_tenths = rd_u8(&r); d.present |= FTMS_HAS_MET; }
	if (flags & 0x0800) { d.elapsed_s = rd_u16(&r); d.present |= FTMS_HAS_ELAPSED; }
	if (flags & 0x1000) { d.remaining_s = rd_u16(&r); d.present |= FTMS_HAS_REMAINING; }

	if (r.bad)
		return FTMS_ERR_SHORT;
	*out = d;
	return FTMS_OK;
}

ftms_status cps_parse_measurement(const uint8_t *buf, size_t len, cps_measurement *out)
{
	reader r;
	uint16_t flags;
	cps_measurement m;

	memset(&m, 0, sizeof m);
	memset(out, 0, sizeof *out);
	r.p = buf; r.len = len; r.off = 0; r.bad = false;
	if (buf == NULL || len < 4)
		return FTMS_ERR_SHORT;

	flags = rd_u16(&r);
	m.power_w = rd_s16(&r);
	if (flags & 0x0001) skip(&r, 1);                 /* pedal power balance */
	if (flags & 0x0004) skip(&r, 2);                 /* accumulated torque */
	if (flags & 0x0010) { m.wheel_revs = rd_u32(&r); m.wheel_event_time = rd_u16(&r); m.present |= CPS_HAS_WHEEL; }
	if (flags & 0x0020) { m.crank_revs = rd_u16(&r); m.crank_event_time = rd_u16(&r); m.present |= CPS_HAS_CRANK; }
	if (flags & 0x0040) skip(&r, 4);                 /* extreme forces */
	if (flags & 0x0080) skip(&r, 4);                 /* extreme torques */
	if (flags & 0x0100) skip(&r, 3);                 /* extreme angles */
	if (flags & 0x0200) skip(&r, 2);                 /* top dead spot */
	if (flags & 0x0400) skip(&r, 2);                 /* bottom dead spot */
	if (flags & 0x0800) { m.energy_kj = rd_u16(&r); m.present |= CPS_HAS_ENERGY; }

	if (r.bad)
		return FTMS_ERR_SHORT;
	*out = m;
	return FTMS_OK;
}

bool cps_crank_cadence_update(cps_crank_cadence *c, uint16_t revs, uint16_t event_time,
                              double now_s, double *rpm)
{
	if (!c->have_prev)
	{
		c->have_prev = true;
		c->revs = revs; c->time = event_time; c->changed_at_s = now_s;
		return false;
	}
	if (event_time == c->time)
	{
		if (now_s - c->changed_at_s > CPS_CADENCE_STOP_AFTER_S)
		{
			c->rpm = 0.0;
			c->have_rpm = true;
		}
	}
	else
	{
		uint16_t d_time = (uint16_t)(event_time - c->time);
		uint16_t d_revs = (uint16_t)(revs - c->revs);
		c->revs = revs; c->time = event_time; c->changed_at_s = now_s;
		c->rpm = (double)d_revs / ((double)d_time / 1024.0) * 60.0;
		c->have_rpm = true;
	}
	if (c->have_rpm && rpm)
		*rpm = c->rpm;
	return c->have_rpm;
}

ftms_status ftms_parse_cp_response(const uint8_t *buf, size_t len, ftms_cp_response *out)
{
	memset(out, 0, sizeof *out);
	if (buf == NULL || len < 3)
		return FTMS_ERR_SHORT;
	if (buf[0] != FTMS_CP_RESPONSE_OPCODE)
		return FTMS_ERR_FORMAT;
	out->request_op_code = buf[1];
	out->result = buf[2];
	return FTMS_OK;
}

void ftms_encode_set_target_resistance(double level, uint8_t out[2])
{
	double raw = level * 10.0 + 0.5;
	int v;
	if (!(raw >= 0.0)) raw = 0.0;      /* also catches NaN */
	if (raw > 255.0) raw = 255.0;
	v = (int)raw;                       /* floor for non-negative */
	out[0] = FTMS_CP_SET_TARGET_RESIST;
	out[1] = (uint8_t)v;
}

ftms_status ftms_parse_feature(const uint8_t *buf, size_t len, uint32_t *machine, uint32_t *target)
{
	reader r;
	r.p = buf; r.len = len; r.off = 0; r.bad = false;
	if (buf == NULL || len < 8)
		return FTMS_ERR_SHORT;
	*machine = rd_u32(&r);
	*target = rd_u32(&r);
	return FTMS_OK;
}
