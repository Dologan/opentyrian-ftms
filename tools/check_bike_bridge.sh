#!/bin/bash
# check_bike_bridge.sh - LOCAL ONLY (not run by CI or `make regress`; it needs Python, uv and the
# Flywheel checkout, and takes about three minutes of real time).
#
#   tools/check_bike_bridge.sh [BIN [DATA_DIR]]
#
# T4.2 integration check.  It starts `flywheel-bridge run --source replay` on a recorded Q200B ride,
# runs the game headless (SDL dummy drivers) in a real-time level with the bike master switch and
# generator on and the live bridge as the source, with --ftms-log writing the per-second calibration
# CSV, and asserts:
#   1. the power refill tracks the bike effort (correlation, and high effort refills more than low);
#   2. after the bridge is killed the refill blends back to the stock generator within a few seconds;
#   3. after the bridge is restarted the client reconnects and the bike drives the refill again.
#
# Regress mode never opens the bridge (by design), so the level is started with the test-only option
# --ftms-test-level=E:L, which runs the regress script's level start path WITHOUT regress mode and
# forces the master switch and the generator on.  It is not for players.  The user's config and saves
# go to a throw-away HOME.
#
# Environment: FLYWHEEL_DIR (default /home/user/flywheel), UV (default uv on PATH, else
#   /root/.local/bin/uv), RIDE (a raw ride .jsonl; default q200b-run1 from the Flywheel fixtures),
#   BRIDGE_PORT (8793), LEVEL (1:3), KEEP=1 keeps the work directory.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-$ROOT/opentyrian}"
DATA="${2:-$ROOT/data}"
FLYWHEEL_DIR="${FLYWHEEL_DIR:-/home/user/flywheel}"
UV="${UV:-$(command -v uv 2>/dev/null || echo /root/.local/bin/uv)}"
RIDE="${RIDE:-$FLYWHEEL_DIR/shared/fixtures/rides/q200b-run1.raw.jsonl}"
PORT="${BRIDGE_PORT:-8793}"
LEVEL="${LEVEL:-1:3}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/bike_bridge.XXXXXX")" || exit 1
CSV="$WORK/calib.csv"
BRIDGE_PID="" GAME_PID=""

