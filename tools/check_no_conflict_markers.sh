#!/bin/bash
# Guard: no committed merge-conflict markers in tracked text files.
# A leftover marker once broke visualc/opentyrian.vcxproj (invalid XML) without
# any Linux/macOS/MSYS2 CI job noticing.
#
#   tools/check_no_conflict_markers.sh              check tracked files
#   tools/check_no_conflict_markers.sh --self-test  prove the check on a synthetic file
set -eu

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATTERN=$'^(<<<<<<<( |\r?$)|>>>>>>>( |\r?$)|=======\r?$)'

scan() { # scan DIR: lists offending lines, returns 1 if any
	local dir=$1 found=0
	while IFS= read -r -d '' f; do
		case "$f" in *.png|*.jpg|*.ico|*.icns|*.zip|*.ogg|*.wav|*.ttf|*.otf|*.a|*.lib|*.dll) continue ;; esac
		[ -f "$dir/$f" ] || continue
		if grep -Iq . "$dir/$f" 2>/dev/null && grep -nE -- "$PATTERN" "$dir/$f" | sed "s|^|$f:|" | grep .; then
			found=1
		fi
	done < <(git -C "$dir" ls-files -z)
	return $found
}

if [ "${1:-}" = "--self-test" ]; then
	T=$(mktemp -d "${TMPDIR:-/tmp}/conflict-test.XXXXXX")
	trap 'rm -rf "$T"' EXIT
	git -C "$T" init -q
	printf 'ok\n' >"$T/clean.txt"
	git -C "$T" add clean.txt
	scan "$T" >/dev/null || { echo "FAIL: clean tree flagged" >&2; exit 1; }
	printf 'a\r\n<<<<<<< HEAD\r\nb\r\n=======\r\nc\r\n>>>>>>> branch\r\n' >"$T/bad.xml"
	git -C "$T" add bad.xml
	if scan "$T" >/dev/null; then echo "FAIL: markers not detected" >&2; exit 1; fi
	echo "conflict-marker guard self-test: PASS"
	exit 0
fi

if ! scan "$ROOT"; then
	echo "ERROR: merge-conflict markers found in tracked files" >&2
	exit 1
fi
echo "conflict markers: none"
