# M1–M7 tuning: from 1% to 38% (2026-11-27)

The first full A/B of every feature against stock bots (`m17-all-vs-stock`, six maps, 120 rounds) lost **119 of 120 rounds**. This page records how that was diagnosed and fixed, reaching 38% with the fights at par (section 8). All runs: timescale 2, difficulty 1, 12 v 12, sides swapped in equal numbers.

## 1. Suicide loop in the spawn room (profiles)

| | A (all features) | B (stock) |
|---|---|---|
| deaths per round | 117.2 | 20.5 |
| of which `TF_DMG_CUSTOM_SUICIDE`, weapon `world` | 73.2 | 0 |

The stock in-spawn class re-evaluation (`CTFBotMainAction::Update`) asks `GetNextSpawnClassname()` every 1–2 s and suicides to switch whenever the answer changes. The profile class choice was weighted-random, so the answer changed constantly. **Fix** (`294426ba`): the class is decided once per life. While alive the answer is the current class; on death a bot keeps it with 75–95% odds. A quick check after the fix showed 0 suicides.

## 2. Single-feature ablations

Each feature alone vs stock, koth_viaduct, 4 rounds per side (n = 8; intervals are wide, read them as a screen).

| Feature | Win (95% CI) | Kills A / B | Damage A / B |
|---|---|---|---|
| profiles | 50% [22–78] | 66.8 / 53.5 | 21358 / 19253 |
| **aim** | **0% [0–32]** | **38.1 / 65.0** | **15514 / 20144** |
| blackboard | 50% [22–78] | 62.9 / 65.5 | 19438 / 22023 |
| perception | 38% [14–69] | 53.1 / 58.2 | 17654 / 18400 |
| movement | 75% [41–93] | 66.4 / 56.4 | 21113 / 18218 |
| medic | 88% [53–98] | 71.8 / 64.1 | 23093 / 22206 |
| engineer | 50% [22–78] | 68.4 / 61.5 | 22144 / 21001 |
| voice | 75% [41–93] | 67.5 / 55.6 | 20237 / 19521 |
| unlocks | 50% [22–78] | 57.1 / 52.9 | 18787 / 18266 |
| mobility | 75% [41–93] | 57.1 / 52.6 | 19377 / 18233 |

Aim was the only clear loser.

## 3. A harness bug found on the way

