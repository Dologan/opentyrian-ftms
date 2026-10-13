/* Standalone test: bike_ws_test HOST PORT SECONDS [cadence@sec,...]
 * e.g.   bike_ws_test 127.0.0.1 8787 30 0@0,60@6,90@12,110@18,0@24
 * Prints one line per telemetry frame plus a summary. */
#define _POSIX_C_SOURCE 199309L
#include "bike_ws.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double epoch_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
	const char *host = argc > 1 ? argv[1] : "127.0.0.1";
	int port = argc > 2 ? atoi(argv[2]) : 8787;
	int secs = argc > 3 ? atoi(argv[3]) : 10;
	const char *sched = argc > 4 ? argv[4] : "";
	double t0, last_rx = 0, sum_lat = 0, max_lat = 0, sum_dt = 0, max_dt = 0;
	int frames = 0, polls = 0;
	char sbuf[256];
	Uint32 open_t0;
	bool ok;
	int next_step = 0;
	int steps_rpm[32], steps_at[32], nsteps = 0;
	const char *p = sched;

	while (*p && nsteps < 32)
	{
		steps_rpm[nsteps] = atoi(p);
		p = strchr(p, '@');
		if (!p) break;
		steps_at[nsteps++] = atoi(++p);
		p = strchr(p, ',');
		if (!p) break;
		++p;
	}

	SDL_Init(SDL_INIT_TIMER);
	open_t0 = SDL_GetTicks();
	ok = bike_ws_open(host, (Uint16)port);
	printf("open(%s:%d) -> %s in %u ms\n", host, port, ok ? "ok" : "FAILED", SDL_GetTicks() - open_t0);

	t0 = epoch_ms();
	while (epoch_ms() - t0 < secs * 1000.0)
	{
		BikeSample s;
		int n = bike_ws_poll(&s);
		double now = epoch_ms();
		++polls;
		if (n > 0)
		{
			double lat = s.wall_ms > 0 ? now - s.wall_ms : -1;
			double dt = last_rx > 0 ? now - last_rx : 0;
			frames += n;
			if (lat >= 0) { sum_lat += lat; if (lat > max_lat) max_lat = lat; }
			if (dt > 0) { sum_dt += dt; if (dt > max_dt) max_dt = dt; }
			last_rx = now;
			printf("t=%6.2fs seq=%-5ld src=%-6s power=%s%.0fW cad=%s%.0frpm age=%s%.0fms lat=%.1fms\n",
			       (now - t0) / 1000.0, s.seq, s.source,
			       s.has_power ? "" : "null:", s.power_w,
			       s.has_cadence ? "" : "null:", s.cadence_rpm,
			       s.has_age ? "" : "n/a:", s.power_age_ms, lat);
		}
		while (next_step < nsteps && bike_ws_state() == BIKE_WS_OPEN && (now - t0) / 1000.0 >= steps_at[next_step])
		{
			snprintf(sbuf, sizeof sbuf, "{\"type\":\"sim\",\"cadenceRpm\":%d}", steps_rpm[next_step]);
			printf("t=%6.2fs >>> send %s (%s)\n", (now - t0) / 1000.0, sbuf,
			       bike_ws_send_text(sbuf) ? "ok" : "not connected");
			++next_step;
		}
		SDL_Delay(10);   /* ~100 polls/s, like a game frame loop */
	}
	printf("SUMMARY: %d telemetry frames in %d s (%.2f Hz), mean inter-arrival %.0f ms (max %.0f), "
	       "mean latency %.1f ms (max %.1f), %d polls, state=%d\n",
	       frames, secs, frames / (double)secs, frames > 1 ? sum_dt / (frames - 1) : 0, max_dt,
	       frames ? sum_lat / frames : 0, max_lat, polls, (int)bike_ws_state());
	bike_ws_close();
	SDL_Quit();
	return 0;
}
