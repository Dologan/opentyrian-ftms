#!/bin/bash
# check_bike.sh -- guard for the exercise-bike core (src/bike*.c).
#
#   check_bike.sh BIN DATA_DIR OUT_DIR [--quick|--update]
#
# Every case runs a scripted level (--regress-script, fixed seed, no input) with a
# scripted feed from test/bike/effort/feeds (--regress-bike-feed) and compares frame hashes,
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
#   - touch (T3b.3): injected finger events (Modern 16:9): a drag steers (sensitivity-scaled), each side-panel
#     tap zone acts, a quick two-finger tap opens the in-game menu, and touch off or the master off is stock.
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
FEEDS="$ROOT/test/bike/effort/feeds"
mkdir -p "$OUT"

fail() { echo "FAIL bike-$1: $2"; exit 1; }
sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum; else shasum -a 256; fi | cut -d' ' -f1; }

SCEN=(--regress-script=1:3 --regress-seed=32402394 --regress-detail=2)
# MSYS2 converts a leading /path in --opt=/path arguments for native programs, but not
# the N:/path form of --regress-snapshot (it would treat it as a path list), so pass
# those as native paths.
native_path() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi; }
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

# --- command-line overrides never reach the saved config (T5.1) -------------------
# --ftms-bridge / --ftms-ftp are run-scoped.  A real config save (--regress-user-files writes the
# configs through the real serializers) must keep the saved values for those keys, keep unrelated
# saved keys, tolerate the retired pointer_smoothing key (config files keep unknown items), and turn an old Air Mouse config into
# remote_buttons = on (Air Mouse used to imply the menu buttons).
CFGDIR="$OUT/cfg-overrides"
rm -rf "$CFGDIR"; mkdir -p "$CFGDIR"
printf "section 'ftms'\n\titem 'enabled' 'on'\n\titem 'ftp' '250'\n\titem 'bridge_host' 'saved.example'\n\titem 'bridge_port' '9001'\n\titem 'pointer_smoothing' 'on'\n\titem 'air_mouse' 'on'\n" >"$CFGDIR/opentyrian.cfg"
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy "$BIN" --data="$DATA_DIR" --regress-user-root="$CFGDIR" --regress-user-files \
	--ftms-bridge=override.example:1234 --ftms-ftp=333 >"$OUT/cfg-overrides.log" 2>&1 || fail config-override "the save run failed"
cfgval() { sed -n "s/^[[:space:]]*item '$1' '\(.*\)'\$/\1/p" "$CFGDIR/opentyrian.cfg" | head -n 1; }
[ "$(cfgval ftp)" = 250 ] && [ "$(cfgval bridge_host)" = saved.example ] && [ "$(cfgval bridge_port)" = 9001 ] \
	|| fail config-override "a command-line override leaked into [ftms] (ftp=$(cfgval ftp) host=$(cfgval bridge_host) port=$(cfgval bridge_port))"
[ "$(cfgval enabled)" = on ] && [ "$(cfgval air_mouse)" = on ] || fail config-override "unrelated saved keys changed"
[ "$(cfgval remote_buttons)" = on ] || fail config-override "an old Air Mouse config did not enable remote_buttons"
echo "PASS bike-config-override: --ftms-bridge / --ftms-ftp are run-scoped, retired key tolerated, Air Mouse config migrated"

# The default port moved 8787 -> 8585: a saved 8787 without the bridge_port_v marker is the old default and
# loads as 8585; the save writes the marker, after which a deliberate 8787 is kept.
for want in "none:8585" "2:8787"; do
	mark="${want%%:*}"; expect="${want##*:}"
	rm -rf "$CFGDIR"; mkdir -p "$CFGDIR"
	{ printf "section 'ftms'\n\titem 'enabled' 'on'\n\titem 'bridge_port' '8787'\n"
	  [ "$mark" = none ] || printf "\titem 'bridge_port_v' '%s'\n" "$mark"; } >"$CFGDIR/opentyrian.cfg"
	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy "$BIN" --data="$DATA_DIR" --regress-user-root="$CFGDIR" --regress-user-files \
		>"$OUT/cfg-port.log" 2>&1 || fail config-port "the save run failed"
	[ "$(cfgval bridge_port)" = "$expect" ] && [ "$(cfgval bridge_port_v)" = 2 ] \
		|| fail config-port "marker $mark: bridge_port=$(cfgval bridge_port) bridge_port_v=$(cfgval bridge_port_v), want $expect / 2"
done
echo "PASS bike-config-port: a saved 8787 without bridge_port_v loads as 8585, with it is kept"

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

