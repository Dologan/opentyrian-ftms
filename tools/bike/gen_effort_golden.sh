#!/bin/sh
# gen_effort_golden.sh - rebuild the effort goldens (test/bike/effort/golden) from this
# repo's own C engine (src/bike_effort.c) and the feeds in test/bike/effort/feeds.
#
#   tools/bike/gen_effort_golden.sh [OUTDIR]   write the goldens and manifest.txt
#                                              (default OUTDIR: test/bike/effort/golden)
#
# Run it ONLY after an intentional change of the engine or of its defaults, then review
# `git diff test/bike/effort` (every changed golden must be a case the change should touch),
# run `tools/check_bike_effort.sh --update` for test/bike/golden/c_digests.txt and
# `tools/check_bike.sh BIN DATA OUT --update` for the game-level digests.  The case list
# (name, feed, FTP) is the manifest's; its ticks column is recomputed (last feed row + 400).
# One case: bike_effort_test ROOT --dump-effort FEED FTP_W [TICKS]  (CSV to stdout).
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CC="${CC:-cc}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/bike_effort_gen.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT INT TERM

"$CC" -std=iso9899:1999 -pedantic -Wall -Wextra -Werror -O1 -I"$ROOT/src" \
	-o "$WORK/bike_effort_test" "$ROOT/test/unit/bike_effort_test.c" \
	"$ROOT/src/bike_effort.c" "$ROOT/src/bike_feed.c" -lm
OUT="${1:-$ROOT/test/bike/effort/golden}"
mkdir -p "$OUT"
"$WORK/bike_effort_test" "$ROOT" --gen-golden "$OUT"
