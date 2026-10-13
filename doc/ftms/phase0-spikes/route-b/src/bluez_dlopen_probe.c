/*
 * bluez_dlopen_probe.c - spike: talk to BlueZ over the system D-Bus with libdbus-1
 * loaded via dlopen() at run time (headers only at build time, nothing extra linked),
 * the way SDL loads its audio/video backends.  Lists org.bluez adapters.
 *
 *   gcc -std=gnu99 -Wall -Wextra -o bluez_probe bluez_dlopen_probe.c -ldl
 *
 * (-std=gnu99, not c99: dlsym() -> function pointer needs the usual POSIX cast.)
 * Shows the graceful-degradation path: no libdbus / no system bus / no bluetoothd
 * each produce a distinct, user-presentable message and the game keeps running.
 */
#define _GNU_SOURCE
#include <dbus/dbus.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define FN(ret, name, args) static ret (*p_##name) args
FN(void, dbus_error_init, (DBusError *));
FN(void, dbus_error_free, (DBusError *));
FN(DBusConnection *, dbus_bus_get, (DBusBusType, DBusError *));
FN(DBusMessage *, dbus_message_new_method_call, (const char *, const char *, const char *, const char *));
FN(DBusMessage *, dbus_connection_send_with_reply_and_block, (DBusConnection *, DBusMessage *, int, DBusError *));
FN(void, dbus_message_unref, (DBusMessage *));
FN(dbus_bool_t, dbus_message_iter_init, (DBusMessage *, DBusMessageIter *));
FN(void, dbus_message_iter_recurse, (DBusMessageIter *, DBusMessageIter *));
FN(dbus_bool_t, dbus_message_iter_next, (DBusMessageIter *));
FN(int, dbus_message_iter_get_arg_type, (DBusMessageIter *));
FN(void, dbus_message_iter_get_basic, (DBusMessageIter *, void *));

static void *must(void *lib, const char *sym, int *ok)
{
	void *p = dlsym(lib, sym);
	if (!p) { fprintf(stderr, "libdbus-1 missing symbol %s\n", sym); *ok = 0; }
	return p;
}
#define LOAD(name) (*(void **)&p_##name = must(lib, #name, &ok))

int main(void)
{
	void *lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
	int ok = 1, n = 0;
	DBusError err;
	DBusConnection *bus;
	DBusMessage *call, *reply;
	DBusMessageIter top, dict;

	if (!lib) { printf("BLE unavailable: libdbus-1.so.3 not installed (%s)\n", dlerror()); return 1; }
	LOAD(dbus_error_init); LOAD(dbus_error_free); LOAD(dbus_bus_get);
	LOAD(dbus_message_new_method_call); LOAD(dbus_connection_send_with_reply_and_block);
	LOAD(dbus_message_unref); LOAD(dbus_message_iter_init); LOAD(dbus_message_iter_recurse);
	LOAD(dbus_message_iter_next); LOAD(dbus_message_iter_get_arg_type); LOAD(dbus_message_iter_get_basic);
	if (!ok) return 1;
	printf("libdbus-1 loaded via dlopen\n");

	p_dbus_error_init(&err);
	bus = p_dbus_bus_get(DBUS_BUS_SYSTEM, &err);
	if (!bus) { printf("BLE unavailable: cannot reach system D-Bus: %s\n", err.message); p_dbus_error_free(&err); return 1; }

	call = p_dbus_message_new_method_call("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
	reply = p_dbus_connection_send_with_reply_and_block(bus, call, 3000, &err);
	p_dbus_message_unref(call);
	if (!reply) { printf("BLE unavailable: BlueZ not reachable: %s\n", err.message); p_dbus_error_free(&err); return 1; }

	p_dbus_message_iter_init(reply, &top);
	p_dbus_message_iter_recurse(&top, &dict);           /* a{oa{sa{sv}}} */
	while (p_dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY)
	{
		DBusMessageIter entry; const char *path = NULL;
		p_dbus_message_iter_recurse(&dict, &entry);
		p_dbus_message_iter_get_basic(&entry, &path);
		if (path && strncmp(path, "/org/bluez/hci", 14) == 0 && !strchr(path + 14, '/')) { printf("adapter: %s\n", path); ++n; }
		p_dbus_message_iter_next(&dict);
	}
	p_dbus_message_unref(reply);
	printf("%d adapter(s)\n", n);
	return n ? 0 : 1;
}
