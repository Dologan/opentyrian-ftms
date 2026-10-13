/*
 * bike_ws: minimal non-blocking WebSocket *client* over SDL2_net (TCP), for
 * reading Flywheel-bridge telemetry from inside OpenTyrian.  Spike code.
 *
 * - No threads.  All socket I/O happens inside bike_ws_poll() with a 0 ms
 *   SDLNet_CheckSockets(), so it is safe to call once per game frame.
 * - The ONLY blocking call is the TCP connect inside bike_ws_open() (SDL2_net
 *   has no async connect).  bike_ws_poll() re-invokes it for auto-reconnect.
 */
#ifndef BIKE_WS_H
#define BIKE_WS_H

#include <SDL_stdinc.h>
#include <stdbool.h>

typedef struct
{
	bool   has_power;     /* powerW was a number (not null/absent) */
	bool   has_cadence;
	bool   has_age;       /* age.power present */
	float  power_w;
	float  cadence_rpm;
	float  power_age_ms;  /* ms since the bridge last saw a power update */
	long   seq;           /* bridge sequence number, -1 if absent */
	double wall_ms;       /* bridge wall clock (epoch ms), 0 if absent */
	Uint32 recv_ticks;    /* SDL_GetTicks() when the frame was parsed */
	char   source[8];     /* "sim", "ftms", "replay", ... */
} BikeSample;

typedef enum
{
	BIKE_WS_CLOSED = 0,
	BIKE_WS_HANDSHAKE,    /* TCP connected, waiting for HTTP 101 */
	BIKE_WS_OPEN
} BikeWsState;

/* Blocking TCP connect + sends the HTTP Upgrade request.  The 101 reply is
 * consumed later by bike_ws_poll().  Returns false if the connect failed
 * (it is retried automatically by bike_ws_poll() every ~2 s). */
bool bike_ws_open(const char *host, Uint16 port);

/* Non-blocking.  Reads whatever is pending, handles ping/close/handshake,
 * parses every telemetry text frame and stores the NEWEST in *latest.
 * Returns the number of telemetry frames parsed (0 = nothing new), or -1 if
 * the link is down. */
int bike_ws_poll(BikeSample *latest);

/* Send one masked text frame (e.g. {"type":"sim","cadenceRpm":90}). */
bool bike_ws_send_text(const char *text);

BikeWsState bike_ws_state(void);
void bike_ws_close(void);   /* also disables auto-reconnect */

#endif
