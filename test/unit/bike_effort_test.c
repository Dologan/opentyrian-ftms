/*
 * Unit test of the fixed-point effort engine (src/bike_effort.c) and the feed
 * reader (src/bike_feed.c) against the golden files in test/bike/effort/golden
 * (feeds in test/bike/effort/feeds; both owned by this repo).  The goldens are
 * written by this engine (tools/bike/gen_effort_golden.sh); they were first copied
 * from the output of Flywheel's TypeScript EffortEngine and the C engine matched
 * them within the tolerances below, which is what the port was held to.  Built
 * and run by tools/check_bike_effort.sh; needs no SDL and no game data.
 *
 * usage: bike_effort_test REPO_ROOT [--update-digests]
 *        bike_effort_test REPO_ROOT --dump-effort FEED FTP_W [TICKS]   golden CSV to stdout
 *        bike_effort_test REPO_ROOT --gen-golden [OUTDIR]              rewrite the goldens
 * The last two are the golden generator (tools/bike/gen_effort_golden.sh): run it only
 * after an intentional change of the engine, then review `git diff test/bike/effort`.
 *
 * Tolerances (fixed point vs the float reference):
 *   effort_w, baseline_w   +-(0.25 W + 0.2 %)
 *   effort_norm, surge     +-0.002, +-0.01
 *   surge_meter            +-0.01
 * The fixed-point error is far below these: inputs are quantised to 2^-16, each
 * EMA step rounds to 2^-16 W (random walk over <= ~50k steps: < 0.01 W) and the
 * Q30 alpha is accurate to ~1e-6 relative.  The slack covers the comparison of
 * discrete decisions: norm/surge/meter are not compared within +-2 ticks of a
 * transition or event, and each boolean transition / event must exist in both
 * outputs, in the same count, within +-2 ticks.
 *
 * Determinism: every case is run twice; the digest of all per-tick outputs and
 * of the final state must be identical, and equal to the committed value in
 * test/bike/effort/c_digests.txt (so other platforms/compilers must agree).
 */
#include "../../src/bike_effort.h"
#include "../../src/bike_feed.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DT_US 28750u
#define MAX_TICKS 200000
#define NEAR_TICKS 2

static int failures = 0;

static void check(int ok, const char *what)
{
	if (!ok)
	{
		printf("FAIL %s\n", what);
		++failures;
	}
}

typedef struct
{
	double effort_w, norm, meter, baseline, surge;
	int moving, stale, surging, events;
} Row;

static uint64_t fold(uint64_t h, uint64_t v)
{
	int i;
	for (i = 0; i < 8; ++i)
	{
		h ^= (uint8_t)(v >> (8 * i));
		h *= 1099511628211ull;
	}
	return h;
}

/* One full run of a case; fills rows[] (as doubles) and returns the digest. */
static uint64_t run_case(const BikeFeed *feed, uint32_t ftp, unsigned ticks, Row *rows)
{
	BikeEffortConfig cfg;
	BikeEffortState st;
	BikeFeedState fs;
	uint64_t h = 14695981039346656037ull;
	unsigned t;

	bike_effort_config_default(&cfg, ftp);
	bike_effort_init(&st, &cfg);
	bike_feed_state_init(&fs);
	for (t = 0; t < ticks; ++t)
	{
		BikeEffortInput in;
		BikeEffortSignals o;

		bike_feed_at(feed, t, &fs);
		in.has_sample = fs.has_sample;
		in.link_up = fs.link_up;
		in.power_valid = fs.power_valid;
		in.seq = fs.seq;
		in.power_w = fs.power_w;
		in.cadence_rpm = fs.cadence_rpm;
		in.age_us = fs.has_sample ? (t - fs.arrival_tick) * DT_US : 0;
		bike_effort_step(&st, &cfg, DT_US, &in, &o);
		if (rows != NULL)
		{
			Row *r = &rows[t];
			r->effort_w = o.effort_w / 65536.0;
			r->norm = o.effort_norm / 65536.0;
			r->meter = o.surge_meter / 65536.0;
			r->baseline = o.baseline_w / 65536.0;
			r->surge = o.surge / 65536.0;
			r->moving = o.moving;
			r->stale = o.stale;
			r->surging = o.surging;
			r->events = o.events;
		}
		h = fold(h, (uint32_t)o.effort_w);
		h = fold(h, (uint32_t)o.effort_norm);
		h = fold(h, (uint32_t)o.surge_meter);
		h = fold(h, (uint32_t)o.baseline_w);
		h = fold(h, (uint32_t)o.surge);
		h = fold(h, (uint32_t)o.power_w);
		h = fold(h, (uint32_t)o.cadence_rpm);
		h = fold(h, (uint64_t)o.moving | (uint64_t)o.stale << 1 | (uint64_t)o.surging << 2 | (uint64_t)o.events << 8);
		h = fold(h, bike_effort_digest(&st));
	}
	return h;
}

