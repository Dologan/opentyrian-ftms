#!/bin/bash
# regress.sh — headless regression harness for OpenTyrian.
#
# Builds the game and compares per-frame 8-bit framebuffer hashes against the
# committed baselines in test/regress/.  Two kinds of cases are run:
#
#   1. the five recorded demos (demo.1 .. demo.5) at every processor detail
#      level (1 .. 6), as demoN-dM;
#   2. synthetic level scenarios that cover render paths the demos never reach
#      (smoothies[] stays zero for the whole of every demo), as
#      scenario-<name>-dM.  See SCENARIOS below;
#   3. the Modern presentation (--regress-modern), which hashes the CPU-composed
#      XRGB8888 canvas instead of the 8-bit frame, as modern-<case>; the
#      modern-wide-<case> subset runs the same canvas at 16:9 (wider canvas plus
#      the procedural side panels).
#
# It also runs the offline audio regression (--regress-audio) as the "audio"
# case, which hashes the converted sound effects, per-second music rendering and
# per-second sound-effect mixing.  See src/regress_audio.c.
#
#   tools/regress.sh              build, run all cases, compare
#   tools/regress.sh --update     regenerate the baselines from the current tree
#   tools/regress.sh -j 3         run up to three cases at once (default: CPUs)
#   make regress REGRESS_JOBS=1  run serially
#   make regress-quick           critical subset plus cheap guards
#   REGRESS_ONLY='^depth-' make regress   case labels / guard script names
#
# The Tyrian data directory comes from $TYRIAN_DATA, defaulting to ./data.  If
# the data is missing, ./get_data.sh fetches the freeware Tyrian 2.1 release.
# Case helpers are also dispatched through a shell-escaped command array.
# shellcheck disable=SC2329
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

UPDATE=0
CASE_FILTER=${REGRESS_ONLY:-.*}
QUICK=0
source "$ROOT/tools/regress_selection.sh"
UPDATE_MANIFEST=0
REPLAY_CHECK=0
INTERP_CHECK=0
SMOOTH_CHECK=0
PARALLAX_CHECK=0
# Bash 3.2 / MSYS2: no wait -n, associative arrays or GNU-only CPU query.
default_jobs() {
	local count
	count=$(nproc 2>/dev/null) || count=$(sysctl -n hw.ncpu 2>/dev/null) || count=${NUMBER_OF_PROCESSORS:-1}
	case "$count" in ''|*[!0-9]*|0) count=1 ;; esac
	printf '%s\n' "$count"
}

JOBS=${REGRESS_JOBS:-$(default_jobs)}
while [ "$#" -gt 0 ]; do
	case "$1" in
		--update) UPDATE=1 ;;
		--quick) QUICK=1 ;;
		--case=*) CASE_FILTER=${1#*=} ;;
		--update-manifest) UPDATE_MANIFEST=1 ;;
		--replay-check) REPLAY_CHECK=1 ;;
		--interp-check) INTERP_CHECK=1 ;;
		--smoothness-check) SMOOTH_CHECK=1 ;;
		--parallax-check) PARALLAX_CHECK=1 ;;
		-j|--jobs)
			if [ "$#" -lt 2 ]; then
				echo "ERROR: $1 requires a positive integer" >&2
				exit 2
			fi
			JOBS=$2
			shift ;;
		--jobs=*) JOBS=${1#*=} ;;
		-j[0-9]*) JOBS=${1#-j} ;;
		-h|--help)
			echo "Usage: tools/regress.sh [-j N|--jobs N] [--quick] [--case=REGEX] [--update|--update-manifest|--replay-check|--interp-check|--smoothness-check|--parallax-check]"
			echo "Jobs default to REGRESS_JOBS or the number of CPUs; REGRESS_ONLY filters case labels and guard names."
			exit 0 ;;
		*) echo "ERROR: unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
validate_case_filter || exit 2
if [ "$QUICK" -eq 1 ] && [ "$((UPDATE + UPDATE_MANIFEST + REPLAY_CHECK + INTERP_CHECK + SMOOTH_CHECK + PARALLAX_CHECK))" -ne 0 ]; then
	echo "ERROR: --quick is a comparison tier; use the full suite for updates or sweeps" >&2; exit 2
fi
case "$JOBS" in
	''|*[!0-9]*|0) echo "ERROR: jobs must be a positive integer" >&2; exit 2 ;;
esac
# Strip leading zeroes so Bash arithmetic cannot interpret an octal number.
while [ "${JOBS#0}" != "$JOBS" ]; do JOBS=${JOBS#0}; done
if [ -z "$JOBS" ] || [ "${#JOBS}" -gt 9 ]; then
	echo "ERROR: jobs must be a positive integer (at most nine digits)" >&2
	exit 2
fi

DATA_DIR="${TYRIAN_DATA:-$ROOT/data}"
BIN="$ROOT/opentyrian"
BASELINE_DIR="$ROOT/test/regress"
ACTUAL_DIR="$BASELINE_DIR/actual"
MANIFEST="$BASELINE_DIR/data-manifest.txt"
DEMOS="1 2 3 4 5"
LEVELS="1 2 3 4 5 6"
# Modern presentation: the product pins the effective render detail to Pentium,
# so every Modern case runs at detail 4 whatever --regress-detail it requests.
# The engine forces and logs the override (a request of 6 is kept: that is the
# SuperWild cheat, which must still work in Modern).  Classic cases keep sweeping
# LEVELS unchanged.
MODERN_DETAIL=4

# Synthetic scenarios: "name episode:level frame-cap detail...".
#
# Each entry starts a level directly (fixed RNG seed, fixed new-game loadout, no
# input, extra lives via the engine's own youAreCheating flag) and hashes the
# requested number of frames.  The details listed are exactly the ones where the
# scenario's covered path is reachable: lava/water need processorType > 2, and
# blur/iced need processorType > 1.  The six render paths are covered as:
#
#   lava_filter            scenario-flip / scenario-spotlight
#   water_filter           scenario-water
#   iced_blur_filter       scenario-iced
#   blur_filter            scenario-blur
#   starShowVGA code 1     scenario-flip        (vertical flip)
#   starShowVGA code 2     scenario-spotlight   (player spotlight)
SCENARIOS=(
	"water     4:9  1200 3 4 5 6"
	"flip      4:12 3600 3 4 5 6"
	"iced      4:8  1200 2 3 4 5 6"
	"spotlight 1:16 1200 3 4 5 6"
	"blur      4:19 1200 2 3 4 5 6"
)

# --- build -------------------------------------------------------------------

if [ "${REGRESS_SKIP_BUILD:-0}" != "1" ]; then
	echo "Building opentyrian..."
	if ! make -s; then
		echo "BUILD FAILED"
		exit 1
	fi
fi

if [ ! -x "$BIN" ]; then
	echo "ERROR: $BIN not found (build failed?)"
	exit 1
fi

# --- source guard ------------------------------------------------------------
#
# C does not sequence two side-effecting calls in one expression (function
# arguments, the operands of +, ...).  Two RNG draws in the same expression
# therefore come out in a compiler-dependent order, which would silently break
# the baselines on another toolchain.  Refuse to run the suite if any such site
# is present.  See tools/check_rng_order.sh and .worker-reports/rng-order.md.
if guard_selected check_rng_order.sh && ! "$ROOT/tools/check_rng_order.sh"; then
	echo ""
	echo "ERROR: an expression draws from the RNG more than once; the order of"
	echo "evaluation is unspecified by C.  Hoist each call into its own statement."
	exit 1
fi

# --- game data ---------------------------------------------------------------

if [ ! -f "$DATA_DIR/tyrian1.lvl" ]; then
	echo "Tyrian data not found in $DATA_DIR; fetching..."
	if ! "$ROOT/get_data.sh" "$DATA_DIR"; then
		echo "ERROR: failed to obtain Tyrian data"
		exit 1
	fi
fi

# --- data lock ---------------------------------------------------------------
#
# The baselines are only valid for the original freeware Tyrian 2.1 data.  A
# different data directory (e.g. a copy where one sprite was replaced) makes the
# output diverge silently, so refuse it up front.  The expected size and CRC of
# every file the cases read live in test/regress/data-manifest.txt; the CRC is
# the POSIX cksum one, which is available on macOS, Linux and MSYS2.

print_manifest_header() {
	echo "# Data files the regression cases read, with the size and POSIX cksum CRC of"
	echo "# the expected Tyrian 2.1 copy.  tools/regress.sh refuses a data directory"
	echo "# whose files do not match, because the baselines are only valid for the"
	echo "# original freeware data.  Regenerate with: tools/regress.sh --update-manifest"
	echo "#"
	echo "# size crc name"
}

# --update-manifest re-hashes the files already listed in the manifest, so it
# does not add or remove entries; edit the manifest for that.
if [ "$UPDATE_MANIFEST" -eq 1 ]; then
	if [ ! -f "$MANIFEST" ]; then
		echo "ERROR: no data manifest to update ($MANIFEST)"
		exit 1
	fi

	tmp="$MANIFEST.tmp"
	print_manifest_header > "$tmp"
	while read -r size crc name; do
		# A checkout with CRLF (e.g. Windows git autocrlf) leaves a stray \r
		# on the name; .gitattributes forces LF, but stay robust anyway.
		size=${size%$'\r'}
		crc=${crc%$'\r'}
		name=${name%$'\r'}
		case "$size" in ''|'#'*) continue ;; esac
		if [ ! -f "$DATA_DIR/$name" ]; then
			echo "missing: $name" >&2
			continue
		fi
		# shellcheck disable=SC2046
		set -- $(cksum "$DATA_DIR/$name")
		printf '%s %s %s\n' "$2" "$1" "$name" >> "$tmp"
	done < "$MANIFEST"
	mv "$tmp" "$MANIFEST"
	echo "Data manifest updated from $DATA_DIR."
	exit 0
fi

if [ ! -f "$MANIFEST" ]; then
	echo "ERROR: missing data manifest $MANIFEST"
	exit 1
fi

