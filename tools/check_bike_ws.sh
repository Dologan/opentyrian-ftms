#!/bin/sh
# check_bike_ws.sh - LOCAL ONLY (not run by CI or `make regress`): exercises
# src/bike_ws.c, the non-blocking Flywheel bridge client.  It needs a C
# compiler, SDL3 + SDL3_net (pkg-config) and python; `live` also needs Flywheel
# (the Rust `flywheel-cli` or the Python bridge checkout with uv), so it stays
# out of the CI matrix.
#
#   tools/check_bike_ws.sh [fake|tolerance|rider|blackhole|refused|live ...]   (default: all)
#
#   fake       protocol cases against an in-process fake server (no network)
#   rider      the shared 'rider' fixtures (test/bike/protocol/rider): the bridge's active rider and FTP
#   tolerance  the shared protocol tolerance fixtures (test/bike/protocol/tolerance,
#              vendored from Flywheel's shared/protocol/fixtures/tolerance) through the same fake server
#   blackhole  10.255.255.1: the poll loop must keep ticking, no call blocks
#   refused    nothing listening: backoff 0.5, 1, 2, 4, 5 s
#   live       real Flywheel `run --source sim`: watts follow the
#              cadence sent with {"type":"sim"}; kill + restart the bridge and
#              the client must reconnect
#
# Environment: FLYWHEEL_CLI (path to the Rust app's `flywheel-cli`; when set, `live` runs it
#              instead of the Python bridge, e.g. $FLYWHEEL_DIR/app/target/debug/flywheel-cli),
#              FLYWHEEL_DIR (Flywheel checkout for the Python bridge, default /home/user/flywheel),
#              UV (default uv on PATH, else /root/.local/bin/uv), CC, BRIDGE_PORT (8791),
#              BLACKHOLE_HOST (10.255.255.1; not routable in some networks - then
#              the check still passes because it only asserts non-blocking behaviour).
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${CC:-cc}"
FLYWHEEL_DIR="${FLYWHEEL_DIR:-/home/user/flywheel}"
FLYWHEEL_CLI="${FLYWHEEL_CLI:-}"
UV="${UV:-$(command -v uv 2>/dev/null || echo /root/.local/bin/uv)}"
PORT="${BRIDGE_PORT:-8791}"
BLACKHOLE_HOST="${BLACKHOLE_HOST:-10.255.255.1}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/bike_ws.XXXXXX")" || exit 1
BRIDGE_PID=""

stop_bridge() {
	if [ -n "$BRIDGE_PID" ]; then
		kill "$BRIDGE_PID" 2>/dev/null
		wait "$BRIDGE_PID" 2>/dev/null
		BRIDGE_PID=""
	fi
}
cleanup() {
	stop_bridge
	rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

start_bridge() {
	# a rider for the bridge to announce (the live test checks the C client received it)
	mkdir -p "$WORK/data/profiles"
	printf '%s\n' '{"id":"ann","name":"Ann","ftpW":230,"ftpSource":"test","ftpUpdatedAt":"2026-10-14T10:00:00Z","ftpBike":{"id":"AA:BB","name":"Q200B"}}' \
		> "$WORK/data/profiles/ann.json"
	if [ -n "$FLYWHEEL_CLI" ]; then
		"$FLYWHEEL_CLI" run --source sim --no-record --rider ann \
			--port "$PORT" --data-dir "$WORK/data" >> "$WORK/bridge.log" 2>&1 &
	else
		(cd "$FLYWHEEL_DIR/bridge" && exec "$UV" run flywheel-bridge run --source sim --no-record --rider ann \
			--port "$PORT" --data-dir "$WORK/data") >> "$WORK/bridge.log" 2>&1 &
	fi
	BRIDGE_PID=$!
	i=0
	while [ "$i" -lt 100 ]; do
		if python3 -I -c "import socket,sys; s=socket.create_connection(('127.0.0.1',$PORT),0.2); s.close()" 2>/dev/null; then
			return 0
		fi
		sleep 0.2
		i=$((i + 1))
	done
	echo "FAIL: bridge did not start (see below)"
	tail -n 20 "$WORK/bridge.log"
	return 1
}

# shellcheck disable=SC2046
if ! "$CC" -std=gnu99 -Wall -Wextra -O1 -DWITH_NETWORK -DTARGET_UNIX \
	$(pkg-config --cflags sdl3 sdl3-net) -I"$ROOT/src" \
	-o "$WORK/bike_ws_test" "$ROOT/test/unit/bike_ws_test.c" "$ROOT/src/bike_ws.c" \
	"$ROOT/src/bike_feed.c" $(pkg-config --libs sdl3 sdl3-net) 2> "$WORK/build.log"; then
	echo "FAIL bike-ws: the test did not build"
	head -n 20 "$WORK/build.log"
	exit 1
fi

[ $# -eq 0 ] && set -- fake tolerance rider blackhole refused live
rc=0
for t in "$@"; do
	case "$t" in
	fake)
		"$WORK/bike_ws_test" fake || rc=1
		;;
	tolerance)
		"$WORK/bike_ws_test" tolerance "$ROOT/test/bike/protocol/tolerance" || rc=1
		;;
	rider)
		"$WORK/bike_ws_test" rider "$ROOT/test/bike/protocol/rider" || rc=1
		;;
	blackhole)
		"$WORK/bike_ws_test" blackhole "$BLACKHOLE_HOST" 8585 || rc=1
		;;
	refused)
		# a port nothing listens on: bind to port 0 to find a free one, release it
		FREE="$(python3 -I -c "import socket; s=socket.socket(); s.bind(('127.0.0.1',0)); print(s.getsockname()[1])")"
		"$WORK/bike_ws_test" refused "$FREE" || rc=1
		;;
	live)
		if [ -n "$FLYWHEEL_CLI" ]; then
			if [ ! -x "$FLYWHEEL_CLI" ]; then
				echo "FAIL live: FLYWHEEL_CLI=$FLYWHEEL_CLI is not an executable"
				rc=1
				continue
			fi
			echo "live: against $FLYWHEEL_CLI"
		elif [ ! -d "$FLYWHEEL_DIR/bridge" ]; then
			echo "SKIP live: no Flywheel checkout at $FLYWHEEL_DIR (set FLYWHEEL_CLI or FLYWHEEL_DIR)"
			continue
		fi
		start_bridge || { rc=1; continue; }
		rm -f "$WORK/a_done"
		"$WORK/bike_ws_test" live 127.0.0.1 "$PORT" "$WORK/a_done" > "$WORK/live.out" 2>&1 &
		TEST_PID=$!
		i=0
		while [ ! -f "$WORK/a_done" ] && kill -0 "$TEST_PID" 2>/dev/null && [ "$i" -lt 600 ]; do
			sleep 0.2
			i=$((i + 1))
		done
		if [ -f "$WORK/a_done" ]; then
			echo "-- killing the bridge"
			stop_bridge
			sleep 3
			echo "-- restarting the bridge"
			start_bridge || rc=1
		fi
		wait "$TEST_PID" || rc=1
		cat "$WORK/live.out"
		stop_bridge
		;;
	*)
		echo "unknown test '$t'" >&2
		rc=2
		;;
	esac
done

if [ "$rc" -eq 0 ]; then
	echo "PASS bike-ws"
else
	echo "FAIL bike-ws"
fi
exit "$rc"