# --- FTP from the bridge (rider message): precedence and timing ------------------------------
# The bridge's rider FTP must behave exactly like typing that number, and only when it should:
#   source Bridge + a rider FTP    == the same number typed in (frames, state and log identical)
#   source Manual + a rider FTP    == the typed value (the rider is ignored)
#   --ftms-ftp given               == that value (the command line beats the bridge)
#   a rider announced mid-level    == unchanged (an FTP change mid-level would make effort jump)
RAMP="$FEEDS/ramp0_300.csv"
case_run ftpm200 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=200,ftp_source=manual
case_run ftpm250 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=250,ftp_source=manual
case_run ftpm300 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=300,ftp_source=manual
case_run ftpbr300 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=200,ftp_source=auto --regress-bike-rider=300:Ann
case_run ftpman 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=200,ftp_source=manual --regress-bike-rider=300:Ann
case_run ftpcli 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=250,ftp_source=auto --ftms-ftp=250 --regress-bike-rider=300:Ann
case_run ftpmid 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=200,ftp_source=auto --regress-bike-events="$ROOT/test/bike/events/rider_mid.csv"
same_run() { for x in f s blog; do cmp -s "$OUT/$1.$x" "$OUT/$2.$x" || fail ftp-source "$1 and $2 differ ($x): $3"; done; }
cmp -s "$OUT/ftpm200.blog" "$OUT/ftpm300.blog" && fail ftp-source "FTP 200 and 300 give the same log, so the cases below prove nothing"
same_run ftpbr300 ftpm300 "the bridge FTP is not the same as typing it"
same_run ftpman ftpm200 "a manual source did not ignore the rider"
same_run ftpcli ftpm250 "--ftms-ftp did not beat the bridge"
same_run ftpmid ftpm200 "a rider announced mid-level changed the run"
echo "PASS bike-ftp-source: bridge FTP == typed FTP; manual and --ftms-ftp win; a mid-level rider waits for the next level"

# --- Flywheel intensity preset: the effective FTP (300 W at 85 % = 255 W) is what plays ----------
# Same precedence as a plain bridge FTP: the typed FTP is never touched, Manual and --ftms-ftp win.
case_run ftpm255 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=255,ftp_source=manual
case_run ftpint 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=200,ftp_source=auto --regress-bike-rider=300@85:Ann
case_run ftpintman 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=200,ftp_source=manual --regress-bike-rider=300@85:Ann
case_run ftpintcli 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=250,ftp_source=auto --ftms-ftp=250 --regress-bike-rider=300@85:Ann
case_run ftpintmid 3000 "$RAMP" --regress-bike-set=surge_superbomb=0,ftp=200,ftp_source=auto --regress-bike-events="$ROOT/test/bike/events/rider_intensity_mid.csv"
cmp -s "$OUT/ftpm255.blog" "$OUT/ftpm300.blog" && fail bike-intensity "FTP 255 and 300 give the same log, so the cases below prove nothing"
same_run ftpint ftpm255 "the intensity FTP is not the same as typing the effective FTP"
same_run ftpintman ftpm200 "a manual source did not ignore the intensity preset"
same_run ftpintcli ftpm250 "--ftms-ftp did not beat the intensity preset"
same_run ftpintmid ftpm200 "an intensity rider announced mid-level changed the run"
echo "PASS bike-intensity: 300 W at 85 % == typed 255; manual and --ftms-ftp win; a mid-level preset waits for the next level"

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
# --- touch controls (T3b.3) -----------------------------------------------------
# Injected finger events (--regress-bike-events; the harness drops real SDL input) go through
# the same handlers as SDL_EVENT_FINGER_*.  Panel zones need Modern widescreen, so these runs
# are Modern 16:9; x, y are 1/1000 of the window.  Menu, pause and two-finger taps land on tick
# 498, the last tick of the run (the in-level menus loop forever on input).
TMX=(--regress-modern --regress-aspect=16:9)
tm() { # label events-file settings; runs twice and checks determinism
	local l=$1 e=$2 st=$3
	run "$l" 600 "${TMX[@]}" --regress-bike-events="$EVD/$e" --regress-bike-set="$st"
	run "${l}b" 600 "${TMX[@]}" --regress-bike-events="$EVD/$e" --regress-bike-set="$st"
	for x in f s; do cmp -s "$OUT/$l.$x" "$OUT/${l}b.$x" || fail "$l" "not deterministic ($x)"; done
	LABELS+=("$l")
}
TON=enabled=1,touch=1,autofire=toggle
TOFF=enabled=1,touch=0,autofire=toggle
tm tidle touch_idle.csv $TON
tm tidle0 touch_idle.csv $TOFF # stock input: the baseline for every touch-off comparison
tm tidlem touch_idle.csv enabled=0,touch=0
tm tidlenaf touch_idle.csv enabled=1,touch=1,autofire=off
for p in 50 100 200; do tm "tnudge$p" touch_nudge.csv $TON,pointer_sensitivity=$p; done
tm tdrag touch_drag.csv $TON
[ "$(cov tdrag steered)" -gt 0 ] && differs tidle tdrag || fail touch-steer "the drag did not steer the ship"
tx50=$(endpos tnudge50 | cut -d' ' -f1) tx100=$(endpos tnudge100 | cut -d' ' -f1) tx200=$(endpos tnudge200 | cut -d' ' -f1)
[ "$tx50" -gt "$tx100" ] && [ "$tx100" -gt "$tx200" ] || fail touch-steer "sensitivity does not scale the drag (x $tx50 / $tx100 / $tx200 at 50 / 100 / 200 %)"
tm tdragoff touch_drag.csv $TOFF
tm tdragm touch_drag.csv enabled=0,touch=1
same tidle0 tdragoff && same tidlem tdragm || fail touch-steer "the drag steered with touch or the master off (it must stay stock)"
echo "PASS bike-touch-steer: drag steers (end x $tx50 / $tx100 / $tx200 at 50 / 100 / 200 %), deterministic; touch off or master off = stock"

