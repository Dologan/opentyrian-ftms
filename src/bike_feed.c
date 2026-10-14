/*
 * bike_feed.c - scripted bike telemetry feeds.
 *
 * New code for the exercise-bike integration; the feed format is shared with the
 * golden-fixture generator (tools/bike/golden/gen_effort_golden.mjs) from
 * Flywheel (GPL-2.0-or-later) so both read the same CSVs.
 */
#include "bike_feed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bq16 bike_parse_decimal(const char *s, const char **end)
{
	const char *p = s;
	bool neg = false, any = false;
	uint64_t ip = 0, fn = 0, fd = 1;
	int64_t q;

	if (*p == '-' || *p == '+')
	{
		neg = (*p == '-');
		++p;
	}
	while (*p >= '0' && *p <= '9')
	{
		any = true;
		if (ip < 100000)
			ip = ip * 10 + (uint64_t)(*p - '0');
		++p;
	}
	if (*p == '.')
	{
		++p;
		while (*p >= '0' && *p <= '9')
		{
			any = true;
			if (fd < 1000000000ull) /* nine fractional digits are plenty */
			{
				fn = fn * 10 + (uint64_t)(*p - '0');
				fd *= 10;
			}
			++p;
		}
	}
	if (end != NULL)
		*end = any ? p : s;
	if (ip > 32767)
		ip = 32767;
	q = (int64_t)(ip * 65536 + (fn * 65536 + fd / 2) / fd);
	if (q > INT32_MAX)
		q = INT32_MAX;
	return (bq16)(neg ? -q : q);
}

/* parse one optional field up to ',' / end of line; *valid false when empty */
static bool parse_field(const char **pp, bq16 *v, bool *valid)
{
	const char *p = *pp, *e;

	while (*p == ' ' || *p == '\t')
		++p;
	if (*p == ',' || *p == '\0' || *p == '\r' || *p == '\n')
	{
		*valid = false;
		*v = 0;
	}
	else
	{
		*v = bike_parse_decimal(p, &e);
		if (e == p)
			return false;
		*valid = true;
		p = e;
		while (*p == ' ' || *p == '\t')
			++p;
	}
	if (*p == ',')
		++p;
	else if (*p != '\0' && *p != '\r' && *p != '\n')
		return false;
	*pp = p;
	return true;
}

static bool load_const(BikeFeed *feed, const char *spec)
{
	const char *p = spec, *e;
	bq16 w, rpm = 80 * 65536;

	w = bike_parse_decimal(p, &e);
	if (e == p)
		return false;
	p = e;
	if (*p == ',')
	{
		++p;
		rpm = bike_parse_decimal(p, &e);
		if (e == p)
			return false;
		p = e;
	}
	if (*p != '\0')
		return false;
	feed->is_const = true;
	memset(&feed->const_row, 0, sizeof feed->const_row);
	feed->const_row.connected = true;
	feed->const_row.power_valid = feed->const_row.cadence_valid = true;
	feed->const_row.power_w = w;
	feed->const_row.cadence_rpm = rpm;
	return true;
}

bool bike_feed_load(BikeFeed *feed, const char *spec)
{
	FILE *f;
	char line[256];
	size_t cap = 0;
	bool first_data = true;

	memset(feed, 0, sizeof *feed);
	if (strncmp(spec, "const:", 6) == 0)
		return load_const(feed, spec + 6);

	f = fopen(spec, "rb");
	if (f == NULL)
		return false;
	while (fgets(line, sizeof line, f) != NULL)
	{
		const char *p = line;
		BikeFeedRow row;
		bq16 conn;
		uint64_t t = 0;
		bool tv;

		while (*p == ' ' || *p == '\t')
			++p;
		if (*p == '#' || *p == '\0' || *p == '\r' || *p == '\n')
			continue;
		if (!((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.'))
		{
			if (first_data)
			{
				first_data = false; /* header row */
				continue;
			}
			goto fail;
		}
		first_data = false;
		memset(&row, 0, sizeof row);
		if (*p < '0' || *p > '9')
			goto fail;
		while (*p >= '0' && *p <= '9' && t <= UINT32_MAX)
			t = t * 10 + (uint64_t)(*p++ - '0');
		while (*p == ' ' || *p == '\t')
			++p;
		if (t > UINT32_MAX || *p != ',')
			goto fail;
		++p;
		row.tick = (uint32_t)t;
		if (!parse_field(&p, &row.power_w, &row.power_valid) ||
		    !parse_field(&p, &row.cadence_rpm, &row.cadence_valid))
			goto fail;
		if (*p == '\0' || *p == '\r' || *p == '\n')
			conn = 65536, tv = true;
		else if (!parse_field(&p, &conn, &tv))
			goto fail;
		row.connected = tv ? conn != 0 : true;
		if (feed->count > 0 && row.tick <= feed->rows[feed->count - 1].tick)
			goto fail;
		if (feed->count == cap)
		{
			size_t ncap = cap ? cap * 2 : 256;
			BikeFeedRow *n = (BikeFeedRow *)realloc(feed->rows, ncap * sizeof *n);
			if (n == NULL)
				goto fail;
			feed->rows = n;
			cap = ncap;
		}
		feed->rows[feed->count++] = row;
	}
	fclose(f);
	return true;

fail:
	fclose(f);
	bike_feed_free(feed);
	return false;
}

void bike_feed_free(BikeFeed *feed)
{
	free(feed->rows);
	memset(feed, 0, sizeof *feed);
}

void bike_feed_state_init(BikeFeedState *st)
{
	memset(st, 0, sizeof *st);
}

static void apply_row(const BikeFeedRow *r, BikeFeedState *st)
{
	if (r->connected)
	{
		st->link_up = true;
		st->has_sample = true;
		++st->seq;
		st->arrival_tick = r->tick;
		st->power_valid = r->power_valid;
		st->cadence_valid = r->cadence_valid;
		st->power_w = r->power_valid ? r->power_w : 0;
		st->cadence_rpm = r->cadence_valid ? r->cadence_rpm : 0;
	}
	else
		st->link_up = false;
}

void bike_feed_at(const BikeFeed *feed, uint32_t tick, BikeFeedState *io)
{
	if (feed->is_const)
	{
		if (tick % BIKE_FEED_CONST_PERIOD == 0)
		{
			BikeFeedRow r = feed->const_row;
			r.tick = tick;
			apply_row(&r, io);
		}
		return;
	}
	while (io->next < feed->count && feed->rows[io->next].tick <= tick)
		apply_row(&feed->rows[io->next++], io);
}
