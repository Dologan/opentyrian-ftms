### T1. Generators (`powerSys`, `power += powerAdd` once per frame)

| id | name | power/frame | units/s (x34.78) | s to fill 0->900 | cost | = watts at k=0.1 u/frame/W |
|---|---|---|---|---|---|---|
| 1 | Standard MR-9 | 10 | 348 | 2.6 | 500 | 100 W |
| 2 | Advanced MR-12 | 14 | 487 | 1.8 | 2000 | 140 W |
| 3 | Gencore Custom MR-12 | 19 | 661 | 1.4 | 5000 | 190 W |
| 4 | Standard MicroFusion | 25 | 870 | 1.0 | 10000 | 250 W |
| 5 | Advanced MircoFusion | 30 | 1043 | 0.9 | 15000 | 300 W |
| 6 | Gravitron Pulse-Wave | 50 | 1739 | 0.5 | 50000 | 500 W |

### T2. Shields (`shieldT = tpwr*20`, +1 shield every 15 frames while shield < 2*mpwr and `power > shieldT`)

| id | name | tpwr | mpwr | shield max (2*mpwr) | drain while regenerating (u/frame) | (u/s) | full refill 0->max: time / pool units |
|---|---|---|---|---|---|---|---|
| 1 | Structural Integrity Field | 5 | 2 | 4 | 6.67 | 232 | 1.7 s / 400 |
| 2 | Advanced Integrity Field | 3 | 3 | 6 | 4.00 | 139 | 2.6 s / 360 |
| 3 | Gencore Low Energy Shield | 4 | 4 | 8 | 5.33 | 186 | 3.5 s / 640 |
| 4 | Gencore High Energy Shield | 6 | 5 | 10 | 8.00 | 278 | 4.3 s / 1200 |
| 5 | MicroCorp LXS Class A | 8 | 6 | 12 | 10.67 | 371 | 5.2 s / 1920 |
| 6 | MicroCorp LXS Class B | 6 | 7 | 14 | 8.00 | 278 | 6.0 s / 1680 |
| 7 | MicroCorp LXS Class C | 5 | 9 | 18 | 6.67 | 232 | 7.8 s / 1800 |
| 8 | MicroCorp HXS Class A | 9 | 10 | 20 | 12.00 | 417 | 8.6 s / 3600 |
| 9 | MicroCorp HXS Class B | 7 | 12 | 24 | 9.33 | 325 | 10.4 s / 3360 |
| 10 | MicroCorp HXS Class C | 6 | 14 | 28 | 8.00 | 278 | 12.1 s / 3360 |

### T3. Ships

| id | name | armor (`dmg`) | spd (unused by engine) | cost |
|---|---|---|---|---|
| 1 | USP Talon   Light Fighter | 10 | 0 | 6000 |
| 2 | SuperCarrot | 26 | -2 | 65000 |
| 3 | Gencore Phoenix | 17 | -1 | 12000 |
| 4 | Gencore Maelstrom | 20 | 0 | 15000 |
| 5 | MicroCorp Stalker | 25 | -2 | 20000 |
| 6 | MicroCorp Stalker-B | 27 | -2 | 25000 |
| 7 | Prototype Stalker-C | 28 | 0 | 50000 |
| 8 | Stalker | 15 | 0 | 10000 |
| 9 | USP Fang    Light Fighter | 14 | 0 | 8000 |
| 10 | U-Ship | 8 | 0 | 8000 |
| 11 | Silver Ship | 10 | 0 | 8000 |
| 12 | Nort Ship | 30 | 0 | 65000 |
| 13 | The Stalker 21.126 | 30 | 0 | 65535 |

### T4. Weapon ports: continuous-fire drain in power units per frame (poweruse / (shotrepeat+1)); x34.78 for units/s

