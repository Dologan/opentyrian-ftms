/*
 * bike_ws.c - non-blocking WebSocket client for the Flywheel bridge (SDL3_net).
 *
 * Ported from the Phase 0 spike (SDL2_net, blocking connect).  SDL3_net is
 * asynchronous end to end, so no call here can stall the game loop:
 *
 *   IDLE -> RESOLVING -> CONNECTING -> HANDSHAKING -> OPEN
 *     ^                                                 |
 *     +------------ BACKOFF <---- any failure ----------+
 *
 * Every wait has its own timeout (so a blackholed address retries instead of
 * hanging for the OS's two-minute connect timeout) and every failure goes to
 * BACKOFF: 0.5 s, 1, 2, 4, then 5 s forever.  Protocol: RFC 6455 client side,
 * masked frames out, ping/pong, close, continuation frames in.  The bridge
 * schema is shared/protocol/telemetry.schema.json in the Flywheel repo.
 */
#include "bike_ws.h"

#ifdef WITH_NETWORK

#include "bike_feed.h"
#include "logging.h"

#include <SDL3_net/SDL_net.h>

#include <stdio.h>
#include <string.h>

#define RX_CAP 16384            /* one frame (header + payload) must fit */
#define MSG_CAP 8192            /* reassembled message limit */
#define TX_MAX_PAYLOAD 1024
#define TX_PENDING_MAX 65536    /* unsent bytes before the link is declared stuck */
#define READS_PER_POLL 16

#define BACKOFF_FIRST_US 500000u
#define BACKOFF_MAX_US 5000000u
#define RESOLVE_TIMEOUT_US 10000000u
#define CONNECT_TIMEOUT_US 8000000u
#define HANDSHAKE_TIMEOUT_US 5000000u
#define SILENCE_TIMEOUT_US 45000000u  /* the bridge pings every 20 s */
#define TELEMETRY_FRESH_US 3000000u

/* RFC 6455 section 1.3 sample key and its accept value. */
static const char ws_key[] = "dGhlIHNhbXBsZSBub25jZQ==";
static const char ws_accept[] = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

static bool net_inited;
static bool want_link;
static BikeWsState state = BIKE_WS_IDLE;
static char host_name[128];
static uint16_t host_port;

static NET_Address *addr;
static NET_StreamSocket *sock;
static uint64_t state_since_us;  /* when the current state was entered */
static uint64_t retry_at_us;
static uint32_t backoff_us = BACKOFF_FIRST_US;
static uint64_t last_rx_us;
static bool failure_logged;      /* log the first failure of an outage only */
static bool v_logged;            /* log an unsupported protocol version once */

static uint8_t rx[RX_CAP];
static int rx_len;
static char msg[MSG_CAP + 1];
static int msg_len;
static int frag_kind;            /* 0 none, 1 text message in progress, 2 ignored (binary/overflow) */

static bool bridge_streaming;    /* last status.state == "streaming" */
static bool have_telemetry;
static uint64_t last_telemetry_us;
static uint32_t rng = 0x9e3779b9u;

/* ---- link teardown ------------------------------------------------------ */

static void set_state(BikeWsState s, uint64_t now_us)
{
	state = s;
	state_since_us = now_us;
}

static void release_net(void)
{
	if (sock != NULL)
	{
		NET_DestroyStreamSocket(sock);
		sock = NULL;
	}
	if (addr != NULL)
	{
		NET_UnrefAddress(addr);
		addr = NULL;
	}
	rx_len = 0;
	msg_len = 0;
	frag_kind = 0;
	bridge_streaming = false;
	have_telemetry = false;
}

/* Any failure: tear down and schedule the next attempt. */
static void fail_link(const char *why, uint64_t now_us)
{
	if (!failure_logged)
	{
		logWarn("bike bridge %s:%u: %s (retrying)", host_name, (unsigned)host_port, why);
		failure_logged = true;
	}
	else
	{
		logDebug("bike bridge %s:%u: %s", host_name, (unsigned)host_port, why);
	}
	release_net();
	retry_at_us = now_us + backoff_us;
	backoff_us = backoff_us * 2u > BACKOFF_MAX_US ? BACKOFF_MAX_US : backoff_us * 2u;
	set_state(BIKE_WS_BACKOFF, now_us);
}

