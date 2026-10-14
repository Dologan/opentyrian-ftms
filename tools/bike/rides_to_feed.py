"""Convert a recorded Q200B ride (.raw.jsonl, FTMS Indoor Bike Data 0x2AD2) into a bike feed CSV.

Feed format (design.md section 4): header `tick,watts,cadence,connected`, one row per new
sample. tick = gameplay frame index relative to the first sample, rounded to the nearest frame
(frame = 28750 us at normal speed). Empty watts/cadence means null. connected is always 1 here.

usage: python3 -I rides_to_feed.py RAW.raw.jsonl OUT.csv [--samples SAMPLES.json]

Prints a JSON summary to stdout. With --samples, every 0x2AD2 packet is decoded and compared
with the bridge's samples.json row (t, cadenceRpm, powerW, resistance); any mismatch exits 1.
Stdlib only, so it runs under `python3 -I`.
"""

from __future__ import annotations

import json
import statistics
import sys

FRAME_US = 28750          # gameplay frame at normal speed
GAP_S = 3.0               # gap threshold reported in the summary
IND_BIKE_CHAR = "2ad2"    # Indoor Bike Data notification characteristic (short id)


def _u(b: bytes, off: int, n: int) -> int:
    return int.from_bytes(b[off:off + n], "little", signed=False)


def _s16(b: bytes, off: int) -> int:
    return int.from_bytes(b[off:off + 2], "little", signed=True)


def decode_indoor_bike(b: bytes) -> dict:
    """Decode one Indoor Bike Data packet per the FTMS layout. Returns only the fields present.

    Keys: cadence_rpm (0.5 rpm wire unit -> float), power_w, resistance. Other fields are
    skipped over by length but not returned. Raises ValueError if the packet is truncated.
    """
    if len(b) < 2:
        raise ValueError("short packet")
    flags = _u(b, 0, 2)
    off = 2
    out: dict = {}

    def need(n: int) -> None:
        if off + n > len(b):
            raise ValueError("truncated packet")

    if not (flags & 0x0001):          # bit 0 CLEAR => instantaneous speed present (uint16, 0.01 km/h)
        need(2); off += 2
    if flags & 0x0002:                # average speed
        need(2); off += 2
    if flags & 0x0004:                # instantaneous cadence, uint16 in 0.5 rpm
        need(2); out["cadence_rpm"] = _u(b, off, 2) * 0.5; off += 2
    if flags & 0x0008:                # average cadence
        need(2); off += 2
    if flags & 0x0010:                # total distance, uint24
        need(3); off += 3
    if flags & 0x0020:                # resistance level, sint16
        need(2); out["resistance"] = _s16(b, off); off += 2
    if flags & 0x0040:                # instantaneous power, sint16 watts
        need(2); out["power_w"] = _s16(b, off); off += 2
    if flags & 0x0080:                # average power
        need(2); off += 2
    if flags & 0x0100:                # total energy u16 + energy/h u16 + energy/min u8
        need(5); off += 5
    if flags & 0x0200:                # heart rate u8
        need(1); off += 1
    if flags & 0x0400:                # metabolic equivalent u8
        need(1); off += 1
    if flags & 0x0800:                # elapsed time u16
        need(2); off += 2
    if flags & 0x1000:                # remaining time u16
        need(2); off += 2
    return out