| port | name | poweruse | cost | L1 | L3 | L6 | L11 | (mode 2: L1 / L6) |
|---|---|---|---|---|---|---|---|---|
| 1 | Pulse-Cannon | 30 | 500 | 7.50 | 6.00 | 5.00 | 5.00 |  |
| 2 | Multi-Cannon | 40 | 750 | 10.00 | 8.00 | 6.67 | 6.67 |  |
| 3 | Mega Cannon | 120 | 1000 | 13.33 | 13.33 | 13.33 | 13.33 |  |
| 4 | Laser | 20 | 900 | 20.00 | 20.00 | 20.00 | 20.00 |  |
| 5 | Zica Laser | 90 | 1100 | 9.00 | 10.00 | 11.25 | 11.25 |  |
| 6 | Protron Z | 50 | 900 | 10.00 | 10.00 | 10.00 | 10.00 |  |
| 7 | Vulcan Cannon | 20 | 600 | 10.00 | 10.00 | 10.00 | 20.00 |  |
| 8 | Lightning Cannon | 70 | 1000 | 11.67 | 11.67 | 11.67 | 35.00 |  |
| 9 | Starburst | 20 | 900 | 6.67 | 10.00 | 6.67 | 6.67 |  |
| 10 | Multi-Cannon | 40 | 750 | 8.00 | 5.71 | 5.71 | 5.71 | 10.00 / 5.71 |
| 11 | Sonic Wave | 50 | 950 | 7.14 | 7.14 | 7.14 | 7.14 | 7.14 / 7.14 |
| 12 | Protron | 50 | 650 | 6.25 | 6.25 | 6.25 | 6.25 | 4.17 / 5.56 |
| 13 | Protron | 50 | 600 | 10.00 | 8.33 | 7.14 | 7.14 |  |
| 14 | Wild Ball | 60 | 800 | 6.67 | 6.67 | 6.67 | 6.67 | 6.67 / 6.67 |
| 15 | Vulcan Cannon | 20 | 500 | 6.67 | 5.00 | 6.67 | 10.00 |  |
| 16 | Miscellaneous Option Weapons | 10 | 0 | 1.67 | 10.00 | 2.50 | 5.00 |  |
| 17 | Missile Launcher | 50 | 850 | 5.56 | 4.55 | 3.85 | 3.85 |  |
| 18 | Fireball | 50 | 1000 | 4.17 | 4.17 | 4.17 | 4.17 | 4.17 / 4.17 |
| 19 | Mega Pulse | 60 | 900 | 15.00 | 12.00 | 10.00 | 10.00 |  |
| 20 | Heavy Missile Launcher | 90 | 1000 | 10.00 | 18.00 | 12.86 | 9.00 |  |
| 21 | Rear Heavy Missile Launcher | 90 | 1000 | 10.00 | 10.00 | 12.86 | 15.00 |  |
| 22 | Rear Mega Pulse | 90 | 900 | 30.00 | 18.00 | 12.86 | 12.86 |  |
| 23 | Banana Blast | 20 | 950 | 3.33 | 4.00 | 5.00 | 5.00 |  |
| 24 | Banana Blast Rear | 20 | 1100 | 4.00 | 4.00 | 0.95 | 1.25 |  |
| 25 | HotDog Front | 40 | 1100 | 10.00 | 8.00 | 10.00 | 5.71 |  |
| 26 | HotDog Rear | 40 | 1100 | 13.33 | 10.00 | 8.00 | 5.71 |  |
| 27 | Hyper Pulse | 50 | 1050 | 16.67 | 16.67 | 16.67 | 12.50 |  |
| 28 | Guided Micro Bombs | 60 | 1100 | 3.75 | 5.45 | 5.45 | 6.00 |  |
| 29 | Heavy Guided Bombs | 80 | 1000 | 3.81 | 3.81 | 3.81 | 3.81 |  |
| 30 | Scatter Wave | 40 | 900 | 8.00 | 6.67 | 6.67 | 6.67 |  |
| 31 | Guided Bombs | 40 | 800 | 4.44 | 6.67 | 3.64 | 2.50 |  |
| 32 | Shuruiken Field | 40 | 850 | 8.00 | 8.00 | 8.00 | 8.00 |  |
| 33 | Poison Bomb | 40 | 800 | 3.08 | 4.44 | 5.71 | 6.67 |  |
| 34 | Protron Wave | 40 | 750 | 8.00 | 5.71 | 5.71 | 5.71 |  |
| 35 | The Orange Juicer | 40 | 900 | 5.71 | 6.67 | 5.00 | 5.71 |  |
| 36 | NortShip Super Pulse | 40 | 1100 | 10.00 | 10.00 | 10.00 | 10.00 |  |
| 37 | NortShip Spreader | 40 | 1100 | 10.00 | 10.00 | 10.00 | 10.00 | 6.67 / 4.44 |
| 38 | NortShip Spreader B | 40 | 1100 | 6.67 | 4.44 | 4.44 | 4.44 |  |
| 39 | Atomic RailGun | 30 | 1101 | 7.50 | 7.50 | 7.50 | 7.50 |  |
| 40 | Widget Beam | 40 | 950 | 13.33 | 10.00 | 10.00 | 10.00 |  |
| 41 | Sonic Impulse | 50 | 1000 | 12.50 | 12.50 | 16.67 | 12.50 |  |
| 42 | RetroBall | 50 | 1000 | 10.00 | 10.00 | 10.00 | 10.00 |  |

