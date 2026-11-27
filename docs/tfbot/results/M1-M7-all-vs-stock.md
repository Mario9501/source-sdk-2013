# A/B results: `final-all-vs-stock`

- **A:** `all`  **B:** `stock`
- 10 rounds per map per side, timescale 2.0, difficulty 1, 24 bots, 6 parallel servers
- Started 2026-11-27T02:55:30, finished 2026-11-27T05:07:43, build `c2f034f4-dirty`
- **Per-map overrides:** cp_process_final: `tf_bot_ab_round_minutes 20`

## Win rate of A (95% Wilson interval)

| Map | A as RED | A as BLU | Combined | Stalemates |
|---|---|---|---|---|
| cp_dustbowl | 40% [17–69] (n=10) | 30% [11–60] (n=10) | 35% [18–57] | 0 |
| cp_process_final | 25% [7–59] (n=8) | 20% [6–51] (n=10) | 22% [9–45] | 2 |
| ctf_2fort | 40% [17–69] (n=10) | 0% [0–28] (n=10) | 20% [8–42] | 0 |
| koth_viaduct | 40% [17–69] (n=10) | 10% [2–40] (n=10) | 25% [11–47] | 0 |
| pl_badwater | 30% [11–60] (n=10) | 0% [0–28] (n=10) | 15% [5–36] | 0 |
| pl_upward | 10% [2–40] (n=10) | 10% [2–40] (n=10) | 10% [3–30] | 0 |
| **All maps** | 31% [21–44] (n=58) | 12% [6–22] (n=60) | **21% [15–29]** (n=118) | 2 |

## Per-team averages per round

| | kills | deaths | damage | captures | objective_seconds | stuck_seconds | hitscan accuracy |
|---|---|---|---|---|---|---|---|
| A | 79.7 | 88.4 | 26992.4 | 1.2 | 279.0 | 64.6 | 32.8% |
| B | 87.6 | 80.8 | 29040.1 | 2.0 | 430.0 | 74.1 | 31.4% |

Feature activity per round (all bots; in a features-vs-stock run only the features side produces these): `aim_acquire` 986.2, `aim_preaim_hold` 838.9, `bb_hold_regroup` 2.0, `bb_hold_uber` 2.7, `bb_sentry_learned` 5.0, `bb_sentry_shared` 42.0, `bb_spy_shared` 183.6, `blast_jump_abort` 1.9, `blast_jump_landed` 1.6, `blast_jump_start` 3.6, `eng_advance_nest` 1.6, `eng_forward_nest` 22.5, `eng_help_building` 6.0, `eng_pickup` 3.4, `eng_placed` 2.5, `eng_save_sentry` 1.7, `move_double_jump` 8.5, `move_jump` 67.6, `move_strafe_change` 615.7, `perception_glance` 30.3, `perception_heard` 12.6, `profile_class_switch` 5.8, `unlock_given` 9.4, `voice_callout` 53.1

Round duration (game seconds): median 434, mean 578, n=120

## Server tick cost (game-side think, ms)

Mean 1.72 ms, max 114.3 ms, 0.05% of 4680921 ticks over the 15.0 ms budget.