static int read_golden(const char *path, Row *rows, unsigned ticks)
{
	FILE *f = fopen(path, "rb");
	char line[512];
	unsigned n = 0;

	if (f == NULL)
		return 0;
	if (fgets(line, sizeof line, f) == NULL) /* header */
	{
		fclose(f);
		return 0;
	}
	while (fgets(line, sizeof line, f) != NULL && n < ticks)
	{
		unsigned tick;
		Row *r = &rows[n];
		if (sscanf(line, "%u,%lf,%lf,%d,%d,%d,%lf,%d,%lf,%lf", &tick, &r->effort_w, &r->norm,
		           &r->moving, &r->stale, &r->surging, &r->meter, &r->events, &r->baseline,
		           &r->surge) != 10 || tick != n)
		{
			fclose(f);
			return 0;
		}
		++n;
	}
	fclose(f);
	return n == ticks;
}

/* ticks at which a boolean column changes */
static unsigned transitions(const Row *r, unsigned ticks, int which, unsigned *out)
{
	unsigned t, n = 0;
	int prev = 0;
	for (t = 0; t < ticks; ++t)
	{
		int v = which == 0 ? r[t].moving : which == 1 ? r[t].stale : r[t].surging;
		if (t > 0 && v != prev && n < 256)
			out[n++] = t;
		prev = v;
	}
	return n;
}

static unsigned event_ticks(const Row *r, unsigned ticks, int bit, unsigned *out)
{
	unsigned t, n = 0;
	for (t = 0; t < ticks; ++t)
		if ((r[t].events & bit) && n < 256)
			out[n++] = t;
	return n;
}

static int match_lists(const unsigned *a, unsigned na, const unsigned *b, unsigned nb)
{
	unsigned i;
	if (na != nb)
		return 0;
	for (i = 0; i < na; ++i)
		if ((a[i] > b[i] ? a[i] - b[i] : b[i] - a[i]) > NEAR_TICKS)
			return 0;
	return 1;
}

typedef struct { double effort_w, norm, meter, baseline, surge; } MaxDev;

static void compare(const char *name, const Row *c, const Row *g, unsigned ticks, MaxDev *md)
{
	static unsigned lc[256], lg[256];
	static unsigned char near_mask[MAX_TICKS];
	unsigned t, i, nc, ng;
	char what[512];
	int w;

	memset(near_mask, 0, ticks);
	for (w = 0; w < 8; ++w)
	{
		if (w < 3)
			ng = transitions(g, ticks, w, lg);
		else if (w < 8)
			ng = event_ticks(g, ticks, 1 << (w - 3), lg);
		for (i = 0; i < ng; ++i)
			for (t = lg[i] > NEAR_TICKS ? lg[i] - NEAR_TICKS : 0; t <= lg[i] + NEAR_TICKS && t < ticks; ++t)
				near_mask[t] = 1;
		if (w < 3)
			nc = transitions(c, ticks, w, lc);
		else
			nc = event_ticks(c, ticks, 1 << (w - 3), lc);
		for (i = 0; i < nc; ++i)
			for (t = lc[i] > NEAR_TICKS ? lc[i] - NEAR_TICKS : 0; t <= lc[i] + NEAR_TICKS && t < ticks; ++t)
				near_mask[t] = 1;
		sprintf(what, "%s: %s transitions/events match (golden %u, C %u)", name,
		        w == 0 ? "moving" : w == 1 ? "stale" : w == 2 ? "surging" :
		        w == 3 ? "movingStart" : w == 4 ? "movingStop" : w == 5 ? "surgeStart" :
		        w == 6 ? "surgeEnd" : "overdrive", ng, nc);
		check(match_lists(lc, nc, lg, ng), what);
	}

	for (t = 0; t < ticks; ++t)
	{
		double d = fabs(c[t].effort_w - g[t].effort_w);
		double db = fabs(c[t].baseline - g[t].baseline);
		if (d > md->effort_w)
			md->effort_w = d;
		if (db > md->baseline)
			md->baseline = db;
		if (d > 0.25 + 0.002 * fabs(g[t].effort_w) || db > 0.25 + 0.002 * fabs(g[t].baseline))
		{
			sprintf(what, "%s tick %u: effort_w %.4f vs golden %.4f / baseline %.4f vs %.4f", name, t,
			        c[t].effort_w, g[t].effort_w, c[t].baseline, g[t].baseline);
			check(0, what);
			return;
		}
		if (near_mask[t])
			continue;
		d = fabs(c[t].norm - g[t].norm);
		if (d > md->norm)
			md->norm = d;
		if (d > 0.002)
		{
			sprintf(what, "%s tick %u: effort_norm %.5f vs golden %.5f", name, t, c[t].norm, g[t].norm);
			check(0, what);
			return;
		}
		d = fabs(c[t].meter - g[t].meter);
		if (d > md->meter)
			md->meter = d;
		if (d > 0.01)
		{
			sprintf(what, "%s tick %u: surge_meter %.5f vs golden %.5f", name, t, c[t].meter, g[t].meter);
			check(0, what);
			return;
		}
		d = fabs(c[t].surge - g[t].surge);
		if (d > md->surge)
			md->surge = d;
		if (d > 0.01)
		{
			sprintf(what, "%s tick %u: surge %.5f vs golden %.5f", name, t, c[t].surge, g[t].surge);
			check(0, what);
			return;
		}
	}
}

