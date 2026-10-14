#!/bin/bash
# check_bike.sh -- guard for the exercise-bike core (src/bike*.c).
#
#   check_bike.sh BIN DATA_DIR OUT_DIR [--quick|--update]
#
# Every case runs a scripted level (--regress-script, fixed seed, no input) with a
# scripted feed from test/bike/feeds (--regress-bike-feed) and compares frame hashes,
# state hashes and the per-tick bike log:
#   - flag off, and every toggle explicitly off, equal the stock baseline (frames
#     and state hashes); a feed with the master or generator off leaves frames alone;
#   - the bike cases (constant 150 W, ramp, dropout -> link-loss blend, stop, FTP 50
#     and 500, the two Q200B rides) run twice with identical output, differ from the
#     stock run, satisfy their log assertions, and match the digests committed in
#     test/bike/regress.txt (--update rewrites that file).
#   - UI: the Bike and Controls setup screens, the in-game bike overlay and the HUD
#     (Classic, Modern 16:9, a surge ride) are snapshotted and hashed; the HUD on/off
#     state hashes must be identical.
#   --quick runs the flag-off, all-off and constant-150 W determinism subset only.
# Only counts and hashes are read from the logs.
set -uo pipefail

if [ "$#" -lt 3 ]; then
	echo "usage: $0 BIN DATA_DIR OUT_DIR [--quick|--update]" >&2
	exit 2
fi
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN=$1 DATA_DIR=$2 OUT=$3
MODE=${4:-full}
BASE="$ROOT/test/bike/regress.txt"
FEEDS="$ROOT/test/bike/feeds"
mkdir -p "$OUT"

fail() { echo "FAIL bike-$1: $2"; exit 1; }
sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum; else shasum -a 256; fi | cut -d' ' -f1; }

SCEN=(--regress-script=1:3 --regress-seed=32402394 --regress-detail=2)
run() { # label frames [args...]
	local l=$1 n=$2; shift 2
	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy "$BIN" --data="$DATA_DIR" "${SCEN[@]}" --regress-frames="$n" \
		--regress-out="$OUT/$l.f" --regress-state-out="$OUT/$l.s" "$@" >"$OUT/$l.log" 2>&1 \
		|| { tail -n 5 "$OUT/$l.log"; fail "$l" "the run exited with $?"; }
}
digest() { cat "$OUT/$1.f" "$OUT/$1.s" "$OUT/$1.blog" 2>/dev/null | sha; }
LABELS=()
ALL_OFF="enabled=0,generator=0,hud=0,surge_superbomb=0,surge_sidekick=0,surge_special=0,surge_shield=0,autofire=off,air_mouse=0,pointer_smoothing=0,touch=0,remote_buttons=0"

# --- flag off == stock ---------------------------------------------------------
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy "$BIN" --data="$DATA_DIR" --regress-demo=3 --regress-detail=4 \
	--regress-out="$OUT/demo3.f" >"$OUT/demo3.log" 2>&1 || fail flag-off "demo3 run failed"
cmp -s "$OUT/demo3.f" "$ROOT/test/regress/demo3-d4.txt" || fail flag-off "demo3-d4 differs from its committed baseline"
echo "PASS bike-flag-off: demo3-d4 matches the committed baseline"

run off 600
run alloff 600 --regress-bike-set="$ALL_OFF"
cmp -s "$OUT/off.f" "$OUT/alloff.f" && cmp -s "$OUT/off.s" "$OUT/alloff.s" || fail all-off "explicit all-off differs from the stock run"
echo "PASS bike-all-off: every toggle explicitly off equals the stock run (frames and state)"

run feedoff 600 --regress-bike-feed=const:150 --regress-bike-set=enabled=0
cmp -s "$OUT/off.f" "$OUT/feedoff.f" || fail master-off "a feed with the master off changed the frames"
run gen0 600 --regress-bike-feed=const:150 --regress-bike-set=generator=0
cmp -s "$OUT/off.f" "$OUT/gen0.f" || fail generator-off "a feed with the generator off changed the frames"
echo "PASS bike-toggles: master off and generator off leave the frames stock"