bad_data=0
while read -r size crc name; do
	# See the note in the --update-manifest loop: strip a stray \r so the lock
	# still works if a CRLF checkout slips through on Windows.
	size=${size%$'\r'}
	crc=${crc%$'\r'}
	name=${name%$'\r'}
	case "$size" in ''|'#'*) continue ;; esac

	if [ ! -f "$DATA_DIR/$name" ]; then
		echo "  missing: $name (expected size $size, crc $crc)"
		bad_data=$((bad_data + 1))
		continue
	fi

	# shellcheck disable=SC2046
	set -- $(cksum "$DATA_DIR/$name")
	got_crc=$1
	got_size=$2

	if [ "$got_size" != "$size" ] || [ "$got_crc" != "$crc" ]; then
		echo "  mismatched: $name (expected size $size, crc $crc; got size $got_size, crc $got_crc)"
		bad_data=$((bad_data + 1))
	fi
done < "$MANIFEST"

if [ "$bad_data" -ne 0 ]; then
	echo ""
	echo "ERROR: the Tyrian data in $DATA_DIR is not the expected Tyrian 2.1 set"
	echo "($bad_data file(s) differ).  The regression baselines are only valid for"
	echo "the exact freeware data they were generated from, so this run is refused"
	echo "to avoid a false divergence.  Point TYRIAN_DATA at that copy (a patched,"
	echo "repacked or differently-sourced release can change a sprite silently)."
	exit 1
fi

# --- helpers -----------------------------------------------------------------

mkdir -p "$BASELINE_DIR"
rm -rf "$ACTUAL_DIR"
mkdir -p "$ACTUAL_DIR"

# No Tyrian 2000 file may land in the tree (metadata-only check, no game data).
if guard_selected check_no_t2000_data.sh && ! "$ROOT/tools/check_no_t2000_data.sh"; then
	echo "ERROR: a Tyrian 2000 data file is in the source tree"
	exit 1
fi

if guard_selected check_variant_bootstrap.sh && ! "$ROOT/tools/check_variant_bootstrap.sh" "$BIN" "$DATA_DIR" "$ACTUAL_DIR/variant-bootstrap"; then
	echo "ERROR: variant/bootstrap checks failed"
	exit 1
fi

if guard_selected check_user_paths.sh && ! "$ROOT/tools/check_user_paths.sh" "$BIN" "$DATA_DIR" "$ACTUAL_DIR/user-paths"; then
	echo "ERROR: user-path/migration checks failed"
	exit 1
fi

if guard_selected check_game_rules.sh && ! "$ROOT/tools/check_game_rules.sh" "$BIN" "$DATA_DIR" "$ACTUAL_DIR/rules-21"; then
	echo "ERROR: game-rules checks failed"
	exit 1
fi

if guard_selected check_bike_effort.sh && ! "$ROOT/tools/check_bike_effort.sh"; then
	echo "ERROR: bike-effort checks failed"
	exit 1
fi

if guard_selected check_installer.sh && ! "$ROOT/tools/check_installer.sh" "$BIN" "$DATA_DIR" "$ACTUAL_DIR/installer"; then
	echo "ERROR: Tyrian 2000 installer checks failed"
	exit 1
fi

if guard_selected check_final_regression.sh; then
	if ! "$ROOT/tools/check_final_regression.sh" "$BIN" 2.1 "$DATA_DIR" "$ACTUAL_DIR/final-regression" ||
       ! cmp "$BASELINE_DIR/final-regression.txt" "$ACTUAL_DIR/final-regression/final-regression.txt"; then
		echo "ERROR: final matrix checks failed"
		exit 1
	fi
fi
if guard_selected check_display.sh; then
	"$ROOT/tools/check_display.sh" "$BIN" "$DATA_DIR" || exit 1
fi

now() {
	if command -v perl >/dev/null 2>&1; then
		perl -MTime::HiRes=time -e 'printf "%.3f", time'
	else
		date +%s
	fi
}

total_start=$(now)
failures=0
pairs=0

# printf removes wc padding without launching a separate tr process.
line_count() {
	local lines
	lines=$(wc -l < "$1")
	printf '%d' "$lines"
}

# Estimate relative work from the unchanged frame streams and render paths.
# Profiling puts Modern/SuperWild demos and audio first; cheap screen cases go
# last. This affects scheduling only, never the cases or their printed order.
case_cost() {
	local kind=$1 label=$2 baseline=$2 frames=1200 factor=1 arg
	case "$kind" in
		run_replay_case|run_interp_case|run_gameplay_case|run_smoothness_case|run_parallax_case|run_layer_case|run_depth_case) baseline=$3 ;;
		run_parallax_level) frames=$3 ;;
	esac
	if [ -f "$BASELINE_DIR/$baseline.txt" ]; then
		frames=$(line_count "$BASELINE_DIR/$baseline.txt")
	fi
	for arg in "$@"; do
		case "$arg" in
			--regress-frames=*) frames=${arg#*=} ;;
			--regress-modern) factor=$((factor * 3)) ;;
			--regress-detail=6) factor=$((factor * 2)) ;;
		esac
	done
	case "$kind" in
		run_interp_case|run_smoothness_case|run_parallax_case) factor=$((factor * 2)) ;;
		run_layer_case) factor=$((factor * 4)) ;;  # two runs, one of them with the interpolated check
		run_depth_case) factor=$((factor * 2)) ;;  # two runs: depth off and on
		run_held_case) factor=$((factor * 3)) ;;   # two runs: effects off, lighting, depth on
	esac
	# Audio hashes whole sound/music streams, not framebuffer records.
	if [ "$label" = audio ]; then
		printf '30000'
	else
		printf '%s' "$((frames * factor))"
	fi
}

source "$ROOT/tools/regress_scheduler.sh"

# Describe a process exit status.  The shell reports a process killed by a
# signal as 128 + N, so a crash is distinguished from a plain non-zero exit.
exit_status_description() {
	local rc=$1
	if [ "$rc" -le 128 ]; then
		echo "exit code $rc"
	elif [ "$rc" -le 192 ]; then
		# 129..192 is 128 + a POSIX signal number, which is how a shell
		# reports a process killed by a signal.
		local sig=$((rc - 128))
		local name
		name=$(kill -l "$sig" 2>/dev/null)
		echo "CRASH: killed by signal $sig${name:+ ($name)}"
	else
		# Windows/other abnormal exit status (e.g. an NTSTATUS like 0xC0000005).
		printf 'CRASH: abnormal exit status %s (0x%X)' "$rc" "$rc"
	fi
}

# Print the captured stdout+stderr of a failed case so a crash, a sanitizer
# report or an engine error is visible in the harness output.  Any sanitizer /
# crash marker lines are shown first, then the tail of the log.
dump_failure_log() {
	local log=$1
	[ -f "$log" ] || return 0

	echo "  --- output ($log) ---"
	if grep -qE 'AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|runtime error:|SUMMARY:|Fatal|assert' "$log" 2>/dev/null; then
		grep -nE 'AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|runtime error:|SUMMARY:|Fatal|assert' "$log" | tail -n 20 | sed 's/^/  /'
	fi
	tail -n 20 "$log" | sed 's/^/  /'
	echo "  --- end output ---"
}

# run_case LABEL "$@" -- run the binary and compare/update one baseline.
# The output file is derived from LABEL.
run_case() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_case "$@"
		return
	fi
	local label=$1
	shift
	local out="$ACTUAL_DIR/$label.txt"
	local log="$ACTUAL_DIR/$label.log"
	local baseline="$BASELINE_DIR/$label.txt"
	local start elapsed rc lines hunk first

	start=$(now)
	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
		run_binary --data="$DATA_DIR" --regress-out="$out" "$@" \
		>"$log" 2>&1
	rc=$?
	elapsed=$(awk "BEGIN { printf \"%.2f\", $(now) - $start }")

	if [ "$rc" -ne 0 ]; then
		echo "FAIL $label: $(exit_status_description "$rc") (${elapsed}s)"
		dump_failure_log "$log"
		failures=$((failures + 1))
		return
	fi

	case "$label" in modern-crt-*|modern-screen-setup-crt-picker)
		if ! grep -q 'CRT coverage:' "$log"; then
			echo "FAIL $label: missing CRT execution coverage"; failures=$((failures + 1)); return
		fi ;;
	esac
	if [ ! -f "$out" ]; then
		echo "FAIL $label: no output written (${elapsed}s)"
		dump_failure_log "$log"
		failures=$((failures + 1))
		return
	fi

	# An empty output file with a zero exit status is an early exit that never
	# presented a frame -- the "first differing line 0" signature in CI.  Call
	# it out instead of comparing it as an ordinary hash divergence.
	if [ ! -s "$out" ]; then
		echo "FAIL $label: empty output file - the run exited before presenting a frame (${elapsed}s)"
		dump_failure_log "$log"
		failures=$((failures + 1))
		return
	fi

	lines=$(line_count "$out")

	if [ "$UPDATE" -eq 1 ]; then
		cp "$out" "$baseline"
		echo "UPDATE $label: $lines lines, ${elapsed}s"
		return
	fi

	if [ ! -f "$baseline" ]; then
		echo "FAIL $label: missing baseline (run tools/regress.sh --update)"
		failures=$((failures + 1))
		return
	fi

	if cmp -s "$baseline" "$out"; then
		echo "PASS $label: $lines lines, ${elapsed}s"
	else
		# First differing hunk, e.g. "12c12" or "5,7c5,9"; line N is record N-1.
		hunk=$(diff "$baseline" "$out" | head -n 1)
		first=${hunk%%[cad]*}
		first=${first%%,*}
		echo "FAIL $label: first differing line $((first - 1)) (${elapsed}s)"
		# A diff can also hide an engine error or a sanitizer report on stderr;
		# keep it in the failure output.
		dump_failure_log "$log"
		failures=$((failures + 1))
	fi
}

# --- demos -------------------------------------------------------------------

for d in $DEMOS; do
	for m in $LEVELS; do
		pairs=$((pairs + 1))
		# --regress-demo-hud-check only observes: a Classic demo keeps the sidebar.
		if [ "$d" = 1 ] && [ "$m" = "$MODERN_DETAIL" ]; then
			run_case "demo$d-d$m" --regress-demo="$d" --regress-detail="$m" --regress-demo-hud-check
		else
			run_case "demo$d-d$m" --regress-demo="$d" --regress-detail="$m"
		fi
	done