/* ---- hand tables ------------------------------------------------------------ */

static void check_alpha(void)
{
	static const uint32_t dts[] = { 28750, 7187, 100000, 1000000, 750000 };
	static const uint32_t taus[] = { 1500000, 3000000, 75000000, 4000000, 300000000, 5000000 };
	unsigned i, j;
	double worst = 0;

	for (i = 0; i < sizeof dts / sizeof dts[0]; ++i)
		for (j = 0; j < sizeof taus / sizeof taus[0]; ++j)
		{
			double ref = 1.0 - exp(-(double)dts[i] / taus[j]);
			double got = bike_ema_alpha_q30(dts[i], taus[j]) / 1073741824.0;
			if (fabs(got - ref) > worst)
				worst = fabs(got - ref);
		}
	/* x up to 1 and beyond goes through the halving path */
	for (i = 1; i <= 40; ++i)
	{
		double x = i * 0.1, ref = 1.0 - exp(-x);
		double got = bike_ema_alpha_q30((uint32_t)(x * 1000000), 1000000) / 1073741824.0;
		if (fabs(got - ref) > worst)
			worst = fabs(got - ref);
	}
	printf("alpha: max |err| vs 1-exp(-x) = %.3g\n", worst);
	check(worst < 2e-6, "EMA alpha within 2e-6 of 1-exp(-x)");
	check(bike_ema_alpha_q30(1000, 0) == (1u << 30), "alpha with tau 0 is 1");
	check(bike_ema_alpha_q30(0, 1000000) == 0, "alpha with dt 0 is 0");
}

static void check_norm(void)
{
	check(bike_effort_norm(0) == 6554, "norm(0) = .10");
	check(bike_effort_norm(-65536) == 6554, "norm clamps below 0");
	check(bike_effort_norm(26214) == 9830, "norm(.40) = .15");
	check(bike_effort_norm(49152) == 32768, "norm(.75) = .5");
	check(bike_effort_norm(72090) == 65536, "norm(1.10) = 1");
	check(bike_effort_norm(5 * 65536) == 65536, "norm clamps above 1.10");
	/* midpoint (0.575, 0.325) */
	check(abs(bike_effort_norm(37683) - 21299) <= 1, "norm(.575) = .325");
}

static void check_parse(void)
{
	const char *e;
	check(bike_parse_decimal("150", &e) == 150 * 65536 && *e == '\0', "parse integer");
	check(bike_parse_decimal("0.5,", &e) == 32768 && *e == ',', "parse .5 and stop at comma");
	check(bike_parse_decimal("-1.25", &e) == -81920, "parse negative");
	check(bike_parse_decimal("0.1", &e) == 6554, "parse 0.1 rounds to nearest");
	check(bike_parse_decimal("x", &e) == 0 && *e == 'x', "parse nothing");
	check(bike_parse_decimal(".25", &e) == 16384, "parse leading dot");
}

static void check_feed_const(void)
{
	BikeFeed f;
	BikeFeedState s;
	uint32_t t;

	check(bike_feed_load(&f, "const:150,90.5"), "load const spec");
	bike_feed_state_init(&s);
	for (t = 0; t < 100; ++t)
	{
		bike_feed_at(&f, t, &s);
		if (t == 31)
			check(s.seq == 1 && s.arrival_tick == 0, "const: one sample before tick 32");
	}
	check(s.seq == 4 && s.arrival_tick == 96 && s.power_w == 150 * 65536 && s.cadence_rpm == 90 * 65536 + 32768,
	      "const: row every 32 ticks");
	bike_feed_free(&f);
	check(!bike_feed_load(&f, "const:abc"), "const: rejects garbage");
}


/* ---- surge threshold, gauge scale, start cue --------------------------------- */

/* A warmed-up, moving engine at `ftp`, built by running the real step for 40 s at 90 rpm and `watts`. */
static void warm_engine(BikeEffortConfig *c, BikeEffortState *s, unsigned ftp, unsigned watts)
{
	BikeEffortInput in;
	BikeEffortSignals out;
	unsigned i;

	bike_effort_config_default(c, ftp);
	bike_effort_init(s, c);
	memset(&in, 0, sizeof in);
	in.has_sample = in.link_up = in.power_valid = true;
	in.power_w = (bq16)(watts * 65536);
	in.cadence_rpm = 90 * 65536;
	for (i = 0; i < 40u * 1000000u / DT_US; ++i)
	{
		in.seq = i;
		bike_effort_step(s, c, DT_US, &in, &out);
	}
}

