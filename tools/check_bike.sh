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
#   - surge effects (T3.1) and the in-play auto-fire toggle with its energy reserve (T3b.1) run
#     with injected events (--regress-bike-events, test/bike/events): each surge effect on vs off,
#     auto-fire switched on and off mid-level, the reserve under low income, manual fire ignoring
#     the reserve, and the settings off leaving the run stock.  Coverage lines ("Bike effects:",
#     "Bike autofire:", "Bike end state:") prove each path really ran.
#   - the older cases pin surge_superbomb=0 (its default is on), so their digests are the ones
#     committed before the surge effects existed.
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

case_run ramp 6500 "$FEEDS/ramp0_300.csv" --regress-bike-set=surge_superbomb=0
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

case_run ftp50 6500 "$FEEDS/ramp0_300.csv" --regress-bike-set=ftp=50,surge_superbomb=0
case_run ftp500 6500 "$FEEDS/ramp0_300.csv" --regress-bike-set=ftp=500,surge_superbomb=0
[ "$(col_max ftp50 6)" -eq 65536 ] || fail ftp50 "FTP 50 W never saturated the effort norm"
[ "$(col_max ftp500 6)" -lt 32768 ] || fail ftp500 "FTP 500 W reached half effort at 300 W"
cmp -s "$OUT/ftp50.blog" "$OUT/ftp500.blog" && fail ftp "FTP 50 and 500 produced the same log"
echo "PASS bike-ftp-edges: 50 W saturates, 500 W stays low, deterministic"

case_run ride1 9000 "$FEEDS/q200b-ride1.csv" --regress-bike-set=surge_superbomb=0
case_run ride2 12000 "$FEEDS/q200b-ride2.csv" --regress-bike-set=surge_superbomb=0
cmp -s "$OUT/off12000.f" "$OUT/ride2.f" && fail ride2 "frames equal the stock run"
echo "PASS bike-rides: both Q200B rides deterministic and observable"

# --- surge effects (T3.1) ------------------------------------------------------
EVD="$ROOT/test/bike/events"
cov() { sed -n "s/.*$2=\([0-9][0-9]*\).*/\1/p" "$OUT/$1.log" | head -n 1; } # label key: from the coverage lines
differs() { ! cmp -s "$OUT/$1.f" "$OUT/$2.f" || ! cmp -s "$OUT/$1.s" "$OUT/$2.s"; }
SG="$FEEDS/surge.csv"
sg() { case_run "$1" 3000 "$SG" --regress-bike-events="$EVD/surge_gear.csv" --regress-bike-set="$2"; }
sg sgoff surge_superbomb=0
sg sgbomb surge_superbomb=1
sg sgside surge_superbomb=0,surge_sidekick=1
sg sgspec surge_superbomb=0,surge_special=1
sg sgshield surge_superbomb=0,surge_shield=1
sg sgall surge_superbomb=1,surge_sidekick=1,surge_special=1,surge_shield=1
for k in superbombs shield_fast sidekick_fast special_extra; do
	[ "$(cov sgoff $k)" = 0 ] || fail surge "all surge effects off still changed $k"