for z in rear skl skr bomb; do tm "t$z" "touch_$z.csv" $TON; differs tidle "t$z" || fail touch-zone "the $z zone changed nothing"; done
[ "$(cov trear zone_taps)" = 2 ] && [ "$(cov tskl zone_taps)" = 1 ] && [ "$(cov tskr zone_taps)" = 1 ] && [ "$(cov tbomb zone_taps)" = 1 ] || fail touch-zone "zone tap coverage"
differs tskl tskr && differs tskl tbomb && differs tskr tbomb || fail touch-zone "the sidekick and bomb zones are not distinct"
tm tauto touch_auto.csv $TON
tm tautonaf touch_auto.csv enabled=1,touch=1,autofire=off
[ "$(cov tauto tautos)" = 2 ] && [ "$(cov tauto toggles)" = 2 ] && differs tidle tauto || fail touch-zone "the AUTO zone did not toggle auto-fire"
[ "$(cov tautonaf toggles)" = 0 ] && same tidlenaf tautonaf || fail touch-zone "the AUTO zone acted with the auto-fire setting off"
tm tmenu touch_menu.csv $TON
tm tpause touch_pause.csv $TON
[ "$(cov tmenu tmenus)" = 1 ] && differs tidle tmenu || fail touch-zone "the MENU zone did not open the in-game menu"
[ "$(cov tpause tpauses)" = 1 ] && differs tidle tpause || fail touch-zone "the PAUSE zone did not pause"
for z in rear skl skr bomb auto menu pause; do
	run "tz${z}off" 600 "${TMX[@]}" --regress-bike-events="$EVD/touch_$z.csv" --regress-bike-set="$TOFF"
	same tidle0 "tz${z}off" || fail touch-zone "the $z zone acted with touch off (it must stay stock)"
done
echo "PASS bike-touch-zones: REAR, SK-L, SK-R, BOMB, AUTO, MENU and PAUSE each act (only with touch on); AUTO is a no-op with auto-fire off"

tm ttwo touch_two.csv $TON
tm tslow touch_slow.csv $TON
[ "$(cov ttwo two_finger)" = 1 ] && [ "$(cov ttwo tmenus)" = 1 ] && differs tidle ttwo || fail touch-two "a quick two-finger tap did not open the in-game menu"
[ "$(cov tslow two_finger)" = 0 ] && [ "$(cov tslow tmenus)" = 0 ] && same tidle tslow || fail touch-two "a slow two-finger touch acted"
run ttwooff 600 "${TMX[@]}" --regress-bike-events="$EVD/touch_two.csv" --regress-bike-set="$TOFF"
same tidle0 ttwooff || fail touch-two "the two-finger tap acted with touch off"
echo "PASS bike-touch-two: a quick two-finger tap opens the in-game menu; a slow one and touch off do nothing"

