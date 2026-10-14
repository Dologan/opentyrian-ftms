#!/bin/sh
# play-with-bike.sh: start the Flywheel bridge, wait for it, play Tyrian with the bike,
# then stop the bridge when the game exits.
#
# Usage: play-with-bike.sh [--flywheel PATH] [--source sim|ble] [--game PATH] [-- GAME_ARGS...]
#
#   --flywheel PATH  Flywheel checkout. Default: $FLYWHEEL_DIR, else ../flywheel next to this repo.
#   --source SRC     sim: simulated bike, no hardware. ble: real FTMS bike over Bluetooth
#                    (the bridge's default ftms source). Default: the bridge's default.
#   --game PATH      game binary. Default: ./opentyrian at the repo root, or $OPENTYRIAN_BIN.
#   -- GAME_ARGS...  everything after -- is passed to the game unchanged.
#
# The bike is requested with --ftms-bridge=127.0.0.1:8787 on the command line only; the game's
# defaults are unchanged. Requires uv and curl.
#
# Environment:
#   FLYWHEEL_DIR     Flywheel checkout (same as --flywheel)
#   OPENTYRIAN_BIN   game binary (same as --game)

set -eu

usage() {
    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
}

fail() {
    echo "play-with-bike: $*" >&2
    exit 1
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PORT=8787
BRIDGE_ADDR="127.0.0.1:${PORT}"
HEALTH_URL="http://127.0.0.1:${PORT}/healthz"
LOG_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/opentyrian-ftms/logs"
DATA_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/opentyrian-ftms/bridge-data"

FLYWHEEL="${FLYWHEEL_DIR:-}"
SOURCE_OPT=""
GAME="${OPENTYRIAN_BIN:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --flywheel)
            [ $# -ge 2 ] || fail "--flywheel needs a path"
            FLYWHEEL="$2"; shift 2 ;;
        --source)
            [ $# -ge 2 ] || fail "--source needs sim or ble"
            SOURCE_OPT="$2"; shift 2 ;;
        --game)
            [ $# -ge 2 ] || fail "--game needs a path"
            GAME="$2"; shift 2 ;;
        -h|--help)
            usage; exit 0 ;;
        --)
            shift; break ;;
        *)
            fail "unknown option: $1 (game options go after --)" ;;
    esac
done

case "$SOURCE_OPT" in
    "")    BRIDGE_SOURCE="" ;;
    sim)   BRIDGE_SOURCE="sim" ;;
    ble)   BRIDGE_SOURCE="ftms" ;;
    *)     fail "--source must be sim or ble" ;;
esac

if [ -z "$FLYWHEEL" ]; then
    FLYWHEEL="$ROOT/../flywheel"
fi
[ -f "$FLYWHEEL/bridge/pyproject.toml" ] \
    || fail "no Flywheel checkout at $FLYWHEEL (use --flywheel PATH or set FLYWHEEL_DIR)"
FLYWHEEL="$(cd "$FLYWHEEL" && pwd)"
BRIDGE_DIR="$FLYWHEEL/bridge"

if [ -z "$GAME" ]; then
    GAME="$ROOT/opentyrian"
fi
case "$GAME" in
    /*) ;;
    *)  GAME="$(pwd)/$GAME" ;;
esac
[ -x "$GAME" ] || fail "game binary not found or not executable: $GAME (build it, or use --game PATH)"

command -v uv >/dev/null 2>&1 || fail "needs uv (https://docs.astral.sh/uv/)"
command -v curl >/dev/null 2>&1 || fail "needs curl"

if curl -fsS --max-time 2 "$HEALTH_URL" >/dev/null 2>&1; then
    fail "something already answers at $HEALTH_URL; quit the other bridge first"
fi

if command -v setsid >/dev/null 2>&1; then
    SETSID="setsid"
else
    SETSID=""
fi

BRIDGE_PID=""
GAME_PID=""

# Signal the bridge's process group when setsid gave it one, else the bridge process alone.
signal_bridge() {
    if [ -n "$SETSID" ]; then
        kill -s "$1" -- "-$BRIDGE_PID" 2>/dev/null || kill -s "$1" "$BRIDGE_PID" 2>/dev/null || true
    else
        kill -s "$1" "$BRIDGE_PID" 2>/dev/null || true
    fi
}

stop_bridge() {
    [ -n "$BRIDGE_PID" ] || return 0
    signal_bridge TERM
    # Escalate if the bridge ignores TERM. The watchdog is killed once the bridge is reaped.
    ( sleep 5; signal_bridge KILL ) &
    watchdog=$!
    wait "$BRIDGE_PID" 2>/dev/null || true
    kill "$watchdog" 2>/dev/null || true
    wait "$watchdog" 2>/dev/null || true
    BRIDGE_PID=""
    echo "play-with-bike: bridge stopped"
}

cleanup() {
    trap - EXIT INT TERM HUP
    if [ -n "$GAME_PID" ] && kill -0 "$GAME_PID" 2>/dev/null; then
        kill -TERM "$GAME_PID" 2>/dev/null || true
        wait "$GAME_PID" 2>/dev/null || true
    fi
    stop_bridge
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP

mkdir -p "$LOG_DIR" "$DATA_DIR"
LOG_FILE="$LOG_DIR/bridge-$(date +%Y%m%d-%H%M%S).log"

# Bridge options go into single words so the game arguments in "$@" stay untouched.
SOURCE_FLAG=""
if [ -n "$BRIDGE_SOURCE" ]; then
    SOURCE_FLAG="--source=$BRIDGE_SOURCE"
fi

echo "play-with-bike: starting bridge from $BRIDGE_DIR (log: $LOG_FILE)"
# shellcheck disable=SC2086  # $SETSID and $SOURCE_FLAG are intentionally bare words or empty
PYTHONUNBUFFERED=1 $SETSID uv run --project "$BRIDGE_DIR" flywheel-bridge run \
    --data-dir "$DATA_DIR" $SOURCE_FLAG >"$LOG_FILE" 2>&1 &
BRIDGE_PID=$!

waited=0
until curl -fsS --max-time 2 "$HEALTH_URL" >/dev/null 2>&1; do
    if ! kill -0 "$BRIDGE_PID" 2>/dev/null; then
        fail "the bridge exited during startup. See $LOG_FILE"
    fi
    if [ "$waited" -ge 30 ]; then
        fail "the bridge did not answer $HEALTH_URL within 30 s. See $LOG_FILE"
    fi
    waited=$((waited + 1))
    sleep 1
done
echo "play-with-bike: bridge healthy at $HEALTH_URL (pid $BRIDGE_PID)"

echo "play-with-bike: launching $GAME --ftms-bridge=$BRIDGE_ADDR $*"
cd "$ROOT"
GAME_STATUS=0
"$GAME" "--ftms-bridge=$BRIDGE_ADDR" "$@" &
GAME_PID=$!
wait "$GAME_PID" || GAME_STATUS=$?
GAME_PID=""
exit "$GAME_STATUS"