run c150 600 --regress-bike-feed=const:150 --regress-bike-log="$OUT/c150.blog"
run c150b 600 --regress-bike-feed=const:150 --regress-bike-log="$OUT/c150b.blog"
for x in f s blog; do cmp -s "$OUT/c150.$x" "$OUT/c150b.$x" || fail determinism "const 150 W is not deterministic ($x)"; done
cmp -s "$OUT/off.f" "$OUT/c150.f" && fail observable "const 150 W frames equal the stock run"
echo "PASS bike-const150: deterministic, differs from stock"
LABELS+=(c150)

if [ "$MODE" = "--quick" ]; then
	echo "PASS bike (quick)"
	exit 0
fi

# --- full cases ----------------------------------------------------------------
# column helpers over the per-tick log: tick link moving surging effort_w norm income add power w af_on af_held events
col_min() { awk -v c="$2" -v from="${3:-0}" '$1>=from { if (n++==0 || $c<m) m=$c } END { print m }' "$OUT/$1.blog"; }
col_max() { awk -v c="$2" -v from="${3:-0}" '$1>=from { if (n++==0 || $c>m) m=$c } END { print m }' "$OUT/$1.blog"; }
col_last() { awk -v c="$2" 'END { print $c }' "$OUT/$1.blog"; }

case_run() { # label frames feed [extra args]; runs twice, checks determinism and observability
	local l=$1 n=$2 f=$3; shift 3
	run "$l" "$n" --regress-bike-feed="$f" --regress-bike-log="$OUT/$l.blog" "$@"
	run "${l}b" "$n" --regress-bike-feed="$f" --regress-bike-log="$OUT/${l}b.blog" "$@"
	for x in f s blog; do cmp -s "$OUT/$l.$x" "$OUT/${l}b.$x" || fail "$l" "not deterministic ($x)"; done
	[ -s "$OUT/$l.blog" ] || fail "$l" "the bike log is empty"
	LABELS+=("$l")
}
run off6500 6500
run off12000 12000

case_run ramp 6500 "$FEEDS/ramp0_300.csv"
cmp -s "$OUT/off6500.f" "$OUT/ramp.f" && fail ramp "frames equal the stock run"
[ "$(col_max ramp 6)" -gt "$(col_min ramp 6 200)" ] || fail ramp "effort norm never rose"
echo "PASS bike-ramp: deterministic, effort norm rises ($(col_min ramp 6 200) -> $(col_max ramp 6))"

case_run dropout 2400 "$FEEDS/dropout.csv"
[ "$(col_min dropout 7 100)" -ge 524288 ] || fail dropout "income fell below the floor"
[ "$(col_max dropout 10 800)" -eq 65536 ] || fail dropout "the blend never reached stock during the 5 s link loss"
[ "$(col_last dropout 10)" -lt 65536 ] || fail dropout "the blend did not return to the bike after the link came back"
echo "PASS bike-dropout: floor held, blend reached stock (w=65536) and returned"

case_run stop 1900 "$FEEDS/stop.csv"
[ "$(col_max stop 3)" -eq 1 ] && [ "$(col_last stop 3)" -eq 0 ] || fail stop "the rider did not go from moving to stopped"
[ "$(col_last stop 6)" -eq 0 ] || fail stop "a stopped rider still has effort"
[ "$(col_last stop 7)" -eq "$(col_min stop 7 100)" ] || fail stop "stopped income is not the minimum (max(F, bias*G))"
echo "PASS bike-stop: moving -> stopped, income falls to max(F, bias*G)"

case_run ftp50 6500 "$FEEDS/ramp0_300.csv" --regress-bike-set=ftp=50
case_run ftp500 6500 "$FEEDS/ramp0_300.csv" --regress-bike-set=ftp=500
[ "$(col_max ftp50 6)" -eq 65536 ] || fail ftp50 "FTP 50 W never saturated the effort norm"
[ "$(col_max ftp500 6)" -lt 32768 ] || fail ftp500 "FTP 500 W reached half effort at 300 W"
cmp -s "$OUT/ftp50.blog" "$OUT/ftp500.blog" && fail ftp "FTP 50 and 500 produced the same log"
echo "PASS bike-ftp-edges: 50 W saturates, 500 W stays low, deterministic"

case_run ride1 9000 "$FEEDS/q200b-ride1.csv"
case_run ride2 12000 "$FEEDS/q200b-ride2.csv"
cmp -s "$OUT/off12000.f" "$OUT/ride2.f" && fail ride2 "frames equal the stock run"
echo "PASS bike-rides: both Q200B rides deterministic and observable"