done

# --- synthetic level scenarios ----------------------------------------------

for spec in "${SCENARIOS[@]}"; do
	# shellcheck disable=SC2086
	set -- $spec
	sname=$1
	slvl=$2
	sframes=$3
	shift 3
	for m in "$@"; do
		pairs=$((pairs + 1))
		run_case "scenario-$sname-d$m" \
			--regress-level="$slvl" --regress-detail="$m" --regress-frames="$sframes"
	done
done

# --- modern presentation -----------------------------------------------------
#
# The Modern path composes on the CPU at the logical resolution; --regress-modern
# hashes its XRGB8888 canvas instead of the 8-bit frame.  Keep the subset small:
# all five demos at the effective Modern detail (Pentium, 4), plus every
# synthetic scenario at the same level (all six render paths are reachable at
# 4).  A Modern SuperWild case covers the cheat's effective level 6.

for d in $DEMOS; do
	pairs=$((pairs + 1))
	run_case "modern-demo$d-d$MODERN_DETAIL" --regress-demo="$d" --regress-detail="$MODERN_DETAIL" --regress-modern
done

# SuperWild (detail 6) is the one level Modern keeps from the player's choice;
# it is not a duplicate of the Pentium cases.
pairs=$((pairs + 1))
run_case "modern-demo1-d6" --regress-demo=1 --regress-detail=6 --regress-modern

for spec in "${SCENARIOS[@]}"; do
	# shellcheck disable=SC2086
	set -- $spec
	sname=$1
	slvl=$2
	sframes=$3
	pairs=$((pairs + 1))
	run_case "modern-scenario-$sname-d$MODERN_DETAIL" \
		--regress-level="$slvl" --regress-detail="$MODERN_DETAIL" --regress-frames="$sframes" --regress-modern
done

# --- modern widescreen presentation ------------------------------------------
#
# The same Modern canvas widened to 16:9 (original 1.2 pixel aspect), hashing
# the whole canvas including the side panels and the relocated HUD.  Three
# representative 16:9 cases keep the extra runtime small; the old modern-* cases
# above pin 4:3.  A 16:10 case is included because 16:10 now has wide-enough
# panels for the relocated HUD (60 px) and a different, compact layout.

run_case "modern-wide-demo1-d$MODERN_DETAIL" --regress-demo=1 --regress-detail="$MODERN_DETAIL" --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))
run_case "modern-wide-demo3-d$MODERN_DETAIL" --regress-demo=3 --regress-detail="$MODERN_DETAIL" --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))
run_case "modern-wide-scenario-spotlight-d$MODERN_DETAIL" \
	--regress-level=1:16 --regress-detail="$MODERN_DETAIL" --regress-frames=1200 --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))
run_case "modern-wide-16x10-demo1-d$MODERN_DETAIL" --regress-demo=1 --regress-detail="$MODERN_DETAIL" --regress-modern --regress-aspect=16:10
pairs=$((pairs + 1))

# Script-driven death: E1:L3 has no item screen before the level, so
# --regress-script reaches real gameplay (playDemo == false) and the
# un-invincible player sits still and dies, presenting the "GAME OVER" banner
# over the playfield.  --regress-seed makes that run reproducible (the script
# path otherwise inherits main()'s time(NULL) seed).  This guards the Modern
# centring of the banner on the playfield, which the demo/scenario paths cannot
# reach (they turn the !playDemo death into an end-of-level).
run_case "modern-wide-script-death-d$MODERN_DETAIL" \
	--regress-script=1:3 --regress-seed=32402394 --regress-detail="$MODERN_DETAIL" \
	--regress-frames=3700 --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))
# Same run with the ESC in-game menu opened on the last frame; owns the baseline
# the gameplay-composition menu case below is compared against.
run_case "modern-wide-script-menu-d$MODERN_DETAIL" \
	--regress-script=1:3 --regress-seed=32402394 --regress-detail="$MODERN_DETAIL" \
	--regress-frames=700 --regress-menu=ingame --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))

# --- analog stick integration -------------------------------------------------
#
# --regress-stick installs a synthetic analog stick and drives the whole
# JE_playerMovement stick path headless (regress never opens a device).  The
# Classic run locks the original reduction/momentum trajectory; the Modern run
# locks the dead-zone/response curve at the same raw full deflection, which the
# trajectory log (--regress-stick-log) shows to be identical for an axis.  See
# src/joystick.c and .worker-reports/analog.md.
run_case "stick-classic-script-d2" \
	--regress-script=1:3 --regress-seed=32402394 --regress-detail=2 \
	--regress-frames=600 --regress-stick=32767,0
pairs=$((pairs + 1))
run_case "stick-modern-script-d$MODERN_DETAIL" \
	--regress-script=1:3 --regress-seed=32402394 --regress-detail="$MODERN_DETAIL" \
	--regress-frames=600 --regress-modern --regress-stick=32767,0
pairs=$((pairs + 1))
# A partial deflection locks the progressive momentum: the curve scales the
# legacy +/-4 velocity cap by s, so 50% input tops out well below full speed.
run_case "stick-modern-partial-script-d$MODERN_DETAIL" \
	--regress-script=1:3 --regress-seed=32402394 --regress-detail="$MODERN_DETAIL" \
	--regress-frames=600 --regress-modern --regress-stick=16383,0
pairs=$((pairs + 1))
# Cross-axis noise: a full X push with a small -Y offset (the bottom wall hides
# +Y) must not drift diagonally, so the momentum target is split per axis and a
# near-axis push is snapped straight.  This would have failed before the fix.
run_case "stick-modern-noise-script-d$MODERN_DETAIL" \
	--regress-script=1:3 --regress-seed=32402394 --regress-detail="$MODERN_DETAIL" \
	--regress-frames=600 --regress-modern --regress-stick=32767,-300
pairs=$((pairs + 1))
# A 20% axial crawl must advance with a Bresenham-like step (consecutive ticks
# differ by at most 1 px), with no momentum bursts.  This would have failed
# before the fix (bursty 0,2,0,2...).
run_case "stick-modern-crawl-script-d$MODERN_DETAIL" \
	--regress-script=1:3 --regress-seed=32402394 --regress-detail="$MODERN_DETAIL" \
	--regress-frames=600 --regress-modern --regress-stick=6553,0
pairs=$((pairs + 1))

# --- Modern VFX (Fase 2) ------------------------------------------------------
#
# VFX are pinned OFF for every other case (so no pre-VFX baseline changes); these
# two opt into a level with the Modern 16:9 canvas and hash the composited
# particles.  One is a busy demo (the demo2 boss fight, many explosions and
# shots) at high, one a ground/scroll synthetic scenario (E4:L12) at low, so both
# new levels have a baseline.  Both must be reproducible run to run.  See
# src/vfx.c.

run_case "vfx-demo2-d$MODERN_DETAIL" --regress-demo=2 --regress-detail="$MODERN_DETAIL" --regress-modern --regress-aspect=16:9 --regress-vfx=high
pairs=$((pairs + 1))
run_case "vfx-scenario-flip-d$MODERN_DETAIL" \
	--regress-level=4:12 --regress-detail="$MODERN_DETAIL" --regress-frames=3600 --regress-modern --regress-aspect=16:9 --regress-vfx=low
pairs=$((pairs + 1))

# --- ambient atmosphere (Fase 2 VFX) -----------------------------------------
#
# Ambient particles ride the same Effects level as the VFX (off/low/high), so
# they run wherever VFX run.  One short Modern 16:9 case per style pins the
# level that selects it from the game's own read-only state (see
# src/vfx_ambient.c): E1:L4 is a starless ground level (dust), E1:L1 keeps the
# starfield (space), E4:L6 has lava (embers), E4:L9 water (mist) and E4:L8 the
# iced blur (snow).  Mixing low and high exercises both densities.  These own
# their baselines; the two vfx-* cases above change because ambient is now part
# of the same Effects layer.

run_case "ambient-dust-d$MODERN_DETAIL" \
	--regress-level=1:4 --regress-detail="$MODERN_DETAIL" --regress-frames=600 --regress-modern --regress-aspect=16:9 --regress-vfx=high
pairs=$((pairs + 1))
run_case "ambient-space-d$MODERN_DETAIL" \
	--regress-level=1:1 --regress-detail="$MODERN_DETAIL" --regress-frames=600 --regress-modern --regress-aspect=16:9 --regress-vfx=low
pairs=$((pairs + 1))
run_case "ambient-embers-d$MODERN_DETAIL" \
	--regress-level=4:6 --regress-detail="$MODERN_DETAIL" --regress-frames=600 --regress-modern --regress-aspect=16:9 --regress-vfx=high
pairs=$((pairs + 1))
run_case "ambient-mist-d$MODERN_DETAIL" \
	--regress-level=4:9 --regress-detail="$MODERN_DETAIL" --regress-frames=600 --regress-modern --regress-aspect=16:9 --regress-vfx=high
pairs=$((pairs + 1))
run_case "ambient-snow-d$MODERN_DETAIL" \
	--regress-level=4:8 --regress-detail="$MODERN_DETAIL" --regress-frames=600 --regress-modern --regress-aspect=16:9 --regress-vfx=low
pairs=$((pairs + 1))

# --- non-gameplay screens -----------------------------------------------------
#
# --regress-screen renders one non-gameplay screen in a deterministic state and
# exits after its frame cap (REGRESS_SCREEN_FRAMES).  The Modern 16:9 canvas
# guards the widescreen compositions added in step S1: Vert- for the title and
# the pic-2 menus (elements sharp at 1x), the widened pic-1 right panel (the
# extra columns inserted past the rightmost element), and the solid edge fill.
# The Classic runs of the same screens prove the harness itself does not alter
# the 8-bit screens.  The 21:9 cases guard the widest compositions.

SCREENS=(
	title episode-select high-scores game-menu upgrade purchase options
	cube-list cube-reader keyboard keyboard-long joystick joystick-multi load-save solid setup
)

for s in "${SCREENS[@]}"; do
	pairs=$((pairs + 1))
	run_case "modern-screen-$s-16x9" \
		--regress-screen="$s" --regress-modern --regress-aspect=16:9
	pairs=$((pairs + 1))
	run_case "screen-$s" --regress-screen="$s"