/* ---- sending ------------------------------------------------------------ */

static uint32_t next_rand(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

/* Client-to-server frames MUST be masked (RFC 6455 5.3).  The write is queued
 * inside SDL3_net, so it never blocks and never short-writes. */
static bool send_frame(uint8_t opcode, const uint8_t *payload, int len)
{
	uint8_t buf[2 + 2 + 4 + TX_MAX_PAYLOAD];
	uint8_t key[4];
	uint32_t m;
	int n = 0, i;

	if (sock == NULL || len < 0 || len > TX_MAX_PAYLOAD)
		return false;
	if (NET_GetStreamSocketPendingWrites(sock) > TX_PENDING_MAX)
		return false;

	buf[n++] = (uint8_t)(0x80 | opcode);
	if (len < 126)
	{
		buf[n++] = (uint8_t)(0x80 | len);
	}
	else
	{
		buf[n++] = 0x80 | 126;
		buf[n++] = (uint8_t)(len >> 8);
		buf[n++] = (uint8_t)(len & 0xff);
	}
	m = next_rand();
	key[0] = (uint8_t)(m >> 24);
	key[1] = (uint8_t)(m >> 16);
	key[2] = (uint8_t)(m >> 8);
	key[3] = (uint8_t)m;
	memcpy(buf + n, key, 4);
	n += 4;
	for (i = 0; i < len; ++i)
		buf[n + i] = (uint8_t)(payload[i] ^ key[i & 3]);
	n += len;
	return NET_WriteToStreamSocket(sock, buf, n);
}

/* ---- tiny JSON scanner --------------------------------------------------- */

/* Walks the members of one flat-or-nested JSON object without allocating.
 * Values are skipped structurally, so a nested object or string that happens
 * to contain "powerW" can never be mistaken for the top-level field, and key
 * order does not matter. */

static const char *skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		++p;
	return p;
}

static const char *skip_string(const char *p) /* p at the opening quote */
{
	++p;
	while (*p != '"')
	{
		if (*p == '\0')
			return NULL;
		if (*p == '\\' && p[1] != '\0')
			++p;
		++p;
	}
	return p + 1;
}

