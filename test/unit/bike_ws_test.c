/*
 * bike_ws_test.c - LOCAL-ONLY test of src/bike_ws.c (not run in CI; POSIX
 * sockets, needs SDL3 + SDL3_net).  Driven by tools/check_bike_ws.sh.
 *
 *   bike_ws_test fake                    protocol cases against an in-process fake WebSocket server
 *   bike_ws_test rider DIR               the shared 'rider' fixtures (test/bike/protocol/rider): the
 *                                        bridge's active rider and FTP, parsed into the mailbox
 *   bike_ws_test tolerance DIR           the shared protocol tolerance fixtures (test/bike/protocol/tolerance)
 *                                        through the fake server: a conforming consumer ignores unknown
 *                                        keys, types and enum values (flywheel docs/protocol.md)
 *   bike_ws_test blackhole HOST PORT     unroutable address: the loop must keep ticking
 *   bike_ws_test refused PORT            nothing listens: backoff 0.5,1,2,4,5 s and keeps ticking
 *   bike_ws_test live HOST PORT SIGFILE  real flywheel-bridge (--source sim): watts follow
 *                                        cadence; then SIGFILE is created, the script kills and
 *                                        restarts the bridge, and the client must reconnect
 *
 * Exit status 0 = all checks passed.
 */
#include "bike_ws.h"

#include <SDL3/SDL.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int failures;
static int checks;