# --- UI snapshots: setup menu screens and the HUD -------------------------------
# The BMPs are hashed into test/bike/regress.txt (snap-*); --update also renders them
# as PNGs under doc/ftms/screens when python3 with PIL is available.
SNAPS=()
snap_screen() { # label screen
	local l=$1 s=$2
	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy "$BIN" --data="$DATA_DIR" --regress-screen="$s" \
		--regress-bike-set=enabled=1,surge_special=1 --regress-frames=40 --regress-snapshot=30:"$OUT/$l.bmp" \
		>"$OUT/$l.log" 2>&1 || fail "$l" "the screen run exited with $?"
	[ -s "$OUT/$l.bmp" ] || fail "$l" "no snapshot was written"
	SNAPS+=("$l")
}
snap_screen snap-menu bike-menu
snap_screen snap-menu-nav bike-menu-nav
snap_screen snap-menu-edit bike-menu-edit
cmp -s "$OUT/snap-menu.bmp" "$OUT/snap-menu-nav.bmp" && fail snap-menu "the key script did not change the menu"
echo "PASS bike-menu-screens: Setup menu, stepper walk and address edit render and differ"

# HUD on vs off: presentation only, so the state hashes must be identical.
HUDARGS=(--regress-bike-feed=const:230,92 --regress-modern --regress-aspect=16:9)
run hud0 700 "${HUDARGS[@]}" --regress-bike-set=hud=0
run hud1 700 "${HUDARGS[@]}" --regress-bike-set=hud=1 --regress-snapshot=650:"$OUT/snap-hud-modern.bmp"
cmp -s "$OUT/hud0.s" "$OUT/hud1.s" || fail hud "the HUD changed the state hashes"
cmp -s "$OUT/hud0.f" "$OUT/hud1.f" && fail hud "the HUD did not change the frames"
run hudc 700 --regress-bike-feed=const:230,92 --regress-bike-set=hud=1 --regress-snapshot=650:"$OUT/snap-hud-classic.bmp"
run hudc0 700 --regress-bike-feed=const:230,92 --regress-bike-set=hud=0
cmp -s "$OUT/hudc0.s" "$OUT/hudc.s" || fail hud-classic "the Classic HUD changed the state hashes"
cmp -s "$OUT/hudc0.f" "$OUT/hudc.f" && fail hud-classic "the Classic HUD did not change the frames"
run hudsurge 2400 "${HUDARGS[@]}" --regress-bike-feed="$FEEDS/surge.csv" --regress-bike-set=hud=1 \
	--regress-snapshot=2350:"$OUT/snap-hud-surge.bmp"
run ingame 700 --regress-menu=bike --regress-bike-set=enabled=1 --regress-snapshot=699:"$OUT/snap-ingame-bike.bmp"
for l in snap-hud-modern snap-hud-classic snap-hud-surge snap-ingame-bike; do
	[ -s "$OUT/$l.bmp" ] || fail "$l" "no snapshot was written"
	SNAPS+=("$l")
done
echo "PASS bike-hud: HUD on/off leave the state hashes alone, change the frames, snapshots written"

# --- committed digests ---------------------------------------------------------
tmp="$OUT/regress.new"
: >"$tmp"
for l in "${LABELS[@]}"; do echo "$l $(digest "$l")" >>"$tmp"; done
for l in "${SNAPS[@]}"; do echo "$l $(sha <"$OUT/$l.bmp")" >>"$tmp"; done
if [ "$MODE" = "--update" ]; then
	cp "$tmp" "$BASE"
	echo "bike digests written to $BASE"
	if python3 -c "import PIL" >/dev/null 2>&1; then
		mkdir -p "$ROOT/doc/ftms/screens"
		for l in "${SNAPS[@]}"; do
			python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" \
				"$OUT/$l.bmp" "$ROOT/doc/ftms/screens/${l#snap-}.png"
		done
		echo "screens written to doc/ftms/screens"
	fi
elif ! diff -u "$BASE" "$tmp" >"$OUT/digests.diff"; then
	head -n 20 "$OUT/digests.diff"
	fail digests "output differs from test/bike/regress.txt (intentional change: tools/check_bike.sh BIN DATA OUT --update)"
else
	echo "PASS bike-digests: ${#LABELS[@]} cases match test/bike/regress.txt"
fi
echo "PASS bike"
