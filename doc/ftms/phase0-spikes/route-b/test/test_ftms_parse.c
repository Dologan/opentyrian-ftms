/* Checks ftms_parse.c against shared/fixtures/ftms-vectors.json (every vector),
 * plus hand-built Cycling Power / cadence / encoder cases.
 * usage: test_ftms_parse path/to/ftms-vectors.json */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>

#include "../src/ftms_parse.h"
#include "tinyjson.h"

static int failures, checks;

#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	long n; char *b;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	b = malloc((size_t)n + 1);
	if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
	b[n] = 0; fclose(f);
	return b;
}

/* expected value of key `k` in expect-object, or NaN if the key is absent (=> must be absent) */
static double want(const jval *e, const char *k, int *present)
{
	const jval *v = j_get(e, k);
	*present = v != NULL && v->type == J_NUM;
	return *present ? v->num : 0.0;
}

#define FIELD(key, flag, actual)                                                        \
	do {                                                                                \
		int pr; double w = want(expect, key, &pr);                                      \
		CHECK(((d.present & (flag)) != 0) == (pr != 0), "%s: presence of %s", name, key); \
		if (pr) CHECK(fabs((double)(actual) - w) < 1e-9, "%s: %s got %.4f want %.4f", name, key, (double)(actual), w); \
	} while (0)

static void run_ibd(const jval *vec)
{
	const char *name = j_str(vec, "name"), *hex = j_str(vec, "hex");
	const jval *expect = j_get(vec, "expect");
	const jval *ok = j_get(expect, "ok");
	unsigned char buf[64];
	int len = hex_decode(hex, buf, sizeof buf);
	ftms_bike_data d;
	ftms_status st;
	int pr;

	CHECK(len >= 0, "%s: bad hex", name);
	st = ftms_parse_indoor_bike_data(buf, (size_t)len, &d);
	if (!ok->num)
	{
		CHECK(st != FTMS_OK, "%s: expected error, got OK", name);
		CHECK(d.present == 0, "%s: output not zeroed on error", name);
		return;
	}
	CHECK(st == FTMS_OK, "%s: expected OK, got %d", name, st);
	if (st != FTMS_OK) return;
	CHECK(d.more_data == (j_get(expect, "moreData")->num != 0), "%s: moreData", name);
	FIELD("speedKmh",            FTMS_HAS_SPEED,       ftms_speed_kmh(&d));
	FIELD("avgSpeedKmh",         FTMS_HAS_AVG_SPEED,   d.avg_speed_centi_kmh * 0.01);
	FIELD("cadenceRpm",          FTMS_HAS_CADENCE,     ftms_cadence_rpm(&d));
	FIELD("avgCadenceRpm",       FTMS_HAS_AVG_CADENCE, d.avg_cadence_half_rpm * 0.5);
	FIELD("distanceM",           FTMS_HAS_DISTANCE,    d.distance_m);
	FIELD("resistance",          FTMS_HAS_RESISTANCE,  d.resistance);
	FIELD("powerW",              FTMS_HAS_POWER,       d.power_w);
	FIELD("avgPowerW",           FTMS_HAS_AVG_POWER,   d.avg_power_w);
	FIELD("totalEnergyKcal",     FTMS_HAS_ENERGY,      d.energy_total_kcal);
	FIELD("heartRateBpm",        FTMS_HAS_HEART_RATE,  d.heart_rate_bpm);
	FIELD("met",                 FTMS_HAS_MET,         d.met_tenths * 0.1);
	FIELD("elapsedS",            FTMS_HAS_ELAPSED,     d.elapsed_s);
	FIELD("remainingS",          FTMS_HAS_REMAINING,   d.remaining_s);
	/* energyPerHour/Minute share the ENERGY presence bit */
	want(expect, "energyPerHourKcal", &pr);
	if (pr) CHECK(d.energy_per_hour_kcal == (uint16_t)j_get(expect, "energyPerHourKcal")->num, "%s: energyPerHour", name);
	want(expect, "energyPerMinuteKcal", &pr);
	if (pr) CHECK(d.energy_per_min_kcal == (uint8_t)j_get(expect, "energyPerMinuteKcal")->num, "%s: energyPerMinute", name);
}

static void run_cp(const jval *vec)
{
	const char *name = j_str(vec, "name"), *hex = j_str(vec, "hex");
	const jval *expect = j_get(vec, "expect");
	unsigned char buf[16];
	int len = hex_decode(hex, buf, sizeof buf);
	ftms_cp_response r;
	ftms_status st = ftms_parse_cp_response(buf, (size_t)len, &r);

	if (!j_get(expect, "ok")->num) { CHECK(st != FTMS_OK, "%s: expected error", name); return; }
	CHECK(st == FTMS_OK, "%s: expected OK", name);
	CHECK(r.request_op_code == (int)j_get(expect, "requestOpCode")->num, "%s: opcode", name);
	CHECK(r.result == (int)j_get(expect, "result")->num, "%s: result", name);
	CHECK((r.result == 1) == (j_get(expect, "success")->num != 0), "%s: success", name);
}