### T5. Sidekicks (all fire through `player_shot_create`, so they drain too)

| id | name | port (poweruse) | shotrepeat | drain u/frame | ammo |
|---|---|---|---|---|---|
| 1 | Single Shot Option | 16 (10) | 5 | 1.67 | inf |
| 2 | Dual Shot Option | 16 (10) | 5 | 1.67 | inf |
| 3 | Charge Cannon | 16 (10) | 6 | 1.43 | inf |
| 4 | Vulcan Shot Option | 16 (10) | 2 | 3.33 | inf |
| 5 | Wobbley | 16 (10) | 3 | 2.50 | inf |
| 6 | MegaMissile    Ammo 5 | 16 (10) | 8 | 1.11 | 5 |
| 7 | Atom Bombs     Ammo 20 | 16 (10) | 10 | 0.91 | 20 |
| 8 | Phoenix Device Ammo 8 | 16 (10) | 50 | 0.20 | 8 |
| 9 | Plasma Storm   Ammo 10 | 16 (10) | 3 | 2.50 | 10 |
| 10 | Mini-Missile   Ammo 100 | 16 (10) | 2 | 3.33 | 100 |
| 11 | Buster Rocket  Ammo 30 | 16 (10) | 10 | 0.91 | 30 |
| 12 | Zica SuperCharger | 16 (10) | 9 | 1.00 | inf |
| 13 | MicroBomb      Ammo 60 | 16 (10) | 20 | 0.48 | 60 |
| 14 | 8-Way MicroBomb  Ammo 30 | 16 (10) | 20 | 0.48 | 30 |
| 15 | Post-It Mine   Ammo 20 | 16 (10) | 2 | 3.33 | 20 |
| 16 | Mint-O-Ship | 16 (10) | 4 | 2.00 | inf |
| 17 | Zica Flamethrower | 4 (20) | 3 | 5.00 | inf |
| 18 | Side Ship      Ammo 100 | 16 (10) | 3 | 2.50 | 100 |
| 19 | Companion Ship Warfly | 16 (10) | 5 | 1.67 | inf |
| 20 | MicroSol FrontBlaster | 16 (10) | 5 | 1.67 | inf |
| 21 | Companion Ship Gerund | 16 (10) | 4 | 2.00 | inf |
| 22 | BattleShip-Class Firebomb | 16 (10) | 5 | 1.67 | inf |
| 23 | Protron Cannon Indigo | 16 (10) | 4 | 2.00 | inf |
| 24 | Companion Ship Quicksilver | 16 (10) | 5 | 1.67 | inf |
| 25 | Protron Cannon Tangerine | 16 (10) | 5 | 1.67 | inf |
| 26 | MicroSol FrontBlaster II | 16 (10) | 4 | 2.00 | inf |
| 27 | Beno Wallop Beam | 16 (10) | 20 | 0.48 | inf |
| 28 | Beno Protron System -B- | 16 (10) | 30 | 0.32 | inf |
| 29 | Tropical Cherry Companion | 16 (10) | 4 | 2.00 | inf |
| 30 | Satellite Marlo | 16 (10) | 4 | 2.00 | inf |

### T6. Illustrative loadouts per game stage: demand vs. stock generator (all weapons firing continuously)

Loadouts are my own illustrative picks (ids from T4/T5), not extracted from game data.

| stage | front (port, lvl) | rear | sidekicks | weapon drain u/frame | u/s | stock gen (u/frame) | sustainable fire % (no shield regen) | s to drain full pool at 100% fire | shield regen u/frame (while healing) | % fire while also healing shield |
|---|---|---|---|---|---|---|---|---|---|---|
| S0 New game (levels 1-2) | Pulse-Cannon L1 (7.5) | - (0.0) | - (0.0) | 7.5 | 261 | Advanced MR-12 14 | 100% | inf | 8.0 | 80% |
| S1 Early (~$10-30k) | Multi-Cannon L3 (8.0) | Vulcan Cannon L2 (6.7) | Single Shot Option, Single Shot Option (3.3) | 18.0 | 626 | Advanced MR-12 14 | 78% | 6.5 | 8.0 | 33% |
| S2 Mid (~$50-100k) | Zica Laser L5 (11.2) | Rear Heavy Missile Launcher L4 (11.2) | Vulcan Shot Option, Vulcan Shot Option (6.7) | 29.2 | 1014 | Standard MicroFusion 25 | 86% | 6.2 | 8.0 | 58% |
| S3 Late (~$200k+) | Hyper Pulse L6 (16.7) | Rear Mega Pulse L6 (12.9) | Zica Flamethrower, Vulcan Shot Option (8.3) | 37.9 | 1317 | Advanced MircoFusion 30 | 79% | 3.3 | 12.0 | 48% |
| S4 Max-burn (worst case) | Lightning Cannon L11 (35.0) | Rear Heavy Missile Launcher L11 (15.0) | Zica Flamethrower, Zica Flamethrower (10.0) | 60.0 | 2087 | Gravitron Pulse-Wave 50 | 83% | 2.6 | 8.0 | 70% |

