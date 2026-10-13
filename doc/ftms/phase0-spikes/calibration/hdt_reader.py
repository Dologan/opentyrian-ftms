#!/usr/bin/env python3
"""Read item tables from Tyrian 2.1 tyrian.hdt (format: src/episodes.c JE_loadItemDat,
doc/tyrian.hdt.txt).  Run with:  python3 -I hdt_reader.py <path/to/tyrian.hdt | tyrian4.lvl> [outdir]

Writes (to outdir, default = script dir):
  items_generators.csv  items_weapon_ports.csv  items_port_levels.csv
  items_shields.csv     items_ships.csv         items_sidekicks.csv
and prints a short summary.  Also importable: load_hdt(path) -> dict.
"""
import struct
import sys
import os
import csv

WEAP_NUM, PORT_NUM, POWER_NUM, SHIP_NUM, OPTION_NUM, SHIELD_NUM, ENEMY_NUM = 780, 42, 6, 13, 30, 10, 850
SPECIAL_NUM = 46
FPS = 1000.0 / (2 * 14.375)   # frameCountMax=2 ticks of 14.375 ms (gameSpeed "Normal")

WEAPON_FMT = "<HBBHBBBB 8B8B8b8b8b8b8H bbBBBB"   # 0x50 bytes, as in doc/tools/weapon.py


class R:
    def __init__(self, data, pos):
        self.d, self.p = data, pos

    def u(self, fmt):
        v = struct.unpack_from("<" + fmt, self.d, self.p)
        self.p += struct.calcsize("<" + fmt)
        return v

    def pstr(self):
        n = self.u("B")[0]
        raw = self.d[self.p:self.p + 30]
        self.p += 30
        return raw[:min(n, 30)].split(b"\0")[0].decode("latin-1").strip()


def ep4_item_offset(lvl_data):
    """Episode 4 keeps its item block in tyrian4.lvl at lvlPos[lvlNum-1] (episodes.c:87, lvllib.c:41-51)."""
    n = struct.unpack_from("<H", lvl_data, 0)[0]
    return struct.unpack_from("<I", lvl_data, 2 + 4 * (n - 1))[0]


def load_hdt(path, episode4=False):
    """episode4=True: `path` is tyrian4.lvl (Tyrian 2000 item block) instead of tyrian.hdt."""
    data = open(path, "rb").read()
    if episode4:
        pos = ep4_item_offset(data)
    else:
        pos = struct.unpack_from("<I", data, 0)[0]   # episode 1 data offset (=32 in 2.1)
    r = R(data, pos)
    counts = r.u("7H")
    assert counts == (WEAP_NUM, PORT_NUM, POWER_NUM, SHIP_NUM, OPTION_NUM, SHIELD_NUM, ENEMY_NUM), counts

    weapons = []
    ws = struct.calcsize(WEAPON_FMT)
    assert ws == 0x50
    for i in range(WEAP_NUM + 1):
        t = struct.unpack_from(WEAPON_FMT, data, r.p)
        r.p += ws
        weapons.append(dict(
            id=i, drain=t[0], shotrepeat=t[1], multi=t[2], weapani=t[3], max=t[4], aim=t[7],
            attack=list(t[8:16]), sx=list(t[24:32]), sy=list(t[32:40]),
            sound=t[-3]))
    ports = []
    for i in range(PORT_NUM + 1):
        name = r.pstr()
        opnum = r.u("B")[0]
        op0 = list(r.u("11H"))
        op1 = list(r.u("11H"))
        cost, itemgraphic, poweruse = r.u("HHH")
        ports.append(dict(id=i, name=name, opnum=opnum, op=[op0, op1], cost=cost, poweruse=poweruse))
    specials = []
    for i in range(SPECIAL_NUM + 1):
        name = r.pstr()
        ig, pwr, stype, wpn = r.u("HBBH")
        specials.append(dict(id=i, name=name, pwr=pwr, stype=stype, wpn=wpn))
    gens = []
    for i in range(POWER_NUM + 1):
        name = r.pstr()
        ig, power, speed, cost = r.u("HBbH")
        gens.append(dict(id=i, name=name, power=power, speed=speed, cost=cost))
    ships = []
    for i in range(SHIP_NUM + 1):
        name = r.pstr()
        sg, ig, ani, spd, dmg, cost, big = r.u("HHBbBHB")
        ships.append(dict(id=i, name=name, spd=spd, armor=dmg, cost=cost))
    opts = []
    for i in range(OPTION_NUM + 1):
        name = r.pstr()
        pwr, ig, cost, tr, option, opspd, ani = r.u("BHHBBbB")
        r.u("20H")
        wport, wpnum, ammo, stop, icongr = r.u("BHBBB")
        opts.append(dict(id=i, name=name, pwr=pwr, cost=cost, tr=tr, wport=wport, wpnum=wpnum, ammo=ammo))
    shields = []
    for i in range(SHIELD_NUM + 1):
        name = r.pstr()
        tpwr, mpwr, ig, cost = r.u("BBHH")
        shields.append(dict(id=i, name=name, tpwr=tpwr, mpwr=mpwr, cost=cost))
    return dict(weapons=weapons, ports=ports, specials=specials, gens=gens, ships=ships,
                options=opts, shields=shields, end_pos=r.p)