static void test_cps(void)
{
	/* flags=0x0020 (crank), power=250, crank revs=1000 time=2048 */
	const uint8_t crank[] = { 0x20, 0x00, 0xFA, 0x00, 0xE8, 0x03, 0x00, 0x08 };
	/* flags=0x0001|0x0800: balance + energy; power=-5 */
	const uint8_t bal[] = { 0x01, 0x08, 0xFB, 0xFF, 0x32, 0x34, 0x12 };
	/* flags=0x0010 wheel+ 0x0020 crank */
	const uint8_t both[] = { 0x30, 0x00, 0x64, 0x00, 1, 0, 0, 0, 2, 0, 3, 0, 4, 0 };
	const uint8_t trunc[] = { 0x20, 0x00, 0xFA, 0x00, 0xE8 };
	cps_measurement m;
	cps_crank_cadence cc;
	double rpm = -1;

	CHECK(cps_parse_measurement(crank, sizeof crank, &m) == FTMS_OK, "cps crank ok");
	CHECK(m.power_w == 250 && m.crank_revs == 1000 && m.crank_event_time == 2048 && (m.present & CPS_HAS_CRANK), "cps crank values");
	CHECK(cps_parse_measurement(bal, sizeof bal, &m) == FTMS_OK, "cps bal ok");
	CHECK(m.power_w == -5 && m.energy_kj == 0x1234 && (m.present & CPS_HAS_ENERGY), "cps balance+energy skips balance byte");
	CHECK(cps_parse_measurement(both, sizeof both, &m) == FTMS_OK, "cps both ok");
	CHECK(m.wheel_revs == 1 && m.wheel_event_time == 2 && m.crank_revs == 3 && m.crank_event_time == 4, "cps wheel+crank");
	CHECK(cps_parse_measurement(trunc, sizeof trunc, &m) == FTMS_ERR_SHORT, "cps truncated");
	CHECK(cps_parse_measurement(crank, 3, &m) == FTMS_ERR_SHORT, "cps too short");

	memset(&cc, 0, sizeof cc);
	CHECK(!cps_crank_cadence_update(&cc, 100, 1000, 0.0, &rpm), "cadence first sample has no rpm");
	CHECK(cps_crank_cadence_update(&cc, 101, 1000 + 683, 0.7, &rpm), "cadence second sample");
	CHECK(fabs(rpm - 1.0 / (683.0 / 1024.0) * 60.0) < 1e-6, "cadence ~90 rpm, got %f", rpm);
	/* uint16 wraparound of time and revs */
	CHECK(cps_crank_cadence_update(&cc, 102, (uint16_t)(1000 + 683 + 683), 1.4, &rpm), "cadence 3rd");
	cps_crank_cadence_update(&cc, 65535, 65000, 2.0, &rpm);
	cps_crank_cadence_update(&cc, 0, (uint16_t)(65000 + 600), 2.6, &rpm);
	CHECK(fabs(rpm - 1.0 / (600.0 / 1024.0) * 60.0) < 1e-6, "cadence across wrap, got %f", rpm);
	/* stalls: same event time for >3 s => 0 rpm */
	cps_crank_cadence_update(&cc, 0, (uint16_t)(65000 + 600), 4.0, &rpm);
	CHECK(rpm > 0, "not yet stopped at 1.4 s");
	cps_crank_cadence_update(&cc, 0, (uint16_t)(65000 + 600), 6.0, &rpm);
	CHECK(rpm == 0.0, "stopped after >3 s");
}

static void test_misc(void)
{
	uint8_t o[2];
	uint32_t mf, tf;
	const uint8_t feat[] = { 0x87, 0x52, 0, 0, 0x0c, 0, 0, 0 };  /* real bike: 21127 / 12 */
	ftms_encode_set_target_resistance(5.0, o);   CHECK(o[0] == 4 && o[1] == 50, "encode 5.0");
	ftms_encode_set_target_resistance(0.04, o);  CHECK(o[1] == 0, "encode 0.04 rounds to 0");
	ftms_encode_set_target_resistance(0.05, o);  CHECK(o[1] == 1, "encode 0.05 -> 1");
	ftms_encode_set_target_resistance(99.0, o);  CHECK(o[1] == 255, "encode clamp hi");
	ftms_encode_set_target_resistance(-3.0, o);  CHECK(o[1] == 0, "encode clamp lo");
	ftms_encode_set_target_resistance(NAN, o);   CHECK(o[1] == 0, "encode NaN");
	CHECK(ftms_parse_feature(feat, 8, &mf, &tf) == FTMS_OK && mf == 21127 && tf == 12, "feature");
	CHECK(ftms_parse_feature(feat, 7, &mf, &tf) == FTMS_ERR_SHORT, "feature short");
}

int main(int argc, char **argv)
{
	char *text; const char *p; jval root; const jval *a; size_t i;
	if (argc < 2) { fprintf(stderr, "usage: %s ftms-vectors.json\n", argv[0]); return 2; }
	text = slurp(argv[1]);
	if (!text) { perror(argv[1]); return 2; }
	p = text;
	if (j_parse(&p, &root)) { fprintf(stderr, "JSON parse error near: %.40s\n", p); return 2; }

	a = j_get(&root, "indoorBikeData");
	for (i = 0; i < a->n; ++i) run_ibd(&a->kids[i]);
	printf("indoorBikeData: %zu vectors\n", a->n);
	a = j_get(&root, "controlPointResponse");
	for (i = 0; i < a->n; ++i) run_cp(&a->kids[i]);
	printf("controlPointResponse: %zu vectors\n", a->n);
	test_cps();
	test_misc();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