done

# Two simultaneous mappings must leave the pic-1 border free at every width.
for aspect in 21:9 32:9; do
	pairs=$((pairs + 1))
	run_case "modern-screen-joystick-multi-${aspect/:/x}" \
		--regress-screen=joystick-multi --regress-modern --regress-aspect="$aspect"
done

pairs=$((pairs + 1))
run_case "modern-screen-shield-16x9" \
	--regress-screen=shield --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))
run_case "modern-screen-shield-21x9" \
	--regress-screen=shield --regress-modern --regress-aspect=21:9

pairs=$((pairs + 1))
run_case "modern-screen-title-21x9" \
	--regress-screen=title --regress-modern --regress-aspect=21:9
pairs=$((pairs + 1))
run_case "modern-screen-game-menu-21x9" \
	--regress-screen=game-menu --regress-modern --regress-aspect=21:9

# Step S2: procedural screens whose content is extended across the canvas by
# code (starfield, grid, star map, credits).  The Modern 16:9 canvas guards the
# extension, the Classic run proves the 8-bit screen is untouched, and the 21:9
# cases guard the widest compositions (nav map and jukebox, the two the plan
# calls out).
S2_SCREENS=( nav-map ship-specs jukebox weapon-sim credits )

for s in "${S2_SCREENS[@]}"; do
	pairs=$((pairs + 1))
	run_case "modern-screen-$s-16x9" \
		--regress-screen="$s" --regress-modern --regress-aspect=16:9
	pairs=$((pairs + 1))
	run_case "screen-$s" --regress-screen="$s"
done

pairs=$((pairs + 1))
run_case "modern-screen-nav-map-21x9" \
	--regress-screen=nav-map --regress-modern --regress-aspect=21:9
pairs=$((pairs + 1))
run_case "modern-screen-jukebox-21x9" \
	--regress-screen=jukebox --regress-modern --regress-aspect=21:9

# --- Load / Save / Quit (S3) --------------------------------------------------
#
# The in-game Load screen is the "load-save" case above.  These cover the
# screens the user reported: the in-game Save (same pic-1 layout), the title
# Load Game (pic 2), and the quit confirmation dialog (over the pic-1 menu).
# The quit 21:9 case guards the centred Modern modal layer, and the ship-specs
# 21:9 case guards the full-width grid.

pairs=$((pairs + 1))
run_case "modern-screen-save-16x9" \
	--regress-screen=save --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))
run_case "screen-save" --regress-screen=save
pairs=$((pairs + 1))
run_case "modern-screen-save-21x9" \
	--regress-screen=save --regress-modern --regress-aspect=21:9

pairs=$((pairs + 1))
run_case "modern-screen-load-16x9" \
	--regress-screen=load --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))
run_case "screen-load" --regress-screen=load

pairs=$((pairs + 1))
run_case "modern-screen-quit-16x9" \
	--regress-screen=quit --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))
run_case "screen-quit" --regress-screen=quit
pairs=$((pairs + 1))
run_case "modern-screen-quit-21x9" \
	--regress-screen=quit --regress-modern --regress-aspect=21:9

pairs=$((pairs + 1))
run_case "modern-screen-ship-specs-21x9" \
	--regress-screen=ship-specs --regress-modern --regress-aspect=21:9

# --- Modern CRT output (hash the exact upload, phase reset at run start) -------
for crt in off scanlines ntsc scanlines+ntsc; do
	pairs=$((pairs + 1))
	run_case "modern-crt-$crt" --regress-level=1:16 --regress-frames=120 \
		--regress-modern --regress-aspect=16:9 --regress-crt="$crt" --regress-crt-height=1080
	done
pairs=$((pairs + 1))
run_case "modern-crt-both-2160" --regress-level=1:16 --regress-frames=120 \
	--regress-modern --regress-aspect=16:9 --regress-crt=scanlines+ntsc --regress-crt-height=2160
pairs=$((pairs + 1))
run_case "modern-screen-setup-crt-picker" --regress-screen=setup-crt-picker \
	--regress-modern --regress-aspect=16:9 --regress-crt=off

# --- modern bloom + dynamic lighting -----------------------------------------
#
# The bloom/dynamic-light pass is pinned OFF unless --regress-lighting opts in,
# so every case above is unaffected.  These two cases cover the effects on the
# 16:9 playfield: a demo with shots and explosions, and a smoothie scenario
# (lava + the player spotlight).  They use "high", which is the pre-merge "low"
# tuning (the strongest level now, after the user found even that too strong
# and asked for the weaker "low" to become the default).

run_case "modern-light-demo1-d$MODERN_DETAIL" \
	--regress-demo=1 --regress-detail="$MODERN_DETAIL" --regress-modern --regress-aspect=16:9 \
	--regress-lighting=high --regress-demo-hud-check
pairs=$((pairs + 1))
run_case "modern-light-scenario-spotlight-d$MODERN_DETAIL" \
	--regress-level=1:16 --regress-detail="$MODERN_DETAIL" --regress-frames=1200 --regress-modern --regress-aspect=16:9 \
	--regress-lighting=high
pairs=$((pairs + 1))

# --- game-state hashes --------------------------------------------------------
#
# The state-hash stream covers the RNG, the players, the enemy/shot arrays,
# boss_bar[], tempW and the level event position, and is independent of the
# presentation.  These cases run it through the Modern 16:9 path (where the
# relocated HUD is active) and compare against baselines that were produced by a
# Classic run, so any Modern change that perturbs game state fails here.  The
# state-scenario-spotlight-2p case additionally exercises the lives block of
# JE_inGameDisplays (and therefore its tempW writes) via --regress-players=2.
# See src/regress.c (regress_state_hash).

run_state_case() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_state_case "$@"
		return
	fi
	local label=$1
	shift
	local out="$ACTUAL_DIR/$label.txt"
	local log="$ACTUAL_DIR/$label.log"
	local baseline="$BASELINE_DIR/$label.txt"

	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
		run_binary --data="$DATA_DIR" --regress-state-out="$out" "$@" \
		>"$log" 2>&1
	rc=$?

	if [ "$rc" -ne 0 ] || [ ! -s "$out" ]; then
		echo "FAIL $label: $(exit_status_description "$rc") (no state output)"
		dump_failure_log "$log"
		failures=$((failures + 1))
		return
	fi

	lines=$(line_count "$out")

	if [ "$UPDATE" -eq 1 ]; then
		cp "$out" "$baseline"
		echo "UPDATE $label: $lines lines"
		return
	fi

	if [ ! -f "$baseline" ]; then
		echo "FAIL $label: missing baseline (run tools/regress.sh --update)"
		failures=$((failures + 1))
		return
	fi

	if cmp -s "$baseline" "$out"; then
		echo "PASS $label: $lines lines"
	else
		hunk=$(diff "$baseline" "$out" | head -n 1)
		first=${hunk%%[cad]*}
		first=${first%%,*}
		echo "FAIL $label: first differing line $((first - 1))"
		dump_failure_log "$log"
		failures=$((failures + 1))
	fi
}

run_state_case "state-demo1-d$MODERN_DETAIL" --regress-demo=1 --regress-detail="$MODERN_DETAIL" --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))
run_state_case "state-demo3-d$MODERN_DETAIL" --regress-demo=3 --regress-detail="$MODERN_DETAIL" --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))
run_state_case "state-scenario-spotlight-d$MODERN_DETAIL" \
	--regress-level=1:16 --regress-detail="$MODERN_DETAIL" --regress-frames=1200 --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))
run_state_case "state-scenario-spotlight-2p-d$MODERN_DETAIL" \
	--regress-level=1:16 --regress-detail="$MODERN_DETAIL" --regress-frames=1200 --regress-players=2 --regress-modern --regress-aspect=16:9
pairs=$((pairs + 1))

# --- draw-list replay check (Fase 2, stages 1-2) -----------------------------
#
# Runs the same case with the level draw list recorded every tick and replayed
# into a scratch surface; the run must reproduce every level frame byte for byte
# and, because recording only observes, its frame-hash stream must equal the
# Classic baseline it is compared against.  Representative cases run in every
# normal pass; the full sweep is `tools/regress.sh --replay-check`.

run_replay_case() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_replay_case "$@"
		return
	fi
	local label=$1 baseline_label=$2
	shift 2
	local out="$ACTUAL_DIR/$label.txt"
	local log="$ACTUAL_DIR/$label.log"
	local baseline="$BASELINE_DIR/$baseline_label.txt"

	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
		run_binary --data="$DATA_DIR" --regress-out="$out" --regress-replay-check "$@" \
		>"$log" 2>&1
	rc=$?

	if [ "$rc" -ne 0 ] || [ ! -s "$out" ]; then
		echo "FAIL $label: replay check failed ($(exit_status_description "$rc"))"
		grep -E "Replay check|mismatch" "$log" | tail -n 3
		dump_failure_log "$log"
		failures=$((failures + 1))
		return
	fi

	if [ ! -f "$baseline" ]; then
		echo "FAIL $label: missing baseline $baseline_label"
		failures=$((failures + 1))
		return
	fi

	if cmp -s "$baseline" "$out"; then
		echo "PASS $label: $(line_count "$out") lines, replay identical"
	else
		echo "FAIL $label: frame hashes differ from $baseline_label"
		dump_failure_log "$log"
		failures=$((failures + 1))
	fi
}

