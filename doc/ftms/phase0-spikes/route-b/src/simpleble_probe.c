/*
 * simpleble_probe.c - throwaway spike: list adapters, scan for FTMS (0x1826) devices,
 * optionally connect to the first one and print Indoor Bike Data via ftms_parse.c.
 *
 * Built against SimpleBLE's C API (simpleble_c headers).  Strict C99.
 *   usage: simpleble_probe [scan_ms=5000] [--connect]
 *
 * NOTE: the --connect path is compile-checked only (no BLE hardware in this container).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <simpleble_c/simpleble.h>
#include "ftms_parse.h"

#define FTMS_SERVICE_UUID "00001826-0000-1000-8000-00805f9b34fb"
#define IBD_CHAR_UUID     "00002ad2-0000-1000-8000-00805f9b34fb"

static int ieq(const char *a, const char *b)
{
	for (; *a && *b; ++a, ++b)
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
	return *a == *b;
}

static simpleble_peripheral_t g_ftms;   /* first FTMS device seen (callback thread writes, main reads after scan) */

static int advertises_ftms(simpleble_peripheral_t p)
{
	size_t i, n = simpleble_peripheral_services_count(p);
	for (i = 0; i < n; ++i)
	{
		simpleble_service_t s;
		if (simpleble_peripheral_services_get(p, i, &s) == SIMPLEBLE_SUCCESS && ieq(s.uuid.value, FTMS_SERVICE_UUID))
			return 1;
	}
	return 0;
}

/* Runs on SimpleBLE's thread.  In the real design this only enqueues into a mutex-protected list. */
static void on_found(simpleble_adapter_t a, simpleble_peripheral_t p, void *ud)
{
	char *id = simpleble_peripheral_identifier(p), *addr = simpleble_peripheral_address(p);
	int ftms = advertises_ftms(p);
	(void)a; (void)ud;
	printf("  found: %-24s %s rssi=%d%s\n", id ? id : "(unnamed)", addr ? addr : "?",
	       (int)simpleble_peripheral_rssi(p), ftms ? "  [FTMS]" : "");
	if (ftms && g_ftms == NULL)
		g_ftms = p;                     /* handle stays valid until released */
	else
		simpleble_peripheral_release_handle(p);
	simpleble_free(id); simpleble_free(addr);
}

static void on_notify(simpleble_uuid_t svc, simpleble_uuid_t chr, const uint8_t *data, size_t len, void *ud)
{
	ftms_bike_data d;
	(void)svc; (void)chr; (void)ud;
	if (ftms_parse_indoor_bike_data(data, len, &d) == FTMS_OK)
		printf("  IBD: %4d W  %5.1f rpm  (more=%d)\n", (d.present & FTMS_HAS_POWER) ? d.power_w : -1,
		       (d.present & FTMS_HAS_CADENCE) ? ftms_cadence_rpm(&d) : -1.0, (int)d.more_data);
}

static void sleep_ms(long ms)
{
	struct timespec ts;
	ts.tv_sec = ms / 1000; ts.tv_nsec = (ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
}

int main(int argc, char **argv)
{
	int scan_ms = 5000, do_connect = 0, i;
	size_t count;
	simpleble_adapter_t ad;

	for (i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "--connect") == 0) do_connect = 1;
		else scan_ms = atoi(argv[i]);
	}

	printf("bluetooth enabled: %s\n", simpleble_adapter_is_bluetooth_enabled() ? "yes" : "no");
	count = simpleble_adapter_get_count();
	printf("adapters: %zu\n", count);
	if (count == 0)
	{
		fprintf(stderr, "no Bluetooth adapter found (expected inside this container)\n");
		return 1;
	}
	ad = simpleble_adapter_get_handle(0);
	{
		char *id = simpleble_adapter_identifier(ad), *addr = simpleble_adapter_address(ad);
		printf("adapter 0: %s [%s]\n", id ? id : "?", addr ? addr : "?");
		simpleble_free(id); simpleble_free(addr);
	}
	simpleble_adapter_set_callback_on_scan_found(ad, on_found, NULL);
	printf("scanning %d ms ...\n", scan_ms);
	if (simpleble_adapter_scan_for(ad, scan_ms) != SIMPLEBLE_SUCCESS)
	{
		fprintf(stderr, "scan failed\n");
		return 1;
	}
	if (do_connect && g_ftms)
	{
		simpleble_uuid_t svc, chr;
		memset(&svc, 0, sizeof svc); memset(&chr, 0, sizeof chr);
		strcpy(svc.value, FTMS_SERVICE_UUID); strcpy(chr.value, IBD_CHAR_UUID);
		if (simpleble_peripheral_connect(g_ftms) != SIMPLEBLE_SUCCESS) { fprintf(stderr, "connect failed\n"); return 1; }
		if (simpleble_peripheral_notify(g_ftms, svc, chr, on_notify, NULL) != SIMPLEBLE_SUCCESS) { fprintf(stderr, "subscribe failed\n"); return 1; }
		for (i = 0; i < 300; ++i) sleep_ms(100);   /* 30 s */
		simpleble_peripheral_unsubscribe(g_ftms, svc, chr);
		simpleble_peripheral_disconnect(g_ftms);
	}
	simpleble_adapter_release_handle(ad);
	return 0;
}
