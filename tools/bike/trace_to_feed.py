"""Convert a recording into a game-fork *feed* CSV (deterministic bike input for game tests).

Feed format, one row per new sample (see docs/protocol.md, "Recordings"):

    tick,watts,cadence,connected

``tick`` is the game frame index counted from the first sample, in frames of 28.75 ms (normal
game speed), rounded half up. An empty ``watts`` or ``cadence`` means null (not reported);
``connected=0`` means the link is down from that tick. Several samples landing on the same frame
become one row, the latest value of each field winning.

Two inputs, told apart by the first line:

* a raw ride log (``.raw.jsonl``, what the bike sent; FTMS Indoor Bike Data is decoded here),
  one row per notification;
* a protocol trace (``.trace.ndjson``, what ``flywheel-bridge run --trace`` recorded), one row
  per telemetry message, plus a ``connected=0`` row when the link leaves ``streaming``.

usage: python3 -I trace_to_feed.py INPUT OUT.csv

Standard library only, so it also runs where the bridge's dependencies are not installed.
Prints a JSON summary to stdout.
"""

from __future__ import annotations

import json
import statistics
import sys
from typing import Any

FRAME_US = 28750  # one game frame at normal speed
GAP_S = 3.0  # gaps longer than this are listed in the summary
IND_BIKE_CHAR = "2ad2"  # Indoor Bike Data notification characteristic (short id)

# A feed row: (tick, watts, cadence, connected); watts/cadence are int or None.
Row = tuple[int, int | None, int | None, int]


def _u(b: bytes, off: int, n: int) -> int:
    return int.from_bytes(b[off : off + n], "little", signed=False)


def _s16(b: bytes, off: int) -> int:
    return int.from_bytes(b[off : off + 2], "little", signed=True)


def decode_indoor_bike(b: bytes) -> dict[str, Any]:
    """Decode one Indoor Bike Data packet per the FTMS layout; only the fields present.

    Keys: cadence_rpm (0.5 rpm wire unit -> float), power_w, resistance. Other fields are skipped
    by length. Raises ValueError if the packet is truncated.
    """
    if len(b) < 2:
        raise ValueError("short packet")
    flags = _u(b, 0, 2)
    off = 2
    out: dict[str, Any] = {}

    def need(n: int) -> None:
        if off + n > len(b):
            raise ValueError("truncated packet")

    if not (flags & 0x0001):  # bit 0 CLEAR => instantaneous speed present (uint16, 0.01 km/h)
        need(2)
        off += 2
    if flags & 0x0002:  # average speed
        need(2)
        off += 2
    if flags & 0x0004:  # instantaneous cadence, uint16 in 0.5 rpm
        need(2)
        out["cadence_rpm"] = _u(b, off, 2) * 0.5
        off += 2
    if flags & 0x0008:  # average cadence
        need(2)
        off += 2
    if flags & 0x0010:  # total distance, uint24
        need(3)
        off += 3
    if flags & 0x0020:  # resistance level, sint16
        need(2)
        out["resistance"] = _s16(b, off)
        off += 2
    if flags & 0x0040:  # instantaneous power, sint16 watts
        need(2)
        out["power_w"] = _s16(b, off)
        off += 2
    if flags & 0x0080:  # average power
        need(2)
        off += 2
    if flags & 0x0100:  # total energy u16 + energy/h u16 + energy/min u8
        need(5)
        off += 5
    if flags & 0x0200:  # heart rate u8
        need(1)
        off += 1
    if flags & 0x0400:  # metabolic equivalent u8
        need(1)
        off += 1
    if flags & 0x0800:  # elapsed time u16
        need(2)
        off += 2
    if flags & 0x1000:  # remaining time u16
        need(2)
        off += 2
    return out