static const char *skip_value(const char *p)
{
	if (*p == '"')
		return skip_string(p);
	if (*p == '{' || *p == '[')
	{
		int depth = 0;
		for (;;)
		{
			if (*p == '\0')
				return NULL;
			if (*p == '"')
			{
				p = skip_string(p);
				if (p == NULL)
					return NULL;
				continue;
			}
			if (*p == '{' || *p == '[')
				++depth;
			else if (*p == '}' || *p == ']')
				--depth;
			++p;
			if (depth == 0)
				return p;
		}
	}
	while (*p != '\0' && *p != ',' && *p != '}' && *p != ']' &&
	       *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
		++p;
	return p;
}

/* *pp points just after the '{' (first call) or after the previous value.
 * Returns true with a member; false at '}' or on malformed input (then
 * *pp == NULL). */
static bool next_member(const char **pp, const char **key, size_t *klen, const char **val)
{
	const char *p, *k, *e;

	if (*pp == NULL)
		return false;
	p = skip_ws(*pp);
	if (*p == ',')
		p = skip_ws(p + 1);
	if (*p == '}')
		return false;
	if (*p != '"')
		goto bad;
	k = p + 1;
	e = skip_string(p);
	if (e == NULL)
		goto bad;
	*klen = (size_t)(e - 1 - k);
	*key = k;
	p = skip_ws(e);
	if (*p != ':')
		goto bad;
	p = skip_ws(p + 1);
	if (*p == '\0')
		goto bad;
	*val = p;
	p = skip_value(p);
	if (p == NULL)
		goto bad;
	*pp = p;
	return true;
bad:
	*pp = NULL;
	return false;
}

static bool key_is(const char *key, size_t klen, const char *name)
{
	return strlen(name) == klen && memcmp(key, name, klen) == 0;
}

static bool val_is_null(const char *v)
{
	return strncmp(v, "null", 4) == 0;
}

/* JSON number -> Q16.16 (integer math, locale independent).  Handles a
 * fraction and a small exponent; false if v is not a number. */
static bool parse_q16(const char *v, bq16 *out)
{
	const char *e;
	int64_t q = bike_parse_decimal(v, &e);

	if (e == v)
		return false;
	if (*e == 'e' || *e == 'E')
	{
		int ex = 0;
		bool eneg = false;
		++e;
		if (*e == '+' || *e == '-')
			eneg = (*e++ == '-');
		while (*e >= '0' && *e <= '9')
		{
			if (ex < 100)
				ex = ex * 10 + (*e - '0');
			++e;
		}
		while (ex-- > 0)
		{
			if (eneg)
				q = (q >= 0 ? q + 5 : q - 5) / 10;
			else
				q *= 10;
			if (q > INT32_MAX)
				q = INT32_MAX;
			if (q < -INT32_MAX)
				q = -INT32_MAX;
		}
	}
	*out = (bq16)q;
	return true;
}

static bool parse_u32(const char *v, uint32_t *out)
{
	uint64_t n = 0;
	bool any = false;

	while (*v >= '0' && *v <= '9')
	{
		n = (n * 10u + (uint64_t)(*v - '0')) & 0xffffffffu; /* wrap: only changes matter */
		any = true;
		++v;
	}
	*out = (uint32_t)n;
	return any;
}

typedef enum { FIELD_ABSENT, FIELD_UNKNOWN, FIELD_NUMBER } FieldKind; /* UNKNOWN: null or not a number */

typedef struct
{
	bool is_telemetry, is_status;
	bool v_present;
	bq16 v;
	FieldKind power, cadence;
	bq16 power_w, cadence_rpm;
	bool seq_present;
	uint32_t seq;
	bool streaming;
	FieldKind age_power, age_cadence;
	bq16 age_power_ms, age_cadence_ms;
} WsMessage;

static void scan_field(const char *val, FieldKind *kind, bq16 *out)
{
	if (!val_is_null(val) && parse_q16(val, out))
		*kind = FIELD_NUMBER;
	else
		*kind = FIELD_UNKNOWN;
}

/* Returns false if the text is not a JSON object we can read. */
static bool parse_message(const char *json, WsMessage *m)
{
	const char *p = skip_ws(json), *key, *val;
	size_t klen;

	memset(m, 0, sizeof *m);
	if (*p != '{')
		return false;
	++p;
	while (next_member(&p, &key, &klen, &val))
	{
		if (key_is(key, klen, "type"))
		{
			if (strncmp(val, "\"telemetry\"", 11) == 0)
				m->is_telemetry = true;
			else if (strncmp(val, "\"status\"", 8) == 0)
				m->is_status = true;
		}
		else if (key_is(key, klen, "v"))
		{
			m->v_present = parse_q16(val, &m->v);
		}
		else if (key_is(key, klen, "seq"))
		{
			m->seq_present = parse_u32(val, &m->seq);
		}
		else if (key_is(key, klen, "powerW"))
		{
			scan_field(val, &m->power, &m->power_w);
		}
		else if (key_is(key, klen, "cadenceRpm"))
		{
			scan_field(val, &m->cadence, &m->cadence_rpm);
		}
		else if (key_is(key, klen, "state"))
		{
			m->streaming = strncmp(val, "\"streaming\"", 11) == 0;
		}
		else if (key_is(key, klen, "age") && *val == '{')
		{
			const char *ap = val + 1, *akey, *aval;
			size_t aklen;
			while (next_member(&ap, &akey, &aklen, &aval))
			{
				if (key_is(akey, aklen, "power"))
					scan_field(aval, &m->age_power, &m->age_power_ms);
				else if (key_is(akey, aklen, "cadence"))
					scan_field(aval, &m->age_cadence, &m->age_cadence_ms);
			}
		}
	}
	return p != NULL;
}

static bq16 clamp_nonneg(bq16 x)
{
	return x < 0 ? 0 : x;
}

static void handle_message(const char *json, BikeMailbox *mb, uint64_t now_us)
{
	WsMessage m;

	if (!parse_message(json, &m))
	{
		logDebug("bike bridge: unreadable message ignored");
		return;
	}
	if (!m.is_telemetry && !m.is_status)
		return;

	/* The first readable message proves the peer speaks our protocol. */
	backoff_us = BACKOFF_FIRST_US;

	if (m.is_status)
	{
		bridge_streaming = m.streaming;
		return;
	}

	if (!m.v_present || m.v != BIKE_Q16_ONE)
	{
		if (!v_logged)
		{
			logWarn("bike bridge: telemetry protocol version is not 1 (v=%d.%03d); ignoring such messages",
			        (int)(m.v / BIKE_Q16_ONE), (int)(((int64_t)(m.v % BIKE_Q16_ONE) * 1000) / BIKE_Q16_ONE));
			v_logged = true;
		}
		return;
	}

	{
		BikeSample s;
		uint64_t age_us = 0;

		memset(&s, 0, sizeof s);
		if (m.power == FIELD_NUMBER)
		{
			s.power_valid = true;
			s.power_w = clamp_nonneg(m.power_w);
		}
		if (m.cadence == FIELD_NUMBER)
		{
			s.cadence_valid = true;
			s.cadence_rpm = clamp_nonneg(m.cadence_rpm);
		}

		/* The bridge repeats the last value of a field that has not been
		 * refreshed; backdate the sample by that field's age so the effort
		 * engine sees it as stale (power first, cadence if there is no power). */
		{
			FieldKind ak = s.power_valid ? m.age_power : m.age_cadence;
			bq16 a = s.power_valid ? m.age_power_ms : m.age_cadence_ms;
			if (ak == FIELD_NUMBER && a > 0)
			{
				uint64_t ms = (uint64_t)a / BIKE_Q16_ONE;
				age_us = (ms > 60000u ? 60000u : ms) * 1000u;
			}
		}
		s.arrival_us = now_us > age_us ? now_us - age_us : 0;

		s.seq = m.seq_present ? m.seq : mb->latest.seq + 1u;
		if (mb->has_sample && s.seq == mb->latest.seq)
			s.seq = mb->latest.seq + 1u; /* e.g. bridge restarted at the same seq: still a new sample */

		mb->latest = s;
		mb->has_sample = true;
		have_telemetry = true;
		last_telemetry_us = now_us;
	}
}

/* ---- framing -------------------------------------------------------------- */

static void consume(int n)
{
	memmove(rx, rx + n, (size_t)(rx_len - n));
	rx_len -= n;
}

/* Parses as many complete frames as rx[] holds.  Returns false if the link
 * had to be dropped (state already moved to BACKOFF). */
static bool process_frames(BikeMailbox *mb, uint64_t now_us)
{
	for (;;)
	{
		uint8_t b0, b1, opcode;
		uint64_t plen;
		int hdr = 2, i;
		bool fin;
		const uint8_t *payload;

		if (rx_len < 2)
			return true;
		b0 = rx[0];
		b1 = rx[1];
		opcode = b0 & 0x0f;
		fin = (b0 & 0x80) != 0;
		if (b0 & 0x70)
		{
			fail_link("peer used reserved bits (no extensions were negotiated)", now_us);
			return false;
		}
		plen = b1 & 0x7f;
		if (plen == 126)
		{
			if (rx_len < 4)
				return true;
			plen = ((uint64_t)rx[2] << 8) | rx[3];
			hdr = 4;
		}
		else if (plen == 127)
		{
			if (rx_len < 10)
				return true;
			plen = 0;
			for (i = 0; i < 8; ++i)
				plen = (plen << 8) | rx[2 + i];
			hdr = 10;
		}
		if (b1 & 0x80)
			hdr += 4; /* servers must not mask; tolerate it */
		if (plen > (uint64_t)(RX_CAP - hdr))
		{
			fail_link("frame too large", now_us);
			return false;
		}
		if ((uint64_t)rx_len < (uint64_t)hdr + plen)
			return true; /* wait for the rest */

		payload = rx + hdr;
		if (b1 & 0x80)
		{
			/* unmask in place (key sits just before the payload) */
			for (i = 0; i < (int)plen; ++i)
				rx[hdr + i] = (uint8_t)(payload[i] ^ rx[hdr - 4 + (i & 3)]);
		}

		if (opcode >= 0x8) /* control frames: never fragmented, <= 125 bytes */
		{
			if (!fin || plen > 125)
			{
				fail_link("malformed control frame", now_us);
				return false;
			}
			if (opcode == 0x9) /* ping -> pong with the same payload */
			{
				if (!send_frame(0xA, payload, (int)plen))
				{
					fail_link("write failed", now_us);
					return false;
				}
			}
			else if (opcode == 0x8) /* close: echo the status code, then reconnect later */
			{
				send_frame(0x8, payload, plen >= 2 ? 2 : 0);
				fail_link("closed by peer", now_us);
				return false;
			}
			/* 0xA pong and reserved opcodes: ignored */
		}
		else if (opcode == 0x1 || opcode == 0x2 || opcode == 0x0)
		{
			if (opcode != 0x0)
			{
				/* a new data message; an unfinished one is a protocol error we shrug off */
				frag_kind = opcode == 0x1 ? 1 : 2;
				msg_len = 0;
			}
			else if (frag_kind == 0)
			{
				frag_kind = 2; /* stray continuation: swallow until FIN */
			}
			if (frag_kind == 1)
			{
				if (msg_len + (int)plen > MSG_CAP)
				{
					frag_kind = 2; /* too big to be ours: skip the rest of this message */
				}
				else
				{
					memcpy(msg + msg_len, payload, (size_t)plen);
					msg_len += (int)plen;
				}
			}
			if (fin)
			{
				if (frag_kind == 1)
				{
					msg[msg_len] = '\0';
					handle_message(msg, mb, now_us);
				}
				frag_kind = 0;
				msg_len = 0;
			}
		}
		consume(hdr + (int)plen);
		if (state != BIKE_WS_OPEN)
			return false;
	}
}

/* ---- handshake -------------------------------------------------------------- */

static bool header_value(const char *headers, const char *name, char *out, size_t cap)
{
	size_t nlen = strlen(name);
	const char *p = headers;

	while (*p != '\0')
	{
		const char *eol = strstr(p, "\r\n");
		size_t llen = eol != NULL ? (size_t)(eol - p) : strlen(p);
		if (llen > nlen && p[nlen] == ':' && SDL_strncasecmp(p, name, nlen) == 0)
		{
			const char *v = p + nlen + 1;
			size_t vlen;
			while (v < p + llen && (*v == ' ' || *v == '\t'))
				++v;
			vlen = (size_t)(p + llen - v);
			while (vlen > 0 && (v[vlen - 1] == ' ' || v[vlen - 1] == '\t'))
				--vlen;
			if (vlen >= cap)
				return false;
			memcpy(out, v, vlen);
			out[vlen] = '\0';
			return true;
		}
		if (eol == NULL)
			break;
		p = eol + 2;
	}
	return false;
}

/* Returns true to keep going, false if the link was dropped. */
static bool process_handshake(uint64_t now_us)
{
	char *eoh;
	char accept[64];
	int used;

	rx[rx_len < RX_CAP ? rx_len : RX_CAP - 1] = '\0'; /* RX_CAP bytes: last may be overwritten, only on overflow */
	eoh = strstr((char *)rx, "\r\n\r\n");
	if (eoh == NULL)
	{
		if (rx_len >= RX_CAP - 1)
		{
			fail_link("handshake response too long", now_us);
			return false;
		}
		return true;
	}
	if (strncmp((char *)rx, "HTTP/1.1 101", 12) != 0)
	{
		fail_link("handshake refused (not 101)", now_us);
		return false;
	}
	*eoh = '\0';
	/* Loose check: the key is the fixed RFC sample, so its accept value is a
	 * constant and no SHA-1 is needed.  Header absent or wrong = not a
	 * WebSocket server we can trust. */
	if (!header_value((char *)rx, "Sec-WebSocket-Accept", accept, sizeof accept) ||
	    strcmp(accept, ws_accept) != 0)
	{
		fail_link("handshake: bad Sec-WebSocket-Accept", now_us);
		return false;
	}
	used = (int)(eoh - (char *)rx) + 4;
	consume(used);

	set_state(BIKE_WS_OPEN, now_us);
	last_rx_us = now_us;
	failure_logged = false;
	logInfo("bike bridge %s:%u: connected", host_name, (unsigned)host_port);
	{
		static const char hello[] = "{\"type\":\"hello\",\"client\":\"game\",\"v\":1}";
		if (!send_frame(0x1, (const uint8_t *)hello, (int)sizeof hello - 1))
		{
			fail_link("write failed", now_us);
			return false;
		}
	}
	return true;
}

/* ---- state machine ---------------------------------------------------------- */

static void start_attempt(uint64_t now_us)
{
	release_net();
	addr = NET_ResolveHostname(host_name);
	if (addr == NULL)
	{
		fail_link("cannot start name resolution", now_us);
		return;
	}
	set_state(BIKE_WS_RESOLVING, now_us);
}

static void read_socket(BikeMailbox *mb, uint64_t now_us)
{
	int iter;

	for (iter = 0; iter < READS_PER_POLL && sock != NULL &&
	               (state == BIKE_WS_HANDSHAKING || state == BIKE_WS_OPEN); ++iter)
	{
		int n = NET_ReadFromStreamSocket(sock, rx + rx_len, RX_CAP - rx_len);
		if (n < 0)
		{
			fail_link(state == BIKE_WS_OPEN ? "connection lost" : "connection closed during handshake", now_us);
			return;
		}
		if (n == 0)
			return;
		rx_len += n;
		last_rx_us = now_us;
		if (state == BIKE_WS_HANDSHAKING && !process_handshake(now_us))
			return;
		if (state == BIKE_WS_OPEN && !process_frames(mb, now_us))
			return;
		if (rx_len == RX_CAP && state == BIKE_WS_HANDSHAKING)
		{
			fail_link("handshake response too long", now_us);
			return;
		}
	}
}

static void publish_link(BikeMailbox *mb, uint64_t now_us)
{
	if (!want_link)
		mb->link = BIKE_LINK_OFF;
	else if (state != BIKE_WS_OPEN)
		mb->link = BIKE_LINK_CONNECTING;
	else if (bridge_streaming || (have_telemetry && now_us - last_telemetry_us < TELEMETRY_FRESH_US))
		mb->link = BIKE_LINK_STREAMING;
	else
		mb->link = BIKE_LINK_WAITING;
}

bool bike_ws_open(const char *host, uint16_t port)
{
	if (host == NULL || host[0] == '\0' || port == 0 || strlen(host) >= sizeof host_name)
		return false;
	if (!net_inited)
	{
		if (!NET_Init())
		{
			logError("bike bridge: NET_Init: %s", SDL_GetError());
			return false;
		}
		net_inited = true;
	}
	release_net();
	memcpy(host_name, host, strlen(host) + 1);
	host_port = port;
	want_link = true;
	backoff_us = BACKOFF_FIRST_US;
	failure_logged = false;
	set_state(BIKE_WS_IDLE, 0);
	return true;
}

void bike_ws_poll(BikeMailbox *mb, uint64_t now_us)
{
	if (want_link)
	{
		switch (state)
		{
		case BIKE_WS_IDLE:
			start_attempt(now_us);
			break;
		case BIKE_WS_BACKOFF:
			if (now_us >= retry_at_us)
				start_attempt(now_us);
			break;
		case BIKE_WS_RESOLVING:
		{
			NET_Status st = NET_GetAddressStatus(addr);
			if (st == NET_SUCCESS)
			{
				sock = NET_CreateClient(addr, host_port, 0);
				if (sock == NULL)
					fail_link("cannot create socket", now_us);
				else
					set_state(BIKE_WS_CONNECTING, now_us);
			}
			else if (st == NET_FAILURE)
				fail_link("name resolution failed", now_us);
			else if (now_us - state_since_us > RESOLVE_TIMEOUT_US)
				fail_link("name resolution timed out", now_us);
			break;
		}
		case BIKE_WS_CONNECTING:
		{
			NET_Status st = NET_GetConnectionStatus(sock);
			if (st == NET_SUCCESS)
			{
				char req[512];
				int n = SDL_snprintf(req, sizeof req,
				                     "GET /ws HTTP/1.1\r\n"
				                     "Host: %s:%u\r\n"
				                     "Upgrade: websocket\r\n"
				                     "Connection: Upgrade\r\n"
				                     "Sec-WebSocket-Key: %s\r\n"
				                     "Sec-WebSocket-Version: 13\r\n\r\n",
				                     host_name, (unsigned)host_port, ws_key);
				if (n <= 0 || n >= (int)sizeof req || !NET_WriteToStreamSocket(sock, req, n))
					fail_link("cannot send upgrade request", now_us);
				else
					set_state(BIKE_WS_HANDSHAKING, now_us);
			}
			else if (st == NET_FAILURE)
				fail_link("connect failed", now_us);
			else if (now_us - state_since_us > CONNECT_TIMEOUT_US)
				fail_link("connect timed out", now_us);
			break;
		}
		case BIKE_WS_HANDSHAKING:
			read_socket(mb, now_us);
			if (state == BIKE_WS_HANDSHAKING && now_us - state_since_us > HANDSHAKE_TIMEOUT_US)
				fail_link("handshake timed out", now_us);
			break;
		case BIKE_WS_OPEN:
			read_socket(mb, now_us);
			if (state == BIKE_WS_OPEN && now_us - last_rx_us > SILENCE_TIMEOUT_US)
				fail_link("peer silent (missed pings)", now_us);
			break;
		}
	}
	publish_link(mb, now_us);
}

bool bike_ws_send_text(const char *text)
{
	if (state != BIKE_WS_OPEN || text == NULL)
		return false;
	return send_frame(0x1, (const uint8_t *)text, (int)strlen(text));
}

BikeWsState bike_ws_state(void)
{
	return state;
}

void bike_ws_close(void)
{
	want_link = false;
	if (state == BIKE_WS_OPEN)
	{
		static const uint8_t normal[2] = { 0x03, 0xe8 }; /* 1000 */
		send_frame(0x8, normal, 2);
	}
	release_net();
	set_state(BIKE_WS_IDLE, 0);
	if (net_inited)
	{
		NET_Quit(); /* reference counted: netplay's own NET_Init is unaffected */
		net_inited = false;
	}
}

#else /* !WITH_NETWORK */

bool bike_ws_open(const char *host, uint16_t port)
{
	(void)host;
	(void)port;
	return false;
}

void bike_ws_poll(BikeMailbox *mb, uint64_t now_us)
{
	(void)now_us;
	mb->link = BIKE_LINK_OFF;
}

void bike_ws_close(void)
{
}

bool bike_ws_send_text(const char *text)
{
	(void)text;
	return false;
}

BikeWsState bike_ws_state(void)
{
	return BIKE_WS_IDLE;
}

#endif /* WITH_NETWORK */

const char *bike_ws_state_name(BikeWsState s)
{
	static const char *const names[] = { "idle", "resolving", "connecting", "handshaking", "open", "backoff" };
	return (unsigned)s < sizeof names / sizeof names[0] ? names[s] : "?";
}