/* Does the real detector start a surge when the smoothed power is `fast` over `baseline`?  dt = 0 leaves both
 * EMAs untouched; with a hold of 1 us and a candidate time of 1 us, a qualifying step keeps it at 1 (event) and
 * any other step resets it to 0 (no event). */
static int detector_starts(const BikeEffortConfig *base, const BikeEffortState *warm, bq16 baseline, bq16 fast)
{
	BikeEffortConfig c = *base;
	BikeEffortState s = *warm;
	BikeEffortInput in;
	BikeEffortSignals out;

	c.surge_hold_us = 1;
	s.surging = false;
	s.surge_cand_us = 1;
	s.baseline_w = baseline;
	s.fast_w = fast;
	memset(&in, 0, sizeof in);
	in.has_sample = in.link_up = in.power_valid = true;
	in.seq = s.last_seq;
	in.power_w = fast;
	in.cadence_rpm = 90 * 65536;
	bike_effort_step(&s, &c, 0, &in, &out);
	return (out.events & BIKE_EV_SURGE_START) != 0;
}

static void check_surge_threshold(void)
{
	static const unsigned ftps[] = { 50, 137, 200, 250, 500 };
	/* min: 0, 0.6, 0.8 (default), 1.0 (the min-pct floor binds at a low baseline); ratio: 1.05 (lowest key value),
	 * 1.2 (default), 1.5, 2.0 (highest key value) */
	static const bq16 mins[] = { 0, 39322, 52429, 65536 };
	static const bq16 ratios[] = { 68813, 78643, 98304, 131072 };
	BikeEffortConfig c, w;
	BikeEffortState s, ws;
	bq16 t = 0;
	unsigned a, b, r, k;
	long bad = 0, tried = 0;

	/* 1. before the warm-up and while not moving there is no threshold (the detector cannot qualify either) */
	warm_engine(&c, &s, 200, 150);
	check(s.moving && s.moving_time_us >= c.surge_warmup_us, "threshold: the engine warmed up and is moving");
	check(bike_surge_threshold(&s, &c, &t), "threshold: exists once warmed up and moving");
	ws = s;
	ws.moving_time_us = c.surge_warmup_us - 1;
	t = 1234;
	check(!bike_surge_threshold(&ws, &c, &t) && t == 1234, "threshold: none one microsecond before the warm-up");
	ws.moving_time_us = c.surge_warmup_us;
	check(bike_surge_threshold(&ws, &c, &t), "threshold: present exactly at the warm-up");
	ws = s;
	ws.moving = false;
	t = 1234;
	check(!bike_surge_threshold(&ws, &c, &t) && t == 1234, "threshold: none when not moving");
	{
		BikeEffortState fresh;
		bike_effort_init(&fresh, &c);
		check(!bike_surge_threshold(&fresh, &c, &t), "threshold: none on a fresh engine");
	}

	/* 2. values, FTP 200 W: floor 100 W, ratio 1.2; first with min-pct 0.6 (the old default), then the default 0.8 */
	bike_effort_config_default(&c, 200);
	check(c.surge_ratio == 78643 && c.surge_min_pct == 52429, "threshold: the default is ratio 1.2 and min-pct 0.8");
	c.surge_min_pct = 39322; /* 0.6 */
	check(abs(bike_surge_threshold_w(&c, 100 * 65536) - 120 * 65536) <= 256, "threshold: at the baseline floor it is 120 W (min-pct = ratio * floor)");
	check(bike_surge_threshold_w(&c, 40 * 65536) == bike_surge_threshold_w(&c, 100 * 65536),
	      "threshold: a baseline below the floor is floored like the detector's");
	check(bike_surge_threshold_w(&c, 0) == bike_surge_threshold_w(&c, 100 * 65536), "threshold: baseline 0 is floored");
	check(abs(bike_surge_threshold_w(&c, 200 * 65536) - 240 * 65536) <= 256, "threshold: a high baseline gives ratio * baseline (240 W)");
	check(abs(bike_surge_threshold_w(&c, 150 * 65536) - 180 * 65536) <= 256, "threshold: 150 W baseline gives 180 W");
	c.surge_min_pct = 52429; /* 0.8 (default): the min-pct floor of 160 W binds below a 133.3 W baseline */
	check(abs(bike_surge_threshold_w(&c, 100 * 65536) - 160 * 65536) <= 256, "threshold: below the min-pct floor it is min_pct * ftp (160 W)");
	check(abs(bike_surge_threshold_w(&c, 120 * 65536) - 160 * 65536) <= 256, "threshold: 120 W baseline still on the min-pct floor");
	check(abs(bike_surge_threshold_w(&c, 140 * 65536) - 168 * 65536) <= 256, "threshold: 140 W baseline is on the ratio side (168 W)");
	/* the other end of the keys: ratio 2.0 with min-pct 1.0 (200 W), and ratio 1.05 with min-pct 0 */
	c.surge_ratio = 131072;
	c.surge_min_pct = 65536;
	check(abs(bike_surge_threshold_w(&c, 100 * 65536) - 200 * 65536) <= 256, "threshold: ratio 2.0 on the 100 W floor gives 200 W, the same as min-pct 1.0");
	check(abs(bike_surge_threshold_w(&c, 150 * 65536) - 300 * 65536) <= 256, "threshold: ratio 2.0 on a 150 W baseline gives 300 W");
	c.surge_ratio = 68813;
	c.surge_min_pct = 0;
	check(abs(bike_surge_threshold_w(&c, 100 * 65536) - 105 * 65536) <= 256, "threshold: ratio 1.05, no min-pct: 105 W on the floor");
	check(abs(bike_surge_threshold_w(&c, 200 * 65536) - 210 * 65536) <= 256, "threshold: ratio 1.05 on a 200 W baseline gives 210 W");
	bike_effort_config_default(&c, 200);
	check(bike_surge_threshold_w(&c, INT32_MAX) == INT32_MAX, "threshold: saturates instead of overflowing");

	/* 3. exactness against the real detector: the threshold is the first smoothed power that starts a surge */
	for (a = 0; a < sizeof ftps / sizeof ftps[0]; ++a)
		for (b = 0; b < sizeof mins / sizeof mins[0]; ++b)
			for (r = 0; r < sizeof ratios / sizeof ratios[0]; ++r)
			{
				warm_engine(&w, &ws, ftps[a], 150);
				w.surge_min_pct = mins[b];
				w.surge_ratio = ratios[r];
				for (k = 0; k < 40; ++k)
				{
					const bq16 base = (bq16)((int64_t)ftps[a] * 65536 * k / 12 + (int64_t)k * 7919);
					const bq16 fl = (bq16)((int64_t)ftps[a] * w.baseline_floor_pct);
					const bq16 floored = base > fl ? base : fl;
					const bq16 thr = bike_surge_threshold_w(&w, base);

					++tried;
					if (!detector_starts(&w, &ws, floored, thr) || detector_starts(&w, &ws, floored, thr - 1) ||
					    bike_surge_threshold_w(&w, floored) != thr)
					{
						if (bad++ < 5)
							printf("  mismatch ftp %u min %d ratio %d base %ld thr %ld\n", ftps[a], (int)mins[b], (int)ratios[r], (long)base, (long)thr);
					}
				}
			}
	printf("surge threshold: %ld baselines x configs against the detector, %ld mismatches\n", tried, bad);
	check(bad == 0, "threshold: equals the first smoothed power at which the detector starts a surge");

	/* 4. along a real run (FTP 230 W, 150 W with two 20 s bursts): the candidate time runs exactly while the
	 *    threshold exists and fast >= threshold, and a start event only happens at or above the threshold (the
	 *    marker cannot disagree with the detector).  The default rule and the other keys' values, the bursts
	 *    chosen so that either the ratio or the min-pct decides whether the rides surge. */
	{
		static const struct { bq16 ratio, min; unsigned burst_w, starts; const char *what; } rides[] = {
			{ 78643, 52429, 330, 2, "default, 330 W bursts" },
			{ 78643, 52429, 200, 2, "default, 200 W bursts: above 0.8 x FTP (184 W) and 1.2 x baseline" },
			{ 78643, 65536, 200, 0, "ratio 1.2, min-pct 1.0 (230 W): a 200 W burst is too low" },
			{ 78643, 39322, 165, 0, "ratio 1.2, min-pct 0.6: 165 W is below 1.2 x baseline" },
			/* 150 W steady already beats 1.05 x the baseline (it is still rising, about 141 W at 59 s): one surge from the
			 * end of the warm-up that never ends */
			{ 68813, 0, 165, 1, "ratio 1.05, no min-pct: 150 W steady is already a surge" },
			/* the first burst lifts the baseline (about 160 W at 80 s, 166 W early in the second burst), so the
			 * smoothed power never reaches 2.0 x baseline again */
			{ 131072, 65536, 330, 1, "ratio 2.0, min-pct 1.0: the first burst only" },
			{ 131072, 65536, 250, 0, "ratio 2.0, min-pct 1.0: 250 W is not twice the baseline" }
		};
		unsigned n;

		for (n = 0; n < sizeof rides / sizeof rides[0]; ++n)
		{
			BikeEffortInput in;
			BikeEffortSignals out;
			unsigned i, starts = 0, agree = 0;
			long disagree = 0;
			char what[160];

			bike_effort_config_default(&c, 230);
			c.surge_ratio = rides[n].ratio;
			c.surge_min_pct = rides[n].min;
			bike_effort_init(&s, &c);
			memset(&in, 0, sizeof in);
			in.has_sample = in.link_up = in.power_valid = true;
			in.cadence_rpm = 90 * 65536;
			for (i = 0; i < 160u * 1000000u / DT_US; ++i)
			{
				const unsigned sec = (unsigned)((uint64_t)i * DT_US / 1000000u);
				const unsigned watts = ((sec >= 60 && sec < 80) || (sec >= 120 && sec < 140)) ? rides[n].burst_w : 150;
				bq16 thr = 0;
				int above;

				in.seq = i;
				in.power_w = (bq16)(watts * 65536);
				bike_effort_step(&s, &c, DT_US, &in, &out);
				above = bike_surge_threshold(&s, &c, &thr) && s.fast_w >= thr;
				if (thr != 0 && thr != bike_surge_threshold_w(&c, out.baseline_w))
					++disagree; /* the signals' floored baseline gives the same threshold as the state's */
				if (out.events & BIKE_EV_SURGE_START)
				{
					++starts;
					if (!above)
						++disagree;
				}
				if (!s.surging && !(out.events & BIKE_EV_SURGE_START))
				{
					if ((s.surge_cand_us > 0) != (above != 0))
						++disagree;
					else
						++agree;
				}
			}
			printf("surge threshold: along a ride (%s) %u starts, %u candidate checks agree, %ld disagree\n", rides[n].what,
			       starts, agree, disagree);
			sprintf(what, "threshold ride (%s): %u surges started", rides[n].what, rides[n].starts);
			check(starts == rides[n].starts, what);
			sprintf(what, "threshold ride (%s): the marker and the detector never disagree", rides[n].what);
			check(disagree == 0, what);
		}
	}

	/* 5. gauge scale: max(1.5 * ftp, 1.25 * threshold) */
	bike_effort_config_default(&c, 200);
	check(bike_surge_gauge_scale_w(&c, false, 999 * 65536) == 300 * 65536, "scale: 1.5 x FTP without a threshold (threshold ignored)");
	check(bike_surge_gauge_scale_w(&c, true, 120 * 65536) == 300 * 65536, "scale: 1.5 x FTP while 1.25 x threshold is lower");
	check(bike_surge_gauge_scale_w(&c, true, 240 * 65536) == 300 * 65536, "scale: tie at a 240 W threshold");
	check(bike_surge_gauge_scale_w(&c, true, 400 * 65536) == 500 * 65536, "scale: 1.25 x threshold when that is larger");
	{
		bq16 thr = 0;
		warm_engine(&c, &s, 200, 400); /* a high baseline pushes the threshold past 1.2 x FTP */
		check(bike_surge_threshold(&s, &c, &thr) && thr <= bike_surge_gauge_scale_w(&c, true, thr), "scale: the marker is always on the scale");
	}
}

