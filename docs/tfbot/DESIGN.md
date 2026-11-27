# TFBot Overhaul: Design

**Status:** agreed 2026-11-26 in a design interview. This is the single source of truth for the bot overhaul. When a decision changes, update this file in the same PR as the code change.

**Code references** (`file:line`) are relative to `src/game/server/` and were checked against commit `68c8b82f`. Line numbers drift; re-check them before relying on them.

---

## 1. Goal

Bots that **play like competent humans**. A bot-filled match should feel like a casual public server.

| | |
|---|---|
| **Product** | A standalone mod (`mod_tf`) built from this repository. **Local only, never shared.** No upstream PRs, no Steam or ModDB release. |
| **Target experience** | One or a few humans on a **listen server**, with bots filling the remaining slots as teammates and opponents. Bot-only matches exist only for testing. |
| **What counts as a bug** | Passive or predictable play, ignoring the objective, and *inhuman* skill (e.g. Expert hitscan aim that tracks perfectly). Bots should lose for human reasons, such as positioning and decisions, not because of artificial nerfs. |
| **Game modes** | All. See [§3.1](#31-mvm-policy) for how MvM is handled. |

### Non-goals
- Machine learning or imitation learning. Playstyle cloning ([M4b](#m4b--playstyle-cloning)) uses plain statistics.
- A commander or team-level planner AI. Each bot decides for itself, using shared information.
- Rubber-banding (dynamic difficulty based on score).
- Bots typing in text chat. Never discussed, so not planned.
- Pruning HL2-era `#ifdef` code from shared engine/game files. Valve's syncs touch those files, so editing them only creates merge conflicts.
- Keeping the Windows build working. `createallprojects.bat` is kept in step, but nobody tests it.

---

## 2. What exists today (baseline inventory)

### 2.1 Per-class coverage
Dedicated behaviour folders exist only for **demoman, engineer, medic, sniper, spy** (`tf/bot/behavior/<class>/`). Scout, soldier, pyro and heavy run the generic tree, plus a few class checks:

| Mechanic | State | Where |
|---|---|---|
| Pyro airblast/reflect | Exists, scaled by difficulty (Easy never … Expert always) | `tf/bot/tf_bot.cpp` `ShouldFireCompressionBlast` (~4073) |
| Heavy spin-up | Exists (rev for 3 s after seeing an enemy) | `tf/bot/behavior/tf_bot_behavior.cpp` (~1284) |
| Sniper headshots | Exists (head aim on Hard and up, plus aim error) | `tf_bot_behavior.cpp` (~721) |
| Spy disguise choice | Exists (Hard and up copy a real enemy's class) | `tf_bot_behavior.cpp` (~231) |
| Soldier feet aim | Exists, but the lead speed is **hard-coded to 1100** and only applies to `TF_WEAPON_ROCKETLAUNCHER` | `tf_bot_behavior.cpp` `SelectTargetPoint` (~609) |
| Rocket / sticky jumping, scout double jump | **Missing** | none |
| Uber timing | Basic. **Kritzkrieg and Quick-Fix are handled exactly like stock uber** | `tf/bot/behavior/medic/tf_bot_medic_heal.cpp` (~631) |
| Engineer building moves | **Missing.** No carry logic exists anywhere | none |
| Engineer on offense | Only adds a forward teleporter exit | `tf/bot/behavior/engineer/tf_bot_engineer_move_to_build.cpp:360` |
| Upgrading teammates' buildings | Missing (TODO) | `tf/bot/behavior/engineer/tf_bot_engineer_building.cpp:68` |

### 2.2 Difficulty implementation
| | Easy | Normal | Hard | Expert | Where |
|---|---|---|---|---|---|
| Aim re-read interval | 1.0 s | 0.25 s | 0.1 s | 0.05 s | `tf/bot/tf_bot_body.cpp:16-42` |
| Reaction time (`GetMinRecognizeTime`) | 1.0 s | 0.5 s | 0.3 s | 0.2 s | `tf/bot/tf_bot_vision.cpp:439` |
| Field of view | 90° | 90° | 90° | 90° | `NextBot/NextBotVisionInterface.h:144` |

Head turning is a fixed `nb_saccade_speed` of 1000°/s with ±20% jitter on samples (`NextBot/Player/NextBotPlayerBody.cpp:205-239`). Effectively, aim skill comes down to one sampling interval, which is why Expert feels like an aimbot and Easy feels braindead.

### 2.3 Perception
- Vision: a 90° cone, range 6000. A target is rejected **before any raycast** if its nav area is not in the viewer area's potentially-visible set (`NextBot/NextBotVisionInterface.cpp:592-600`).
- Hearing: **weapon fire only**, through `CTFBot::OnWeaponFired` (`tf/bot/tf_bot.cpp:2716`). Footsteps don't reach bots.
- Hooks we can build on: every bot component receives `OnSound` and `OnSpokeConcept` (`NextBot/NextBotEventResponderInterface.h:74-75`). `OnSpokeConcept` fires for response-system concepts such as voice commands.

### 2.4 Teamwork
- Squads (`tf/bot/tf_bot_squad.cpp`) are only created by MvM, creep waves and raid. **There is no team coordination in PvP**: no shared targets, no focus fire, no grouping up.
- Medics answer **human** "MEDIC!" calls only (`tf_bot_medic_heal.cpp:131-136`, an explicit `!IsBot()` check).

### 2.5 Loadouts
- In PvP, bots are effectively stock-only. `CTFPlayer::UpdateInventory` returns early for fake clients (`tf/tf_player.cpp:~3441`), so loadouts fall back to the base items.
- Giving bots items **works without the Game Coordinator**. The item schema loads from `tf/scripts/items/items_game.txt`. `CTFBot::AddItem` (`tf/bot/tf_bot.cpp:~4897`) and `CItemGeneration::GenerateItemFromDefIndex` (`shared/econ/econ_entity_creation.cpp:~54`) create items straight from the schema.
- Humans in the SDK build get their real TF2 inventory through a web API (`INVENTORY_VIA_WEBAPI`; local loadout stored in `cfg/local_loadout.txt`). Unverified for a non-Valve mod appid.

### 2.6 Class selection
`CTFBot::GetNextSpawnClassname` (`tf/bot/tf_bot.cpp:1182`) uses fixed per-difficulty tables filtered by `CanBotChooseClass`. It doesn't consider what the team needs.

### 2.7 Navigation
- Bots require a `.nav` file. Of 238 installed official maps, **46** ship one in `tf2_misc_dir.vpk`, **111** embed one in the BSP's pakfile, and **81 have none**, e.g. koth_harvest_final, cp_steel, plr_hightower, ctf_doublecross (`tools/tfbot/nav_batch.py list`; an earlier estimate of 192 without a nav missed the BSP-embedded ones).
- **How navs are found:** the loader reads the "MOD" search path first, then the BSP's embedded files (`nav_file.cpp:1401-1406`). `nav_generate` saves to "MOD" (`game/mod_tf/maps/`), which would shadow a shipped nav.
- **Generation:**
  - `CNavMesh::BeginGeneration` / `AddWalkableSeed` are public (`nav_mesh.h:357-364`), so game code can generate without `sv_cheats`.
  - Generation is time-sliced across frames, then runs analysis, saves, and **reloads the level** (`nav_generate.cpp:3994-4029`).
  - Stock generation seeds only from the first `info_player_start` (`nav_generate.cpp:3394-3411`), which most TF maps lack.
- **TF attributes computed at runtime** (`tf/nav_mesh/tf_nav_mesh.cpp:1125-1134`): spawn rooms and exits, health and ammo, control points, door blocking, incursion distances, sentry danger.
- **Hand-marked only** (`TF_NAV_PERSISTENT_ATTRIBUTES`, `tf/nav_mesh/tf_nav_area.h:58`): sniper and sentry spots, setup gates, and others. In PvP, sniper and sentry spots are **not read**; snipers and engineers fall back to runtime sampling.
- Visibility and hiding-spot data exist **only** when analysis ran. Without it, bots are effectively blind (see §2.3).

### 2.8 MvM share
About 10–15% of bot code is MvM-only. `IsMannVsMachineMode` appears 73 times across 24 files.

### 2.9 Known bugs found
| Bug | Where | Fix in |
|---|---|---|
| `IsGoodUberTarget` returns `false` on every path, so a medic with uber ready never sticks with a good (combat-class) patient: the "keep this patient" branch at line 271 never runs. *(Corrected: it gates patient choice, not the deploy decision at line 641.)* | `tf/bot/behavior/medic/tf_bot_medic_heal.cpp:428-441` | M3 (`medic` feature) |
| *(Not a bug)* `&` where `&&` was meant | `tf_bot_behavior.cpp:1319`. Both sides are `!x`, so the result is identical | none |

---

## 3. Rules that apply everywhere

### 3.1 MvM policy
- **Shared improvements apply in MvM:** aim model, movement, pathing, bug fixes.
- **New decision-making is PvP-only:** retreating, flanking, grouping up, the blackboard, team-needs class picks. Gate it with `!TFGameRules()->IsMannVsMachineMode()`.
- **Popfile attributes always win.** MvM robots use the **base profile of their popfile `Skill` tier**, with no per-bot variation and no style traits.

*Why:* MvM robots are meant to act as a horde, and waves are balanced around popfile behaviour.

### 3.2 Skill tiers and profiles
- `tf_bot_difficulty` 0–3 stays as the setting people use, but now means **human skill tiers**: *new player / casual / experienced / competitive*. A tier scales decision quality (positioning, target choice, when to retreat) as well as mechanics.
- Each bot has a **profile** with skill parameters and play-style traits, plus **per-bot variation**, so two bots on the same tier don't play identically. A profile is tied to a **bot name**, so a given bot behaves the same way for the whole session.
- Aim and reaction parameters are calibrated from **published human data** (e.g. roughly 200–250 ms visual reaction for experienced FPS players) and tuned by playtesting.

### 3.3 Information honesty
No omniscience. Everything a bot or the team blackboard knows comes from something actually **seen, heard, or taken damage from**, by itself or a teammate (human or bot). Knowledge **fades** over time. Heard positions are **approximate**.

### 3.4 Team balance with humans
Fixed balancing: profiles are chosen so team skill totals are roughly equal. Each human counts as a configurable tier, defaulting to *experienced*. **No rubber-banding.**

### 3.5 Performance budget
**You plus 23 bots on the listen server must hold 66 tick.** Each new system reports its cost through `VPROF_BUDGET(..., "NextBot")`, and the A/B harness logs cost per feature.

### 3.6 Code layout
- New systems go in **new files and folders** (e.g. `tf/bot/profile/`, `tf/bot/aim/`, `tf/bot/blackboard/`, `tf/nav_mesh/autogen/`).
- Valve's existing files only get **small, flag-gated hooks** that call into those systems.
- Kept HL2 files stay at their original paths (see C0).

*Why:* Valve keeps shipping sync commits (e.g. `39f6dde8`) that touch the same `tf_bot*.cpp` files. Small hooks keep merges cheap.

### 3.7 Feature flags and A/B testing
- Each improvement sits behind **its own per-bot feature flag**. Each team gets a convar choosing which flags its bots spawn with.
- **Syntax:** `tf_bot_features_red` / `tf_bot_features_blu` hold a comma list of feature names, or the keywords `stock` (none), `default` (proven features; the convar default) or `all`. A bot reads its team's value **at every spawn** (`TFBotFeatures::OnBotSpawn`, hooked in `CTFBot::Spawn`). `tf_bot_features_list` and `tf_bot_features_dump` show the registry and each bot's set. Code: `tf/bot/features/`.
- New features are appended to `ETFBotFeature`, never renumbered. Time feature code with `TFBOT_FEATURE_COST_SCOPE( feature )` so its cost appears in the metrics.
- This supports full new-vs-stock matches and **ablation runs** that switch off one feature at a time.
- A feature becomes **on by default** once A/B results and playtests support it.
- Old and new code paths must be able to run **side by side in the same match**.

### 3.8 Evaluation
- **Automated A/B**: new bots against stock bots on headless srcds with `sv_cheats 1` and an increased `host_timescale`, over many rounds.
- **Metrics:**
  - win rate
  - time on objective
  - time stuck (and where)
  - accuracy compared with typical human ranges
  - cost per feature
- **Your playtests are the final judge.**
- **Fairness:** every matchup runs **in both directions in equal numbers** (A on RED vs B on BLU, then swapped), because most test maps are asymmetric. Results are reported per side and combined.
- **Unit:** one data point is one **round**. Only real round-timer stalemates count as rounds; other stalemate kinds are ignored. Matches are **12 vs 12**.
- **Throughput:** several servers **in parallel** (default 4; one 22-bot server uses about 0.7 GB RAM and a quarter of a core at timescale 1). A raised `host_timescale` is used only once stock-vs-stock results at that speed match timescale 1 within noise. *Why:* NextBot runs at most one round of full bot updates per server frame, with a 15 ms wall-clock budget (`NextBot/NextBotManager.cpp:318-327`, `nb_update_framelimit`), so packing more ticks into a frame could slow bots' thinking in game time.
- **Metrics:** JSON Lines per match, from `tf/bot/metrics/`. Hitscan accuracy counts a hit when a `player_hurt` comes from an attacker whose active hitscan gun fired in that same tick, which excludes sentry damage. Projectile accuracy is deferred to M2.
- **Control:** the harness drives srcds only through the command line. With `tf_bot_ab_rounds N`, the game writes its metrics and quits after N rounds. No RCON.

### 3.9 Workflow
- One short-lived **branch per milestone**. **No PRs.** When the milestone's build and checks pass, Claude merges it into `master` with linear history (rebase / fast-forward), pushes, and deletes the branch. *(Changed 2026-11-26: PRs add nothing on a personal, local-only project.)*
- Each milestone's A/B summary goes in `docs/tfbot/results/<milestone>.md`, committed with the milestone.
- Your playtests still decide whether a feature's flag becomes on by default (§3.7).
- Within a milestone, work in small steps that each build.

---

## 4. Environment

| Item | Location / note |
|---|---|
| Build | `src/buildallprojects [release\|debug]` (podman + Steam Runtime "sniper"). Needs `~/.ccache`. |
| Mod binaries | `game/mod_tf/bin/linux64/{server,client}.so` |
| Listen server / client | `game/mod_tf_linux64` |
| Dedicated server | Source SDK Base 2013 Dedicated Server (app 244310) at `~/srcds/sdk2013/` |
| steamcmd | `~/steamcmd/steamcmd.sh` |
| Bot-only matches | Need `tf_bot_join_after_player 0` (default 1, `tf/bot/tf_bot_manager.cpp:28`) **and** `tf_allow_server_hibernation 0`; otherwise an empty dedicated server stops simulating |
| `host_timescale` | Not cheat-protected (flags: `replicated` only). Measured: 4× gave about 5.7× the kills of timescale 1 in the same wall time. |
| srcds setup | `tools/tfbot/ds_setup.py` builds `~/srcds/views/` (see below). Re-run it after adding top-level folders to `game/mod_tf`. |
| A/B runs | `tools/tfbot/ab.py run …` / `ab.py report …` / `ab.py compare …`; output goes to `tfbot_runs/` (gitignored). Each run copies its own `server.so`, so rebuilding during a run is safe. |
| Building while servers run | `src/buildnocopy` links `server.so`/`client.so` into `src/_vpc_/ninja/release/objs/` without copying them over `game/mod_tf/bin`. The normal build copies with `cp`, which rewrites a `.so` that live servers (or your game) have mapped. Test such a build with `ab.py run --server-so <path>`. |
| Nav batch | `tools/tfbot/nav_batch.py list` / `run` (M5) |

**Why srcds needs a "server view" of the mod** (all found while setting up M0):
1. The dedicated server app ships only a **32-bit** launcher. `srcds_linux64` is copied from the SDK Base client install.
2. `gameinfo.txt`'s `|appid_N|` paths resolve only through the Steam client's `SteamApps()` (`src/public/filesystem_init.cpp:697-745`), which srcds lacks. The view's `gameinfo.txt` uses absolute paths instead, and those go through **space-free symlinks**, because paths with spaces ("Team Fortress 2") failed to mount.
3. The dedicated engine loads **`server_srv.so`**, never `server.so`, so the view aliases it.
4. Our `server.so` links the client-flavour `libtier0.so` / `libvstdlib.so` and asks for `soundemittersystem.so` / `scenefilecache.so` (`_DLL_EXT` is baked in at compile time). `~/srcds/views/lib/` aliases each to the engine's `_srv` copy, so only one instance of each is loaded.
5. srcds ignores commands piped into stdin when there's no terminal, so everything goes on the command line.

### Test maps
| Purpose | Maps |
|---|---|
| With Valve navs (daily development + A/B) | ctf_2fort, pl_badwater, pl_upward, cp_dustbowl, cp_process_final, koth_viaduct |
| With no nav (pipeline testing) | koth_harvest_final, cp_steel, plr_hightower, ctf_doublecross |
| Pipeline quality | ctf_2fort: Valve's nav vs a generated nav, with bots A/B'd |

---

## 5. Milestones

Order is set by dependencies: **measure before changing**, and **data before features**.

```text
C0 cleanup ─ D0 design doc ─ M0 harness ─ M1 profiles ─ M1b profile GUI ─ M2 aim
   ─ M3 blackboard+voice ─ M3b engineers ─ M4 combat movement ─ M4b cloning
   ─ M5 nav pipeline ─ M6 unlocks ─ M7 mobility jumps
```

M5 barely touches the AI and could move earlier if playing more maps soon matters more than smarter bots.

Each milestone lists **proposed exit criteria**. These are proposals, to be confirmed when the milestone starts.

### C0 · Cleanup *(done: `45cdcad2`)*
Removed the HL2 / HL2:DM / Episodic / Lost Coast projects and `game/mod_hl2mp`. Kept the 14 `hl2/` files the TF build still compiles or lists, at their original paths. Build scripts now build TF only, `.gitignore` covers build output, and the README is updated. Verified: the TF object set (2068 objects) is identical before and after.

### D0 · Design doc *(this file; done: `b1617b15`)*

### M0 · Harness
- **Feature-flag system** (§3.7): per-bot flags set at spawn from per-team convars.
- **Metrics logging** (§3.8) to a machine-readable file per match.
- **A/B runner script** for headless srcds: map rotation over the test set, team assignment, timescale, N rounds, and a results summary.
- **Performance tracking** (§3.5).
- **Baseline runs** of stock vs stock on all six Valve-nav maps.
- *Proposed exit:* stock-vs-stock runs land near 50% win rate on each map, which shows the harness itself is fair, and a results summary is generated automatically.

### M1 · Profiles
- **Profile files:**
  - stock `scripts/tf_bot_profiles.txt`, custom `scripts/tf_bot_profiles_custom.txt`, never overwritten
  - **inheritance** through a `"base"` key (a tier or another profile)
  - **style presets** (e.g. `battle_engie`, `forward_engie`, `pocket_medic`, `roamer_soldier`, `flank_scout`)
  - **validated values**: defined ranges, out-of-range values clamped with a warning, unknown keys flagged
- **Contents:**
  - skill: reaction time, aim error, decision quality, awareness, …
  - style: aggression, flank vs front line, preferred classes, callout rate, engineer style, loadouts (M6)
- **Tier redefinition** (§3.2), per-bot variation, one profile per bot name.
- **Team balancing** (§3.4).
- **Class selection:** profile preferences weighted by team needs (no medic, so a medic-liking bot switches; defenders lean engineer and heavy; an attack stuck at a sentry pulls in demo or spy). Class limits respected; switch **only on respawn**.
- **MvM mapping** (§3.1).
- **Console tooling:** `tf_bot_profile_new <name> [base]`, `tf_bot_profile_set <name> <key> <value>` (tab completion), `tf_bot_profile_show`, `tf_bot_profile_save`, `tf_bot_profile_reload` (hot reload; live bots update), `tf_bot_add_profile <name> [team]`.

### M1b · Profile GUI
- An in-game panel opened with `tf_bot_profile_editor`. Its sliders and fields are **built from the validation schema**, so later keys appear automatically.
- **Edit the bot you're looking at** with live hot reload, plus spawn-test-bot and save buttons.
- Reads and writes the profile files directly, which works because everything is local, and uses the M1 commands as its back end.
- *Why before M2:* it becomes the main tool for tuning aim by feel.

### M2 · Aim model
Replace the "re-read every N seconds + jitter + fixed turn speed" model with:
- a **reaction delay** before responding to a new threat
- **flicks that overshoot and then correct**
- **tracking error that grows with how fast the target crosses the bot's view**, so strafing works against bots
- **pre-aiming** at likely enemy positions
- **projectile leading using each weapon's real projectile speed** (no hard-coded 1100)

All parameters come from the profile.

### M3 · Blackboard, voice, perception
- A **per-team blackboard**: enemy sightings, known sentries, uber readiness, push readiness, spy alerts. Fed only by perception (§3.3) and fading over time.
- **Behaviours that use it:** grouping up before a push, waiting for an uber that's nearly charged, focus fire on called targets, medics answering **bot** calls (remove the human-only check).
- **Voice in both directions:** bots say the matching voice command when they post something important (rate-limited). Human voice commands, heard through `OnSpokeConcept`, feed the blackboard.
- **Perception additions:** short-range hearing of footsteps, jumps and landings, giving approximate positions, plus occasional glancing around when not fighting. Both scaled by the profile's awareness value.
- Fix `IsGoodUberTarget` (§2.9).
- *Note:* the perception additions were placed here during planning; move them if that turns out wrong.

### M3b · Engineers
- **Three styles**, chosen by profile and fixed for the session:
  - *Turtle:* today's behaviour, improved.
  - *Forward:* the nest follows the push.
  - *Battle:* Gunslinger minis. The style is defined here but enabled in M6.
- **Position depends on the situation:**
  - Forward on attack: just behind the front line.
  - Forward on defense: the furthest choke the team holds, falling back toward the point.
  - Turtle: the objective or the last choke before it.
  - Battle: wherever the fighting is.
- **Every engineer can carry buildings:** pick up (alt-fire on own building), carry, redeploy. Used to follow the push, save a badly damaged sentry, move after a capture, and leave a spot that keeps getting spammed. Uses the existing player carry code in `tf/tf_player.cpp`; no new game mechanics needed.
- **Turtle improvements:** better choke choice and upgrading teammates' buildings (the TODO at `tf_bot_engineer_building.cpp:68`).
- Depends on M3 (push state).

### M4 · Combat movement (phase 1)
Strafing patterns, scout double-jump dodges, jumping around corners. **No pathfinding changes.** All of it is driven by the profile.

### M4b · Playstyle cloning
- **Opt-in live recording:** `tf_bot_record_player <name>`, normally just the host. Stats build up across sessions in a local stats file.
- **What it measures:**
  - class and loadout choices
  - how far forward the player plays (aggression)
  - typical engagement distances
  - time to first shot after an enemy comes into view
  - accuracy by weapon type
  - the health level at which the player retreats
  - how often the player calls things out
  - strafing and jumping habits in fights
- `tf_bot_profile_from_record <name>` fits these stats onto profile parameters with **plain statistics, no ML**. It writes an ordinary custom profile based on the closest tier, editable in the GUI.
- Later milestones add their own measurements as they land.

### M5 · Nav pipeline
- **Batch generation (main path):** a script runs headless srcds per nav-less map, generates and analyses, saves to `game/mod_tf/maps/`, then quits.
- **Fallback at map load:** if a map has no nav from any source, generate it, show a console and chat notice, keep bots out until it's done, then reload the map once.
- **Never shadow an existing nav:** detection uses the **loader's answer**, which covers the VPK, the mod folder and BSP-embedded navs. A force-regenerate convar exists for comparisons.
- **Starting points:** our own seeds at every `info_player_teamspawn` plus objectives (control points, flags, cart path nodes), instead of `info_player_start`. Hook: `CNavMesh::AddWalkableSeeds()` is virtual and documented as the place for "positions a mod specifies" (`nav_mesh.h:364`), so override it in `CTFNavMesh`.
- During generation the **payload cart and doors are non-solid**.
- **Analysis always runs**, with `nav_quicksave 1` skipping the steps TF doesn't use (sniper spots, encounter spots, occupy times). Without it bots are blind (§2.7).
- **Validation:** a path from each team's spawn to every objective, a check for areas cut off from the mesh, and stuck-spot logging during bot runs. Maps that fail are warned about and marked **unverified**.
- **Scope:** all installed official maps, plus any community or Workshop BSP.
- **No generated sniper/sentry hints.** Revisit only if A/B data shows snipers or engineers doing worse on generated navs.

### M6 · Unlocks
- A **curated whitelist**: an item is added only once the bot code handles it.
- **Loadouts set per profile**, falling back to stock.
- **First batch:**
  - Direct Hit: its own projectile speed and splash logic
  - Kritzkrieg: its own deploy logic
  - Gunslinger: enables the battle engineer style; stock shotgun and pistol only
- Later batches: Frontier Justice, Widowmaker, Rescue Ranger, Quick-Fix logic, …
- Items are created through `AddItem` / `GenerateItemFromDefIndex` (§2.5). No random items from the full schema.

### M7 · Mobility jumps (phase 2)
Rocket and sticky jumps as **jump links between nav areas**. This requires changes to the path planner and locomotion (`NextBot/Path/NextBotPathFollow.cpp`, `tf/bot/tf_bot_locomotion.cpp`).

---

## 5b. Implementation notes (as built)

Every feature below is registered in `tf/bot/features/` and **off by default** ("testing") until A/B results and your playtests support it (§3.7). Turn features on per team, e.g. `tf_bot_features_blu all`, or list them: `tf_bot_features_red profiles,aim,blackboard`. `tf_bot_features_list` shows the registry.

| Milestone | Feature flag(s) | Code | Hooks in Valve code |
|---|---|---|---|
| M0 | `test_noop` | `tf/bot/features/`, `tf/bot/metrics/`, `tools/tfbot/` | `CTFBot::Spawn`, `CTFGameStats::Event_PlayerFiredWeapon` |
| M1 | `profiles` | `tf/bot/profile/`, `shared/tf/tf_bot_profile_schema.*`, `game/mod_tf/scripts/tf_bot_profiles.txt` | `CTFBot::Spawn`, `CTFBot::GetNextSpawnClassname`, `CTFBotBody::GetHeadAimTrackingInterval`, `CTFBotVision::GetMinRecognizeTime`, tactical monitor health threshold |
| M1b | (client) | `client/tf/tf_bot_profile_editor.cpp` | none (new file, `client_tf.vpc`) |
| M2 | `aim` | `tf/bot/aim/` | `CTFBotMainAction::SelectTargetPoint`, `CTFBotBody::GetMaxHeadAngularVelocity` (new override), `CTFBot::UpdateLookingAroundForIncomingPlayers` |
| M3 | `blackboard`, `voice`, `perception`, `medic` | `tf/bot/blackboard/` | `CTFPlayer::SpeakConceptIfAllowed`, `CTFBot::RealizeSpy`, `SelectMoreDangerousThreat`, capture-point and payload-push actions, medic patient choice, `IsGoodUberTarget` |
| M3b | `engineer` | `tf/bot/engineer/` | engineer building loop, move-to-build (nest choice, carried sentry) |
| M4 | `movement` | `tf/bot/movement/` | `CTFBotMainAction::Dodge` |
| M4b | (commands) | `tf/bot/clone/` | none (chained on the M0/M3 hooks) |
| M5 | (server-wide convars) | `tf/nav_mesh/autogen/`, `tools/tfbot/nav_batch.py`, `game/mod_tf/maps/` | `CTFNavMesh::AddWalkableSeeds` (declared in `tf_nav_mesh.h`) |
| M6 | `unlocks` | `tf/bot/unlocks/` | `CTFBot::Spawn`, medic uber decision, engineer building loop |
| M7 | `mobility` | `tf/bot/mobility/` | `CTFBotLocomotion::GetMaxJumpHeight` (no longer inline), `ClimbUpToLedge` (new override), `Update` |

**Console commands:** `tf_bot_features_list`, `tf_bot_features_dump`, `tf_bot_profile_keys`, `tf_bot_profile_list`, `tf_bot_profile_show`, `tf_bot_profile_new`, `tf_bot_profile_set`, `tf_bot_profile_save`, `tf_bot_profile_reload`, `tf_bot_add_profile`, `tf_bot_profile_editor` (client), `tf_bot_record_player`, `tf_bot_profile_from_record`, `tf_bot_nav_validate`, `tf_bot_unlocks_list`.

**Where the build differs from the plan:**
- **M1:** anonymous bots need no hand-written profiles. Each bot name maps to its tier plus a name-seeded variation, and its style traits and class preferences are derived from the name too, so personalities are stable per name. `tf_bot_human_tier` sets how humans count when balancing.
- **M1b:** the editor sends `tf_bot_profile_*` commands instead of writing files itself, so the console and the GUI can't disagree. It reads the files only to show values.
- **M2:** the stock tracking loop is kept, and its levers are profile-driven: tracking interval (M1), turn speed, and the target point with the error model. A longer tracking interval means the bot keeps extrapolating a stale velocity, which is what makes strafing work against it.
- **M3:** `TheNextBots().OnSpokeConcept` is never called in this codebase, so voice commands come through a new hook in `CTFPlayer::SpeakConceptIfAllowed`. Hearing turns the bot toward an approximate spot (`INTERESTING` look) instead of creating a known entity, which keeps it honest.
- **M3b:** pickup uses `CTFPlayer::TryToPickupBuilding()` while facing the building; placement reuses the stock "valid placement → fire, else shuffle" loop.
- **M5:** doors and the payload cart need no special handling during generation (brush-only trace mask; doors are treated as passable). Generation also needs `tf_allow_server_hibernation 0` on an empty server.
- **M7:** soldiers (rocket launcher, Direct Hit, Cow Mangler) and demomen (stickybomb launcher) only, at 60%+ health.

## 6. Wishlist (not scheduled)
- Further unlock batches beyond M6.
- Generated sniper and sentry hints, if the M5 data calls for them.
- Calibrating aim from recorded `.dem` demos, if hand-tuned values feel off.
- Parsing demos as an extra input for playstyle cloning (M4b uses live recording).

## 7. Open details (decide during the relevant milestone)
- The exact profile key list and value ranges (M1).
- Recognising human voice commands: which response concepts map to which blackboard entries (M3).
- *(Settled in M0: metrics format, runner command line, feature-flag syntax, `host_timescale` doesn't need `sv_cheats`. See §3.7, §3.8, §4.)*
