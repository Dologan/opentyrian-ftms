"""Vendor Flywheel's shared test data and tools into this repo, and check they were not edited here.

Flywheel (MIT) owns the effort-engine conformance data, the protocol tolerance fixtures and the
trace-to-feed converter. This repo keeps pinned copies so its tests need no Flywheel checkout:

    flywheel/shared/conformance/effort/feeds/   ->  test/bike/feeds/
    flywheel/shared/conformance/effort/golden/  ->  test/bike/golden/   (c_digests.txt stays ours)
    flywheel/shared/protocol/fixtures/tolerance ->  test/bike/protocol/tolerance/
    flywheel/shared/protocol/fixtures/rider     ->  test/bike/protocol/rider/
    flywheel/bridge/tools/trace_to_feed.py      ->  tools/bike/trace_to_feed.py

usage:
    python3 -I tools/bike/vendor.py sync FLYWHEEL_DIR   copy the files, rewrite test/bike/VENDORED_FROM
    python3 -I tools/bike/vendor.py verify               check the files still match VENDORED_FROM

`verify` runs in tools/check_bike_effort.sh (so in `make regress`): a hand edit of a vendored file,
a missing file or an extra one fails the check. Change the data in Flywheel, then re-sync.
Line endings are normalised before hashing, so a CRLF checkout on Windows still verifies.
Standard library only.
"""

from __future__ import annotations

import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / "test" / "bike" / "VENDORED_FROM"
SOURCE_URL = "https://github.com/Dologan/flywheel"

# (flywheel path, path here, is a directory)
MAP = [
    ("shared/conformance/effort/feeds", "test/bike/feeds", True),
    ("shared/conformance/effort/golden", "test/bike/golden", True),
    ("shared/protocol/fixtures/tolerance", "test/bike/protocol/tolerance", True),
    ("shared/protocol/fixtures/rider", "test/bike/protocol/rider", True),
    ("bridge/tools/trace_to_feed.py", "tools/bike/trace_to_feed.py", False),
]
# Files inside a vendored directory that belong to this repo and are never touched.
OURS = {"test/bike/golden/c_digests.txt"}
ENGINE_VERSION_RE = re.compile(r"^# effort-engine-version: (\S+)$", re.MULTILINE)


def _digest(path: Path) -> str:
    data = path.read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha256(data).hexdigest()


def _rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def vendored_files() -> list[Path]:
    """Every file here that is (or should be) a copy of a Flywheel file."""
    out: list[Path] = []
    for _, dst, is_dir in MAP:
        target = ROOT / dst
        if is_dir:
            if target.is_dir():
                out += [p for p in target.rglob("*") if p.is_file() and _rel(p) not in OURS]
        elif target.is_file():
            out.append(target)
    return sorted(out, key=_rel)


def _tree_digest(entries: dict[str, str]) -> str:
    text = "".join(f"{name} {digest}\n" for name, digest in sorted(entries.items()))
    return hashlib.sha256(text.encode()).hexdigest()


def sync(flywheel: Path) -> int:
    if not (flywheel / "shared" / "conformance" / "effort").is_dir():
        print(f"{flywheel} is not a Flywheel checkout with shared/conformance/effort", file=sys.stderr)
        return 2
    for src_rel, dst_rel, is_dir in MAP:
        src, dst = flywheel / src_rel, ROOT / dst_rel
        if not src.exists():
            print(f"missing in Flywheel: {src_rel}", file=sys.stderr)
            return 2
        if is_dir:
            dst.mkdir(parents=True, exist_ok=True)
            wanted = {p.relative_to(src).as_posix() for p in src.rglob("*") if p.is_file()}
            for old in [p for p in dst.rglob("*") if p.is_file() and _rel(p) not in OURS]:
                if old.relative_to(dst).as_posix() not in wanted:
                    old.unlink()
            for name in sorted(wanted):
                (dst / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(src / name, dst / name)
        else:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)

    commit = subprocess.run(
        ["git", "-C", str(flywheel), "rev-parse", "HEAD"], capture_output=True, text=True, check=False
    ).stdout.strip()
    dirty = subprocess.run(
        ["git", "-C", str(flywheel), "status", "--porcelain", "--", *[m[0] for m in MAP]],
        capture_output=True,
        text=True,
        check=False,
    ).stdout.strip()
    manifest = (ROOT / "test/bike/golden/manifest.txt").read_text()
    version = ENGINE_VERSION_RE.search(manifest)
    entries = {_rel(p): _digest(p) for p in vendored_files()}
    lines = [
        "# Pinned copies of files owned by Flywheel (MIT). Do not edit them here: change Flywheel,",
        "# then re-sync with `python3 -I tools/bike/vendor.py sync <flywheel checkout>`.",
        "# tools/check_bike_effort.sh verifies this record (`vendor.py verify`).",
        f"source {SOURCE_URL}",
        f"commit {commit or 'unknown'}{'+uncommitted' if dirty else ''}",
        f"effort-engine-version {version.group(1) if version else 'unknown'}",
        f"tree-sha256 {_tree_digest(entries)}",
        *[f"file {name} {digest}" for name, digest in sorted(entries.items())],
    ]
    RECORD.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"vendored {len(entries)} files from {flywheel} ({lines[4]})")
    if dirty:
        print("warning: Flywheel has uncommitted changes in the vendored paths", file=sys.stderr)
    return 0


def verify() -> int:
    if not RECORD.is_file():
        print(f"FAIL vendored: {_rel(RECORD)} is missing", file=sys.stderr)
        return 1
    recorded: dict[str, str] = {}
    tree = None
    for line in RECORD.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "file":
            recorded[parts[1]] = parts[2]
        elif len(parts) == 2 and parts[0] == "tree-sha256":
            tree = parts[1]
    actual = {_rel(p): _digest(p) for p in vendored_files()}
    problems = [f"edited: {n}" for n in sorted(recorded) if n in actual and actual[n] != recorded[n]]
    problems += [f"missing: {n}" for n in sorted(recorded) if n not in actual]
    problems += [f"not in the record: {n}" for n in sorted(actual) if n not in recorded]
    if not recorded or tree is None or _tree_digest(recorded) != tree:
        problems.append(f"{_rel(RECORD)} itself was edited or is incomplete")
    if problems:
        print("FAIL vendored: files copied from Flywheel differ from their record")
        for p in problems[:20]:
            print(f"  {p}")
        print("  Change the data in Flywheel and re-sync (tools/bike/vendor.py sync <flywheel>).")
        return 1
    print(f"vendored files verified: {len(actual)}")
    return 0


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[1] == "sync":
        return sync(Path(argv[2]).resolve())
    if len(argv) == 2 and argv[1] == "verify":
        return verify()
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