# run_interp_case LABEL BASELINE_LABEL "$@" -- interpolate-check one case.
# The stage-3 renderer must reproduce the real game_screen at alpha=1 (the run
# exits non-zero on any mismatch) and, because recording only observes, the
# frame-hash stream must still equal the Classic baseline.
run_interp_case() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_interp_case "$@"
		return
	fi
	local label=$1 baseline_label=$2
	shift 2
	local out="$ACTUAL_DIR/$label.txt"
	local log="$ACTUAL_DIR/$label.log"
	local baseline="$BASELINE_DIR/$baseline_label.txt"

	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
		run_binary --data="$DATA_DIR" --regress-out="$out" --regress-interp-check "$@" \
		>"$log" 2>&1
	rc=$?

	if [ "$rc" -ne 0 ] || [ ! -s "$out" ]; then
		echo "FAIL $label: interpolation check failed ($(exit_status_description "$rc"))"
		grep -E "Interp check|mismatch" "$log" | tail -n 3
		dump_failure_log "$log"
		failures=$((failures + 1))
		return
	fi

	if [ ! -f "$baseline" ]; then
		echo "FAIL $label: missing baseline $baseline_label"
		failures=$((failures + 1))
		return
	fi

	if cmp -s "$baseline" "$out"; then
		echo "PASS $label: $(line_count "$out") lines, interp alpha=1 identical"
	else
		echo "FAIL $label: frame hashes differ from $baseline_label"
		dump_failure_log "$log"
		failures=$((failures + 1))
	fi
}

# run_gameplay_case LABEL BASELINE_LABEL "$@" -- assert every presented in-level
# Modern frame uses the gameplay composition (drops the classic sidebar).  The
# run also emits the Modern canvas hash stream, which must still equal the
# Modern baseline for the same case.
run_gameplay_case() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_gameplay_case "$@"
		return
	fi
	local label=$1 baseline_label=$2
	shift 2
	local out="$ACTUAL_DIR/$label.txt"
	local log="$ACTUAL_DIR/$label.log"
	local baseline="$BASELINE_DIR/$baseline_label.txt"

	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
		run_binary --data="$DATA_DIR" --regress-out="$out" --regress-gameplay-check "$@" \
		>"$log" 2>&1
	rc=$?

	if [ "$rc" -ne 0 ] || [ ! -s "$out" ]; then
		echo "FAIL $label: gameplay composition check failed ($(exit_status_description "$rc"))"
		grep -E "Gameplay composition|FAILED" "$log" | tail -n 3
		dump_failure_log "$log"
		failures=$((failures + 1))
		return
	fi

	if [ ! -f "$baseline" ]; then
		echo "FAIL $label: missing baseline $baseline_label"
		failures=$((failures + 1))
		return
	fi

	if cmp -s "$baseline" "$out"; then
		echo "PASS $label: $(line_count "$out") lines, sidebar dropped"
	else
		echo "FAIL $label: frame hashes differ from $baseline_label"
		dump_failure_log "$log"
		failures=$((failures + 1))
	fi
}

# run_smoothness_case LABEL BASELINE_LABEL "$@" -- per tick, check that every
# background layer and matched object moves monotonically across the sub-frames
# (the run exits non-zero on any non-monotonic motion) and that the frame-hash
# stream still equals the Classic baseline.
run_smoothness_case() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_smoothness_case "$@"
		return
	fi
	local label=$1 baseline_label=$2
	shift 2
	local out="$ACTUAL_DIR/$label.txt"
	local log="$ACTUAL_DIR/$label.log"
	local baseline="$BASELINE_DIR/$baseline_label.txt"

	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
		run_binary --data="$DATA_DIR" --regress-out="$out" --regress-interp-smoothness "$@" \
		>"$log" 2>&1
	rc=$?

	if [ "$rc" -ne 0 ] || [ ! -s "$out" ]; then
		echo "FAIL $label: smoothness check failed ($(exit_status_description "$rc"))"
		grep -E "Smoothness|FAILED" "$log" | tail -n 3
		dump_failure_log "$log"
		failures=$((failures + 1))
		return
	fi

	if [ ! -f "$baseline" ]; then
		echo "FAIL $label: missing baseline $baseline_label"
		failures=$((failures + 1))
		return
	fi

	if cmp -s "$baseline" "$out"; then
		echo "PASS $label: $(line_count "$out") lines, motion monotonic"
	else
		echo "FAIL $label: frame hashes differ from $baseline_label"
		dump_failure_log "$log"
		failures=$((failures + 1))
	fi
}

# run_parallax_case LABEL BASELINE_LABEL "$@" -- assert the interpolated
# presentation never advances the starfield or the background scroll, so the
# per-tick motion is the same with smooth motion on or off.  The run also emits
# the frame-hash stream, which must still equal the Classic baseline.
run_parallax_case() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_parallax_case "$@"
		return
	fi
	local label=$1 baseline_label=$2
	shift 2
	local out="$ACTUAL_DIR/$label.txt"
	local log="$ACTUAL_DIR/$label.log"
	local baseline="$BASELINE_DIR/$baseline_label.txt"

	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
		run_binary --data="$DATA_DIR" --regress-out="$out" --regress-parallax-check "$@" \
		>"$log" 2>&1
	rc=$?

	if [ "$rc" -ne 0 ] || [ ! -f "$out" ]; then
		echo "FAIL $label: parallax check failed (exit $rc)"
		grep -E "Parallax|FAILED" "$log" | tail -n 3
		failures=$((failures + 1))
		return
	fi

	if [ ! -f "$baseline" ]; then
		echo "FAIL $label: missing baseline $baseline_label"
		failures=$((failures + 1))
		return
	fi

	if cmp -s "$baseline" "$out"; then
		echo "PASS $label: $(line_count "$out") lines, per-tick motion preserved"
	else
		echo "FAIL $label: frame hashes differ from $baseline_label"
		failures=$((failures + 1))
	fi
}

# run_parallax_level LABEL FRAMES EPISODE:LEVEL -- the reported ASTEROID levels
# have no frame baseline, so this only asserts the check's exit code and prints
# its summary.
run_parallax_level() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_parallax_level "$@"
		return
	fi
	local label=$1 frames=$2 lvl=$3
	local out="$ACTUAL_DIR/$label.txt"
	local log="$ACTUAL_DIR/$label.log"

	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
		run_binary --data="$DATA_DIR" --regress-out="$out" --regress-parallax-check \
		--regress-level="$lvl" --regress-frames="$frames" \
		>"$log" 2>&1
	rc=$?

	if [ "$rc" -ne 0 ]; then
		echo "FAIL $label: parallax check failed (exit $rc)"
		grep -E "Parallax|FAILED" "$log" | tail -n 3
		failures=$((failures + 1))
		return
	fi

	echo "PASS $label: $(grep -oE 'Parallax check: .*' "$log" | tail -n 1)"
}

# run_layer_case LABEL BASELINE_LABEL REQUIRE "$@" -- depth layer buffer, stage 1.
# tools/check_depth_layers.sh runs the scenario with the layer buffer stamped and
# checked every tick (--regress-layer-check) and without it: the frame and state
# hash streams must be identical (the layers change no pixel, no state, no RNG),
# match BASELINE_LABEL when one is given ("-" for none), and the log must carry
# "Layer check PASS" plus every REQUIRE coverage assertion, so the case proves
# that the paths it names really ran.
run_layer_case() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_layer_case "$@"
		return
	fi
	local label=$1 baseline_label=$2 require=$3
	shift 3
	local baseline=-
	[ "$baseline_label" = - ] || baseline="$BASELINE_DIR/$baseline_label.txt"

	local result rc
	result=$("$ROOT/tools/check_depth_layers.sh" "$BIN" 2.1 "$DATA_DIR" "$ACTUAL_DIR/layers" \
		"$label" "$baseline" "$require" -- "$@")
	rc=$?
	printf '%s\n' "$result"
	if [ "$rc" -ne 0 ]; then
		failures=$((failures + 1))
	fi
}

# run_depth_case LABEL OFF_BASELINE_LABEL LEVEL REQUIRE "$@" -- depth shadows, stage 2.
# tools/check_depth_shadows.sh runs the scenario with Depth off and on: the
# state/RNG streams must be identical (the shadows are display-only), the Off frames
# must equal OFF_BASELINE_LABEL when one is given ("-" for none), the LEVEL frames
# must equal this case's own committed baseline test/regress/LABEL.txt (written by
# --update), and the log must carry the REQUIRE coverage assertions on the
# "Depth shadows:" line, so the case proves the paths it names really ran.
run_depth_case() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_depth_case "$@"
		return
	fi
	local label=$1 off_label=$2 level=$3 require=$4
	shift 4
	local off_baseline=-
	[ "$off_label" = - ] || off_baseline="$BASELINE_DIR/$off_label.txt"

	local result rc
	result=$(DEPTH_UPDATE=$UPDATE "$ROOT/tools/check_depth_shadows.sh" "$BIN" 2.1 "$DATA_DIR" "$ACTUAL_DIR/depth" \
		"$label" "$off_baseline" "$BASELINE_DIR/$label.txt" "$level" "$require" -- "$@")
	rc=$?
	printf '%s\n' "$result"
	if [ "$rc" -ne 0 ]; then
		failures=$((failures + 1))
	fi
}

# run_held_case LABEL REQUIRE "$@" -- depth shadows and bloom/lighting on held in-level
# frames (the pause screen, the in-game menu, the in-game help).
# tools/check_depth_held.sh runs the scenario (which opens the screen on its last
# frame through --regress-menu) with every effect off, lighting alone, Depth On: the state/RNG streams must be identical, the held frame must change
# with the lighting and again with the shadows, the On frames must equal this case's
# own committed baseline test/regress/LABEL.txt (written by --update), and the log
# must carry the REQUIRE assertions on the "Depth held:", "Light held:" and
# "Held check:" lines, so the case proves the held frame really got shadows and light
# and that the overlay got none.
run_held_case() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_held_case "$@"
		pairs=$((pairs + 1))
		return
	fi
	local label=$1 require=$2
	shift 2

	local result rc
	result=$(HELD_UPDATE=$UPDATE "$ROOT/tools/check_depth_held.sh" "$BIN" 2.1 "$DATA_DIR" "$ACTUAL_DIR/held" \
		"$label" "$BASELINE_DIR/$label.txt" "$require" -- "$@")
	rc=$?
	printf '%s\n' "$result"
	if [ "$rc" -ne 0 ]; then
		failures=$((failures + 1))
	fi
}