done
[ "$(cov sgbomb superbombs)" -ge 1 ] && [ "$(cov sgbomb superbombs)" -le 5 ] || fail surge "no superbomb was granted on overdrive-ready"
[ "$(cov sgside sidekick_fast)" -ge 1 ] || fail surge "the sidekick charge never ran faster"
[ "$(cov sgspec special_extra)" -ge 1 ] || fail surge "the special recharge never ran faster"
[ "$(cov sgshield shield_fast)" -ge 1 ] || fail surge "the shield regen interval never shortened"
for k in sgbomb:shield_fast sgbomb:sidekick_fast sgbomb:special_extra sgside:superbombs sgspec:superbombs sgshield:superbombs; do
	[ "$(cov "${k%%:*}" "${k##*:}")" = 0 ] || fail surge "${k%%:*} touched ${k##*:} with that effect off"
done
for l in sgbomb sgside sgspec sgshield sgall; do differs sgoff "$l" || fail surge "$l is not observable against the all-off run"; done
[ "$(cov sgbomb superbombs_cooled)" -ge 1 ] || fail surge "the superbomb cooldown never held a grant back"
grep -q "end state: .*superbombs=$(cov sgbomb superbombs) " "$OUT/sgbomb.log" || fail surge "end-state superbombs differ from the grants"
echo "PASS bike-surge: superbombs +$(cov sgbomb superbombs), sidekick x$(cov sgside sidekick_fast), special x$(cov sgspec special_extra), shield x$(cov sgshield shield_fast) (each only with its toggle, deterministic, observable)"

# Balance: a long Q200B ride fires 26 overdrive events in about 3 min; the cooldown (60 s) and cap (5) must hold the grants
# to one per minute of play, and the cooldown setting 0 must lift the rate limit.
case_run sgride 12000 "$FEEDS/q200b-ride2.csv" --regress-bike-set=surge_superbomb=1
case_run sgride0 12000 "$FEEDS/q200b-ride2.csv" --regress-bike-set=surge_superbomb=1,superbomb_cooldown=0
[ "$(cov sgride superbombs)" -ge 2 ] && [ "$(cov sgride superbombs)" -le 4 ] || fail superbomb-balance "ride2 granted $(cov sgride superbombs) superbombs in ~170 s (want 2-4: one per minute of play)"
[ "$(cov sgride0 superbombs)" -gt "$(cov sgride superbombs)" ] && [ "$(cov sgride0 superbombs_cooled)" = 0 ] || fail superbomb-balance "cooldown 0 should lift the rate limit (grants $(cov sgride0 superbombs), cooled $(cov sgride0 superbombs_cooled))"
held=$(sed -n 's/.*end state: .*superbombs=\([0-9]*\) .*/\1/p' "$OUT/sgride0.log" | head -n 1)
[ "$held" -le 5 ] || fail superbomb-balance "the cap of 5 was exceeded (holding $held)"
echo "PASS bike-superbomb-balance: ride2 grants $(cov sgride superbombs) (cooled $(cov sgride superbombs_cooled)); cooldown 0 grants $(cov sgride0 superbombs), holding $held <= 5"

# --- auto-fire toggle and reserve (T3b.1) --------------------------------------
AF="const:60"
case_run c60 1500 "$AF"
case_run aftog 1500 "$AF" --regress-bike-events="$EVD/af_toggle.csv" --regress-bike-set=autofire=toggle
differs c60 aftog || fail autofire "toggling auto-fire did not change the run"
# columns: tick link moving surging effort_w norm income add power w af_on af_held events
[ "$(awk '$1>=100 && $1<500 && $11!=1' "$OUT/aftog.blog" | wc -l)" -eq 0 ] || fail autofire "the latch was not on between the first two presses"
[ "$(awk '($1<100 || ($1>=500 && $1<800) || $1>=1000) && $11!=0' "$OUT/aftog.blog" | wc -l)" -eq 0 ] || fail autofire "the latch was on outside the pressed windows"
[ "$(awk '$1>=800 && $1<1000 && $11!=1' "$OUT/aftog.blog" | wc -l)" -eq 0 ] || fail autofire "the third press did not switch it back on"
[ "$(cov aftog toggles)" = 4 ] && [ "$(cov aftog fired)" -ge 1 ] || fail autofire "toggle coverage: toggles=$(cov aftog toggles) fired=$(cov aftog fired)"
run afoff 1500 --regress-bike-feed="$AF" --regress-bike-log="$OUT/afoff.blog" --regress-bike-events="$EVD/af_toggle.csv" --regress-bike-set=autofire=off
for x in f s blog; do cmp -s "$OUT/c60.$x" "$OUT/afoff.$x" || fail autofire "autofire=off with presses differs from the plain run ($x)"; done
run afmaster 1500 --regress-bike-events="$EVD/af_toggle.csv" --regress-bike-set=autofire=toggle
run nobike 1500
cmp -s "$OUT/nobike.f" "$OUT/afmaster.f" && cmp -s "$OUT/nobike.s" "$OUT/afmaster.s" || fail autofire "presses with the master off changed the run"
echo "PASS bike-autofire-toggle: on/off mid-level (ticks 100-500, 800-1000), off and master-off stay stock"

AR="$EVD/af_reserve.csv"
for r in 0 25 50; do case_run "ar$r" 1500 "$AF" --regress-bike-events="$AR" --regress-bike-set=autofire=toggle,autofire_reserve=$r; done
m0=$(col_min ar0 9 300) m25=$(col_min ar25 9 300) m50=$(col_min ar50 9 300)
[ "$m25" -gt "$m0" ] && [ "$m50" -gt "$m25" ] || fail reserve "min power does not rise with the reserve ($m0 $m25 $m50)"
[ "$m25" -ge 90 ] && [ "$m50" -ge 270 ] || fail reserve "power sank well below the reserve (25%: $m25, 50%: $m50)"
[ "$(cov ar0 held)" = 0 ] && [ "$(cov ar25 held)" -ge 1 ] && [ "$(cov ar50 held)" -ge 1 ] || fail reserve "held-back coverage"
sh0=$(sed -n 's/.*shield=\([0-9]*\).*/\1/p' "$OUT/ar0.log" | head -n 1); sh50=$(sed -n 's/.*shield=\([0-9]*\).*/\1/p' "$OUT/ar50.log" | head -n 1)
[ "$sh50" -gt "$sh0" ] || fail reserve "shields did not regenerate better with the reserve ($sh0 -> $sh50)"
echo "PASS bike-autofire-reserve: min power $m0 / $m25 / $m50 for Off / 25% / 50%, shield $sh0 -> $sh50 at the end"

for r in 0 50; do case_run "mf$r" 1500 "$AF" --regress-bike-events="$EVD/fire_manual.csv" --regress-bike-set=autofire=toggle,autofire_reserve=$r; done
for x in f s blog; do cmp -s "$OUT/mf0.$x" "$OUT/mf50.$x" || fail manual-fire "the reserve changed manual fire ($x)"; done
[ "$(col_min mf50 9 300)" -lt 225 ] || fail manual-fire "manual fire did not drain below the 25% line"
echo "PASS bike-manual-fire: the reserve never holds back manual fire"
# --- air mouse and remote buttons (T3b.2) ---------------------------------------
# Injected pointer motion / buttons / media keys (--regress-bike-events); the harness drops
# real SDL input, so these go straight to the bike_controls hooks.  air_menu.csv and
# air_pause.csv press on tick 498, the last tick of this 600-frame run: the in-level menus
# loop forever on input, so the run ends on the menu's first frame.
am() { # label events-file settings; runs twice and checks determinism
	local l=$1 e=$2 st=$3
	run "$l" 600 --regress-bike-events="$EVD/$e" --regress-bike-set="$st"
	run "${l}b" 600 --regress-bike-events="$EVD/$e" --regress-bike-set="$st"
	for x in f s; do cmp -s "$OUT/$l.$x" "$OUT/${l}b.$x" || fail "$l" "not deterministic ($x)"; done
	LABELS+=("$l")
}
endpos() { sed -n 's/.*end state: .* x=\([0-9-]*\) y=\([0-9-]*\).*/\1 \2/p' "$OUT/$1.log" | head -n 1; }
same() { cmp -s "$OUT/$1.f" "$OUT/$2.f" && cmp -s "$OUT/$1.s" "$OUT/$2.s"; }
am amidle air_idle.csv enabled=1,air_mouse=1
am amidle0 air_idle.csv enabled=1,air_mouse=0 # stock input: the baseline for every air_mouse-off comparison
for p in 50 100 200; do am "amsteer$p" air_steer.csv enabled=1,air_mouse=1,pointer_sensitivity=$p; done
[ "$(cov amsteer100 moved)" -gt 0 ] && differs amidle amsteer100 || fail air-mouse "pointer motion did not steer the ship"
x50=$(endpos amsteer50 | cut -d' ' -f1) x100=$(endpos amsteer100 | cut -d' ' -f1) x200=$(endpos amsteer200 | cut -d' ' -f1)
[ "$x50" -gt "$x100" ] && [ "$x100" -gt "$x200" ] || fail air-mouse "sensitivity does not scale the motion (x $x50 / $x100 / $x200 at 50 / 100 / 200 %)"
am amoff air_steer.csv enabled=1,air_mouse=0
am ammaster air_steer.csv enabled=0,air_mouse=1
am amidlem air_idle.csv enabled=0,air_mouse=0
same amidle0 amoff && same amidlem ammaster || fail air-mouse "the pointer steered the ship with air_mouse or the master off (it must stay stock)"
echo "PASS bike-air-steer: pointer steers (end x $x50 / $x100 / $x200 at 50 / 100 / 200 %), deterministic; air_mouse off or master off = stock"

am ambtn air_buttons.csv enabled=1,air_mouse=1
differs amidle ambtn || fail air-buttons "OK / media keys changed nothing"
[ "$(cov ambtn media)" -ge 1 ] || fail air-buttons "the media next / previous never fired"
am ambtnoff air_buttons.csv enabled=1,air_mouse=0
same amidle0 ambtnoff || fail air-buttons "buttons and media keys acted with air_mouse off"
echo "PASS bike-air-buttons: OK holds fire, media next / previous act (media=$(cov ambtn media)); air_mouse off = stock"

am ammenu air_menu.csv enabled=1,air_mouse=1
am ammenuoff air_menu.csv enabled=1,air_mouse=0
[ "$(cov ammenu menu_opens)" = 1 ] && [ "$(cov ammenuoff menu_opens)" = 0 ] || fail air-menu "Back did not open the in-game menu exactly when air_mouse is on"
differs amidle ammenu && same amidle0 ammenuoff || fail air-menu "the in-game menu run is not observable, or air_mouse off is not stock"
am ampause air_pause.csv enabled=1,air_mouse=1
am ampauseoff air_pause.csv enabled=1,air_mouse=0
[ "$(cov ampause pauses)" = 1 ] && [ "$(cov ampauseoff pauses)" = 0 ] || fail air-pause "media Play/Pause did not pause exactly when air_mouse is on"
differs amidle ampause && same amidle0 ampauseoff || fail air-pause "the pause run is not observable, or air_mouse off is not stock"
echo "PASS bike-air-menu: Back opens the in-game menu, Play/Pause pauses (only with air_mouse on)"
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