def load_packets(raw_path: str) -> tuple[list[tuple[int, bytes]], int]:
    """Return ([(t_us, payload)], n_other_lines) for every 0x2AD2 notification, in file order.

    Timestamps are the raw log's ms values converted to integer microseconds (the log has 0.1 ms
    resolution, so this is exact).
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
                continue                       # malformed lines are skipped, like the bridge reader
            if "char" not in obj:
                other += 1                     # event line (phase, status, device...)
                continue
            if obj["char"] != IND_BIKE_CHAR:
                other += 1
                continue
            packets.append((round(float(obj["t"]) * 1000), bytes.fromhex(obj["hex"])))
    return packets, other


def frame_tick(t_us: int, t0_us: int) -> int:
    """Nearest frame index of t_us relative to t0_us, half-up, in integer arithmetic."""
    delta = t_us - t0_us
    return (2 * delta + FRAME_US) // (2 * FRAME_US)


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    raw_path, out_path = argv[1], argv[2]
    samples_path = None
    if "--samples" in argv:
        samples_path = argv[argv.index("--samples") + 1]

    packets, other = load_packets(raw_path)
    if not packets:
        print("no 0x2AD2 packets", file=sys.stderr)
        return 1

    # Decode every packet once; a decode error is fatal (none expected in these rides).
    decoded = []
    for t_us, payload in packets:
        decoded.append((t_us, decode_indoor_bike(payload)))

    # Cross-check against the bridge's decode, one packet per row, before merging.
    mismatches = []
    if samples_path:
        with open(samples_path, encoding="utf-8") as fh:
            rows = json.load(fh)["samples"]
        if len(rows) != len(decoded):
            print(f"sample count differs: samples.json {len(rows)} vs packets {len(decoded)}",
                  file=sys.stderr)
            return 1
        for i, ((t_us, d), row) in enumerate(zip(decoded, rows)):
            rt, rcad, rpw, rres = row
            ok = (
                abs(round(t_us / 1000) / 1000 - rt) <= 0.006
                and d.get("cadence_rpm") == rcad
                and d.get("power_w") == int(rpw)
                and d.get("resistance") == int(rres)
            )
            if not ok:
                mismatches.append({"index": i, "raw_t_ms": t_us / 1000,
                                   "decoded": d, "samples_json": row})
        if mismatches:
            print(json.dumps({"cross_check": "FAIL", "mismatches": mismatches[:10],
                              "count": len(mismatches)}, indent=2), file=sys.stderr)
            return 1

    # Merge packets that land on the same frame into one sample: latest value per field wins.
    t0_us = decoded[0][0]
    samples: list[dict] = []                   # {tick, t_us, watts, cadence, rounded}
    rounded_cadence = 0
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
            samples.append({"tick": tick, "t_us": t_us,
                            "watts": d.get("power_w"), "cadence": d.get("cadence_rpm")})

    # Integer rounding of watts/cadence (half-up). The design sets no rule; these rides are
    # already integer-valued, so this count is expected to be 0.
    rows = []
    for s in samples:
        w = s["watts"]
        c = s["cadence"]
        if c is not None and c != int(c):
            rounded_cadence += 1
        w_i = None if w is None else int(w)
        c_i = None if c is None else int(c + 0.5)
        rows.append((s["tick"], w_i, c_i))

    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("tick,watts,cadence,connected\n")
        for tick, w_i, c_i in rows:
            fh.write(f"{tick},{'' if w_i is None else w_i},{'' if c_i is None else c_i},1\n")

    # Summary.
    watts = [r[1] for r in rows if r[1] is not None]
    cads = [r[2] for r in rows if r[2] is not None]
    gaps = []
    for a, b in zip(samples, samples[1:]):
        dt_s = (b["t_us"] - a["t_us"]) / 1e6
        if dt_s > GAP_S:
            gaps.append({"from_s": round((a["t_us"] - t0_us) / 1e6, 2),
                         "to_s": round((b["t_us"] - t0_us) / 1e6, 2),
                         "gap_s": round(dt_s, 2)})
    summary = {
        "source": raw_path,
        "output": out_path,
        "packets_2ad2": len(packets),
        "other_lines": other,
        "samples": len(rows),
        "merged_packets": len(packets) - len(samples),
        "duration_s": round((samples[-1]["t_us"] - t0_us) / 1e6, 2),
        "watts": {"min": min(watts), "mean": round(statistics.fmean(watts), 1), "max": max(watts),
                  "null_rows": len(rows) - len(watts)} if watts else None,
        "cadence": {"min": min(cads), "max": max(cads), "null_rows": len(rows) - len(cads)}
        if cads else None,
        "gaps_over_3s": gaps,
        "rounded_cadence_values": rounded_cadence,
        "cross_check": ("PASS (%d packets vs samples.json)" % len(decoded)) if samples_path else "not run",
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