# run_smooth_effects_case LABEL "$@" -- per level tick, present the palette fade
# and HUD bars interpolated at a genuine mid-tick alpha and require every value
# to stay between the two ticks (the run exits non-zero otherwise).  Check-only:
# the interpolated canvas is not compared against a baseline.
run_smooth_effects_case() {
	if [ "${CASE_WORKER:-0}" -eq 0 ]; then
		queue_case run_smooth_effects_case "$@"
		return
	fi
	local label=$1
	shift 1
	local out="$ACTUAL_DIR/$label.txt"
	local log="$ACTUAL_DIR/$label.log"

	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
		run_binary --data="$DATA_DIR" --regress-out="$out" --regress-smooth-effects-check \
		--regress-modern --regress-aspect=16:9 "$@" \
		>"$log" 2>&1
	rc=$?

	if [ "$rc" -ne 0 ]; then
		echo "FAIL $label: smooth-effects check failed (exit $rc)"
		grep -E "Smooth effects|FAILED" "$log" | tail -n 3
		dump_failure_log "$log"
		failures=$((failures + 1))
		return
	fi

	echo "PASS $label: $(grep -oE 'Smooth effects check: .*' "$log" | tail -n 1)"
}

# Depth shadows own committed baselines (their On frame hashes), so unlike the
# check-only cases below they also run under --update.
if [ "$REPLAY_CHECK" -eq 0 ] && [ "$INTERP_CHECK" -eq 0 ] && [ "$SMOOTH_CHECK" -eq 0 ] && [ "$PARALLAX_CHECK" -eq 0 ]; then
	M4="--regress-detail=$MODERN_DETAIL --regress-modern"
	# --- depth shadows (modern depth, stage 2) ----------------------------------
	#
	# Each case runs Depth off and on.  The two state/RNG streams must
	# match, Off must reproduce the existing baseline of the same scenario, and the
	# On frames are pinned by the case's own baseline.  The REQUIRE list
	# names the casters and paths that must really have run ("identical" = the
	# case must not shadow at all).
	run_depth_case "depth-wide-demo1-on-d$MODERN_DETAIL" "modern-wide-demo1-d$MODERN_DETAIL" on \
		"frames>0 shadowed_px>0 bg2>0 ground>0 sky>0 player>0 sidekick>0 bg3>0 top>0 blend>0 space_frames=0 fog.fogged_px>0" \
		--regress-demo=1 $M4 --regress-aspect=16:9
	pairs=$((pairs + 1))
	run_depth_case "depth-wide-demo5-on-d$MODERN_DETAIL" - on \
		"frames>0 shadowed_px>0 bg2>0 blend=0 ground>0 sky>0 player>0 sidekick>0 bg3>0 space_frames=0 fog.fogged_px>0" \
		--regress-demo=5 $M4 --regress-aspect=16:9
	pairs=$((pairs + 1))
	run_depth_case "depth-e1-level16-bg3-on-d$MODERN_DETAIL" - on \
		"frames>0 shadowed_px>0 bg3>0 top>0 sky>0 player>0 space_frames=0 fog.fogged_px>0" \
		--regress-level=1:16 $M4 --regress-frames=1200 --regress-aspect=16:9
	pairs=$((pairs + 1))
	run_depth_case "depth-e1-level16-2p-on-d$MODERN_DETAIL" - on \
		"frames>0 shadowed_px>0 player>0 sidekick>0 bg3>0 space_frames=0 fog.fogged_px>0" \
		--regress-level=1:16 $M4 --regress-frames=1200 --regress-players=2 --regress-aspect=16:9
	pairs=$((pairs + 1))
	run_depth_case "depth-scenario-water-on-d$MODERN_DETAIL" - on \
		"frames>0 shadowed_px>0 sky>0 player>0 blend>0 space_frames=0 fog.fogged_px>0" \
		--regress-level=4:9 $M4 --regress-frames=1200 --regress-fire --regress-aspect=16:9
	pairs=$((pairs + 1))
	run_depth_case "depth-scenario-flip-on-d$MODERN_DETAIL" - on \
		"frames>0 shadowed_px>0 bg2>0 sky>0 top>0 player>0 flipped>0 space_frames=0 fog.fogged_px>0" \
		--regress-level=4:12 $M4 --regress-frames=3600 --regress-fire --regress-aspect=16:9
	pairs=$((pairs + 1))
	run_depth_case "depth-demo4-space-on-d$MODERN_DETAIL" - on \
		"identical frames>0 shadowed_px=0 space_frames>0 bg3=0 top=0 sky=0 player=0 fog.fogged_px=0 fog.space_frames>0" \
		--regress-demo=4 $M4 --regress-aspect=16:9
	pairs=$((pairs + 1))
	run_depth_case "depth-e1-level1-space-on-d$MODERN_DETAIL" - on \
		"identical frames>0 shadowed_px=0 space_frames>0 fog.fogged_px=0 fog.space_frames>0" \
		--regress-level=1:1 $M4 --regress-frames=1200 --regress-fire --regress-aspect=16:9
	pairs=$((pairs + 1))
	# Modern 4:3: no side panels, the playfield sits at the canvas's own offset.
	run_depth_case "depth-demo1-4x3-on-d$MODERN_DETAIL" "modern-demo1-d$MODERN_DETAIL" on \
		"frames>0 shadowed_px>0 bg2>0 ground>0 sky>0 player>0 bg3>0 top>0 space_frames=0 fog.fogged_px>0" \
		--regress-demo=1 $M4
	pairs=$((pairs + 1))
	# 16:9 with smooth motion: the presented frames are the interpolated ones.
	run_depth_case "depth-smooth-wide-flip-on-d$MODERN_DETAIL" - on \
		"frames>0 shadowed_px>0 bg2>0 sky>0 player>0 interpolated>0 flipped>0 space_frames=0 fog.fogged_px>0" \
		--regress-level=4:12 $M4 --regress-frames=3600 --regress-fire --regress-aspect=16:9 --regress-interp-alpha=0.5
	pairs=$((pairs + 1))

	# HOLES (physical 11, script section 28): floating land on BG3 above BG1.
	run_depth_case "depth-holes-on-d$MODERN_DETAIL" - on \
		"frames>0 shadowed_px>0 bg3>0 bg2>0 blend>0 player>0 space_frames=0 fog.fogged_px>0" \
		--regress-level=1:11 $M4 --regress-frames=1200 --regress-aspect=16:9
	pairs=$((pairs + 1))
	# TYRIAN is physical 9, section 3, the first Full Game level (not physical 1).
	run_depth_case "depth-tyrian-on-d$MODERN_DETAIL" - on \
		"frames>0 shadowed_px>0 bg2>0 blend>0 ground>0 player>0 space_frames=0 fog.fogged_px>0" \
		--regress-level=1:9 $M4 --regress-frames=1200 --regress-aspect=16:9
	pairs=$((pairs + 1))

	# --- depth stage 3: fog on bg1 and per-layer light --------------------------
	#
	# With Lighting on, Depth Off must reproduce the committed lighting baselines of the
	# same scenarios (so the per-layer path leaves the old light bit for bit), and Depth On
	# must change the frames: fog on bg1, light weighted by layer ("light." keys read the
	# "Depth light:" line, "fog." keys the "Depth fog:" line).  A space level gets neither.
	run_depth_case "depth-light-demo1-on-d$MODERN_DETAIL" "modern-light-demo1-d$MODERN_DETAIL" on \
		"frames>0 fog.fogged_px>0 fog.blend_fogged_px>0 fog.space_frames=0 light.frames>0 light.reduced_px>0 light.bg1>0 light.bg2>0 light.ground>0 light.sky>0 light.player>0 light.bg3>0" \
		--regress-demo=1 $M4 --regress-aspect=16:9 --regress-lighting=high --regress-demo-hud-check
	pairs=$((pairs + 1))
	run_depth_case "depth-light-level16-on-d$MODERN_DETAIL" "modern-light-scenario-spotlight-d$MODERN_DETAIL" on \
		"frames>0 fog.fogged_px>0 light.frames>0 light.reduced_px>0 light.bg3>0 light.top>0 light.player>0" \
		--regress-level=1:16 $M4 --regress-frames=1200 --regress-aspect=16:9 --regress-lighting=high
	pairs=$((pairs + 1))
	run_depth_case "depth-light-holes-on-d$MODERN_DETAIL" - on \
		"frames>0 fog.fogged_px>0 fog.blend_fogged_px>0 light.reduced_px>0 light.bg3>0 light.bg1>0" \
		--regress-level=1:11 $M4 --regress-frames=1200 --regress-aspect=16:9 --regress-lighting=high
	pairs=$((pairs + 1))
	run_depth_case "depth-light-space-on-d$MODERN_DETAIL" - on \
		"identical fog.fogged_px=0 fog.frames=0 fog.space_frames>0 light.frames=0 light.reduced_px=0" \
		--regress-level=1:1 $M4 --regress-frames=1200 --regress-fire --regress-aspect=16:9 --regress-lighting=high
	pairs=$((pairs + 1))

	# --- held in-level screens: shadows and light kept on the frozen playfield ---
	#
	# --regress-menu opens the pause screen / in-game menu / in-game help on the
	# run's last frame.  The scenario is the pinned E1:L3 script (real gameplay).
	# Each case names what the held frame must have: shadowed pixels, emitters and
	# lit pixels, an overlay that stayed untouched, and a held frame that matches
	# the last live frame to within the fringe of what sits under the overlay.
	H4="--regress-script=1:3 --regress-seed=32402394 --regress-detail=$MODERN_DETAIL --regress-modern"
	run_held_case "depth-held-pause-wide-d$MODERN_DETAIL" \
		"frames=1 shadowed_px>0 emitter_px>0 lit_px>40000 overlay_px>0 overlay_changed_px=0 compared_px>40000 mismatch_max_dist<12 fogged_px>0 reduced_px>0" \
		$H4 --regress-frames=350 --regress-menu=pause --regress-aspect=16:9
	run_held_case "depth-held-ingame-wide-d$MODERN_DETAIL" \
		"frames=1 shadowed_px>0 emitter_px>0 lit_px>10000 overlay_px>20000 overlay_changed_px=0 compared_px>10000 mismatch_max_dist<20 fogged_px>0 reduced_px>0" \
		$H4 --regress-frames=1050 --regress-menu=ingame --regress-aspect=16:9
	run_held_case "depth-held-help-wide-d$MODERN_DETAIL" \
		"frames=1 shadowed_px>0 overlay_px>40000 overlay_changed_px=0 lit_px>0 mismatch_max_dist<4 fogged_px>0 reduced_px>0" \
		$H4 --regress-frames=350 --regress-menu=help --regress-aspect=16:9
	# Modern 4:3 (no side panels): the playfield sits at x = 0.
	run_held_case "depth-held-pause-4x3-d$MODERN_DETAIL" \
		"frames=1 shadowed_px>0 emitter_px>0 lit_px>40000 overlay_px>0 overlay_changed_px=0 mismatch_max_dist<12 fogged_px>0 reduced_px>0" \
		$H4 --regress-frames=350 --regress-menu=pause
	run_held_case "depth-held-ingame-21x9-d$MODERN_DETAIL" \
		"frames=1 shadowed_px>0 lit_px>10000 overlay_px>20000 overlay_changed_px=0 mismatch_max_dist<20 fogged_px>0 reduced_px>0" \
		$H4 --regress-frames=350 --regress-menu=ingame --regress-aspect=21:9
	# A starfield level: no shadow on a held frame either, but the held light stays.
	run_held_case "depth-held-pause-space-d$MODERN_DETAIL" \
		"frames=1 shadowed_px=0 space_frames=1 lit_px>5000 overlay_px>0 overlay_changed_px=0 mismatch_px=0 fogged_px=0 reduced_px=0" \
		--regress-script=1:5 --regress-seed=32402394 --regress-detail=$MODERN_DETAIL --regress-modern \
		--regress-frames=300 --regress-menu=pause --regress-aspect=16:9
	# Lava level with a big explosion under the PAUSED text.
	run_held_case "depth-held-ingame-lava-d$MODERN_DETAIL" \
		"frames=1 shadowed_px>0 emitter_px>0 lit_px>10000 overlay_px>20000 overlay_changed_px=0 mismatch_max_dist<40 fogged_px>0 reduced_px>0" \
		--regress-script=4:12 --regress-seed=32402394 --regress-detail=$MODERN_DETAIL --regress-modern \
		--regress-frames=2400 --regress-menu=ingame --regress-aspect=16:9
	# Smooth motion: the last live frame came from the interpolated renderer.
	run_held_case "depth-held-pause-smooth-d$MODERN_DETAIL" \
		"frames=1 shadowed_px>0 emitter_px>0 lit_px>40000 overlay_px>0 overlay_changed_px=0 mismatch_max_dist<12 fogged_px>0 reduced_px>0" \
		$H4 --regress-frames=350 --regress-menu=pause --regress-aspect=16:9 --regress-interp-alpha=0.5
