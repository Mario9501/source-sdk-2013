# A/B results: `final-v6-all-vs-stock`

- **A:** `all`  **B:** `stock`
- 10 rounds per map per side, timescale 2.0, difficulty 1, 24 bots, 6 parallel servers
- Started 2026-11-27T18:16:38, finished 2026-11-27T21:00:51, build `f6da18c5-dirty`
- **Per-map overrides:** cp_process_final: `tf_bot_ab_round_minutes 20`
- *Note:* "dirty" was uncommitted docs only. Every server ran a snapshot of `server.so` built from `f6da18c5`. Three jobs (cp_dustbowl both sides, one cp_process_final) started after a playtest saved `tf_bot_profiles_custom.txt`, which A/B views then still read (fixed in `03db0ee0`). Its edits didn't apply to the casual-tier bots used here, apart from the engineer style of one named bot.
- Earlier run, before the engineer and class-mix fixes: [M1-M7-all-vs-stock.md](M1-M7-all-vs-stock.md) (21%).

## Win rate of A (95% Wilson interval)

| Map | A as RED | A as BLU | Combined | Stalemates |
|---|---|---|---|---|
| cp_dustbowl | 50% [24–76] (n=10) | 40% [17–69] (n=10) | 45% [26–66] | 0 |
| cp_process_final | 30% [11–60] (n=10) | 60% [31–83] (n=10) | 45% [26–66] | 0 |
| ctf_2fort | 30% [11–60] (n=10) | 60% [31–83] (n=10) | 45% [26–66] | 0 |
| koth_viaduct | 50% [24–76] (n=10) | 0% [0–28] (n=10) | 25% [11–47] | 0 |
| pl_badwater | 80% [49–94] (n=10) | 10% [2–40] (n=10) | 45% [26–66] | 0 |
| pl_upward | 0% [0–28] (n=10) | 40% [17–69] (n=10) | 20% [8–42] | 0 |
| **All maps** | 40% [29–53] (n=60) | 35% [24–48] (n=60) | **38% [29–46]** (n=120) | 0 |

## Per-team averages per round

| | kills | deaths | damage | captures | objective_seconds | stuck_seconds | hitscan accuracy |
|---|---|---|---|---|---|---|---|
| A | 93.0 | 98.7 | 31725.7 | 2.0 | 352.7 | 64.0 | 33.2% |
| B | 97.7 | 94.3 | 33008.4 | 2.2 | 444.3 | 55.4 | 32.1% |

Feature activity per round (all bots; in a features-vs-stock run only the features side produces these): `aim_acquire` 1143.2, `aim_preaim_hold` 842.1, `bb_hold_regroup` 2.5, `bb_hold_uber` 3.3, `bb_sentry_learned` 6.1, `bb_sentry_shared` 51.6, `bb_spy_shared` 215.1, `blast_jump_abort` 2.5, `blast_jump_landed` 2.2, `blast_jump_start` 4.9, `eng_advance_nest` 0.8, `eng_battle_nest` 4.1, `eng_forward_nest` 5.8, `eng_help_building` 3.5, `eng_pickup` 2.3, `eng_placed` 1.7, `eng_save_sentry` 1.3, `move_double_jump` 9.6, `move_jump` 89.8, `move_strafe_change` 704.5, `perception_glance` 33.5, `perception_heard` 13.7, `profile_class_roster` 91.2, `profile_class_switch` 3.6, `unlock_given` 7.9, `voice_callout` 61.8

Round duration (game seconds): median 481, mean 638, n=120

## Server tick cost (game-side think, ms)

Mean 2.47 ms, max 631.6 ms, 0.13% of 5162090 ticks over the 15.0 ms budget.