#define CHECK(cond, ...) \
	do { \
		++checks; \
		if (!(cond)) { \
			++failures; \
			printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

static uint64_t now_us(void)
{
	return SDL_GetTicksNS() / 1000u + 100000000u; /* +100 s: a sample age never underflows the clock */
}

/* One tick of the pretend game loop: polls the client and tracks the slowest
 * poll and the largest gap between ticks. */
static BikeMailbox mb;
static uint64_t max_poll_us, max_gap_us, last_tick_us, ticks;
static const char *max_poll_state = "";

static void tick(void)
{
	uint64_t t0 = now_us(), t1;

	bike_ws_poll(&mb, t0);
	t1 = now_us();
	if (t1 - t0 > max_poll_us)
	{
		max_poll_us = t1 - t0;
		max_poll_state = bike_ws_state_name(bike_ws_state()); /* state after the call */
	}
	if (last_tick_us != 0 && t0 - last_tick_us > max_gap_us)
		max_gap_us = t0 - last_tick_us;
	last_tick_us = t0;
	++ticks;
}

static void reset_loop_stats(void)
{
	max_poll_us = max_gap_us = last_tick_us = ticks = 0;
}

/* ---- fake server ------------------------------------------------------------ */

static int lsock = -1, csock = -1;
static uint16_t lport;
static uint8_t srv_rx[65536];
static int srv_rx_len;
static const char accept_good[] = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
static const char *accept_to_send = accept_good;

static void srv_listen(void)
{
	struct sockaddr_in a;
	socklen_t al = sizeof a;
	int one = 1;

	lsock = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(lsock, (struct sockaddr *)&a, sizeof a) != 0 || listen(lsock, 4) != 0)
	{
		perror("bind/listen");
		exit(2);
	}
	getsockname(lsock, (struct sockaddr *)&a, &al);
	lport = ntohs(a.sin_port);
	fcntl(lsock, F_SETFL, O_NONBLOCK);
}

static void srv_drop(void)
{
	if (csock >= 0)
		close(csock);
	csock = -1;
	srv_rx_len = 0;
}

static bool srv_have_request;

static void srv_service(void)
{
	if (csock < 0)
	{
		csock = accept(lsock, NULL, NULL);
		if (csock >= 0)
		{
			fcntl(csock, F_SETFL, O_NONBLOCK);
			srv_rx_len = 0;
			srv_have_request = false;
		}
	}
	if (csock >= 0)
	{
		ssize_t n = recv(csock, srv_rx + srv_rx_len, sizeof srv_rx - (size_t)srv_rx_len, 0);
		if (n > 0)
			srv_rx_len += (int)n;
		if (!srv_have_request && srv_rx_len >= 4)
		{
			srv_rx[srv_rx_len] = '\0';
			char *eoh = strstr((char *)srv_rx, "\r\n\r\n");
			if (eoh != NULL)
			{
				char resp[256];
				int used = (int)(eoh - (char *)srv_rx) + 4, rl;
				CHECK(strncmp((char *)srv_rx, "GET /ws HTTP/1.1", 16) == 0, "request line");
				CHECK(strstr((char *)srv_rx, "Upgrade: websocket") != NULL, "Upgrade header");
				CHECK(strstr((char *)srv_rx, "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==") != NULL, "key header");
				memmove(srv_rx, srv_rx + used, (size_t)(srv_rx_len - used));
				srv_rx_len -= used;
				rl = snprintf(resp, sizeof resp,
				              "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
				              "Connection: Upgrade\r\nsec-websocket-accept:   %s \r\n\r\n", accept_to_send);
				send(csock, resp, (size_t)rl, MSG_NOSIGNAL);
				srv_have_request = true;
			}
		}
	}
}

static void step(void)
{
	srv_service();
	tick();
	SDL_DelayNS(500000);
}

static bool pump_until(bool (*cond)(void), int timeout_ms)
{
	uint64_t end = now_us() + (uint64_t)timeout_ms * 1000u;
	while (now_us() < end)
	{
		step();
		if (cond())
			return true;
	}
	return false;
}

static void pump_ms(int ms)
{
	uint64_t end = now_us() + (uint64_t)ms * 1000u;
	while (now_us() < end)
		step();
}

static int build_frame(uint8_t *out, uint8_t opcode, bool fin, const void *payload, int len)
{
	int n = 0;
	out[n++] = (uint8_t)((fin ? 0x80 : 0) | opcode);
	if (len < 126)
		out[n++] = (uint8_t)len;
	else
	{
		out[n++] = 126;
		out[n++] = (uint8_t)(len >> 8);
		out[n++] = (uint8_t)len;
	}
	memcpy(out + n, payload, (size_t)len);
	return n + len;
}

static void srv_send_raw(const uint8_t *b, int n)
{
	send(csock, b, (size_t)n, MSG_NOSIGNAL);
}

static void srv_send_frame(uint8_t opcode, bool fin, const char *payload)
{
	uint8_t buf[2048];
	int n = build_frame(buf, opcode, fin, payload, (int)strlen(payload));
	srv_send_raw(buf, n);
}

/* Reads one client frame (must be masked) into payload; returns opcode or -1. */
static int srv_read_frame(char *payload, int cap, int *plen_out)
{
	uint64_t end = now_us() + 2000000u;
	while (now_us() < end)
	{
		step();
		if (srv_rx_len >= 2)
		{
			int plen = srv_rx[1] & 0x7f, hdr = 2, i, op = srv_rx[0] & 0x0f;
			if (plen == 126)
			{
				if (srv_rx_len < 4)
					continue;
				plen = (srv_rx[2] << 8) | srv_rx[3];
				hdr = 4;
			}
			CHECK((srv_rx[1] & 0x80) != 0, "client frame must be masked");
			if (srv_rx_len < hdr + 4 + plen)
				continue;
			for (i = 0; i < plen && i < cap - 1; ++i)
				payload[i] = (char)(srv_rx[hdr + 4 + i] ^ srv_rx[hdr + (i & 3)]);
			payload[plen < cap - 1 ? plen : cap - 1] = '\0';
			*plen_out = plen;
			memmove(srv_rx, srv_rx + hdr + 4 + plen, (size_t)(srv_rx_len - hdr - 4 - plen));
			srv_rx_len -= hdr + 4 + plen;
			return op;
		}
	}
	return -1;
}

static bool c_open(void) { return bike_ws_state() == BIKE_WS_OPEN && csock >= 0 && srv_have_request; }
static bool c_backoff(void) { return bike_ws_state() == BIKE_WS_BACKOFF; }
static bool c_streaming(void) { return mb.link == BIKE_LINK_STREAMING; }
static uint32_t seq_mark;
static bool c_newseq(void) { return mb.has_sample && mb.latest.seq != seq_mark; }

static void expect_sample(const char *json_frame_payload, uint32_t seq_before)
{
	seq_mark = seq_before;
	srv_send_frame(0x1, true, json_frame_payload);
}

static int q16_milli(bq16 q)
{
	return (int)(((int64_t)q * 1000) / 65536);
}

static void run_fake(void)
{
	char pl[256];
	int plen, op;
	uint32_t s0;

	printf("fake-server protocol cases\n");
	srv_listen();
	CHECK(bike_ws_open("127.0.0.1", lport), "open");
	CHECK(!bike_ws_open("", 1) || 1, "open(empty) must not crash");
	CHECK(bike_ws_open("127.0.0.1", lport), "reopen");
	CHECK(pump_until(c_open, 3000), "handshake completes (state=%s)", bike_ws_state_name(bike_ws_state()));
	op = srv_read_frame(pl, sizeof pl, &plen);
	CHECK(op == 1 && strcmp(pl, "{\"type\":\"hello\",\"client\":\"game\",\"v\":1,\"caps\":[\"rider\",\"intensity\"]}") == 0, "hello frame, got op=%d '%s'", op, pl);
	CHECK(mb.link == BIKE_LINK_WAITING, "open without status or telemetry is WAITING (%d)", mb.link);

	/* status handling */
	srv_send_frame(0x1, true, "{\"type\":\"status\",\"state\":\"scanning\",\"source\":\"ftms\"}");
	pump_ms(30);
	CHECK(mb.link == BIKE_LINK_WAITING, "status scanning -> WAITING (%d)", mb.link);
	srv_send_frame(0x1, true, "{\"state\":\"streaming\",\"type\":\"status\",\"device\":{\"name\":\"x\",\"services\":[\"a\",\"b\"]}}");
	CHECK(pump_until(c_streaming, 500), "status streaming (even with nested device, reordered) -> STREAMING");
	srv_send_frame(0x1, true, "{\"type\":\"status\",\"state\":\"reconnecting\"}");
	pump_ms(30);
	CHECK(mb.link == BIKE_LINK_WAITING, "status reconnecting -> WAITING");
	CHECK(!mb.has_sample, "status never creates a sample");

	/* reordered keys, nested age before the fields, nulls, extra members */
	s0 = mb.latest.seq;
	expect_sample("{\"age\":{\"cadence\":10,\"power\":0},\"source\":\"sim\",\"powerW\":123.5,"
	              "\"cadenceRpm\":null,\"v\":1,\"seq\":7,\"t\":1.5e3,\"wall\":1700000000000.5,\"type\":\"telemetry\"}", s0);
	CHECK(pump_until(c_newseq, 500), "reordered telemetry parsed");
	CHECK(mb.latest.seq == 7, "seq 7, got %u", (unsigned)mb.latest.seq);
	CHECK(mb.latest.power_valid && q16_milli(mb.latest.power_w) == 123500, "power 123.5, got %d", q16_milli(mb.latest.power_w));
	CHECK(!mb.latest.cadence_valid, "cadence null -> unknown");
	CHECK(mb.link == BIKE_LINK_STREAMING, "fresh telemetry makes the link STREAMING even after a non-streaming status");

	/* power null + cadence number, scientific notation, string-in-string trap */
	expect_sample("{\"type\":\"telemetry\",\"v\":1,\"seq\":8,\"note\":\"powerW\\\":999\",\"powerW\":null,\"cadenceRpm\":9.0e1,"
	              "\"age\":{}}", 7);
	CHECK(pump_until(c_newseq, 500), "telemetry 2");
	CHECK(!mb.latest.power_valid && mb.latest.cadence_valid && q16_milli(mb.latest.cadence_rpm) == 90000,
	      "power null / cadence 90, got pv=%d cad=%d", mb.latest.power_valid, q16_milli(mb.latest.cadence_rpm));

	/* power given as a non-number is treated as unknown, not 0 */
	expect_sample("{\"type\":\"telemetry\",\"v\":1,\"seq\":9,\"powerW\":\"oops\",\"cadenceRpm\":60}", 8);
	CHECK(pump_until(c_newseq, 500), "telemetry 3");
	CHECK(!mb.latest.power_valid, "non-numeric power -> unknown");

	/* v != 1 and missing v are ignored; a later v=1 works */
	expect_sample("{\"type\":\"telemetry\",\"v\":2,\"seq\":10,\"powerW\":500,\"cadenceRpm\":60}", 9);
	pump_ms(50);
	CHECK(mb.latest.seq == 9, "v=2 ignored (seq still %u)", (unsigned)mb.latest.seq);
	expect_sample("{\"type\":\"telemetry\",\"seq\":11,\"powerW\":500}", 9);
	pump_ms(50);
	CHECK(mb.latest.seq == 9, "missing v ignored");
	expect_sample("{\"type\":\"telemetry\",\"v\":1,\"seq\":12,\"powerW\":150,\"cadenceRpm\":80}", 9);
	CHECK(pump_until(c_newseq, 500), "v=1 after v=2");
	CHECK(mb.latest.seq == 12 && q16_milli(mb.latest.power_w) == 150000, "values after v=2");

	/* garbage and unknown types leave the link alone */
	srv_send_frame(0x1, true, "not json at all {{{");
	srv_send_frame(0x1, true, "{\"type\":\"mystery\",\"powerW\":1}");
	srv_send_frame(0x1, true, "{\"type\":\"telemetry\",\"v\":1,\"powerW\":");  /* truncated */
	pump_ms(50);
	CHECK(bike_ws_state() == BIKE_WS_OPEN && mb.latest.seq == 12, "garbage ignored");

	/* age backdates the sample */
	expect_sample("{\"type\":\"telemetry\",\"v\":1,\"seq\":13,\"powerW\":150,\"cadenceRpm\":80,\"age\":{\"power\":2500}}", 12);
	CHECK(pump_until(c_newseq, 500), "aged sample");
	CHECK(now_us() - mb.latest.arrival_us >= 2500000u && now_us() - mb.latest.arrival_us < 2700000u,
	      "arrival backdated by ~2.5 s, delta=%llu us", (unsigned long long)(now_us() - mb.latest.arrival_us));

	/* seq collision after a bridge restart still yields a change */
	expect_sample("{\"type\":\"telemetry\",\"v\":1,\"seq\":13,\"powerW\":10}", 12);
	pump_ms(50);
	CHECK(mb.latest.seq != 13 && q16_milli(mb.latest.power_w) == 10000, "same seq twice -> bumped, seq=%u", (unsigned)mb.latest.seq);

	/* partial reads: one byte per poll */
	{
		uint8_t buf[512];
		const char *j = "{\"type\":\"telemetry\",\"v\":1,\"seq\":40,\"powerW\":77.25,\"cadenceRpm\":55}";
		int n = build_frame(buf, 0x1, true, j, (int)strlen(j)), i;
		seq_mark = mb.latest.seq;
		for (i = 0; i < n; ++i)
		{
			srv_send_raw(buf + i, 1);
			pump_ms(1);
			CHECK(i == n - 1 || mb.latest.seq != 40, "no early parse at byte %d", i);
		}
		CHECK(pump_until(c_newseq, 500) && mb.latest.seq == 40 && q16_milli(mb.latest.power_w) == 77250, "byte-by-byte frame");
	}

	/* fragmentation with an interleaved ping, 16-bit length continuation */
	{
		char big[300], j[600];
		uint8_t buf[1024];
		int n, half;
		memset(big, 'x', sizeof big - 1);
		big[sizeof big - 1] = '\0';
		snprintf(j, sizeof j, "{\"type\":\"telemetry\",\"v\":1,\"junk\":\"%s\",\"seq\":41,\"powerW\":200,\"cadenceRpm\":90}", big);
		half = (int)strlen(j) / 2;
		seq_mark = mb.latest.seq;
		n = build_frame(buf, 0x1, false, j, half);
		srv_send_raw(buf, n);
		srv_send_frame(0x9, true, "ping!");
		op = srv_read_frame(pl, sizeof pl, &plen);
		CHECK(op == 0xA && plen == 5 && strcmp(pl, "ping!") == 0, "pong echoes the ping payload (op=%d '%s')", op, pl);
		CHECK(mb.latest.seq != 41, "not parsed before FIN");
		n = build_frame(buf, 0x0, true, j + half, (int)strlen(j) - half);
		srv_send_raw(buf, n);
		CHECK(pump_until(c_newseq, 500) && mb.latest.seq == 41 && q16_milli(mb.latest.power_w) == 200000, "fragmented message reassembled");
		CHECK(mb.latest.cadence_valid, "fields after the fragment boundary survive");
	}

	/* oversized message is skipped, link stays up; binary is ignored */
	{
		static char huge[12000];
		uint8_t *buf = malloc(13000);
		int n;
		memset(huge, 'y', sizeof huge - 1);
		huge[sizeof huge - 1] = '\0';
		n = build_frame(buf, 0x1, true, "{\"type\":\"telemetry\",\"v\":1,\"seq\":50,\"powerW\":1}", 48);
		(void)n;
		/* build a 12000-byte text message as 3 fragments of 4000 bytes */
		n = build_frame(buf, 0x1, false, huge, 4000);
		srv_send_raw(buf, n);
		n = build_frame(buf, 0x0, false, huge, 4000);
		srv_send_raw(buf, n);
		n = build_frame(buf, 0x0, true, huge, 4000);
		srv_send_raw(buf, n);
		srv_send_frame(0x2, true, "binary");
		pump_ms(80);
		CHECK(bike_ws_state() == BIKE_WS_OPEN, "oversized/binary messages do not drop the link");
		free(buf);
		seq_mark = mb.latest.seq;
		srv_send_frame(0x1, true, "{\"type\":\"telemetry\",\"v\":1,\"seq\":51,\"powerW\":3}");
		CHECK(pump_until(c_newseq, 500) && mb.latest.seq == 51, "still parsing after oversized message");
	}

	/* close frame: echoed, then reconnect after backoff */
	{
		uint8_t cl[2] = { 0x03, 0xe8 }, buf[16];
		int n = build_frame(buf, 0x8, true, cl, 2);
		srv_send_raw(buf, n);
		op = srv_read_frame(pl, sizeof pl, &plen);
		CHECK(op == 0x8 && plen == 2 && (uint8_t)pl[0] == 0x03 && (uint8_t)pl[1] == 0xe8, "close echoed (op=%d)", op);
		CHECK(pump_until(c_backoff, 500), "close -> BACKOFF");
		CHECK(mb.link == BIKE_LINK_CONNECTING, "link CONNECTING during backoff");
		srv_drop();
		CHECK(pump_until(c_open, 3000), "reconnects after close");
		(void)srv_read_frame(pl, sizeof pl, &plen); /* hello */
	}

	/* TCP drop (no close frame) */
	srv_drop();
	CHECK(pump_until(c_backoff, 1500), "abrupt drop -> BACKOFF");
	CHECK(pump_until(c_open, 3000), "reconnects after abrupt drop");
	(void)srv_read_frame(pl, sizeof pl, &plen);

	/* bad Sec-WebSocket-Accept is rejected */
	accept_to_send = "AAAAAAAAAAAAAAAAAAAAAAAAAAA=";
	srv_drop();
	{
		uint64_t end = now_us() + 4000000u;
		bool seen_backoff_after_bad = false;
		while (now_us() < end && !seen_backoff_after_bad)
		{
			step();
			if (srv_have_request && bike_ws_state() == BIKE_WS_BACKOFF)
				seen_backoff_after_bad = true;
			CHECK(!(srv_have_request && bike_ws_state() == BIKE_WS_OPEN), "must not open with a bad accept");
		}
		CHECK(seen_backoff_after_bad, "bad accept -> BACKOFF");
	}
	accept_to_send = accept_good;
	srv_drop();
	CHECK(pump_until(c_open, 8000), "recovers once the accept value is right");

	/* send_text is masked and arrives intact */
	CHECK(bike_ws_send_text("{\"type\":\"sim\",\"cadenceRpm\":90}"), "send_text");
	{
		bool got = false;
		while ((op = srv_read_frame(pl, sizeof pl, &plen)) >= 0)
			if (op == 1 && strstr(pl, "\"sim\"") != NULL) { got = true; break; }
		CHECK(got, "sim frame arrived, masked");
	}

	bike_ws_close();
	CHECK(bike_ws_state() == BIKE_WS_IDLE, "close -> IDLE");
	bike_ws_poll(&mb, now_us());
	CHECK(mb.link == BIKE_LINK_OFF, "closed -> link OFF");
	close(lsock);
	srv_drop();
}

/* ---- blackhole / refused ------------------------------------------------------ */

static void run_blackhole(const char *host, uint16_t port)
{
	uint64_t start = now_us(), end = start + 12000000u;
	int last_state = -1;

	printf("blackhole %s:%u for 12 s: the loop must keep ticking\n", host, (unsigned)port);
	CHECK(bike_ws_open(host, port), "open");
	reset_loop_stats();
	while (now_us() < end)
	{
		tick();
		if ((int)bike_ws_state() != last_state)
		{
			last_state = (int)bike_ws_state();
			printf("  +%5.2fs state=%s link=%d\n", (double)(now_us() - start) / 1e6, bike_ws_state_name(bike_ws_state()), mb.link);
		}
		SDL_DelayNS(1000000);
	}
	printf("  ticks=%llu max_poll=%llu us (ended in %s) max_gap=%llu us\n", (unsigned long long)ticks,
	       (unsigned long long)max_poll_us, max_poll_state, (unsigned long long)max_gap_us);
	CHECK(ticks > 6000, "loop ran freely (%llu ticks in 12 s)", (unsigned long long)ticks);
	CHECK(max_poll_us < 20000, "no poll took 20 ms or more (max %llu us)", (unsigned long long)max_poll_us);
	CHECK(max_gap_us < 100000, "no gap between ticks over 100 ms (max %llu us)", (unsigned long long)max_gap_us);
	CHECK(mb.link == BIKE_LINK_CONNECTING && !mb.has_sample, "link CONNECTING, no sample");
	bike_ws_close();
}

static void run_refused(uint16_t port)
{
	uint64_t start = now_us(), end = start + 13000000u;
	int last_state = -1, n_backoffs = 0;
	uint64_t backoff_at[16];

	printf("refused 127.0.0.1:%u for 13 s: backoff 0.5, 1, 2, 4, 5 s\n", (unsigned)port);
	CHECK(bike_ws_open("127.0.0.1", port), "open");
	reset_loop_stats();
	while (now_us() < end)
	{
		tick();
		if ((int)bike_ws_state() != last_state)
		{
			last_state = (int)bike_ws_state();
			if (last_state == BIKE_WS_BACKOFF && n_backoffs < 16)
				backoff_at[n_backoffs++] = now_us();
		}
		SDL_DelayNS(1000000);
	}
	/* entering BACKOFF at t0..: gaps between consecutive entries = backoff + attempt time */
	{
		static const double want[] = { 0.5, 1.0, 2.0, 4.0, 5.0 };
		int i;
		printf("  backoff entries: %d\n", n_backoffs);
		CHECK(n_backoffs >= 5, "at least 5 attempts in 13 s (%d)", n_backoffs);
		for (i = 1; i < n_backoffs && i <= 5; ++i)
		{
			double gap = (double)(backoff_at[i] - backoff_at[i - 1]) / 1e6;
			printf("  gap %d: %.3f s (want ~%.1f)\n", i, gap, want[i - 1]);
			CHECK(gap > want[i - 1] - 0.05 && gap < want[i - 1] + 0.25, "backoff gap %d = %.3f s", i, gap);
		}
	}
	printf("  max_poll=%llu us (ended in %s) max_gap=%llu us\n", (unsigned long long)max_poll_us, max_poll_state,
	       (unsigned long long)max_gap_us);
	CHECK(max_poll_us < 20000 && max_gap_us < 100000, "never blocked (poll %llu us, gap %llu us)",
	      (unsigned long long)max_poll_us, (unsigned long long)max_gap_us);
	bike_ws_close();
}

/* ---- live bridge --------------------------------------------------------------- */

static bool file_exists(const char *p)
{
	return access(p, F_OK) == 0;
}

/* Runs until pred() or timeout, ticking at ~1 kHz. */
static bool wait_for(bool (*pred)(void), int timeout_ms)
{
	uint64_t end = now_us() + (uint64_t)timeout_ms * 1000u;
	while (now_us() < end)
	{
		tick();
		if (pred())
			return true;
		SDL_DelayNS(1000000);
	}
	return false;
}

static bool l_sample(void) { return mb.has_sample && mb.link == BIKE_LINK_STREAMING; }
static bool l_down(void) { return mb.link != BIKE_LINK_STREAMING; }
static uint32_t l_seq_mark;
static bool l_newer(void) { return mb.latest.seq != l_seq_mark && mb.link == BIKE_LINK_STREAMING; }

/* Sets sim cadence and waits until the smoothed power settles; returns watts. */
static double settle(int rpm, int wait_s)
{
	char cmd[96];
	uint64_t end;
	snprintf(cmd, sizeof cmd, "{\"type\":\"sim\",\"cadenceRpm\":%d}", rpm);
	CHECK(bike_ws_send_text(cmd), "send sim %d", rpm);
	end = now_us() + (uint64_t)wait_s * 1000000u;
	while (now_us() < end)
	{
		tick();
		SDL_DelayNS(1000000);
	}
	return (double)q16_milli(mb.latest.power_w) / 1000.0;
}

static void run_live(const char *host, uint16_t port, const char *sigfile)
{
	double w0, w60, w90, w110;
	uint32_t seq_a;
	bool saw_drop;

	printf("live bridge %s:%u\n", host, (unsigned)port);
	CHECK(bike_ws_open(host, port), "open");
	reset_loop_stats();
	CHECK(wait_for(l_sample, 8000), "streaming with a sample within 8 s (state=%s link=%d)", bike_ws_state_name(bike_ws_state()), mb.link);
	printf("  first sample: seq=%u power=%d.%03d valid=%d cadence=%d.%03d valid=%d\n", (unsigned)mb.latest.seq,
	       q16_milli(mb.latest.power_w) / 1000, q16_milli(mb.latest.power_w) % 1000, mb.latest.power_valid,
	       q16_milli(mb.latest.cadence_rpm) / 1000, q16_milli(mb.latest.cadence_rpm) % 1000, mb.latest.cadence_valid);
	/* The script starts the bridge with a rider profile (Ann, 230 W, measured on the Q200B) and
	 * --rider ann: our hello asked for "rider", so the real bridge must have announced her. */
	CHECK(mb.rider.valid && mb.rider.ftp_w == 230 && strcmp(mb.rider.name, "Ann") == 0 && strcmp(mb.rider.id, "ann") == 0,
	      "the real bridge announced its active rider (valid=%d ftp=%u name='%s')", mb.rider.valid, (unsigned)mb.rider.ftp_w, mb.rider.name);
	CHECK(strcmp(mb.rider.source, "test") == 0 && strcmp(mb.rider.bike, "Q200B") == 0, "rider source '%s' and bike '%s'", mb.rider.source, mb.rider.bike);
	l_seq_mark = mb.latest.seq;
	CHECK(wait_for(l_newer, 3000), "seq advances");
	seq_a = mb.latest.seq;

	w0 = settle(0, 10);
	w60 = settle(60, 10);
	w90 = settle(90, 8);
	w110 = settle(110, 8);
	printf("  watts at cadence 0/60/90/110: %.1f %.1f %.1f %.1f\n", w0, w60, w90, w110);
	CHECK(w0 < w60 && w60 < w90 && w90 < w110, "watts rise with cadence");
	CHECK(w110 > 150.0 && w60 > 30.0, "plausible magnitudes");
	CHECK(mb.latest.seq != seq_a, "samples kept arriving");
	CHECK(max_poll_us < 20000, "poll never slow (%llu us)", (unsigned long long)max_poll_us);

	if (sigfile != NULL)
	{
		FILE *f = fopen(sigfile, "w");
		if (f != NULL)
			fclose(f);
		printf("  A_DONE: waiting for the script to kill and restart the bridge\n");
		saw_drop = wait_for(l_down, 30000);
		CHECK(saw_drop, "link drop observed after the bridge was killed");
		printf("  drop seen: state=%s link=%d; ticks continued: max_gap=%llu us\n", bike_ws_state_name(bike_ws_state()), mb.link,
		       (unsigned long long)max_gap_us);
		l_seq_mark = mb.latest.seq;
		CHECK(wait_for(l_sample, 40000) && file_exists(sigfile), "reconnected and streaming again (state=%s link=%d)",
		      bike_ws_state_name(bike_ws_state()), mb.link);
		l_seq_mark = mb.latest.seq;
		CHECK(wait_for(l_newer, 5000), "fresh samples after reconnect (seq %u)", (unsigned)mb.latest.seq);
		CHECK(max_gap_us < 100000, "loop never stalled across the outage (max gap %llu us)", (unsigned long long)max_gap_us);
	}
	bike_ws_close();
}

/* ---- shared tolerance fixtures ------------------------------------------------ */

typedef struct
{
	char file[128];
	char kind[16];
	char rest[256]; /* "key=value key=value ..." */
} TolRow;

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	long n;

	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc((size_t)n + 1);
	if (buf != NULL && fread(buf, 1, (size_t)n, f) == (size_t)n)
		buf[n] = '\0';
	else
	{
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

static int tol_load_manifest(const char *dir, TolRow *rows, int cap)
{
	char path[512], *text, *line, *save = NULL;
	int n = 0;

	snprintf(path, sizeof path, "%s/manifest.txt", dir);
	text = slurp(path);
	if (text == NULL)
		return -1;
	for (line = strtok_r(text, "\n", &save); line != NULL && n < cap; line = strtok_r(NULL, "\n", &save))
	{
		TolRow *r = &rows[n];

		while (*line == ' ' || *line == '\t')
			++line;
		if (*line == '\0' || *line == '#')
			continue;
		r->rest[0] = '\0';
		if (sscanf(line, "%127s %15s %255[^\n]", r->file, r->kind, r->rest) < 2)
			continue;
		++n;
	}
	free(text);
	return n;
}

/* Looks up key in "k=v k=v ..."; copies the value into out. */
static bool tol_get(const TolRow *r, const char *key, char *out, size_t cap)
{
	const char *p = r->rest;
	size_t klen = strlen(key);

	while (*p != '\0')
	{
		const char *end;

		while (*p == ' ')
			++p;
		end = p;
		while (*end != '\0' && *end != ' ')
			++end;
		if ((size_t)(end - p) > klen && strncmp(p, key, klen) == 0 && p[klen] == '=')
		{
			size_t vl = (size_t)(end - p) - klen - 1;
			if (vl >= cap)
				vl = cap - 1;
			memcpy(out, p + klen + 1, vl);
			out[vl] = '\0';
			return true;
		}
		p = end;
	}
	return false;
}

static char *tol_message(const char *dir, const TolRow *r)
{
	char path[640], *m;
	size_t n;

	snprintf(path, sizeof path, "%s/%s", dir, r->file);
	m = slurp(path);
	if (m != NULL)
		for (n = strlen(m); n > 0 && (m[n - 1] == '\n' || m[n - 1] == '\r'); --n)
			m[n - 1] = '\0';
	return m;
}

static void run_tolerance(const char *dir)
{
	TolRow rows[64];
	char pl[256], val[64], prime[160];
	int n, i, plen;

	printf("shared tolerance fixtures (%s)\n", dir);
	n = tol_load_manifest(dir, rows, 64);
	CHECK(n > 0, "manifest.txt readable and non-empty (%d)", n);
	if (n <= 0)
		return;

	srv_listen();
	CHECK(bike_ws_open("127.0.0.1", lport), "open");
	CHECK(pump_until(c_open, 3000), "handshake completes");
	(void)srv_read_frame(pl, sizeof pl, &plen); /* hello */

	/* Pass 1, before any telemetry: status fixtures. Streaming only when state is "streaming". */
	for (i = 0; i < n; ++i)
	{
		char *msg;

		if (strcmp(rows[i].kind, "status") != 0)
			continue;
		srv_send_frame(0x1, true, "{\"type\":\"status\",\"state\":\"idle\"}");
		pump_ms(20);
		CHECK(mb.link == BIKE_LINK_WAITING && !mb.has_sample, "%s: reset to WAITING before the fixture (%d)", rows[i].file, mb.link);
		msg = tol_message(dir, &rows[i]);
		CHECK(msg != NULL, "%s readable", rows[i].file);
		if (msg == NULL)
			continue;
		srv_send_frame(0x1, true, msg);
		pump_ms(30);
		if (tol_get(&rows[i], "streaming", val, sizeof val))
			CHECK((mb.link == BIKE_LINK_STREAMING) == (strcmp(val, "true") == 0), "%s: streaming=%s but link=%d", rows[i].file, val, mb.link);
		CHECK(!mb.has_sample && bike_ws_state() == BIKE_WS_OPEN, "%s: status creates no sample and keeps the link", rows[i].file);
		free(msg);
	}

	/* Pass 2: telemetry and ignored fixtures, each after a priming sample with a different seq. */
	for (i = 0; i < n; ++i)
	{
		char *msg;
		uint32_t before;

		if (strcmp(rows[i].kind, "status") == 0)
			continue;
		snprintf(prime, sizeof prime, "{\"type\":\"telemetry\",\"v\":1,\"seq\":%d,\"powerW\":1,\"cadenceRpm\":1}", 1000 + i);
		expect_sample(prime, mb.latest.seq);
		CHECK(pump_until(c_newseq, 500) && mb.latest.seq == (uint32_t)(1000 + i), "%s: priming sample", rows[i].file);
		before = mb.latest.seq;
		msg = tol_message(dir, &rows[i]);
		CHECK(msg != NULL, "%s readable", rows[i].file);
		if (msg == NULL)
			continue;
		if (strcmp(rows[i].kind, "telemetry") == 0)
		{
			expect_sample(msg, before);
			CHECK(pump_until(c_newseq, 500), "%s: telemetry parsed", rows[i].file);
			if (tol_get(&rows[i], "seq", val, sizeof val))
				CHECK(mb.latest.seq == (uint32_t)atoi(val), "%s: seq %s, got %u", rows[i].file, val, (unsigned)mb.latest.seq);
			if (tol_get(&rows[i], "powerW", val, sizeof val))
			{
				if (strcmp(val, "null") == 0)
					CHECK(!mb.latest.power_valid, "%s: powerW null -> unknown", rows[i].file);
				else
					CHECK(mb.latest.power_valid && q16_milli(mb.latest.power_w) == atoi(val) * 1000, "%s: powerW %s, got %d", rows[i].file, val, q16_milli(mb.latest.power_w));
			}
			if (tol_get(&rows[i], "cadenceRpm", val, sizeof val))
			{
				if (strcmp(val, "null") == 0)
					CHECK(!mb.latest.cadence_valid, "%s: cadenceRpm null -> unknown", rows[i].file);
				else
					CHECK(mb.latest.cadence_valid && q16_milli(mb.latest.cadence_rpm) == atoi(val) * 1000, "%s: cadenceRpm %s, got %d", rows[i].file, val, q16_milli(mb.latest.cadence_rpm));
			}
		}
		else
		{
			srv_send_frame(0x1, true, msg);
			pump_ms(50);
			CHECK(mb.latest.seq == before, "%s: ignored (seq %u, was %u)", rows[i].file, (unsigned)mb.latest.seq, (unsigned)before);
		}
		CHECK(bike_ws_state() == BIKE_WS_OPEN, "%s: link still open", rows[i].file);
		free(msg);
	}
	bike_ws_close();
}

static void send_and_settle(const char *json)
{
	srv_send_frame(0x1, true, json);
	pump_ms(30);
}

static void run_rider(const char *dir)
{
	TolRow rows[64];
	char pl[256], val[64];
	int n, i, plen, op;

	printf("shared rider fixtures (%s)\n", dir);
	n = tol_load_manifest(dir, rows, 64);
	CHECK(n > 0, "manifest.txt readable and non-empty (%d)", n);
	if (n <= 0)
		return;

	srv_listen();
	CHECK(bike_ws_open("127.0.0.1", lport), "open");
	CHECK(pump_until(c_open, 3000), "handshake completes");
	op = srv_read_frame(pl, sizeof pl, &plen);
	CHECK(op == 1 && strstr(pl, "\"caps\":[\"rider\",\"intensity\"]") != NULL,
	      "hello asks for the rider and intensity capabilities, got '%s'", pl);

	/* An older bridge never sends a rider: the client plays on with its own FTP. */
	send_and_settle("{\"type\":\"status\",\"state\":\"streaming\"}");
	CHECK(!mb.rider.valid && mb.link == BIKE_LINK_STREAMING, "no rider message, no rider (the manual FTP stays)");

	for (i = 0; i < n; ++i)
	{
		char *msg = tol_message(dir, &rows[i]);

		CHECK(msg != NULL, "%s readable", rows[i].file);
		if (msg == NULL)
			continue;
		if (strcmp(rows[i].kind, "rider") == 0)
		{
			send_and_settle(msg);
			CHECK(mb.rider.valid, "%s: a rider with an FTP is stored", rows[i].file);
			if (tol_get(&rows[i], "id", val, sizeof val))
				CHECK(strcmp(mb.rider.id, val) == 0, "%s: id '%s', got '%s'", rows[i].file, val, mb.rider.id);
			if (tol_get(&rows[i], "name", val, sizeof val))
				CHECK(strcmp(mb.rider.name, val) == 0, "%s: name '%s', got '%s'", rows[i].file, val, mb.rider.name);
			if (tol_get(&rows[i], "ftpClamped", val, sizeof val))
				CHECK(mb.rider.ftp_w == (uint16_t)atoi(val), "%s: FTP %s, got %u", rows[i].file, val, (unsigned)mb.rider.ftp_w);
			/* intensity: 'none' (or absent) -> played at the measured FTP; else the block's own values */
			if (tol_get(&rows[i], "playClamped", val, sizeof val))
				CHECK(mb.rider.eff_ftp_w == (uint16_t)atoi(val), "%s: plays at %s W, got %u", rows[i].file, val, (unsigned)mb.rider.eff_ftp_w);
			if (tol_get(&rows[i], "intensity", val, sizeof val) && strcmp(val, "none") != 0)
			{
				CHECK(mb.rider.has_intensity && strcmp(mb.rider.intensity_name, val) == 0, "%s: preset '%s', got '%s' (%d)", rows[i].file, val, mb.rider.intensity_name, mb.rider.has_intensity);
				if (tol_get(&rows[i], "percent", val, sizeof val))
					CHECK(mb.rider.intensity_pct == (unsigned)atoi(val), "%s: percent %s, got %u", rows[i].file, val, (unsigned)mb.rider.intensity_pct);
			}
			else
				CHECK(!mb.rider.has_intensity && mb.rider.intensity_name[0] == '\0' && mb.rider.eff_ftp_w == mb.rider.ftp_w,
				      "%s: no usable intensity block, played at the measured FTP", rows[i].file);
			if (tol_get(&rows[i], "source", val, sizeof val))
				CHECK(strcmp(mb.rider.source, val) == 0, "%s: source '%s', got '%s'", rows[i].file, val, mb.rider.source);
			else
				CHECK(mb.rider.source[0] == '\0', "%s: no source was sent, got '%s'", rows[i].file, mb.rider.source);
			if (strcmp(rows[i].file, "rider-basic.json") == 0 || strcmp(rows[i].file, "rider-decoys.json") == 0)
				CHECK(strcmp(mb.rider.bike, "Q200B") == 0, "%s: bike 'Q200B', got '%s'", rows[i].file, mb.rider.bike);
			else
				CHECK(mb.rider.bike[0] == '\0', "%s: no bike was sent, got '%s'", rows[i].file, mb.rider.bike);
		}
		else
		{
			/* a message with no usable FTP for the rider already in use leaves that FTP alone */
			send_and_settle("{\"type\":\"rider\",\"id\":\"ann\",\"name\":\"Ann\",\"ftpW\":200}");
			CHECK(mb.rider.valid && mb.rider.ftp_w == 200, "%s: primed with 200 W", rows[i].file);
			send_and_settle(msg);
			CHECK(mb.rider.valid && mb.rider.ftp_w == 200 && strcmp(mb.rider.id, "ann") == 0,
			      "%s: no usable FTP keeps the one in use (valid=%d ftp=%u)", rows[i].file, mb.rider.valid, (unsigned)mb.rider.ftp_w);
		}
		free(msg);
		CHECK(bike_ws_state() == BIKE_WS_OPEN, "%s: link still open", rows[i].file);
	}

	/* A different rider without an FTP: the previous rider's FTP is not theirs. */
	send_and_settle("{\"type\":\"rider\",\"id\":\"ann\",\"name\":\"Ann\",\"ftpW\":210}");
	send_and_settle("{\"type\":\"rider\",\"id\":\"bo\",\"name\":\"Bo\",\"ftpW\":null}");
	CHECK(!mb.rider.valid, "another rider with no FTP drops the old FTP");
	send_and_settle("{\"type\":\"rider\",\"id\":\"bo\",\"name\":\"Bo\",\"ftpW\":150}");
	CHECK(mb.rider.valid && mb.rider.ftp_w == 150 && strcmp(mb.rider.name, "Bo") == 0, "the next rider with an FTP is stored");

	/* Riders are not samples, and telemetry still works alongside them. */
	CHECK(!mb.has_sample, "a rider message never creates a sample");
	expect_sample("{\"type\":\"telemetry\",\"v\":1,\"seq\":1,\"powerW\":120,\"cadenceRpm\":70}", mb.latest.seq);
	CHECK(pump_until(c_newseq, 500) && mb.has_sample && mb.rider.ftp_w == 150, "telemetry after a rider");
	bike_ws_close();
}

int main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);
	if (argc >= 2 && strcmp(argv[1], "fake") == 0)
		run_fake();
	else if (argc >= 3 && strcmp(argv[1], "tolerance") == 0)
		run_tolerance(argv[2]);
	else if (argc >= 3 && strcmp(argv[1], "rider") == 0)
		run_rider(argv[2]);
	else if (argc >= 4 && strcmp(argv[1], "blackhole") == 0)
		run_blackhole(argv[2], (uint16_t)atoi(argv[3]));
	else if (argc >= 3 && strcmp(argv[1], "refused") == 0)
		run_refused((uint16_t)atoi(argv[2]));
	else if (argc >= 4 && strcmp(argv[1], "live") == 0)
		run_live(argv[2], (uint16_t)atoi(argv[3]), argc >= 5 ? argv[4] : NULL);
	else
	{
		fprintf(stderr, "usage: %s fake | tolerance DIR | rider DIR | blackhole HOST PORT | refused PORT | live HOST PORT [SIGFILE]\n", argv[0]);
		return 2;
	}
	printf("%s: %d checks, %d failed\n", argv[1], checks, failures);
	return failures != 0;
}