static void check_surge_cue(void)
{
	const uint32_t dt = 28750u;
	uint32_t rem = 0, t = 0, lit = 0, flashes = 0;
	int prev = 0;

	check(!bike_surge_cue_lit(0), "cue: not lit when idle");
	check(bike_surge_cue_step(0, dt, 0, false) == 0, "cue: stays idle without a start");
	check(bike_surge_cue_step(0, dt, 0, true) == 0, "cue: a surge already running does not start a cue");
	rem = bike_surge_cue_step(0, dt, BIKE_EV_SURGE_START, true);
	check(rem == BIKE_SURGE_CUE_US && bike_surge_cue_lit(rem), "cue: SURGE_START sets 1.5 s and starts lit");
	/* count it down in gameplay time (frame dt), as bike.c does */
	while (rem > 0 && t < 3000000u)
	{
		const int l = bike_surge_cue_lit(rem);
		if (l)
			++lit;
		if (l && !prev)
			++flashes;
		prev = l;
		rem = bike_surge_cue_step(rem, dt, 0, true);
		t += dt;
	}
	printf("surge cue: ran %u us in %u us frames, lit %u frames, %u flashes\n", (unsigned)t, (unsigned)dt, (unsigned)lit, (unsigned)flashes);
	check(t >= BIKE_SURGE_CUE_US && t < BIKE_SURGE_CUE_US + dt, "cue: lasts 1.5 s of summed frame time");
	check(flashes == 3, "cue: three flashes");
	/* frame-rate independent: 1 ms frames and 100 ms frames light about the same total time */
	{
		const uint32_t dts[2] = { 1000u, 100000u };
		uint32_t n, lit_us[2] = { 0, 0 };

		for (n = 0; n < 2; ++n)
		{
			rem = bike_surge_cue_step(0, dts[n], BIKE_EV_SURGE_START, true);
			while (rem > 0)
			{
				if (bike_surge_cue_lit(rem))
					lit_us[n] += dts[n];
				rem = bike_surge_cue_step(rem, dts[n], 0, true);
			}
		}
		check(lit_us[0] >= 740000u && lit_us[0] <= 760000u && lit_us[1] >= 600000u && lit_us[1] <= 900000u,
		      "cue: about 0.75 s lit in total whatever the frame rate");
	}
	check(bike_surge_cue_step(1000000u, dt, BIKE_EV_SURGE_END, false) == 0, "cue: ends when the surge ends");
	check(bike_surge_cue_step(100u, dt, 0, true) == 0, "cue: the last frame saturates at 0");
	check(bike_surge_cue_step(100000u, dt, BIKE_EV_SURGE_START, true) == BIKE_SURGE_CUE_US, "cue: a new start restarts it");
	check(!bike_surge_cue_lit(BIKE_SURGE_CUE_US + 1), "cue: out-of-range remaining time is not lit");
}

