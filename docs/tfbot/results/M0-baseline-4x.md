# A/B results: `m0-baseline`

*Sanity record only: this A/A ran at 4×, which failed the timescale check for payload round length (M0-timescale.md). cp_process_final jobs hit their time limit here (5CP bot rounds run 20-90 game minutes), which led to the per-map round cap. The baseline of record is M0-baseline.md (2×).*

- **A:** `test_noop`  **B:** `stock`
- 10 rounds per map per side, timescale 4.0, difficulty 1, 24 bots, 6 parallel servers
- Started 2026-11-26T21:59:22, finished 2026-11-26T23:43:04, build `240ebd8c-dirty`
- **Jobs with problems:** cp_process_final__a_red (timeout), cp_process_final__a_blu (timeout)

## Win rate of A (95% Wilson interval)

| Map | A as RED | A as BLU | Combined | Stalemates |
|---|---|---|---|---|
| cp_dustbowl | 70% [40–89] (n=10) | 10% [2–40] (n=10) | 40% [22–61] | 0 |
| cp_process_final | 100% [34–100] (n=2) | 50% [9–91] (n=2) | 75% [30–95] | 0 |
| ctf_2fort | 40% [17–69] (n=10) | 50% [24–76] (n=10) | 45% [26–66] | 0 |
| koth_viaduct | 60% [31–83] (n=10) | 40% [17–69] (n=10) | 50% [30–70] | 0 |
| pl_badwater | 100% [72–100] (n=10) | 40% [17–69] (n=10) | 70% [48–85] | 0 |
| pl_upward | 50% [24–76] (n=10) | 30% [11–60] (n=10) | 40% [22–61] | 0 |
| **All maps** | 65% [52–77] (n=52) | 35% [23–48] (n=52) | **50% [41–59]** (n=104) | 0 |

## Per-team averages per round

| | kills | deaths | damage | captures | objective_seconds | stuck_seconds | hitscan accuracy |
|---|---|---|---|---|---|---|---|
| A | 88.4 | 90.2 | 29649.2 | 1.8 | 386.0 | 54.4 | 32.3% |
| B | 88.5 | 90.1 | 29473.8 | 1.7 | 389.4 | 67.2 | 32.4% |

Round duration (game seconds): median 441, mean 589, n=104

## Server tick cost (game-side think, ms)

Mean 2.00 ms, max 642.9 ms, 0.06% of 3426309 ticks over the 15.0 ms budget.