fi

if [ "$UPDATE" -eq 0 ] && [ "$REPLAY_CHECK" -eq 0 ] && [ "$INTERP_CHECK" -eq 0 ] && [ "$SMOOTH_CHECK" -eq 0 ] && [ "$PARALLAX_CHECK" -eq 0 ]; then
	run_replay_case "replay-demo1-d2"   "demo1-d2"   --regress-demo=1 --regress-detail=2
	pairs=$((pairs + 1))
	run_replay_case "replay-demo3-d4"   "demo3-d4"   --regress-demo=3 --regress-detail=4
	pairs=$((pairs + 1))
	run_replay_case "replay-scenario-flip-d3" "scenario-flip-d3" \
		--regress-level=4:12 --regress-detail=3 --regress-frames=3600
	pairs=$((pairs + 1))
	run_replay_case "replay-scenario-iced-d2" "scenario-iced-d2" \
		--regress-level=4:8 --regress-detail=2 --regress-frames=1200
	pairs=$((pairs + 1))

	# --- interpolation identity check (Fase 2, stage 3) -------------------------
	#
	# Render every level tick's interpolated frame at alpha=1 with the persistent
	# stage-3 renderer and compare it byte for byte with the real game_screen.
	run_interp_case "interp-demo1-d2"   "demo1-d2"   --regress-demo=1 --regress-detail=2
	pairs=$((pairs + 1))
	run_interp_case "interp-demo3-d4"   "demo3-d4"   --regress-demo=3 --regress-detail=4
	pairs=$((pairs + 1))
	run_interp_case "interp-scenario-flip-d3" "scenario-flip-d3" \
		--regress-level=4:12 --regress-detail=3 --regress-frames=3600
	pairs=$((pairs + 1))
	run_interp_case "interp-scenario-iced-d2" "scenario-iced-d2" \
		--regress-level=4:8 --regress-detail=2 --regress-frames=1200
	pairs=$((pairs + 1))

	# --- smoothness check (Fase 2, stage 3) -------------------------------------
	#
	# Per level tick, re-derive the sub-frame positions and require every
	# background layer (including the ship-following pan that used to wrap) and
	# every matched object to move monotonically between the two ticks.
	run_smoothness_case "smoothness-demo1-d2" "demo1-d2" \
		--regress-demo=1 --regress-detail=2
	pairs=$((pairs + 1))
	run_smoothness_case "smoothness-scenario-flip-d3" "scenario-flip-d3" \
		--regress-level=4:12 --regress-detail=3 --regress-frames=3600
	pairs=$((pairs + 1))

	# --- parallax guard (starfield/background per-tick motion) ------------------
	#
	# The interpolated presentation must not advance the starfield or the
	# background scroll; the guard exits non-zero if it does, and the frame-hash
	# stream must still equal the Classic baseline.  The two ASTEROID levels are
	# the reported case and have no baseline, so they are check-only.
	run_parallax_case "parallax-demo1-d2" "demo1-d2" --regress-demo=1 --regress-detail=2
	pairs=$((pairs + 1))
	run_parallax_level "parallax-scenario-asteroid" 1200 "1:1"
	pairs=$((pairs + 1))
	run_parallax_level "parallax-scenario-asteroid2" 1200 "1:2"
	pairs=$((pairs + 1))

	# --- depth layer buffer (modern depth, stage 1) -----------------------------
	#
	# Each case stamps the per-pixel layer buffer, checks every tick that the
	# interpolated frame at alpha = 1 carries the same buffer and a consistent
	# first-drawn rank table, and proves layers on/off leave the frame and state
	# hashes alone.  The REQUIRE list names the paths that must really have run.
	M4="--regress-detail=$MODERN_DETAIL --regress-modern"
	run_layer_case "layer-wide-demo1-d$MODERN_DETAIL" "modern-wide-demo1-d$MODERN_DETAIL" \
		"bg1>0 bg2blend>0 bg3>0 ground>0 sky>0 top>0 player>0 sidekick>0 shots>0 enemyshots>0 explosion>0 orders>1 frames>0" \
		--regress-demo=1 $M4 --regress-aspect=16:9
	pairs=$((pairs + 1))
	# SuperWild (blended bg2 plus the darkening passes).
	run_layer_case "layer-demo1-d6" "modern-demo1-d6" "bg2blend>0 bg3>0" \
		--regress-demo=1 --regress-detail=6 --regress-modern
	pairs=$((pairs + 1))
	# Opaque bg2 (no blend) with a bg3.
	run_layer_case "layer-demo5-d$MODERN_DETAIL" "modern-demo5-d$MODERN_DETAIL" "bg2>0 bg2blend=0 bg3>0 orders>1" \
		--regress-demo=5 $M4
	pairs=$((pairs + 1))
	# Starfield and superpixels.
	run_layer_case "layer-demo4-d$MODERN_DETAIL" "modern-demo4-d$MODERN_DETAIL" "starfield>0 superpixel>0 shots>0" \
		--regress-demo=4 $M4
	pairs=$((pairs + 1))
	# The smoothie scenarios (each filter copies its layer from VGAScreen2), the
	# vertical flip and the player spotlight.
	run_layer_case "layer-scenario-water-d$MODERN_DETAIL" "modern-scenario-water-d$MODERN_DETAIL" "water>0 bg2blend>0" \
		--regress-level=4:9 $M4 --regress-frames=1200
	pairs=$((pairs + 1))
	run_layer_case "layer-scenario-flip-d$MODERN_DETAIL" "modern-scenario-flip-d$MODERN_DETAIL" "lava>0 flipped>0" \
		--regress-level=4:12 $M4 --regress-frames=3600
	pairs=$((pairs + 1))
	run_layer_case "layer-scenario-iced-d$MODERN_DETAIL" "modern-scenario-iced-d$MODERN_DETAIL" "iced>0" \
		--regress-level=4:8 $M4 --regress-frames=1200
	pairs=$((pairs + 1))
	run_layer_case "layer-scenario-blur-d$MODERN_DETAIL" "modern-scenario-blur-d$MODERN_DETAIL" "blur>0" \
		--regress-level=4:19 $M4 --regress-frames=1200
	pairs=$((pairs + 1))
	run_layer_case "layer-wide-spotlight-2p-d$MODERN_DETAIL" - "player>0 sidekick>0 lava>0" \
		--regress-level=1:16 $M4 --regress-frames=1200 --regress-players=2 --regress-aspect=16:9
	pairs=$((pairs + 1))
	run_layer_case "layer-wide-asteroid-fire-d$MODERN_DETAIL" - "starfield>0 shots>0 player>0" \
		--regress-level=1:1 $M4 --regress-frames=1200 --regress-fire --regress-aspect=16:9
	pairs=$((pairs + 1))
	# Smooth motion: the presented frames come from the interpolated renderer
	# (alpha = 0.5), so the presented layer buffer is the interpolated one, with
	# the flip, the lava filter and shots in it.
	run_layer_case "layer-smooth-wide-flip-d$MODERN_DETAIL" - "interpolated>0 flipped>0 lava>0 shots>0 frames>0" \
		--regress-level=4:12 $M4 --regress-frames=3600 --regress-fire --regress-aspect=16:9 --regress-interp-alpha=0.5
	pairs=$((pairs + 1))

	# --- dynamic fade/HUD interpolation (Fase 2, stage 4) -----------------------
	#
	# Present the palette fades and the dynamic HUD bars at a genuine mid-tick
	# alpha and require every interpolated value to stay between the two ticks
	# (the level run covers the intro palette fade; both cover the HUD bars).
	run_smooth_effects_case "smooth-effects-scenario-level1" \
		--regress-level=1:1 --regress-detail="$MODERN_DETAIL" --regress-frames=1200
	pairs=$((pairs + 1))
	run_smooth_effects_case "smooth-effects-demo2-d$MODERN_DETAIL" \
		--regress-demo=2 --regress-detail="$MODERN_DETAIL"
	pairs=$((pairs + 1))

	# --- gameplay composition check (Fase 2, bug B) -----------------------------
	#
	# Assert that no presented in-level Modern frame falls back to the full
	# 320x200 composition (the classic sidebar); the Modern canvas hash stream
	# must still equal the modern-wide baseline, i.e. the check only observes.
	run_gameplay_case "gameplay-wide-scenario-spotlight-d$MODERN_DETAIL" "modern-wide-scenario-spotlight-d$MODERN_DETAIL" \
		--regress-level=1:16 --regress-detail="$MODERN_DETAIL" --regress-frames=1200 --regress-modern --regress-aspect=16:9
	pairs=$((pairs + 1))

	# Attract demos follow the active mode's HUD: every in-level Modern demo frame
	# composes the filled side panels (the check also fails if none was seen).
	run_gameplay_case "demo-hud-wide-demo1-d$MODERN_DETAIL" "modern-wide-demo1-d$MODERN_DETAIL" \
		--regress-demo=1 --regress-detail="$MODERN_DETAIL" --regress-modern --regress-aspect=16:9 --regress-demo-hud-check
	pairs=$((pairs + 1))
	run_gameplay_case "demo-hud-wide-16x10-demo1-d$MODERN_DETAIL" "modern-wide-16x10-demo1-d$MODERN_DETAIL" \
		--regress-demo=1 --regress-detail="$MODERN_DETAIL" --regress-modern --regress-aspect=16:10 --regress-demo-hud-check
	pairs=$((pairs + 1))

	# In-game menu composition (pause-hud): --regress-menu opens the ESC in-game
	# menu on the run's last presented frame, so the gameplay-composition check
	# sees it presented over the Modern playfield + HUD panels instead of the
	# classic full-frame composition.  Same pinned E1:L3 script/seed as the death
	# case (real gameplay, playDemo == false); the menu replaces the final frame.
	# This fails if the pause/menu stops holding the gameplay composition.
	run_gameplay_case "gameplay-wide-script-menu-d$MODERN_DETAIL" "modern-wide-script-menu-d$MODERN_DETAIL" \
		--regress-script=1:3 --regress-seed=32402394 --regress-detail="$MODERN_DETAIL" \
		--regress-frames=700 --regress-menu=ingame --regress-modern --regress-aspect=16:9
	pairs=$((pairs + 1))
