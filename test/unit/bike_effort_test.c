/*
 * Unit test of the fixed-point effort engine (src/bike_effort.c) and the feed
 * reader (src/bike_feed.c) against golden output produced by Flywheel's real
 * TypeScript EffortEngine (tools/bike/golden/gen_effort_golden.mjs, outputs in
 * test/bike/golden).  Built and run by tools/check_bike_effort.sh; needs no SDL
 * and no game data.
 *
 * usage: bike_effort_test REPO_ROOT [--update-digests]
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
 * test/bike/golden/c_digests.txt (so other platforms/compilers must agree).
 */
#include "../../src/bike_effort.h"
#include "../../src/bike_feed.h"

#include <math.h>
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

/* ---- main ------------------------------------------------------------------- */

static Row cbuf[MAX_TICKS], gbuf[MAX_TICKS];

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
		printf("usage: bike_effort_test REPO_ROOT [--update-digests]\n");
		return 2;
	}

	check_alpha();
	check_norm();
	check_parse();
	check_feed_const();

	sprintf(path, "%s/test/bike/golden/c_digests.txt", argv[1]);
	dig_in = fopen(path, "rb");
	if (dig_in != NULL)
	{
		while (ndig < 64 && fscanf(dig_in, "%127s %llx", dig_names[ndig], &dig_vals[ndig]) == 2)
			++ndig;
		fclose(dig_in);
	}
	if (update)
		dig_out = fopen(path, "wb");

	sprintf(path, "%s/test/bike/golden/manifest.txt", argv[1]);
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
		sprintf(path, "%s/test/bike/feeds/%s", argv[1], feed);
		if (!bike_feed_load(&bf, path))
		{
			printf("FAIL %s: cannot load feed %s\n", name, path);
			++failures;
			continue;
		}
		sprintf(path, "%s/test/bike/golden/%s.csv", argv[1], name);
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