def load_packets(raw_path: str) -> tuple[list[tuple[int, bytes]], int]:
    """([(t_us, payload)], n_other_lines) for every 0x2AD2 notification of a raw log, in order.

    Timestamps are the log's ms values as integer microseconds (the log has 0.1 ms resolution,
    so this is exact). Malformed lines are skipped, like the bridge's reader does.
    """
    packets: list[tuple[int, bytes]] = []
    other = 0
    with open(raw_path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except ValueError:
                continue
            if "char" not in obj or obj["char"] != IND_BIKE_CHAR:
                other += 1
                continue
            packets.append((round(float(obj["t"]) * 1000), bytes.fromhex(obj["hex"])))
    return packets, other


def frame_tick(t_us: int, t0_us: int) -> int:
    """Nearest frame index of t_us relative to t0_us, half up, in integer arithmetic."""
    return (2 * (t_us - t0_us) + FRAME_US) // (2 * FRAME_US)


def _round_half_up(x: float | None) -> int | None:
    return None if x is None else int(x + 0.5)


def rows_from_raw(raw_path: str) -> tuple[list[Row], dict[str, Any]]:
    """Feed rows from a raw ride log. Returns (rows, summary extras)."""
    packets, other = load_packets(raw_path)
    if not packets:
        raise ValueError("no 0x2AD2 packets")
    decoded = [(t_us, decode_indoor_bike(payload)) for t_us, payload in packets]
    t0_us = decoded[0][0]
    samples: list[dict[str, Any]] = []  # {tick, t_us, watts, cadence}
    for t_us, d in decoded:
        tick = frame_tick(t_us, t0_us)
        if samples and samples[-1]["tick"] == tick:
            cur = samples[-1]
            cur["t_us"] = t_us
            if "power_w" in d:
                cur["watts"] = d["power_w"]
            if "cadence_rpm" in d:
                cur["cadence"] = d["cadence_rpm"]
        else:
            samples.append(
                {
                    "tick": tick,
                    "t_us": t_us,
                    "watts": d.get("power_w"),
                    "cadence": d.get("cadence_rpm"),
                }
            )
    rounded_cadence = 0
    rows: list[Row] = []
    for s in samples:
        w, c = s["watts"], s["cadence"]
        if c is not None and c != int(c):
            rounded_cadence += 1
        rows.append((s["tick"], None if w is None else int(w), _round_half_up(c), 1))
    gaps = []
    for a, b in zip(samples, samples[1:], strict=False):
        dt_s = (b["t_us"] - a["t_us"]) / 1e6
        if dt_s > GAP_S:
            gaps.append(
                {
                    "from_s": round((a["t_us"] - t0_us) / 1e6, 2),
                    "to_s": round((b["t_us"] - t0_us) / 1e6, 2),
                    "gap_s": round(dt_s, 2),
                }
            )
    return rows, {
        "input": "raw",
        "packets_2ad2": len(packets),
        "other_lines": other,
        "merged_packets": len(packets) - len(samples),
        "duration_s": round((samples[-1]["t_us"] - t0_us) / 1e6, 2),
        "gaps_over_3s": gaps,
        "rounded_cadence_values": rounded_cadence,
    }


def rows_from_trace(trace_path: str) -> tuple[list[Row], dict[str, Any]]:
    """Feed rows from a protocol trace (one per telemetry message, plus link-down rows)."""
    msgs: list[tuple[float, dict[str, Any]]] = []
    with open(trace_path, encoding="utf-8") as fh:
        first = fh.readline()
        try:
            header = json.loads(first)
        except ValueError:
            header = None
        if not isinstance(header, dict) or "trace" not in header:
            raise ValueError(f"{trace_path} is not a protocol trace")
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                at, msg = float(obj["at"]), obj["msg"]
            except (ValueError, KeyError, TypeError):
                continue  # a trace cut short by a crash still converts
            if isinstance(msg, dict):
                msgs.append((at, msg))

    rows: list[Row] = []
    t0_us: int | None = None
    link_up = False
    telemetry = 0
    for at_ms, msg in msgs:
        kind = msg.get("type")
        if kind == "telemetry" and msg.get("v") == 1:
            t_us = round(at_ms * 1000)
            if t0_us is None:
                t0_us = t_us
            w, c = msg.get("powerW"), msg.get("cadenceRpm")
            row: Row = (
                frame_tick(t_us, t0_us),
                None if w is None else int(w + 0.5),
                _round_half_up(c),
                1,
            )
            link_up = True
            telemetry += 1
        elif kind == "status" and t0_us is not None and link_up and msg.get("state") != "streaming":
            row = (frame_tick(round(at_ms * 1000), t0_us), None, None, 0)
            link_up = False
        else:
            continue
        if rows and rows[-1][0] == row[0]:
            rows[-1] = row  # same frame: the latest wins, ticks stay strictly ascending
        else:
            rows.append(row)
    if telemetry == 0:
        raise ValueError("no telemetry messages in the trace")
    return rows, {
        "input": "trace",
        "telemetry_messages": telemetry,
        "link_down_rows": sum(1 for r in rows if r[3] == 0),
        "duration_s": round(rows[-1][0] * FRAME_US / 1e6, 2),
    }


def is_trace(path: str) -> bool:
    with open(path, encoding="utf-8") as fh:
        try:
            first = json.loads(fh.readline())
        except ValueError:
            return False
    return isinstance(first, dict) and "trace" in first


def write_feed(rows: list[Row], out_path: str) -> None:
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("tick,watts,cadence,connected\n")
        for tick, w, c, up in rows:
            fh.write(f"{tick},{'' if w is None else w},{'' if c is None else c},{up}\n")


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    src, out = argv[1], argv[2]
    try:
        rows, extra = rows_from_trace(src) if is_trace(src) else rows_from_raw(src)
    except (OSError, ValueError) as exc:
        print(f"{src}: {exc}", file=sys.stderr)
        return 1
    write_feed(rows, out)
    watts = [r[1] for r in rows if r[1] is not None]
    cads = [r[2] for r in rows if r[2] is not None]
    summary = {
        "source": src,
        "output": out,
        "rows": len(rows),
        **extra,
        "watts": {
            "min": min(watts),
            "mean": round(statistics.fmean(watts), 1),
            "max": max(watts),
            "null_rows": len(rows) - len(watts),
        }
        if watts
        else None,
        "cadence": {"min": min(cads), "max": max(cads), "null_rows": len(rows) - len(cads)}
        if cads
        else None,
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