fi

# Full interpolation identity sweep: every demo and scenario at every detail
# level, comparing the alpha=1 interpolated frame with the real one.
# (tools/regress.sh --interp-check)
if [ "$INTERP_CHECK" -eq 1 ]; then
	for d in $DEMOS; do
		for m in $LEVELS; do
			run_interp_case "interp-demo$d-d$m" "demo$d-d$m" --regress-demo="$d" --regress-detail="$m"
		done
	done

	for spec in "${SCENARIOS[@]}"; do
		# shellcheck disable=SC2086
		set -- $spec
		sname=$1
		slvl=$2
		sframes=$3
		shift 3
		for m in "$@"; do
			run_interp_case "interp-scenario-$sname-d$m" "scenario-$sname-d$m" \
				--regress-level="$slvl" --regress-detail="$m" --regress-frames="$sframes"
		done
	done

	run_queued_cases

	total=$(awk "BEGIN { printf \"%.1f\", $(now) - $total_start }")
	if [ "$failures" -eq 0 ]; then
		echo "All interpolation-check cases passed in ${total}s."
		exit 0
	fi
	echo "$failures interpolation-check cases failed in ${total}s."
	exit 1
fi

# Full smoothness sweep: every demo and scenario at every detail level, checking
# the sub-frame motion of every background layer and matched object.
# (tools/regress.sh --smoothness-check)
if [ "$SMOOTH_CHECK" -eq 1 ]; then
	for d in $DEMOS; do
		for m in $LEVELS; do
			run_smoothness_case "smoothness-demo$d-d$m" "demo$d-d$m" --regress-demo="$d" --regress-detail="$m"
		done
	done

	for spec in "${SCENARIOS[@]}"; do
		# shellcheck disable=SC2086
		set -- $spec
		sname=$1
		slvl=$2
		sframes=$3
		shift 3
		for m in "$@"; do
			run_smoothness_case "smoothness-scenario-$sname-d$m" "scenario-$sname-d$m" \
				--regress-level="$slvl" --regress-detail="$m" --regress-frames="$sframes"
		done
	done

	run_queued_cases

	total=$(awk "BEGIN { printf \"%.1f\", $(now) - $total_start }")
	if [ "$failures" -eq 0 ]; then
		echo "All smoothness-check cases passed in ${total}s."
		exit 0
	fi
	echo "$failures smoothness-check cases failed in ${total}s."
	exit 1
fi

# Full parallax sweep: every demo and scenario at every detail level, requiring
# the interpolated presentation to leave the starfield/background scroll
# untouched (tools/regress.sh --parallax-check).
if [ "$PARALLAX_CHECK" -eq 1 ]; then
	for d in $DEMOS; do
		for m in $LEVELS; do
			run_parallax_case "parallax-demo$d-d$m" "demo$d-d$m" --regress-demo="$d" --regress-detail="$m"
		done
	done

	for spec in "${SCENARIOS[@]}"; do
		# shellcheck disable=SC2086
		set -- $spec
		sname=$1
		slvl=$2
		sframes=$3
		shift 3
		for m in "$@"; do
			run_parallax_case "parallax-scenario-$sname-d$m" "scenario-$sname-d$m" \
				--regress-level="$slvl" --regress-detail="$m" --regress-frames="$sframes"
		done
	done

	# The reported ASTEROID levels (check-only: no baseline).
	run_parallax_level "parallax-scenario-asteroid" 1200 "1:1"
	run_parallax_level "parallax-scenario-asteroid2" 1200 "1:2"

	run_queued_cases

	total=$(awk "BEGIN { printf \"%.1f\", $(now) - $total_start }")
	if [ "$failures" -eq 0 ]; then
		echo "All parallax-check cases passed in ${total}s."
		exit 0
	fi
	echo "$failures parallax-check cases failed in ${total}s."
	exit 1
fi

# Full replay sweep: every demo and scenario at every detail level.  This is the
# complete stages 1-2 proof (tools/regress.sh --replay-check).
if [ "$REPLAY_CHECK" -eq 1 ]; then
	for d in $DEMOS; do
		for m in $LEVELS; do
			run_replay_case "replay-demo$d-d$m" "demo$d-d$m" --regress-demo="$d" --regress-detail="$m"
		done
	done

	for spec in "${SCENARIOS[@]}"; do
		# shellcheck disable=SC2086
		set -- $spec
		sname=$1
		slvl=$2
		sframes=$3
		shift 3
		for m in "$@"; do
			run_replay_case "replay-scenario-$sname-d$m" "scenario-$sname-d$m" \
				--regress-level="$slvl" --regress-detail="$m" --regress-frames="$sframes"
		done
	done

	run_queued_cases

	total=$(awk "BEGIN { printf \"%.1f\", $(now) - $total_start }")
	if [ "$failures" -eq 0 ]; then
		echo "All replay-check cases passed in ${total}s."
		exit 0
	fi
	echo "$failures replay-check cases failed in ${total}s."
	exit 1
fi

# Quick pause aggregate reuses the pause rows of the full matrix baseline.
# Both state/RNG and presented frames are compared, with real menu coverage.
run_modern_pause() {
    local label=pause-pause out="$ACTUAL_DIR/modern-pause" crc bytes rc=0
    SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy run_binary --variant=2.1 --data="$DATA_DIR" \
        --regress-script=1:3 --regress-seed=32402394 --regress-detail=4 \
        --regress-frames=700 --regress-menu=pause --regress-modern --regress-aspect=16:9 \
        --regress-gameplay-check --regress-data-audit="$DATA_DIR" --regress-state-out="$out.state" --regress-out="$out.frames" > "$out.log" 2>&1 || rc=$?
    : > "$out.txt"
    if [ "$rc" -eq 0 ] && grep -Fq 'Menu coverage: in-level request' "$out.log" && grep -Fq 'Gameplay composition check:' "$out.log"; then
        read -r crc bytes _ < <(cksum "$out.state")
        printf '%s %s %s\n' "$label" "$bytes" "$crc" >> "$out.txt"
        read -r crc bytes _ < <(cksum "$out.frames")
        printf '%s %s %s\n' "$label-frames" "$bytes" "$crc" >> "$out.txt"
        grep '^pause-pause ' "$BASELINE_DIR/final-regression.txt" > "$out.expected"
        grep '^pause-pause-frames ' "$BASELINE_DIR/final-regression.txt" >> "$out.expected"
        if cmp -s "$out.expected" "$out.txt"; then echo "PASS modern-pause: state/frame aggregate and pause coverage"; return; fi
    fi
    echo "FAIL modern-pause: aggregate or coverage differs"
    failures=$((failures + 1))
}
if [ "$QUICK" -eq 1 ]; then queue_case run_modern_pause modern-pause; fi

# --- offline audio -----------------------------------------------------------

pairs=$((pairs + 1))
run_case "audio" --regress-audio

run_queued_cases
pairs=$case_count

if [ "$QUICK" -eq 1 ]; then
	REGRESS_SKIP_BUILD=1 "$ROOT/tools/regress-2000.sh" --quick --case="$CASE_FILTER" -j "$JOBS" || failures=$((failures + 1))
fi

total=$(awk "BEGIN { printf \"%.1f\", $(now) - $total_start }")

if [ "$UPDATE" -eq 1 ] && [ "$failures" -eq 0 ]; then
	echo "Baselines updated in $BASELINE_DIR ($case_count selected cases, ${total}s total)"
	exit 0
fi

if [ "$failures" -eq 0 ]; then
	echo "All $pairs regression cases passed in ${total}s."
	exit 0
fi

echo "$failures of $pairs regression cases failed in ${total}s."
exit 1