The first two aim sweeps passed their knobs with `--override`, but the knobs are `FCVAR_CHEAT` and the job cfg never set `sv_cheats`, so the server rejected them ("Can't use cheat cvar …") and **every variant ran the defaults**. That briefly suggested that even error-free aim lost. Fixed in `c2f034f4`: `sv_cheats 1` is the first line of every job cfg (as DESIGN 3.8 specified; it doesn't change bot behaviour). The invalid runs are kept apart under `tfbot_runs/invalid-no-sv_cheats/`.

## 4. Aim calibration

Corrected sweep, aim alone vs stock, koth_viaduct, 6 rounds per side (n = 12):

| Variant (old tier values) | Win | Kills A / B | Hitscan acc. A / B |
|---|---|---|---|
| defaults + wobble settling | 25% | 57.2 / 70.5 | 27.8% / 34.3% |
| **no error, no pre-aim, stock turn speed** | **50%** | **54.8 / 55.9** | **36.6% / 36.6%** |
| no pre-aim, stock turn speed | 25% | 53.2 / 66.9 | 31.2% / 33.7% |
| + motion penalty 0.04 | 0% | 45.1 / 67.2 | 33.7% / 32.6% |
| pre-aim held 1/3 as long, motion 0.04 | 17% | 40.5 / 62.2 | 31.0% / 32.4% |
| no pre-aim, motion 0.04, error 0.6× | 17% | 49.7 / 62.2 | 35.4% / 33.7% |

The error-free control lands at exact parity, so target-point choice and projectile leading match stock. The whole handicap came from the error terms. The variants between are within noise of each other.

**Calibration rule** (now DESIGN 3.2): tier N is mechanically at par with stock difficulty N. Human character comes from small errors that settle, not from being weaker. Changes (`964e7012`):
- tier turn speed 700/1000/1300/1700 deg/s (stock: 1000 at every difficulty)
- tier aim error 4.5/2.5/1.5/0.8 deg, settling to 25% within about a second on target
- smaller overshoot and motion penalty
- lead error tied to aim error (about ±10% at casual)
- pre-aim held for 1–2 s

Validation, aim alone vs stock:

| Run | n | Win | Kill ratio A/B |
|---|---|---|---|
| koth_viaduct, defaults | 16 | 38% [18–61] | 0.85 |
| ctf_2fort, defaults | 12 | 58% [32–81] | 1.04 |
| **combined** | **28** | **46%** | |
| koth_viaduct, error 0.5× | 16 | 62% [39–82] | 1.06 |
| koth_viaduct, error 1.5× | 16 | 19% [7–43] | 0.73 |

The bracket shows a clear slope, with the defaults at parity. `tf_bot_aim_error_scale` is the one dial for making every tier's aim sloppier or sharper while playtesting.

## 5. Perception

Alone it lost slightly: 38% (n = 8), then 31% (n = 16) after the first fix. Gaze is the scarce resource: a bot only notices what is inside its 90° view cone, and perception was pointing it at walls. Hearing turned bots toward footsteps behind walls, and glances looked over the shoulder at random. Now hearing reacts only to an enemy in the open behind the bot and yields to any known threat, and glances check an enemy approach outside the view cone. Re-check (`perc5-viaduct`, n = 16): **38% [18–61]**, kills 50.9 / 59.1. Within noise of parity, but not shown to help; it stays a testing feature.

## 6. All features vs stock, after the fixes above

[M1-M7-all-vs-stock.md](M1-M7-all-vs-stock.md): **21% [15–29]** (n = 118, six maps). Far better than 1%, but well short of parity, although no feature lost clearly on its own (section 2). Kills 79.7 / 87.6 and hitscan accuracy 32.8% / 31.4% put the fights close to even; the deficit is objective play (objective time 279 s / 430 s, captures 1.2 / 2.0).

Per class (kills and deaths per round, six maps):

| Class | A K/D | B K/D | Note |
|---|---|---|---|
| Engineer | 0.83 | 2.01 | A's engineers die twice as often; their sentries kill 3.5 per round against B's 8.7 |
| Soldier | 1.25 | 1.50 | A's rockets kill 12.0 per round against 16.7 |
| Sniper | 1.86 | 1.64 | A fields more snipers (kills + deaths 18.3 against 12.5) |

Engineer alone had looked strong (K/D 2.29 against 1.20), but without profiles every engineer plays turtle. With profiles, about two thirds play forward or battle, and the forward nest ignores the objective. Diagnosis continues in section 7.

## 7. Leave-one-out, and the engineer and class-mix fixes

Each feature removed from `all` in turn, vs stock, koth_viaduct + pl_upward, 4 rounds per side per map (n = 16; `all` scored about 17% on these maps):

| Removed | Win | Objective s A / B |
|---|---|---|
| **engineer** | **44% [23–67]** | 328 / 448 |
| aim | 38% [18–61] | 373 / 476 |
| profiles | 25% [10–49] | 282 / 476 |
| mobility | 19% [7–43] | 271 / 519 |
| unlocks | 12% [3–36] | 213 / 469 |
| perception | 12% [3–36] | 332 / 583 |
| movement | 12% [3–36] | 306 / 538 |
| blackboard | 0% [0–19] | 259 / 554 |

**Engineers.** Engineer alone had run without profiles, and without a profile every engineer plays turtle. With profiles, aggression picks the style, so about two thirds played forward or battle. The forward nest went to a random area 400 units behind the *furthest* living teammate, ignoring the objective, and kept getting repacked. **Fix** (`f6da18c5`):
- Forward engineers choose from the stock build areas (those that see the objective with a clear line of fire), taking the most advanced half of the ones behind the team's main group. The main group is measured at the teammate a third of the way back, not the furthest one.
- On payload the nest keeps up with the cart. Elsewhere the stock out-of-position check moves it when the objective moves.
- Battle engineers still drop minis at the fight, measured from the same main group.

**Class mix.** Profile favourites gave a near-uniform class spread: often one medic, extra snipers, fewer soldiers and demomen. Stock uses a 12-slot preset roster (2 medics, 2 soldiers, 2 demomen, an engineer that doubles on defense, ...). **Fix** (`f6da18c5`): bots fill the roster's open slots, and favourites decide which bot plays which.

After both fixes (same maps, 6 rounds per side per map, n = 24):

| Variant | Win | Kills A / B | Objective s A / B |
|---|---|---|---|
| **all** | **46% [28–65]** | **77.9 / 77.9** | **420 / 471** |
| all minus aim | 38% [21–57] | 74.9 / 80.3 | 359 / 492 |
| all minus engineer | 38% [21–57] | 76.2 / 80.1 | 370 / 470 |

Engineer K/D rose from 0.83 to 1.40, and removing either feature no longer helps.

## 8. Final headline, and what's left

[M1-M7-final-all-vs-stock.md](M1-M7-final-all-vs-stock.md): **38% [29–46]** (n = 120, six maps), up from 21%.
- **Fights:** kills 93.0 / 97.7, hitscan accuracy 33.2% / 32.1%. Every class's K/D is within about 15% of stock's, sniper higher.
- **Class mix:** matches stock; deaths per class are nearly equal.
- **Objective time:** 353 s / 444 s.
- **Per map:** cp_dustbowl, cp_process_final, ctf_2fort and pl_badwater each score 45%. Badwater's 80% / 10% split is the map favouring defense for bots: A's attack captured 19 points to stock's 22.

Two weak spots account for most of the rest:

| Weak spot | Evidence |
|---|---|
| **pl_upward on defense: 0/10** | Stock pushed through all 4 checkpoints every round (40 captures). A's sentries killed 8.8 a round against 23.9 for stock's defending engineers. A's engineers re-picked a forward nest 12 times a round. The same logic defends badwater well (sentry kills 18.7 vs 15.8, 8 holds in 10), so the cause looks specific to upward's layout. |
| **koth_viaduct as BLU: 0/10** | Below. |

**Side asymmetry on koth_viaduct.** In every run of this build on viaduct (10 runs: all features, or all minus one), A won **31/66 as RED (47%)** and **6/66 as BLU (9%)**. Stock vs stock is symmetric there (40% / 40%, M0-baseline). Ruled out:
- **Any single feature:** removing each one in turn left the gap (the `v6-no-*` and `vd-no-*` runs), and so did turning pre-aim off.
- **NextBot update scheduling:** with `nb_update_frequency 0`, every bot thinks every tick, and BLU still won 1/8 against RED's 4/8.
- **Name-seeded personalities:** traits come from bot names, but names start at a random index on each server start. Server logs from two starts on the same map showed different names on each side, so neither side gets a fixed set.
- **Team handling in the overhaul code:** team masks are correct, and there are no team-indexed arrays.

ctf_2fort looked the same on the previous build (BLU 0/10, RED 4/10). This build shows the opposite (BLU 11/16, RED 6/16 over two runs), so what remains is specific to viaduct. Still unexplained; listed in DESIGN §6.

**Status:** every feature stays *testing* (off by default). The numbers say the overhaul fights at par and plays the objective slightly worse. Whether it plays *like people* is for the playtests (`exec tfbot_playtest`).
