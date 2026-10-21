#!/bin/sh
# check_bike_effort.sh - build and run test/unit/bike_effort_test.c: the fixed-point
# effort engine (src/bike_effort.c) and feed reader (src/bike_feed.c) against golden
# golden output (test/bike/effort/golden; feeds in test/bike/effort/feeds, both owned
# by this repo; the goldens are written by this engine, tools/bike/gen_effort_golden.sh), plus a
# determinism self-check against committed digests.  No SDL, no game data.
# First it verifies that the vendored Flywheel files (the protocol fixtures) still
# match test/bike/VENDORED_FROM: they are Flywheel's data, changed there and
# re-synced with tools/bike/vendor.py.
# regress.sh runs it with the other structural checks.
#   tools/check_bike_effort.sh            run the check
#   tools/check_bike_effort.sh --update   rewrite test/bike/effort/c_digests.txt
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${CC:-cc}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/bike_effort_test.XXXXXX")" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

if ! python3 -I "$ROOT/tools/bike/vendor.py" verify > "$WORK/vendor.log" 2>&1; then
	cat "$WORK/vendor.log"
	echo "FAIL bike-effort: vendored Flywheel files differ from test/bike/VENDORED_FROM"
	exit 1
fi

ARG=""
[ "${1:-}" = "--update" ] && ARG="--update-digests"


if ! "$CC" -std=iso9899:1999 -pedantic -Wall -Wextra -Wno-format-truncation -Werror -O1 -I"$ROOT/src" \
	-o "$WORK/bike_effort_test" "$ROOT/test/unit/bike_effort_test.c" \
	"$ROOT/src/bike_effort.c" "$ROOT/src/bike_feed.c" -lm 2> "$WORK/build.log"; then
	echo "FAIL bike-effort: the check did not build"
	sed 's/^/  /' "$WORK/build.log" | head -n 20
	exit 1
fi

# shellcheck disable=SC2086
if ! "$WORK/bike_effort_test" "$ROOT" $ARG > "$WORK/run.log" 2>&1; then
	echo "FAIL bike-effort"
	grep -v '^[a-z0-9_-]* *ticks' "$WORK/run.log" | head -n 30
	exit 1
fi
cat "$WORK/run.log" | sed 's/^/  /'
echo "PASS bike-effort"
