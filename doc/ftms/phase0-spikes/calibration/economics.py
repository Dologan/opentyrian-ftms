#!/usr/bin/env python3
"""Per-second economics + candidate watts->generator mappings.
Run:  python3 -I economics.py <path/to/tyrian.hdt> [outdir]
Writes economics_tables.md (generated Markdown tables used by ANALYSIS.md) and prints it.
Imports hdt_reader.py from its own directory (sys.path is set explicitly because of -I).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hdt_reader as H  # noqa: E402

FPS = H.FPS
SHIELD_PERIOD = 15          # frames between shield regen steps (tyrian2.c shieldWait)
POOL_MAX = 900

# ---- Flywheel effortNorm curve (effort-engine.ts EFFORT_NORM_KNOTS) -------------------------
KNOTS = [(0.0, 0.1), (0.4, 0.15), (0.75, 0.5), (1.1, 1.0)]


def piecewise(x, knots=KNOTS):
    if x <= knots[0][0]:
        return knots[0][1]
    if x >= knots[-1][0]:
        return knots[-1][1]
    for (x0, y0), (x1, y1) in zip(knots, knots[1:]):
        if x <= x1:
            return y0 + (x - x0) / (x1 - x0) * (y1 - y0)


def n_of(r):
    return piecewise(r)


# ---- candidate mappings: all return power units PER FRAME ------------------------------------
FLOOR_PF = 8.0      # ~ one Pulse-Cannon L1 (7.5 u/frame) -> starting weapon always sustainable
K_ABS = 0.1         # units/frame per watt  (250 W == 25 == Standard MicroFusion)
G_REF = 25.0        # tier used by mapping B


def map_a(watts, ftp, gen_pf):
    return max(FLOOR_PF, K_ABS * watts)


def map_b(watts, ftp, gen_pf):
    r = watts / ftp
    return max(FLOOR_PF, G_REF * n_of(r) / n_of(1.0))


def map_c(watts, ftp, gen_pf):
    r = watts / ftp
    return max(FLOOR_PF, gen_pf * (0.5 + n_of(r)))


def main():
    path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else HERE
    h = H.load_hdt(path)
    W, P, G, S, O, SH = h["weapons"], h["ports"], h["gens"], h["ships"], h["options"], h["shields"]
    L = []
    w = L.append

    def pf_port(pid, lvl, mode=1):
        p = P[pid]
        wi = p["op"][mode - 1][lvl - 1]
        return p["poweruse"] / (W[wi]["shotrepeat"] + 1)

    def pf_side(oid):
        o = O[oid]
        return P[o["wport"]]["poweruse"] / (W[o["wpnum"]]["shotrepeat"] + 1)

    # ---- T1 generators
    w("### T1. Generators (`powerSys`, `power += powerAdd` once per frame)\n")
    w("| id | name | power/frame | units/s (x%.2f) | s to fill 0->900 | cost | = watts at k=0.1 u/frame/W |" % FPS)
    w("|---|---|---|---|---|---|---|")
    for g in G[1:]:
        w("| %d | %s | %d | %.0f | %.1f | %d | %d W |" % (g["id"], g["name"], g["power"], g["power"] * FPS,
                                                          POOL_MAX / g["power"] / FPS, g["cost"], g["power"] * 10))
    w("")

    # ---- T2 shields
    w("### T2. Shields (`shieldT = tpwr*20`, +1 shield every 15 frames while shield < 2*mpwr and `power > shieldT`)\n")
    w("| id | name | tpwr | mpwr | shield max (2*mpwr) | drain while regenerating (u/frame) | (u/s) | full refill 0->max: time / pool units |")
    w("|---|---|---|---|---|---|---|---|")
    for s in SH[1:]:
        pf = s["tpwr"] * 20 / SHIELD_PERIOD
        smax = s["mpwr"] * 2
        w("| %d | %s | %d | %d | %d | %.2f | %.0f | %.1f s / %d |" % (s["id"], s["name"], s["tpwr"], s["mpwr"], smax, pf, pf * FPS,
                                                                    smax * SHIELD_PERIOD / FPS, smax * s["tpwr"] * 20))
    w("")

    # ---- T3 ships
    w("### T3. Ships\n")
    w("| id | name | armor (`dmg`) | spd (unused by engine) | cost |")
    w("|---|---|---|---|---|")
    for s in S[1:]:
        w("| %d | %s | %d | %d | %d |" % (s["id"], s["name"], s["armor"], s["spd"], s["cost"]))
    w("")

    # ---- T4 weapon ports
    w("### T4. Weapon ports: continuous-fire drain in power units per frame (poweruse / (shotrepeat+1)); x34.78 for units/s\n")
    w("| port | name | poweruse | cost | L1 | L3 | L6 | L11 | (mode 2: L1 / L6) |")
    w("|---|---|---|---|---|---|---|---|---|")
    for p in P[1:]:
        def cell(l, m=1):
            if p["op"][m - 1][l - 1] == 0:
                return "-"
            return "%.2f" % pf_port(p["id"], l, m)
        m2 = ("%s / %s" % (cell(1, 2), cell(6, 2))) if p["opnum"] == 2 else ""
        w("| %d | %s | %d | %d | %s | %s | %s | %s | %s |" % (p["id"], p["name"], p["poweruse"], p["cost"],
                                                          cell(1), cell(3), cell(6), cell(11), m2))
    w("")

    # ---- T5 sidekicks
    w("### T5. Sidekicks (all fire through `player_shot_create`, so they drain too)\n")
    w("| id | name | port (poweruse) | shotrepeat | drain u/frame | ammo |")
    w("|---|---|---|---|---|---|")
    for o in O[1:]:
        if o["wport"] and o["wpnum"]:
            w("| %d | %s | %d (%d) | %d | %.2f | %s |" % (o["id"], o["name"], o["wport"], P[o["wport"]]["poweruse"],
                                                         W[o["wpnum"]]["shotrepeat"], pf_side(o["id"]), o["ammo"] or "inf"))
    w("")

    # ---- T6 stage loadouts
    stages = [
        ("S0 New game (levels 1-2)", dict(front=(1, 1), rear=None, sk=[], gen=2, shield=4)),
        ("S1 Early (~$10-30k)", dict(front=(2, 3), rear=(15, 2), sk=[1, 1], gen=2, shield=4)),
        ("S2 Mid (~$50-100k)", dict(front=(5, 5), rear=(21, 4), sk=[4, 4], gen=4, shield=6)),
        ("S3 Late (~$200k+)", dict(front=(27, 6), rear=(22, 6), sk=[17, 4], gen=5, shield=8)),
        ("S4 Max-burn (worst case)", dict(front=(8, 11), rear=(21, 11), sk=[17, 17], gen=6, shield=10)),
    ]
    w("### T6. Illustrative loadouts per game stage: demand vs. stock generator (all weapons firing continuously)\n")
    w("Loadouts are my own illustrative picks (ids from T4/T5), not extracted from game data.\n")
    w("| stage | front (port, lvl) | rear | sidekicks | weapon drain u/frame | u/s | stock gen (u/frame) | sustainable fire % (no shield regen) | s to drain full pool at 100% fire | shield regen u/frame (while healing) | % fire while also healing shield |")
    w("|---|---|---|---|---|---|---|---|---|---|---|")
    stage_rows = []
    for name, st in stages:
        d = pf_port(*st["front"])
        rear = pf_port(*st["rear"]) if st["rear"] else 0.0
        sk = sum(pf_side(i) for i in st["sk"])
        tot = d + rear + sk
        gp = G[st["gen"]]["power"]
        sreg = SH[st["shield"]]["tpwr"] * 20 / SHIELD_PERIOD
        stage_rows.append((name, st, tot, gp, sreg))
        fr = "%s L%d" % (P[st["front"][0]]["name"], st["front"][1])
        re = ("%s L%d" % (P[st["rear"][0]]["name"], st["rear"][1])) if st["rear"] else "-"
        sks = ", ".join(O[i]["name"] for i in st["sk"]) or "-"
        defi = tot - gp
        tdrain = ("%.1f" % (POOL_MAX / defi / FPS)) if defi > 0 else "inf"
        w("| %s | %s (%.1f) | %s (%.1f) | %s (%.1f) | %.1f | %.0f | %s %d | %.0f%% | %s | %.1f | %.0f%% |" % (
            name, fr, d, re, rear, sks, sk, tot, tot * FPS, G[st["gen"]]["name"], gp,
            min(100, 100 * gp / tot) if tot else 100, tdrain, sreg, min(100, 100 * max(gp - sreg, 0) / tot)))
    w("")

    # ---- T7 mapping examples
    riders_w = [100, 150, 200, 250]
    ftps = [150, 250]
    w("### T7. Candidate mappings: power units PER FRAME handed to `power += ...` (steady-state smoothed watts)\n")
    w("Definitions: r = W/FTP; n(r) = Flywheel `effortNorm` piecewise curve through (0,0.10) (0.40,0.15) (0.75,0.50) (1.10,1.00), flat above 1.10.")
    w("- A absolute: `max(8, 0.1*W)`  (generator item ignored)")
    w("- B FTP-relative, fixed tier: `max(8, 25 * n(r)/n(1.0))`, n(1.0)=%.3f  (generator item ignored; r=1.0 -> 25 = MicroFusion)" % n_of(1.0))
    w("- C generator-scaled: `max(8, G * (0.5 + n(r)))`, G = stock `powerSys[gen].power` (r=0.75 -> exactly stock; r>=1.1 -> 1.5x; r=0 -> 0.6x)\n")
    w("| FTP | watts | r | n(r) | A | B | C gen2 (G=14) | C gen4 (G=25) | C gen6 (G=50) | stock gen2 / gen4 / gen6 |")
    w("|---|---|---|---|---|---|---|---|---|---|")
    for f in ftps:
        for wt in riders_w:
            r = wt / f
            w("| %d | %d | %.2f | %.2f | %.1f | %.1f | %.1f | %.1f | %.1f | 14 / 25 / 50 |" % (
                f, wt, r, n_of(r), map_a(wt, f, 0), map_b(wt, f, 0), map_c(wt, f, 14), map_c(wt, f, 25), map_c(wt, f, 50)))
    w("")

    w("### T8. Same riders: % of continuous fire sustained (income / demand, capped at 100%) for S1 and S2 loadouts\n")
    w("S1 demand %.1f u/frame with gen2 (stock 14 -> %.0f%%); S2 demand %.1f u/frame with gen4 (stock 25 -> %.0f%%).\n" % (
        stage_rows[1][2], 100 * 14 / stage_rows[1][2], stage_rows[2][2], 100 * 25 / stage_rows[2][2]))
    w("| FTP | watts | A: S1 / S2 | B: S1 / S2 | C: S1 (gen2) / S2 (gen4) |")
    w("|---|---|---|---|---|")
    d1, d2 = stage_rows[1][2], stage_rows[2][2]
    for f in ftps:
        for wt in riders_w:
            a, b = map_a(wt, f, 0), map_b(wt, f, 0)
            c1, c2 = map_c(wt, f, 14), map_c(wt, f, 25)
            pc = lambda x, d: "%.0f%%" % min(100, 100 * x / d)
            w("| %d | %d | %s / %s | %s / %s | %s / %s |" % (f, wt, pc(a, d1), pc(a, d2), pc(b, d1), pc(b, d2), pc(c1, d1), pc(c2, d2)))
    w("")

    w("### T9. Time to fill an empty pool (power = 0 at every level start, tyrian2.c:747) at various incomes\n")
    w("| income u/frame | 5 | 8 | 10 | 14 | 19 | 25 | 30 | 50 |")
    w("|---|---|---|---|---|---|---|---|---|")
    w("| seconds to 900 | " + " | ".join("%.1f" % (POOL_MAX / x / FPS) for x in (5, 8, 10, 14, 19, 25, 30, 50)) + " |")
    w("")

    txt = "\n".join(L)
    with open(os.path.join(out, "economics_tables.md"), "w") as f:
        f.write(txt + "\n")
    print(txt)


if __name__ == "__main__":
    main()
