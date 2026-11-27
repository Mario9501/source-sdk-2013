# A/B results: `m0-baseline-2x`

- **A:** `test_noop`  **B:** `stock`
- 10 rounds per map per side, timescale 2.0, difficulty 1, 24 bots, 4 parallel servers
- Started 2026-11-26T23:46:36, finished 2026-11-27T02:54:08, build `ebd3a995-dirty`
- **Per-map overrides:** cp_process_final: `tf_bot_ab_round_minutes 20`

## Win rate of A (95% Wilson interval)

| Map | A as RED | A as BLU | Combined | Stalemates |
|---|---|---|---|---|
| cp_dustbowl | 70% [40–89] (n=10) | 30% [11–60] (n=10) | 50% [30–70] | 0 |
| cp_process_final | 40% [17–69] (n=10) | 60% [31–83] (n=10) | 50% [30–70] | 0 |
| ctf_2fort | 60% [31–83] (n=10) | 50% [24–76] (n=10) | 55% [34–74] | 0 |
| koth_viaduct | 40% [17–69] (n=10) | 40% [17–69] (n=10) | 40% [22–61] | 0 |
| pl_badwater | 90% [60–98] (n=10) | 0% [0–28] (n=10) | 45% [26–66] | 0 |
| pl_upward | 30% [11–60] (n=10) | 60% [31–83] (n=10) | 45% [26–66] | 0 |
| **All maps** | 55% [42–67] (n=60) | 40% [29–53] (n=60) | **48% [39–56]** (n=120) | 0 |

## Per-team averages per round

| | kills | deaths | damage | captures | objective_seconds | stuck_seconds | hitscan accuracy |
|---|---|---|---|---|---|---|---|
| A | 90.1 | 92.5 | 31241.9 | 1.9 | 383.0 | 66.2 | 32.8% |
| B | 91.3 | 91.5 | 31503.5 | 2.0 | 385.3 | 75.4 | 32.8% |

Round duration (game seconds): median 480, mean 611, n=120

## Server tick cost (game-side think, ms)

Mean 2.56 ms, max 133.5 ms, 0.07% of 4950633 ticks over the 15.0 ms budget.