/* ---- main ------------------------------------------------------------------- */

static Row cbuf[MAX_TICKS], gbuf[MAX_TICKS];

/* ---- golden generator ---------------------------------------------------------
 * Writes what this engine does for a feed, in the golden CSV format (columns
 * tick,effort_w,effort_norm,moving,stale,surging,surge_meter,events,baseline_w,surge),
 * from the very same run_case() the comparison above uses. */

#define GOLDEN_TAIL_TICKS 400 /* silence after the last feed row, so the stop is in the golden */

static void write_golden(FILE *f, const Row *rows, unsigned ticks)
{
	unsigned t;

	fputs("tick,effort_w,effort_norm,moving,stale,surging,surge_meter,events,baseline_w,surge\n", f);
	for (t = 0; t < ticks; ++t)
	{
		const Row *r = &rows[t];
		fprintf(f, "%u,%.5f,%.6f,%d,%d,%d,%.6f,%d,%.5f,%.6f\n", t, r->effort_w, r->norm, r->moving,
		        r->stale, r->surging, r->meter, r->events, r->baseline, r->surge);
	}
}

/* ticks of a case: the last feed row plus the tail (a const: feed has no end: pass TICKS) */
static unsigned golden_ticks(const BikeFeed *bf)
{
	if (bf->is_const || bf->count == 0)
		return 0;
	return bf->rows[bf->count - 1].tick + GOLDEN_TAIL_TICKS;
}

