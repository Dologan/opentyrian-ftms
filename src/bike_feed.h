/*
 * bike_feed.h - scripted bike telemetry feeds (CSV files and "const:" specs).
 *
 * Feed CSV: tick,watts,cadence,connected.  Each connected=1 row is a new
 * notification; an empty watts or cadence field means null; connected=0 means
 * the link is down from that tick.  '#' lines and a header row are allowed.
 * "const:W[,RPM]" emits a row every BIKE_FEED_CONST_PERIOD ticks.
 * Decimals are parsed with integer arithmetic (no strtod, no locale).
 */
#ifndef BIKE_FEED_H
#define BIKE_FEED_H

#include "bike_effort.h"

#include <stddef.h>

#define BIKE_FEED_CONST_PERIOD 32

typedef struct
{
	uint32_t tick;
	bool connected, power_valid, cadence_valid;
	bq16 power_w, cadence_rpm;
} BikeFeedRow;

typedef struct
{
	BikeFeedRow *rows;
	size_t count;
	bool is_const;
	BikeFeedRow const_row;
} BikeFeed;

/* cursor and the values in force at the current tick */
typedef struct
{
	size_t next;
	bool link_up, has_sample, power_valid, cadence_valid;
	uint32_t seq;
	uint32_t arrival_tick;
	bq16 power_w, cadence_rpm;
} BikeFeedState;

bool bike_feed_load(BikeFeed *feed, const char *spec); /* file path or const:W[,RPM] */
void bike_feed_free(BikeFeed *feed);
void bike_feed_state_init(BikeFeedState *st);
/* Advance st to `tick`; call with ascending ticks. */
void bike_feed_at(const BikeFeed *feed, uint32_t tick, BikeFeedState *io);
/* [-]digits[.digits] -> Q16.16, rounded to nearest; *end is set past the number
 * (equal to the input when nothing parsed). */
bq16 bike_parse_decimal(const char *s, const char **end);

#endif
