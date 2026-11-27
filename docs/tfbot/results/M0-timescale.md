# Compare `m0-ts1` (timescale 1.0) vs `m0-ts4` (timescale 4.0)

Same if the means are within ±10% and the 95% intervals overlap. Stuck time is reported but not judged (too rare for a stable rate).

## koth_viaduct (n = 20 vs 20 rounds)

| metric | A | B | B/A | verdict |
|---|---|---|---|---|
| round_seconds | 317.995 [296.978–339.013] | 318.382 [289.981–346.784] | 1.00 | same |
| kills_per_min | 22.998 [22.482–23.515] | 22.706 [22.029–23.384] | 0.99 | same |
| damage_per_min | 7621.257 [7423.559–7818.954] | 7588.548 [7352.600–7824.496] | 1.00 | same |
| objective_s_per_min | 151.027 [142.648–159.406] | 148.120 [139.479–156.760] | 0.98 | same |
| stuck_s_per_min | 3.601 [0.579–6.622] | 4.023 [0.584–7.462] | 1.12 | (not judged) |
| hitscan_accuracy | 0.354 [0.345–0.364] | 0.359 [0.345–0.373] | 1.01 | same |

## pl_badwater (n = 20 vs 20 rounds)

| metric | A | B | B/A | verdict |
|---|---|---|---|---|
| round_seconds | 693.266 [566.843–819.688] | 564.196 [440.343–688.050] | 0.81 | **differs** |
| kills_per_min | 16.919 [16.648–17.191] | 16.532 [16.156–16.908] | 0.98 | same |
| damage_per_min | 5946.798 [5798.967–6094.628] | 5835.597 [5638.656–6032.539] | 0.98 | same |
| objective_s_per_min | 81.224 [74.603–87.846] | 78.877 [73.846–83.908] | 0.97 | same |
| stuck_s_per_min | 17.896 [14.466–21.325] | 17.230 [13.204–21.255] | 0.96 | (not judged) |
| hitscan_accuracy | 0.331 [0.318–0.344] | 0.337 [0.325–0.348] | 1.02 | same |

## all maps (n = 40 vs 40 rounds)

| metric | A | B | B/A | verdict |
|---|---|---|---|---|
| round_seconds | 505.630 [419.209–592.052] | 441.289 [367.661–514.918] | 0.87 | **differs** |
| kills_per_min | 19.959 [18.962–20.955] | 19.619 [18.577–20.661] | 0.98 | same |
| damage_per_min | 6784.027 [6494.390–7073.664] | 6712.072 [6397.940–7026.205] | 0.99 | same |
| objective_s_per_min | 116.126 [103.970–128.282] | 113.498 [101.564–125.432] | 0.98 | same |
| stuck_s_per_min | 10.748 [7.567–13.930] | 10.627 [7.291–13.962] | 0.99 | (not judged) |
| hitscan_accuracy | 0.343 [0.334–0.351] | 0.348 [0.338–0.357] | 1.02 | same |

**Overall (all maps): DIFFERS**

---

# Badwater at 2× (follow-up)

The 1× vs 4× comparison above failed on round length (pl_badwater only; koth_viaduct matched exactly), so per the rule 2× was checked on badwater:

Same if the means are within ±10% and the 95% intervals overlap. Stuck time is reported but not judged (too rare for a stable rate).

## pl_badwater (n = 20 vs 20 rounds)

| metric | A | B | B/A | verdict |
|---|---|---|---|---|
| round_seconds | 693.266 [566.843–819.688] | 629.522 [499.523–759.522] | 0.91 | same |
| kills_per_min | 16.919 [16.648–17.191] | 16.909 [16.522–17.296] | 1.00 | same |
| damage_per_min | 5946.798 [5798.967–6094.628] | 6051.438 [5869.914–6232.963] | 1.02 | same |
| objective_s_per_min | 81.224 [74.603–87.846] | 84.662 [79.082–90.242] | 1.04 | same |
| stuck_s_per_min | 17.896 [14.466–21.325] | 18.928 [13.866–23.990] | 1.06 | (not judged) |
| hitscan_accuracy | 0.331 [0.318–0.344] | 0.327 [0.316–0.338] | 0.99 | same |

## all maps (n = 20 vs 20 rounds)

| metric | A | B | B/A | verdict |
|---|---|---|---|---|
| round_seconds | 693.266 [566.843–819.688] | 629.522 [499.523–759.522] | 0.91 | same |
| kills_per_min | 16.919 [16.648–17.191] | 16.909 [16.522–17.296] | 1.00 | same |
| damage_per_min | 5946.798 [5798.967–6094.628] | 6051.438 [5869.914–6232.963] | 1.02 | same |
| objective_s_per_min | 81.224 [74.603–87.846] | 84.662 [79.082–90.242] | 1.04 | same |
| stuck_s_per_min | 17.896 [14.466–21.325] | 18.928 [13.866–23.990] | 1.06 | (not judged) |
| hitscan_accuracy | 0.331 [0.318–0.344] | 0.327 [0.316–0.338] | 0.99 | same |

**Overall (all maps): same within tolerance**

Same if the means are within ±10% and the 95% intervals overlap. Stuck time is reported but not judged (too rare for a stable rate).

## pl_badwater (n = 20 vs 20 rounds)

| metric | A | B | B/A | verdict |
|---|---|---|---|---|
| round_seconds | 629.522 [499.523–759.522] | 564.196 [440.343–688.050] | 0.90 | **differs** |
| kills_per_min | 16.909 [16.522–17.296] | 16.532 [16.156–16.908] | 0.98 | same |
| damage_per_min | 6051.438 [5869.914–6232.963] | 5835.597 [5638.656–6032.539] | 0.96 | same |
| objective_s_per_min | 84.662 [79.082–90.242] | 78.877 [73.846–83.908] | 0.93 | same |
| stuck_s_per_min | 18.928 [13.866–23.990] | 17.230 [13.204–21.255] | 0.91 | (not judged) |
| hitscan_accuracy | 0.327 [0.316–0.338] | 0.337 [0.325–0.348] | 1.03 | same |

## all maps (n = 20 vs 20 rounds)

| metric | A | B | B/A | verdict |
|---|---|---|---|---|
| round_seconds | 629.522 [499.523–759.522] | 564.196 [440.343–688.050] | 0.90 | **differs** |
| kills_per_min | 16.909 [16.522–17.296] | 16.532 [16.156–16.908] | 0.98 | same |
| damage_per_min | 6051.438 [5869.914–6232.963] | 5835.597 [5638.656–6032.539] | 0.96 | same |
| objective_s_per_min | 84.662 [79.082–90.242] | 78.877 [73.846–83.908] | 0.93 | same |
| stuck_s_per_min | 18.928 [13.866–23.990] | 17.230 [13.204–21.255] | 0.91 | (not judged) |
| hitscan_accuracy | 0.327 [0.316–0.338] | 0.337 [0.325–0.348] | 1.03 | same |

**Overall (all maps): DIFFERS**

## Decision

**A/B runs use 2×.** 2× matches 1× on every metric. Payload round length falls with timescale (693 s at 1×, 630 s at 2×, 564 s at 4×); the intervals overlap, but the trend suggests 4× slightly distorts payload rounds (fewer bot think cycles per game second, see design doc 3.8). Per-minute behaviour (kills, damage, objective time, accuracy) matched within 2–7% at every speed.