static int dump_effort(const char *feed, unsigned ftp, unsigned ticks)
{
	BikeFeed bf;

	if (!bike_feed_load(&bf, feed))
	{
		fprintf(stderr, "cannot load feed %s\n", feed);
		return 1;
	}
	if (ticks == 0)
		ticks = golden_ticks(&bf);
	if (ticks == 0 || ticks > MAX_TICKS)
	{
		fprintf(stderr, "give TICKS (1..%d) for this feed\n", MAX_TICKS);
		bike_feed_free(&bf);
		return 1;
	}
	run_case(&bf, ftp, ticks, cbuf);
	write_golden(stdout, cbuf, ticks);
	bike_feed_free(&bf);
	return 0;
}

/* Reads REPO_ROOT/test/bike/effort/golden/manifest.txt for the cases (name feed ftp_w; its ticks
 * column is recomputed) and writes the CSVs and the manifest into OUTDIR (default: that folder). */
static int gen_golden(const char *root, const char *outdir)
{
	char path[1024], line[512], name[128], feed[128];
	char names[64][128], feeds[64][128], dir[512];
	unsigned ftps[64], tickv[64], n = 0, i, ftp, ticks;
	FILE *man, *out;

	if (strlen(outdir) >= sizeof dir)
	{
		fprintf(stderr, "OUTDIR is too long\n");
		return 1;
	}
	strcpy(dir, outdir);
	sprintf(path, "%s/test/bike/effort/golden/manifest.txt", root);
	man = fopen(path, "rb");
	if (man == NULL)
	{
		fprintf(stderr, "cannot open %s\n", path);
		return 1;
	}
	while (n < 64 && fgets(line, sizeof line, man) != NULL)
		if (sscanf(line, "%127s %127s %u %u", name, feed, &ftp, &ticks) == 4)
		{
			strcpy(names[n], name);
			strcpy(feeds[n], feed);
			ftps[n++] = ftp;
		}
	fclose(man);

	for (i = 0; i < n; ++i)
	{
		BikeFeed bf;

		sprintf(path, "%s/test/bike/effort/feeds/%s", root, feeds[i]);
		if (!bike_feed_load(&bf, path))
		{
			fprintf(stderr, "cannot load feed %s\n", path);
			return 1;
		}
		ticks = golden_ticks(&bf);
		if (ticks == 0 || ticks > MAX_TICKS)
		{
			fprintf(stderr, "%s: no usable length\n", names[i]);
			bike_feed_free(&bf);
			return 1;
		}
		run_case(&bf, ftps[i], ticks, cbuf);
		bike_feed_free(&bf);
		tickv[i] = ticks;
		sprintf(path, "%s/%s.csv", dir, names[i]);
		out = fopen(path, "wb");
		if (out == NULL)
		{
			fprintf(stderr, "cannot write %s\n", path);
			return 1;
		}
		write_golden(out, cbuf, ticks);
		fclose(out);
	}
	sprintf(path, "%s/manifest.txt", dir);
	out = fopen(path, "wb");
	if (out == NULL)
	{
		fprintf(stderr, "cannot write %s\n", path);
		return 1;
	}
	fputs("# Effort golden data of this repo, generated from its own C engine by\n"
	      "# tools/bike/gen_effort_golden.sh (run it only after an intentional engine change): do not edit.\n"
	      "# columns: name feed ftp_w ticks\n", out);
	for (i = 0; i < n; ++i)
		fprintf(out, "%s %s %u %u\n", names[i], feeds[i], ftps[i], tickv[i]);
	fclose(out);
	printf("wrote %u goldens to %s\n", n, dir);
	return 0;
}

