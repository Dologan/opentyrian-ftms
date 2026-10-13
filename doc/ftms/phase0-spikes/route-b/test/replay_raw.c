/* replay_raw: feed a flywheel *.raw.jsonl FTMS capture through ftms_parse.c.
 *
 * usage: replay_raw ride.raw.jsonl [print_every_s=10] [ride.samples.json]
 *
 * Prints t / watts / cadence / resistance every print_every_s seconds of ride time.
 * With a samples.json (the Python decoder's output for the same ride) it additionally
 * checks every 0x2AD2 packet decodes to the identical (t, cadence, power, resistance).
 * Also demonstrates the "latest-sample mailbox" merge: fields persist across More-Data
 * fragments, exactly like the Python telemetry aggregator. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>

#include "../src/ftms_parse.h"
#include "tinyjson.h"

typedef struct   /* what the game loop would read each frame */
{
	int     have_power, have_cadence, have_resistance;
	int16_t power_w;
	double  cadence_rpm;
	int16_t resistance;
	unsigned long packets;
} latest_sample;

static void merge(latest_sample *s, const ftms_bike_data *d)
{
	if (d->present & FTMS_HAS_POWER)      { s->power_w = d->power_w; s->have_power = 1; }
	if (d->present & FTMS_HAS_CADENCE)    { s->cadence_rpm = ftms_cadence_rpm(d); s->have_cadence = 1; }
	if (d->present & FTMS_HAS_RESISTANCE) { s->resistance = d->resistance; s->have_resistance = 1; }
	s->packets++;
}

int main(int argc, char **argv)
{
	FILE *f;
	static char line[1 << 16];
	double every = argc > 2 ? atof(argv[2]) : 10.0, next_print = 0;
	jval samples; const jval *rows = NULL;
	size_t row = 0, mismatches = 0, decode_errors = 0, n2ad2 = 0, other = 0;
	latest_sample s;
	memset(&s, 0, sizeof s);

	if (argc < 2) { fprintf(stderr, "usage: %s ride.raw.jsonl [every_s] [samples.json]\n", argv[0]); return 2; }
	if (argc > 3)
	{
		FILE *sf = fopen(argv[3], "rb");
		char *txt; long n; const char *p;
		if (!sf) { perror(argv[3]); return 2; }
		fseek(sf, 0, SEEK_END); n = ftell(sf); fseek(sf, 0, SEEK_SET);
		txt = malloc((size_t)n + 1);
		if (fread(txt, 1, (size_t)n, sf) != (size_t)n) return 2;
		txt[n] = 0; fclose(sf); p = txt;
		if (j_parse(&p, &samples)) { fprintf(stderr, "bad samples json\n"); return 2; }
		rows = j_get(&samples, "samples");
	}
	f = fopen(argv[1], "r");
	if (!f) { perror(argv[1]); return 2; }

	printf("%8s %6s %8s %5s\n", "t(s)", "watts", "cad_rpm", "res");
	while (fgets(line, sizeof line, f))
	{
		jval o; const char *p = line, *ch, *hex;
		unsigned char buf[64]; int len; ftms_bike_data d;
		const jval *tv;
		if (j_parse(&p, &o)) continue;          /* skip malformed lines like the Python reader */
		ch = j_str(&o, "char");
		if (!ch || strcmp(ch, "2ad2") != 0) { ++other; continue; }
		hex = j_str(&o, "hex"); tv = j_get(&o, "t");
		if (!hex || !tv) continue;
		len = hex_decode(hex, buf, sizeof buf);
		++n2ad2;
		if (len < 0 || ftms_parse_indoor_bike_data(buf, (size_t)len, &d) != FTMS_OK) { ++decode_errors; continue; }
		merge(&s, &d);
		if (rows && row < rows->n)
		{
			const jval *r = &rows->kids[row++];
			double t = tv->num / 1000.0;
			if (fabs(t - r->kids[0].num) > 0.006 ||
			    fabs(ftms_cadence_rpm(&d) - r->kids[1].num) > 1e-9 ||
			    d.power_w != (int)r->kids[2].num ||
			    d.resistance != (int)r->kids[3].num)
			{
				if (++mismatches <= 5) printf("MISMATCH row %zu: t=%.2f cad=%.1f pw=%d res=%d vs t=%.2f cad=%.1f pw=%d res=%d\n",
					row, t, ftms_cadence_rpm(&d), d.power_w, d.resistance,
					r->kids[0].num, r->kids[1].num, (int)r->kids[2].num, (int)r->kids[3].num);
			}
		}
		if (tv->num / 1000.0 >= next_print)
		{
			printf("%8.2f %6d %8.1f %5d\n", tv->num / 1000.0, s.power_w, s.cadence_rpm, s.resistance);
			next_print = tv->num / 1000.0 + every;
		}
	}
	fclose(f);
	printf("-- %zu Indoor Bike Data packets (%zu other lines skipped), %zu decode errors\n", n2ad2, other, decode_errors);
	if (rows)
		printf("-- compared with %zu Python-decoded samples: %zu mismatches%s\n", rows->n, mismatches,
		       (row == rows->n && !mismatches && !decode_errors) ? " => IDENTICAL" : "");
	return (mismatches || decode_errors) ? 1 : 0;
}
