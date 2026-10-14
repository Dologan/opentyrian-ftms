/*
 * bike_ws.h - Flywheel bridge client: a fully non-blocking WebSocket reader
 * over SDL3_net.  Ported from the Phase 0 spike (doc/ftms/phase0-spikes/route-a).
 *
 * No threads and no call ever blocks: name resolution, TCP connect, the HTTP
 * Upgrade handshake and every read/write are polled from bike_ws_poll(), which
 * bike_service() calls from handleSdlEvents().  The client only writes the
 * mailbox; the effort engine consumes it once per gameplay frame.
 *
 * The whole implementation is under WITH_NETWORK.  Without it every function
 * is a stub and the link stays BIKE_LINK_OFF.
 */
#ifndef BIKE_WS_H
#define BIKE_WS_H

#include "bike.h"

#include <stdbool.h>
#include <stdint.h>


typedef enum
{
	BIKE_WS_IDLE = 0,    /* not started, or about to start an attempt */
	BIKE_WS_RESOLVING,   /* DNS lookup pending */
	BIKE_WS_CONNECTING,  /* TCP connect pending */
	BIKE_WS_HANDSHAKING, /* Upgrade request sent, waiting for 101 */
	BIKE_WS_OPEN,
	BIKE_WS_BACKOFF      /* waiting to retry: 0.5, 1, 2, 4, then 5 s */
} BikeWsState;

/* Starts (or restarts) the link to host:port.  Never blocks: it only records
 * the target and initialises SDL3_net.  Returns false if SDL3_net is
 * unavailable or the arguments are unusable.  Reconnects forever until
 * bike_ws_close(). */
bool bike_ws_open(const char *host, uint16_t port);

/* Advances the state machine and drains the socket; never blocks.  now_us is
 * a monotonic microsecond clock (SDL_GetTicksNS() / 1000).  Updates mb->link
 * every call; on each telemetry message stores the sample, with arrival_us =
 * now_us minus the bridge's reported sensor age, in mb->latest and sets
 * mb->has_sample.  An old sample is kept across link loss (it ages out). */
void bike_ws_poll(BikeMailbox *mb, uint64_t now_us);

/* Sends a close frame (best effort), tears the link down, stops reconnecting. */
void bike_ws_close(void);

/* Sends one masked text frame, e.g. {"type":"sim","cadenceRpm":90}.  False if
 * the link is not open. */
bool bike_ws_send_text(const char *text);

BikeWsState bike_ws_state(void);
const char *bike_ws_state_name(BikeWsState s);

#endif /* BIKE_WS_H */