fail() { echo "FAIL bike-bridge: $*"; echo "(work dir kept: $WORK)"; KEEP=1; exit 1; }
stop_bridge() {
	if [ -n "$BRIDGE_PID" ]; then
		kill -TERM -- "-$BRIDGE_PID" 2>/dev/null || kill "$BRIDGE_PID" 2>/dev/null
		wait "$BRIDGE_PID" 2>/dev/null
		BRIDGE_PID=""
	fi
}
cleanup() {
	stop_bridge
	[ -n "$GAME_PID" ] && kill "$GAME_PID" 2>/dev/null && wait "$GAME_PID" 2>/dev/null
	[ "${KEEP:-0}" = 1 ] || rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

[ -x "$BIN" ] || fail "game binary '$BIN' not found (build it first)"
[ -d "$DATA" ] || fail "data directory '$DATA' not found"
[ -f "$RIDE" ] || fail "ride '$RIDE' not found"
[ -x "$UV" ] || fail "uv not found (set UV=)"

start_bridge() {
	local i=0
	(cd "$FLYWHEEL_DIR/bridge" && exec setsid "$UV" run flywheel-bridge run --source replay --replay-file "$RIDE" \
		--loop --no-record --port "$PORT" --data-dir "$WORK/bridge-data") >>"$WORK/bridge.log" 2>&1 &
	BRIDGE_PID=$!
	while [ "$i" -lt 150 ]; do
		python3 -I -c "import socket; socket.create_connection(('127.0.0.1',$PORT),0.2).close()" 2>/dev/null && return 0
		sleep 0.2
		i=$((i + 1))
	done
	tail -n 20 "$WORK/bridge.log"
	fail "the bridge did not start"
}
rows() { [ -f "$CSV" ] && echo $(($(wc -l <"$CSV") - 1)) || echo 0; } # data rows written so far
wait_rows() { # count timeout_s: wait until the CSV has that many rows
	local n=$1 t=$2 i=0
	while [ "$(rows)" -lt "$n" ]; do
		kill -0 "$GAME_PID" 2>/dev/null || { tail -n 15 "$WORK/game.log"; fail "the game exited early"; }
		[ "$i" -ge $((t * 5)) ] && fail "timed out waiting for $n CSV rows (have $(rows))"
		sleep 0.2
		i=$((i + 1))
	done
}
count_link() { [ -f "$CSV" ] || { echo 0; return; }; awk -F, -v from="$1" 'NR>1 && NR-1>from && $4=="Streaming"{n++} END{print n+0}' "$CSV"; }
wait_streaming() { # since_row count timeout_s
	local i=0
	while [ "$(count_link "$1")" -lt "$2" ]; do
		kill -0 "$GAME_PID" 2>/dev/null || { tail -n 15 "$WORK/game.log"; fail "the game exited early"; }
		[ "$i" -ge $(($3 * 5)) ] && fail "the link never reached Streaming ($2 rows within $3 s after row $1)"
		sleep 0.2
		i=$((i + 1))
	done
}

echo "bridge: replaying $(basename "$RIDE") on port $PORT"
start_bridge
mkdir -p "$WORK/home"
HOME="$WORK/home" XDG_CONFIG_HOME="$WORK/home/.config" XDG_DATA_HOME="$WORK/home/.local/share" \
	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy "$BIN" --variant=2.1 --data="$DATA" --ftms-test-level="$LEVEL" \
	--ftms-bridge="127.0.0.1:$PORT" --ftms-log="$CSV" >"$WORK/game.log" 2>&1 &
GAME_PID=$!

# Phase A: ~120 s of the ride (220 W, then ~14 W, then 400-800 W in the Q200B run1 recording).
wait_streaming 0 5 40
echo "link: streaming"
A_START=$(rows)
wait_rows $((A_START + 118)) 160
KILL_ROW=$(rows)
echo "kill: stopping the bridge after $KILL_ROW CSV rows"
stop_bridge
wait_rows $((KILL_ROW + 12)) 40
RESTART_ROW=$(rows)
echo "restart: starting the bridge after $RESTART_ROW CSV rows"
start_bridge
wait_streaming "$RESTART_ROW" 12 60
wait_rows $(( $(rows) + 3 )) 20
kill -0 "$GAME_PID" 2>/dev/null || fail "the game is no longer running"
kill "$GAME_PID" 2>/dev/null; wait "$GAME_PID" 2>/dev/null; GAME_PID=""
cp "$CSV" "$WORK/calib.final.csv"

python3 -I - "$CSV" "$A_START" "$KILL_ROW" "$RESTART_ROW" <<'PY' || fail "assertions (see above); CSV: $CSV"
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
a0, kill, restart = (int(x) for x in sys.argv[2:5])
f = lambda r, k: float(r[k])

def pearson(xs, ys):
    n = len(xs); mx = sum(xs) / n; my = sum(ys) / n
    sx = sum((x - mx) ** 2 for x in xs) ** .5; sy = sum((y - my) ** 2 for y in ys) ** .5
    return 0.0 if sx == 0 or sy == 0 else sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / (sx * sy)

def ok(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond:
        sys.exit(1)

# 1. refill tracks effort (the mapping is clamp(G*(0.5+norm), floor, 1.5G) of the effort norm; skip 6 s so the blend from stock has settled)
A = [r for r in rows[a0 + 6:kill] if r["link"] == "Streaming"]
ok(len(A) >= 60, "phase A has %d streaming rows" % len(A))
eff = [f(r, "effort_w") for r in A]; ref = [f(r, "refill_per_frame") for r in A]; w = [f(r, "watts") for r in A]
nrm = [f(r, "effort_norm") for r in A]
c = pearson(nrm, ref)
hi = [y for x, y in zip(eff, ref) if x > 300]; lo = [y for x, y in zip(eff, ref) if x < 60]
print("  watts %.0f..%.0f, effort_w %.0f..%.0f, refill %.1f..%.1f, corr(effort_norm, refill)=%.3f, corr(watts, refill)=%.3f"
      % (min(w), max(w), min(eff), max(eff), min(ref), max(ref), c, pearson(w, ref)))
ok(len(hi) >= 5 and len(lo) >= 5, "the ride covers both low and high effort (%d low, %d high rows)" % (len(lo), len(hi)))
ok(c >= 0.9, "refill correlates with the bike effort (r=%.3f >= 0.9)" % c)
ok(sum(hi) / len(hi) >= sum(lo) / len(lo) + 5, "high effort refills %.1f/frame vs %.1f at low effort"
   % (sum(hi) / len(hi), sum(lo) / len(lo)))

# 2. link loss: blend back to stock
B = rows[kill:restart]
j = next((i for i, r in enumerate(B) if f(r, "blend") >= 0.99), None)
ok(j is not None and j <= 6, "after the bridge was killed the refill is back on stock in %s s (blend>=0.99, limit 6)" % j)
st = B[j + 1]  # the second after: the row where the blend reached 1 averages the ramp
ok(abs(f(st, "refill_per_frame") - f(st, "stock_power_add")) < 0.1,
   "one second on, the refill (%.2f) equals the stock generator (%.2f)" % (f(st, "refill_per_frame"), f(st, "stock_power_add")))
ok(all(r["link"] != "Streaming" for r in B[3:]), "the link left Streaming (%s)" % B[-1]["link"])

# 3. reconnect
C = rows[restart:]
k = next((i for i, r in enumerate(C) if r["link"] == "Streaming"), None)
ok(k is not None, "the client reconnected and streams again %s s after the restart" % k)
back = [r for r in C[k:] if f(r, "blend") < 0.5]
ok(len(back) >= 1, "the bike drives the refill again after the reconnect (blend %.2f at the end)" % f(C[-1], "blend"))
PY
echo "PASS bike-bridge"