# --- UI snapshots: setup menu screens and the HUD -------------------------------
# The BMPs are hashed into test/bike/regress.txt (snap-*); --update also renders them
# as PNGs under doc/ftms/screens when python3 with PIL is available.
SNAPS=()
snap_screen() { # label screen
	local l=$1 s=$2
	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy "$BIN" --data="$DATA_DIR" --regress-screen="$s" \
		--regress-bike-set=enabled=1,surge_special=1 --regress-frames=40 --regress-snapshot=30:"$(native_path "$OUT/$l.bmp")" \
		>"$OUT/$l.log" 2>&1 || fail "$l" "the screen run exited with $?"
	[ -s "$OUT/$l.bmp" ] || fail "$l" "no snapshot was written"
	SNAPS+=("$l")
}
snap_screen snap-menu bike-menu
snap_screen snap-menu-nav bike-menu-nav
snap_screen snap-menu-edit bike-menu-edit
cmp -s "$OUT/snap-menu.bmp" "$OUT/snap-menu-nav.bmp" && fail snap-menu "the key script did not change the menu"
# the same menu with a rider from the bridge in force: the FTP row shows its value and name
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy "$BIN" --data="$DATA_DIR" --regress-screen=bike-menu \
	--regress-bike-set=enabled=1,surge_special=1,ftp_source=auto --regress-bike-rider=230:Ann --regress-frames=40 \
	--regress-snapshot=30:"$(native_path "$OUT/snap-menu-rider.bmp")" >"$OUT/snap-menu-rider.log" 2>&1 \
	|| fail snap-menu-rider "the screen run exited with $?"
[ -s "$OUT/snap-menu-rider.bmp" ] || fail snap-menu-rider "no snapshot was written"
SNAPS+=(snap-menu-rider)
cmp -s "$OUT/snap-menu.bmp" "$OUT/snap-menu-rider.bmp" && fail snap-menu-rider "a bridge rider did not change the menu"
# and with an intensity preset in force: "196 W Easy 85 %"
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy "$BIN" --data="$DATA_DIR" --regress-screen=bike-menu \
	--regress-bike-set=enabled=1,surge_special=1,ftp_source=auto --regress-bike-rider=230@85:Ann --regress-frames=40 \
	--regress-snapshot=30:"$(native_path "$OUT/snap-menu-intensity.bmp")" >"$OUT/snap-menu-intensity.log" 2>&1 \
	|| fail snap-menu-intensity "the screen run exited with $?"
[ -s "$OUT/snap-menu-intensity.bmp" ] || fail snap-menu-intensity "no snapshot was written"
SNAPS+=(snap-menu-intensity)
cmp -s "$OUT/snap-menu-rider.bmp" "$OUT/snap-menu-intensity.bmp" && fail snap-menu-intensity "a preset did not change the FTP row"
echo "PASS bike-menu-screens: Setup menu, stepper walk, address edit and a bridge rider render and differ"

# HUD on vs off: presentation only, so the state hashes must be identical.
HUDARGS=(--regress-bike-feed=const:230,92 --regress-modern --regress-aspect=16:9)
run hud0 700 "${HUDARGS[@]}" --regress-bike-set=hud=0
run hud1 700 "${HUDARGS[@]}" --regress-bike-set=hud=1 --regress-snapshot=650:"$(native_path "$OUT/snap-hud-modern.bmp")"
cmp -s "$OUT/hud0.s" "$OUT/hud1.s" || fail hud "the HUD changed the state hashes"
cmp -s "$OUT/hud0.f" "$OUT/hud1.f" && fail hud "the HUD did not change the frames"
# Touch zones on the panels: presentation only (state hashes of the HUD run), and they change the frames.
run hudt 700 "${HUDARGS[@]}" --regress-bike-set=hud=1,touch=1,autofire=toggle --regress-snapshot=650:"$(native_path "$OUT/snap-hud-touch.bmp")"
cmp -s "$OUT/hud1.s" "$OUT/hudt.s" || fail hud-touch "the touch zones changed the state hashes"
cmp -s "$OUT/hud1.f" "$OUT/hudt.f" && fail hud-touch "the touch zones did not change the frames"
run hudc 700 --regress-bike-feed=const:230,92 --regress-bike-set=hud=1 --regress-snapshot=650:"$(native_path "$OUT/snap-hud-classic.bmp")"
run hudc0 700 --regress-bike-feed=const:230,92 --regress-bike-set=hud=0
cmp -s "$OUT/hudc0.s" "$OUT/hudc.s" || fail hud-classic "the Classic HUD changed the state hashes"
cmp -s "$OUT/hudc0.f" "$OUT/hudc.f" && fail hud-classic "the Classic HUD did not change the frames"
run hudsurge 2400 "${HUDARGS[@]}" --regress-bike-feed="$FEEDS/surge.csv" --regress-bike-set=hud=1 \
	--regress-snapshot=2350:"$(native_path "$OUT/snap-hud-surge.bmp")"
run ingame 700 --regress-menu=bike --regress-bike-set=enabled=1 --regress-snapshot=699:"$(native_path "$OUT/snap-ingame-bike.bmp")"
for l in snap-hud-modern snap-hud-classic snap-hud-surge snap-ingame-bike snap-hud-touch; do
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