### T7. Candidate mappings: power units PER FRAME handed to `power += ...` (steady-state smoothed watts)

Definitions: r = W/FTP; n(r) = Flywheel `effortNorm` piecewise curve through (0,0.10) (0.40,0.15) (0.75,0.50) (1.10,1.00), flat above 1.10.
- A absolute: `max(8, 0.1*W)`  (generator item ignored)
- B FTP-relative, fixed tier: `max(8, 25 * n(r)/n(1.0))`, n(1.0)=0.857  (generator item ignored; r=1.0 -> 25 = MicroFusion)
- C generator-scaled: `max(8, G * (0.5 + n(r)))`, G = stock `powerSys[gen].power` (r=0.75 -> exactly stock; r>=1.1 -> 1.5x; r=0 -> 0.6x)

| FTP | watts | r | n(r) | A | B | C gen2 (G=14) | C gen4 (G=25) | C gen6 (G=50) | stock gen2 / gen4 / gen6 |
|---|---|---|---|---|---|---|---|---|---|
| 150 | 100 | 0.67 | 0.42 | 10.0 | 12.2 | 12.8 | 22.9 | 45.8 | 14 / 25 / 50 |
| 150 | 150 | 1.00 | 0.86 | 15.0 | 25.0 | 19.0 | 33.9 | 67.9 | 14 / 25 / 50 |
| 150 | 200 | 1.33 | 1.00 | 20.0 | 29.2 | 21.0 | 37.5 | 75.0 | 14 / 25 / 50 |
| 150 | 250 | 1.67 | 1.00 | 25.0 | 29.2 | 21.0 | 37.5 | 75.0 | 14 / 25 / 50 |
| 250 | 100 | 0.40 | 0.15 | 10.0 | 8.0 | 9.1 | 16.2 | 32.5 | 14 / 25 / 50 |
| 250 | 150 | 0.60 | 0.35 | 15.0 | 10.2 | 11.9 | 21.2 | 42.5 | 14 / 25 / 50 |
| 250 | 200 | 0.80 | 0.57 | 20.0 | 16.7 | 15.0 | 26.8 | 53.6 | 14 / 25 / 50 |
| 250 | 250 | 1.00 | 0.86 | 25.0 | 25.0 | 19.0 | 33.9 | 67.9 | 14 / 25 / 50 |

### T8. Same riders: % of continuous fire sustained (income / demand, capped at 100%) for S1 and S2 loadouts

S1 demand 18.0 u/frame with gen2 (stock 14 -> 78%); S2 demand 29.2 u/frame with gen4 (stock 25 -> 86%).

| FTP | watts | A: S1 / S2 | B: S1 / S2 | C: S1 (gen2) / S2 (gen4) |
|---|---|---|---|---|
| 150 | 100 | 56% / 34% | 68% / 42% | 71% / 79% |
| 150 | 150 | 83% / 51% | 100% / 86% | 100% / 100% |
| 150 | 200 | 100% / 69% | 100% / 100% | 100% / 100% |
| 150 | 250 | 100% / 86% | 100% / 100% | 100% / 100% |
| 250 | 100 | 56% / 34% | 44% / 27% | 51% / 56% |
| 250 | 150 | 83% / 51% | 57% / 35% | 66% / 73% |
| 250 | 200 | 100% / 69% | 93% / 57% | 83% / 92% |
| 250 | 250 | 100% / 86% | 100% / 86% | 100% / 100% |

### T9. Time to fill an empty pool (power = 0 at every level start, tyrian2.c:747) at various incomes

| income u/frame | 5 | 8 | 10 | 14 | 19 | 25 | 30 | 50 |
|---|---|---|---|---|---|---|---|---|
| seconds to 900 | 5.2 | 3.2 | 2.6 | 1.8 | 1.4 | 1.0 | 0.9 | 0.5 |

