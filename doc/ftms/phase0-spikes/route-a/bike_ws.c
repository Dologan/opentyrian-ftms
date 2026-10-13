#include "bike_ws.h"

#include <SDL_net.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_CAP 16384
#define RECONNECT_MS 2000

static TCPsocket     sock;
static SDLNet_SocketSet set;
static BikeWsState   state = BIKE_WS_CLOSED;
static char          rx[RX_CAP];
static int           rx_len;
static char          host_name[128];
static Uint16        host_port;
static bool          want_link;
static Uint32        last_attempt;
static bool          net_inited;
static Uint32        rng = 0x9e3779b9u;

/* ---- helpers ------------------------------------------------------------ */

static void drop_link(void)
{
	if (set != NULL && sock != NULL)
		SDLNet_TCP_DelSocket(set, sock);
	if (sock != NULL)
		SDLNet_TCP_Close(sock);
	if (set != NULL)
		SDLNet_FreeSocketSet(set);
	sock = NULL;
	set = NULL;
	rx_len = 0;
	state = BIKE_WS_CLOSED;
}

static Uint32 next_rand(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

/* Client->server frames MUST be masked (RFC 6455 5.3). */
static bool send_frame(Uint8 opcode, const char *payload, int len)
{
	Uint8 buf[2 + 2 + 4 + 1024];
	int n = 0;
	if (sock == NULL || len < 0 || len > 1024)
		return false;
	buf[n++] = 0x80 | opcode;
	if (len < 126)
	{
		buf[n++] = 0x80 | (Uint8)len;
	}
	else
	{
		buf[n++] = 0x80 | 126;
		buf[n++] = (Uint8)(len >> 8);
		buf[n++] = (Uint8)(len & 0xff);
	}
	{
		Uint32 m = next_rand();
		Uint8 key[4];
		int i;
		key[0] = (Uint8)(m >> 24); key[1] = (Uint8)(m >> 16);
		key[2] = (Uint8)(m >> 8);  key[3] = (Uint8)m;
		memcpy(buf + n, key, 4);
		n += 4;
		for (i = 0; i < len; ++i)
			buf[n + i] = (Uint8)payload[i] ^ key[i & 3];
		n += len;
	}
	if (SDLNet_TCP_Send(sock, buf, n) < n)
	{
		drop_link();
		return false;
	}
	return true;
}

/* ---- tiny JSON field scanner (flat keys, bridge uses compact separators) -- */

static const char *find_key(const char *json, const char *quoted_key)
{
	const char *p = strstr(json, quoted_key);
	if (p == NULL)
		return NULL;
	p += strlen(quoted_key);
	while (*p == ' ' || *p == ':')
		++p;
	return p;
}

static bool scan_double(const char *json, const char *quoted_key, double *out)
{
	const char *p = find_key(json, quoted_key);
	char *end;
	double v;
	if (p == NULL || *p == 'n' /* null */)
		return false;
	v = strtod(p, &end);
	if (end == p)
		return false;
	*out = v;
	return true;
}

static void parse_telemetry(const char *json, BikeSample *s)
{
	double v;
	const char *age;
	memset(s, 0, sizeof *s);
	s->seq = -1;
	if (scan_double(json, "\"powerW\"", &v))      { s->has_power = true;   s->power_w = (float)v; }
	if (scan_double(json, "\"cadenceRpm\"", &v))  { s->has_cadence = true; s->cadence_rpm = (float)v; }
	if (scan_double(json, "\"seq\"", &v))         s->seq = (long)v;
	if (scan_double(json, "\"wall\"", &v))        s->wall_ms = v;
	age = strstr(json, "\"age\"");
	if (age != NULL && scan_double(age, "\"power\"", &v))
	{
		s->has_age = true;
		s->power_age_ms = (float)v;
	}
	{
		const char *p = find_key(json, "\"source\"");
		if (p != NULL && *p == '"')
		{
			int i = 0;
			++p;
			while (*p != '"' && *p != '\0' && i < (int)sizeof s->source - 1)
				s->source[i++] = *p++;
			s->source[i] = '\0';
		}
	}
	s->recv_ticks = SDL_GetTicks();
}

/* ---- frame parsing ------------------------------------------------------ */

/* Consumes as much of rx[] as possible.  Returns telemetry count, or -1 if
 * the link had to be dropped. */
static int process_rx(BikeSample *latest)
{
	int telemetry = 0;

	if (state == BIKE_WS_HANDSHAKE)
	{
		char *eoh;
		rx[rx_len < RX_CAP ? rx_len : RX_CAP - 1] = '\0';
		eoh = strstr(rx, "\r\n\r\n");
		if (eoh == NULL)
		{
			if (rx_len >= RX_CAP - 1) { drop_link(); return -1; }
			return 0;
		}
		if (strncmp(rx, "HTTP/1.1 101", 12) != 0)
		{
			fprintf(stderr, "bike_ws: handshake refused: %.40s\n", rx);
			drop_link();
			return -1;
		}
		/* NOTE: Sec-WebSocket-Accept is not verified (spike). */
		{
			int used = (int)(eoh - rx) + 4;
			memmove(rx, rx + used, (size_t)(rx_len - used));
			rx_len -= used;
		}
		state = BIKE_WS_OPEN;
		{
		static const char hello[] = "{\"type\":\"hello\",\"client\":\"game\",\"v\":1}";
		send_frame(0x1, hello, (int)sizeof hello - 1);
	}
	}

	for (;;)
	{
		Uint8 b0, b1, opcode;
		Uint64 plen;
		int hdr = 2, masked;
		const Uint8 *u = (const Uint8 *)rx;

		if (rx_len < 2)
			break;
		b0 = u[0]; b1 = u[1];
		opcode = b0 & 0x0f;
		masked = (b1 & 0x80) != 0;
		plen = b1 & 0x7f;
		if (plen == 126)
		{
			if (rx_len < 4) break;
			plen = ((Uint64)u[2] << 8) | u[3];
			hdr = 4;
		}
		else if (plen == 127)
		{
			int i;
			if (rx_len < 10) break;
			plen = 0;
			for (i = 0; i < 8; ++i)
				plen = (plen << 8) | u[2 + i];
			hdr = 10;
		}
		if (masked)
			hdr += 4;   /* servers must not mask; tolerate and ignore key */
		if (plen > (Uint64)(RX_CAP - hdr))
		{
			fprintf(stderr, "bike_ws: frame too large (%llu), dropping link\n", (unsigned long long)plen);
			drop_link();
			return -1;
		}
		if ((Uint64)rx_len < (Uint64)hdr + plen)
			break;   /* wait for the rest */

		{
			const char *payload = rx + hdr;
			int pl = (int)plen;

			if (opcode == 0x1 && (b0 & 0x80))        /* text, unfragmented */
			{
				static char msg[RX_CAP + 1];
				memcpy(msg, payload, (size_t)pl);
				msg[pl] = '\0';
				if (strstr(msg, "\"type\":\"telemetry\"") != NULL)
				{
					parse_telemetry(msg, latest);
					++telemetry;
				}
			}
			else if (opcode == 0x9)                   /* ping -> pong */
			{
				if (!send_frame(0xA, payload, pl > 125 ? 125 : pl))
					return -1;
			}
			else if (opcode == 0x8)                   /* close */
			{
				send_frame(0x8, payload, pl >= 2 ? 2 : 0);
				drop_link();
				return -1;
			}
			/* binary / continuation / pong: ignored */
		}
		{
			int used = hdr + (int)plen;
			memmove(rx, rx + used, (size_t)(rx_len - used));
			rx_len -= used;
		}
	}
	return telemetry;
}

/* ---- public API --------------------------------------------------------- */

bool bike_ws_open(const char *host, Uint16 port)
{
	IPaddress ip;
	char req[512];
	int n;

	if (!net_inited)
	{
		if (SDLNet_Init() != 0)
		{
			fprintf(stderr, "bike_ws: SDLNet_Init: %s\n", SDLNet_GetError());
			return false;
		}
		net_inited = true;
	}
	if (host != host_name)   /* auto-reconnect passes our own buffer back in */
		snprintf(host_name, sizeof host_name, "%s", host);
	host_port = port;
	want_link = true;
	last_attempt = SDL_GetTicks();

	drop_link();
	if (SDLNet_ResolveHost(&ip, host, port) != 0)
		return false;
	sock = SDLNet_TCP_Open(&ip);            /* BLOCKING connect */
	if (sock == NULL)
		return false;
	set = SDLNet_AllocSocketSet(1);
	if (set == NULL || SDLNet_TCP_AddSocket(set, sock) < 0)
	{
		drop_link();
		return false;
	}
	n = snprintf(req, sizeof req,
	             "GET /ws HTTP/1.1\r\n"
	             "Host: %s:%u\r\n"
	             "Upgrade: websocket\r\n"
	             "Connection: Upgrade\r\n"
	             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
	             "Sec-WebSocket-Version: 13\r\n\r\n",
	             host, (unsigned)port);
	if (SDLNet_TCP_Send(sock, req, n) < n)
	{
		drop_link();
		return false;
	}
	rx_len = 0;
	state = BIKE_WS_HANDSHAKE;
	return true;
}

int bike_ws_poll(BikeSample *latest)
{
	int total = 0, iter;

	if (state == BIKE_WS_CLOSED)
	{
		if (want_link && SDL_GetTicks() - last_attempt >= RECONNECT_MS)
			bike_ws_open(host_name, host_port);
		if (state == BIKE_WS_CLOSED)
			return -1;
	}
	for (iter = 0; iter < 16 && set != NULL; ++iter)
	{
		int n, r;
		if (SDLNet_CheckSockets(set, 0) <= 0 || !SDLNet_SocketReady(sock))
			break;
		n = SDLNet_TCP_Recv(sock, rx + rx_len, RX_CAP - rx_len);
		if (n <= 0)
		{
			drop_link();       /* peer closed / error */
			return total > 0 ? total : -1;
		}
		rx_len += n;
		r = process_rx(latest);
		if (r < 0)
			return total > 0 ? total : -1;
		total += r;
	}
	return total;
}

bool bike_ws_send_text(const char *text)
{
	if (state != BIKE_WS_OPEN)
		return false;
	return send_frame(0x1, text, (int)strlen(text));
}

BikeWsState bike_ws_state(void)
{
	return state;
}

void bike_ws_close(void)
{
	want_link = false;
	if (state == BIKE_WS_OPEN)
		send_frame(0x8, "\x03\xe8", 2);   /* 1000 normal closure */
	drop_link();
}