int main(int argc, char **argv)
{
	char path[1024], line[512], name[128], feed[128];
	unsigned ftp, ticks, ncases = 0;
	int update = argc > 2 && strcmp(argv[2], "--update-digests") == 0;
	FILE *man, *dig_in, *dig_out = NULL;
	char dig_names[64][128];
	unsigned long long dig_vals[64];
	unsigned ndig = 0;
	unsigned i;

	if (argc < 2)
	{
		printf("usage: bike_effort_test REPO_ROOT [--update-digests | --dump-effort FEED FTP_W [TICKS] | --gen-golden [OUTDIR]]\n");
		return 2;
	}
	if (argc >= 5 && strcmp(argv[2], "--dump-effort") == 0)
		return dump_effort(argv[3], (unsigned)strtoul(argv[4], NULL, 10), argc > 5 ? (unsigned)strtoul(argv[5], NULL, 10) : 0);
	if (argc >= 3 && strcmp(argv[2], "--gen-golden") == 0)
	{
		char dir[1024];

		sprintf(dir, "%s/test/bike/effort/golden", argv[1]);
		return gen_golden(argv[1], argc > 3 ? argv[3] : dir);
	}

	check_alpha();
	check_norm();
	check_parse();
	check_feed_const();
	check_surge_threshold();
	check_surge_cue();

	sprintf(path, "%s/test/bike/effort/c_digests.txt", argv[1]);
	dig_in = fopen(path, "rb");
	if (dig_in != NULL)
	{
		while (ndig < 64 && fscanf(dig_in, "%127s %llx", dig_names[ndig], &dig_vals[ndig]) == 2)
			++ndig;
		fclose(dig_in);
	}
	if (update)
		dig_out = fopen(path, "wb");

	sprintf(path, "%s/test/bike/effort/golden/manifest.txt", argv[1]);
	man = fopen(path, "rb");
	if (man == NULL)
	{
		printf("FAIL cannot open %s\n", path);
		return 1;
	}
	while (fgets(line, sizeof line, man) != NULL)
	{
		BikeFeed bf;
		MaxDev md;
		uint64_t h1, h2;
		int found = 0;

		if (sscanf(line, "%127s %127s %u %u", name, feed, &ftp, &ticks) != 4)
			continue;
		++ncases;
		if (ticks > MAX_TICKS)
		{
			check(0, "case too long");
			continue;
		}
		sprintf(path, "%s/test/bike/effort/feeds/%s", argv[1], feed);
		if (!bike_feed_load(&bf, path))
		{
			printf("FAIL %s: cannot load feed %s\n", name, path);
			++failures;
			continue;
		}
		sprintf(path, "%s/test/bike/effort/golden/%s.csv", argv[1], name);
		if (!read_golden(path, gbuf, ticks))
		{
			printf("FAIL %s: cannot read golden %s\n", name, path);
			++failures;
			bike_feed_free(&bf);
			continue;
		}
		h1 = run_case(&bf, ftp, ticks, cbuf);
		h2 = run_case(&bf, ftp, ticks, NULL);
		memset(&md, 0, sizeof md);
		compare(name, cbuf, gbuf, ticks, &md);
		printf("%-18s ticks %6u  max dev: effort_w %.4f W, baseline %.4f W, norm %.5f, meter %.5f, surge %.5f  digest %016llx\n",
		       name, ticks, md.effort_w, md.baseline, md.norm, md.meter, md.surge, (unsigned long long)h1);
		check(h1 == h2, "determinism: two runs give identical digests");
		if (dig_out != NULL)
			fprintf(dig_out, "%s %016llx\n", name, (unsigned long long)h1);
		else
		{
			for (i = 0; i < ndig; ++i)
				if (strcmp(dig_names[i], name) == 0)
				{
					found = 1;
					if (dig_vals[i] != h1)
					{
						printf("FAIL %s: digest %016llx differs from committed %016llx\n", name,
						       (unsigned long long)h1, dig_vals[i]);
						++failures;
					}
				}
			if (!found)
			{
				printf("FAIL %s: no committed digest (run tools/check_bike_effort.sh --update)\n", name);
				++failures;
			}
		}
		bike_feed_free(&bf);
	}
	fclose(man);
	if (dig_out != NULL)
		fclose(dig_out);
	check(ncases > 0, "manifest has cases");
	if (failures != 0)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("%u cases ok\n", ncases);
	return 0;
}