def shots_per_s(shotrepeat, fps=FPS):
    """player_shot_create sets shotRepeat=shotrepeat; the main loop decrements it once per
    frame while >0 and fires on the frame it is 0 -> period = shotrepeat+1 frames."""
    return fps / (shotrepeat + 1)


def write_csv(path, header, rows):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(__file__))
    h = load_hdt(sys.argv[1], episode4=sys.argv[1].lower().endswith(".lvl"))
    W = h["weapons"]
    write_csv(os.path.join(out, "items_generators.csv"),
              ["id", "name", "power_per_frame", "units_per_s", "cost"],
              [[g["id"], g["name"], g["power"], round(g["power"] * FPS, 1), g["cost"]] for g in h["gens"]])
    write_csv(os.path.join(out, "items_weapon_ports.csv"),
              ["id", "name", "poweruse", "cost", "opnum"],
              [[p["id"], p["name"], p["poweruse"], p["cost"], p["opnum"]] for p in h["ports"]])
    rows = []
    for p in h["ports"]:
        for mode in range(max(1, p["opnum"])):
            for lvl in range(11):
                wi = p["op"][mode][lvl]
                if wi == 0:
                    continue
                w = W[wi]
                sps = shots_per_s(w["shotrepeat"])
                rows.append([p["id"], p["name"], mode + 1, lvl + 1, wi, w["shotrepeat"], w["multi"],
                             w["attack"][0], round(sps, 2), p["poweruse"], round(p["poweruse"] * sps, 1)])
    write_csv(os.path.join(out, "items_port_levels.csv"),
              ["port", "name", "mode", "level", "weapon_idx", "shotrepeat", "multi", "attack0",
               "shots_per_s", "poweruse", "drain_units_per_s"], rows)
    write_csv(os.path.join(out, "items_shields.csv"),
              ["id", "name", "tpwr", "mpwr", "cost", "regen_units_per_s"],
              [[s["id"], s["name"], s["tpwr"], s["mpwr"], s["cost"], round(s["tpwr"] * 20 / 15 * FPS, 1)]
               for s in h["shields"]])
    write_csv(os.path.join(out, "items_ships.csv"), ["id", "name", "spd", "armor", "cost"],
              [[s["id"], s["name"], s["spd"], s["armor"], s["cost"]] for s in h["ships"]])
    write_csv(os.path.join(out, "items_sidekicks.csv"),
              ["id", "name", "pwr", "cost", "tr", "wport", "wpnum", "ammo", "port_poweruse", "shotrepeat",
               "drain_units_per_s"],
              [[o["id"], o["name"], o["pwr"], o["cost"], o["tr"], o["wport"], o["wpnum"], o["ammo"],
                h["ports"][o["wport"]]["poweruse"] if o["wport"] else "",
                W[o["wpnum"]]["shotrepeat"] if o["wpnum"] else "",
                round(h["ports"][o["wport"]]["poweruse"] * shots_per_s(W[o["wpnum"]]["shotrepeat"]), 1)
                if o["wport"] and o["wpnum"] else ""] for o in h["options"]])
    print("fps = %.3f" % FPS)
    print("generators:")
    for g in h["gens"]:
        print("  %d %-28s power=%d speed=%d cost=%d  -> %.1f u/s" % (g["id"], g["name"], g["power"], g["speed"], g["cost"], g["power"] * FPS))
    print("shields:")
    for s in h["shields"]:
        print("  %d %-28s tpwr=%d mpwr=%d cost=%d" % (s["id"], s["name"], s["tpwr"], s["mpwr"], s["cost"]))
    print("ships:")
    for s in h["ships"]:
        print("  %d %-28s spd=%d armor=%d cost=%d" % (s["id"], s["name"], s["spd"], s["armor"], s["cost"]))
    print("ports:", len(h["ports"]), "consumed up to byte", h["end_pos"])


if __name__ == "__main__":
    main()
