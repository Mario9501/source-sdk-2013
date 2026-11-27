#!/usr/bin/env python3
"""A/B harness for the TFBot overhaul.

  ab.py run     --maps valve6 --a stock --b default --rounds 10 --instances 4
  ab.py report  tfbot_runs/<run> [--write docs/tfbot/results/M0.md]
  ab.py compare tfbot_runs/<run1> tfbot_runs/<run2>   (e.g. timescale check)

`run` plays every map in both directions (A on RED / B on BLU, then swapped),
`--rounds` rounds each, on headless srcds instances in parallel. Each server
quits by itself after the rounds (tf_bot_ab_rounds); the per-match JSON Lines
files are collected into tfbot_runs/<run>/. `report` turns a run into win
rates (Wilson 95% intervals) and per-team averages.

Feature sets (--a / --b) use tf_bot_features_* syntax: a comma list of feature
names, or stock / default / all.

Requires tools/tfbot/ds_setup.py to have been run once.
See docs/tfbot/DESIGN.md sections 3.7, 3.8 and the M0 milestone.
"""
import argparse
import concurrent.futures
import datetime
import glob
import json
import math
import os
import shutil
import signal
import subprocess
import sys
import threading
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RUNS_DIR = os.path.join(REPO, "tfbot_runs")

MAP_SETS = {
    "valve6": ["ctf_2fort", "pl_badwater", "pl_upward", "cp_dustbowl", "cp_process_final", "koth_viaduct"],
    "navless4": ["koth_harvest_final", "cp_steel", "plr_hightower", "ctf_doublecross"],
}

# Per-map convar overrides for maps whose rounds run too long with bots
# (design doc §3.8 / M0 Q59). Always listed in run.json and in reports so
# results stay comparable between milestones. Extend with --override.
MAP_OVERRIDES = {
}


# --------------------------------------------------------------------------- run

def expand_maps(specs):
    maps = []
    for spec in specs:
        for item in spec.split(","):
            item = item.strip()
            if item:
                maps.extend(MAP_SETS.get(item, [item]))
    return list(dict.fromkeys(maps))


def map_overrides(args):
    """MAP_OVERRIDES plus --override MAP:CVAR=VALUE entries."""
    out = {m: dict(cvars) for m, cvars in MAP_OVERRIDES.items()}
    for spec in args.override or []:
        try:
            m, rest = spec.split(":", 1)
            cvar, value = rest.split("=", 1)
        except ValueError:
            sys.exit(f"bad --override '{spec}', expected MAP:CVAR=VALUE")
        out.setdefault(m, {})[cvar] = value
    return out


def build_jobs(args, run_id):
    overrides = map_overrides(args)
    jobs = []
    for m in expand_maps(args.maps):
        for a_team in ("red", "blu"):
            red, blu = (args.a, args.b) if a_team == "red" else (args.b, args.a)
            jobs.append({
                "name": f"{m}__a_{a_team}",
                "map": m,
                "a_team": a_team,
                "red": red,
                "blu": blu,
                "overrides": overrides.get(m, {}),
                "metrics_run_id": f"{run_id}/{m}__a_{a_team}",
            })
    return jobs


def server_command(args, job, port, view, lib):
    srv_bin = os.path.join(args.srcds, "bin", "linux64")
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = f"{srv_bin}:{lib}"
    cmd = [
        "./srcds_linux64", "-game", view, "-console", "-norestart", "-insecure",
        "-port", str(port),
        "+sv_lan", "1",
        "+maxplayers", str(args.bots),
        "+tf_allow_server_hibernation", "0",
        "+tf_bot_join_after_player", "0",
        "+tf_bot_auto_vacate", "0",          # else the quota keeps a slot free: 12 vs 11
        "+tf_bot_quota_mode", "normal",
        "+tf_bot_quota", str(args.bots),
        "+tf_bot_difficulty", str(args.difficulty),
        "+mp_timelimit", "0",
        "+mp_maxrounds", "0",
        "+mp_winlimit", "0",
        "+mp_bonusroundtime", "5",
        "+host_timescale", str(args.timescale),
        "+tf_bot_features_red", job["red"],
        "+tf_bot_features_blu", job["blu"],
        "+tf_bot_metrics", "1",
        "+tf_bot_metrics_run_id", job["metrics_run_id"],
        "+tf_bot_ab_rounds", str(args.rounds),
    ]
    for cvar, value in job.get("overrides", {}).items():
        cmd += [f"+{cvar}", str(value)]
    cmd += ["+map", job["map"]]
    return cmd, env


def run_job(args, job, port, view, lib, run_dir, print_lock):
    job_dir = os.path.join(run_dir, "jobs", job["name"])
    os.makedirs(job_dir, exist_ok=True)
    cmd, env = server_command(args, job, port, view, lib)
    timeout = 120 + args.rounds * args.round_timeout_min * 60 / max(args.timescale, 0.1)

    start = time.time()
    with open(os.path.join(job_dir, "server.log"), "w") as log:
        proc = subprocess.Popen(cmd, cwd=args.srcds, env=env, stdin=subprocess.DEVNULL,
                                stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        timed_out = False
        try:
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()

    src = os.path.join(view, "tfbot_metrics", job["metrics_run_id"])
    files = sorted(glob.glob(os.path.join(src, "*.jsonl")))
    for f in files:
        shutil.copy2(f, job_dir)

    result = dict(job, port=port, exit_code=proc.returncode, timed_out=timed_out,
                  wall_seconds=round(time.time() - start, 1), metrics_files=[os.path.basename(f) for f in files])
    with print_lock:
        status = "TIMEOUT" if timed_out else f"exit {proc.returncode}"
        print(f"  {job['name']:<40} {status:<10} {result['wall_seconds']:>7.0f}s  {len(files)} metrics file(s)",
              flush=True)
    return result


def snapshot_view(shared_view, run_dir):
    """Per-run copy of the server view with its own copy of the binaries.

    Rebuilding the mod during a run then can't swap code under live servers
    or mix two builds in one A/B run. Non-binary entries stay symlinks.
    """
    view = os.path.join(run_dir, "view")
    os.makedirs(os.path.join(view, "bin", "linux64"))
    for entry in os.listdir(shared_view):
        src = os.path.join(shared_view, entry)
        if entry == "bin":
            continue
        if entry == "gameinfo.txt":
            shutil.copy2(src, os.path.join(view, entry))
        elif os.path.islink(src):
            os.symlink(os.readlink(src), os.path.join(view, entry))
    bin_src = os.path.join(shared_view, "bin", "linux64")
    for f in os.listdir(bin_src):
        if f == "server_srv.so":
            continue
        shutil.copy2(os.path.realpath(os.path.join(bin_src, f)), os.path.join(view, "bin", "linux64", f))
    os.symlink("server.so", os.path.join(view, "bin", "linux64", "server_srv.so"))
    return view


def cmd_run(args):
    shared_view = os.path.join(args.views, "mod_tf")
    lib = os.path.join(args.views, "lib")
    for path in (shared_view, lib, os.path.join(args.srcds, "srcds_linux64")):
        if not os.path.exists(path):
            sys.exit(f"missing {path}: run tools/tfbot/ds_setup.py first")

    run_id = args.label or datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = os.path.join(RUNS_DIR, run_id)
    if os.path.exists(run_dir):
        sys.exit(f"run directory already exists: {run_dir}")
    os.makedirs(run_dir)
    view = snapshot_view(shared_view, run_dir)
    git_rev = subprocess.run(["git", "-C", REPO, "describe", "--always", "--dirty"],
                             capture_output=True, text=True).stdout.strip()

    jobs = build_jobs(args, run_id)
    manifest = {
        "run_id": run_id,
        "git": git_rev,
        "started": datetime.datetime.now().isoformat(timespec="seconds"),
        "a": args.a, "b": args.b, "rounds_per_job": args.rounds,
        "timescale": args.timescale, "difficulty": args.difficulty, "bots": args.bots,
        "instances": args.instances, "jobs": [],
    }
    print(f"run {run_id}: {len(jobs)} jobs, {args.instances} in parallel, A='{args.a}' B='{args.b}', "
          f"{args.rounds} rounds/job, timescale {args.timescale}", flush=True)

    print_lock = threading.Lock()
    ports = [args.base_port + 10 * i for i in range(args.instances)]
    free_ports = list(ports)
    port_lock = threading.Lock()

    def worker(job):
        with port_lock:
            port = free_ports.pop()
        try:
            return run_job(args, job, port, view, lib, run_dir, print_lock)
        finally:
            with port_lock:
                free_ports.append(port)

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.instances) as pool:
        results = list(pool.map(worker, jobs))

    # The binaries are big; keep only the metrics.
    shutil.rmtree(os.path.join(view, "bin"), ignore_errors=True)

    manifest["jobs"] = results
    manifest["finished"] = datetime.datetime.now().isoformat(timespec="seconds")
    with open(os.path.join(run_dir, "run.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"done: {run_dir}")
    print(f"report: tools/tfbot/ab.py report {os.path.relpath(run_dir, REPO)}")


# ------------------------------------------------------------------------ report

def wilson(wins, n, z=1.96):
    if n == 0:
        return (0.0, 0.0, 0.0)
    p = wins / n
    denom = 1 + z * z / n
    center = (p + z * z / (2 * n)) / denom
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / denom
    return (p, max(0.0, center - half), min(1.0, center + half))


def fmt_rate(wins, n):
    if n == 0:
        return "n/a"
    p, lo, hi = wilson(wins, n)
    return f"{100 * p:.0f}% [{100 * lo:.0f}–{100 * hi:.0f}]"


def load_run(run_dir):
    with open(os.path.join(run_dir, "run.json")) as f:
        manifest = json.load(f)
    rounds = []
    matches = []
    for job in manifest["jobs"]:
        job_dir = os.path.join(run_dir, "jobs", job["name"])
        for fname in job.get("metrics_files", []):
            with open(os.path.join(job_dir, fname)) as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        rec = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if rec["type"] == "round_end":
                        rounds.append((job, rec))
                    elif rec["type"] == "match_end":
                        matches.append((job, rec))
    return manifest, rounds, matches


def counted(rec):
    """Rounds that count toward results: wins, and real round-timer stalemates."""
    return not rec["stalemate"] or rec["reason"] == 1  # STALEMATE_TIMER


TEAM_FIELDS = ["kills", "deaths", "damage", "captures", "objective_seconds", "stuck_seconds"]


def cmd_report(args):
    run_dir = os.path.abspath(args.run_dir)
    manifest, rounds, matches = load_run(run_dir)
    a_label, b_label = manifest["a"], manifest["b"]

    by_map = {}
    totals = {"a_wins": 0, "b_wins": 0, "stalemates": 0}
    side = {"red": [0, 0], "blu": [0, 0]}          # A's wins / decided rounds, by A's team
    per_team = {"a": {k: 0.0 for k in TEAM_FIELDS}, "b": {k: 0.0 for k in TEAM_FIELDS}}
    hits = {"a": [0, 0], "b": [0, 0]}
    durations = []
    n_rounds = 0

    for job, rec in rounds:
        if not counted(rec):
            continue
        a_team = job["a_team"]
        b_team = "blu" if a_team == "red" else "red"
        m = by_map.setdefault(job["map"], {"a_wins": 0, "b_wins": 0, "stalemates": 0,
                                           "a_red": [0, 0], "a_blu": [0, 0]})
        n_rounds += 1
        durations.append(rec["duration"])
        if rec["stalemate"] or rec["winner"] == "none":
            m["stalemates"] += 1
            totals["stalemates"] += 1
        else:
            a_won = rec["winner"] == a_team
            key = "a_wins" if a_won else "b_wins"
            m[key] += 1
            totals[key] += 1
            m[f"a_{a_team}"][0] += int(a_won)
            m[f"a_{a_team}"][1] += 1
            side[a_team][0] += int(a_won)
            side[a_team][1] += 1
        for who, team in (("a", a_team), ("b", b_team)):
            st = rec["teams"][team]
            for k in TEAM_FIELDS:
                per_team[who][k] += st[k]
            hits[who][0] += st["hitscan_hits"]
            hits[who][1] += st["hitscan_shots"]

    lines = []
    lines.append(f"# A/B results: `{manifest['run_id']}`")
    lines.append("")
    lines.append(f"- **A:** `{a_label}`  **B:** `{b_label}`")
    lines.append(f"- {manifest['rounds_per_job']} rounds per map per side, timescale {manifest['timescale']}, "
                 f"difficulty {manifest['difficulty']}, {manifest['bots']} bots, "
                 f"{manifest['instances']} parallel servers")
    lines.append(f"- Started {manifest['started']}, finished {manifest.get('finished', '?')}, "
                 f"build `{manifest.get('git', '?')}`")
    overrides = {j["map"]: j["overrides"] for j in manifest["jobs"] if j.get("overrides")}
    if overrides:
        lines.append("- **Per-map overrides:** " + "; ".join(
            f"{m}: " + ", ".join(f"`{k} {v}`" for k, v in cv.items()) for m, cv in sorted(overrides.items())))
    bad = [j for j in manifest["jobs"] if j["timed_out"] or not j.get("metrics_files")]
    if bad:
        lines.append(f"- **Jobs with problems:** " + ", ".join(
            f"{j['name']} ({'timeout' if j['timed_out'] else 'no metrics'})" for j in bad))
    lines.append("")

    decided = totals["a_wins"] + totals["b_wins"]
    lines.append("## Win rate of A (95% Wilson interval)")
    lines.append("")
    lines.append("| Map | A as RED | A as BLU | Combined | Stalemates |")
    lines.append("|---|---|---|---|---|")
    for name in sorted(by_map):
        m = by_map[name]
        lines.append(f"| {name} | {fmt_rate(*m['a_red'])} (n={m['a_red'][1]}) "
                     f"| {fmt_rate(*m['a_blu'])} (n={m['a_blu'][1]}) "
                     f"| {fmt_rate(m['a_wins'], m['a_wins'] + m['b_wins'])} | {m['stalemates']} |")
    lines.append(f"| **All maps** | {fmt_rate(*side['red'])} (n={side['red'][1]}) "
                 f"| {fmt_rate(*side['blu'])} (n={side['blu'][1]}) "
                 f"| **{fmt_rate(totals['a_wins'], decided)}** (n={decided}) | {totals['stalemates']} |")
    lines.append("")

    lines.append("## Per-team averages per round")
    lines.append("")
    lines.append("| | " + " | ".join(TEAM_FIELDS) + " | hitscan accuracy |")
    lines.append("|---|" + "---|" * (len(TEAM_FIELDS) + 1))
    for who, label in (("a", "A"), ("b", "B")):
        cells = [f"{per_team[who][k] / n_rounds:.1f}" if n_rounds else "n/a" for k in TEAM_FIELDS]
        acc = f"{100 * hits[who][0] / hits[who][1]:.1f}%" if hits[who][1] else "n/a"
        lines.append(f"| {label} | " + " | ".join(cells) + f" | {acc} |")
    if durations:
        durations.sort()
        lines.append("")
        lines.append(f"Round duration (game seconds): median {durations[len(durations) // 2]:.0f}, "
                     f"mean {sum(durations) / len(durations):.0f}, n={len(durations)}")
    lines.append("")

    if matches:
        ticks = sum(r["tick_ms"]["ticks"] for _, r in matches)
        mean = sum(r["tick_ms"]["mean"] * r["tick_ms"]["ticks"] for _, r in matches) / max(ticks, 1)
        peak = max(r["tick_ms"]["max"] for _, r in matches)
        over = sum(r["tick_ms"]["over_budget"] for _, r in matches)
        lines.append("## Server tick cost (game-side think, ms)")
        lines.append("")
        lines.append(f"Mean {mean:.2f} ms, max {peak:.1f} ms, {100 * over / max(ticks, 1):.2f}% of {ticks} ticks "
                     f"over the {1000 / 66.67:.1f} ms budget.")
        costs = {}
        for _, r in matches:
            for k, v in r.get("feature_cost_ms", {}).items():
                costs[k] = costs.get(k, 0.0) + v
        if any(v > 0 for v in costs.values()):
            lines.append("")
            lines.append("Feature cost (total ms): " + ", ".join(f"`{k}` {v:.1f}" for k, v in sorted(costs.items())))
        lines.append("")

    text = "\n".join(lines)
    print(text)
    if args.write:
        out = os.path.abspath(args.write)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "w") as f:
            f.write(text)
        print(f"\nwritten to {out}", file=sys.stderr)


# ----------------------------------------------------------------------- compare

def round_rates(rec):
    """Per-round rates for both teams combined, per game minute."""
    minutes = max(rec["duration"], 1.0) / 60.0
    t = rec["teams"]
    both = lambda k: t["red"][k] + t["blu"][k]
    shots = both("hitscan_shots")
    return {
        "round_seconds": rec["duration"],
        "kills_per_min": both("kills") / minutes,
        "damage_per_min": both("damage") / minutes,
        "objective_s_per_min": both("objective_seconds") / minutes,
        "stuck_s_per_min": both("stuck_seconds") / minutes,
        "hitscan_accuracy": (both("hitscan_hits") / shots) if shots else None,
    }


RATE_KEYS = ["round_seconds", "kills_per_min", "damage_per_min", "objective_s_per_min",
             "stuck_s_per_min", "hitscan_accuracy"]


def intervals_overlap(lo1, hi1, lo2, hi2):
    if any(math.isnan(x) for x in (lo1, hi1, lo2, hi2)):
        return False
    return lo1 <= hi2 and lo2 <= hi1


def mean_ci(values):
    values = [v for v in values if v is not None]
    n = len(values)
    if n == 0:
        return (float("nan"), float("nan"), float("nan"), 0)
    m = sum(values) / n
    if n < 2:
        return (m, float("nan"), float("nan"), n)
    sd = math.sqrt(sum((v - m) ** 2 for v in values) / (n - 1))
    half = 1.96 * sd / math.sqrt(n)
    return (m, m - half, m + half, n)


def cmd_compare(args):
    runs = []
    for d in (args.run_a, args.run_b):
        manifest, rounds, _ = load_run(os.path.abspath(d))
        runs.append((manifest, [(job["map"], round_rates(rec)) for job, rec in rounds if counted(rec)]))

    (man_a, rounds_a), (man_b, rounds_b) = runs
    maps = sorted({m for m, _ in rounds_a} | {m for m, _ in rounds_b})

    lines = [f"# Compare `{man_a['run_id']}` (timescale {man_a['timescale']}) vs "
             f"`{man_b['run_id']}` (timescale {man_b['timescale']})", ""]
    lines.append(f"Same if the means are within ±{100 * args.tolerance:.0f}% and the 95% intervals overlap. "
                 "Stuck time is reported but not judged (too rare for a stable rate).")
    all_same = True
    for scope in maps + ["all maps"]:
        sel_a = [r for m, r in rounds_a if scope in ("all maps", m)]
        sel_b = [r for m, r in rounds_b if scope in ("all maps", m)]
        lines += ["", f"## {scope} (n = {len(sel_a)} vs {len(sel_b)} rounds)", "",
                  "| metric | A | B | B/A | verdict |", "|---|---|---|---|---|"]
        for k in RATE_KEYS:
            ma, la, ha, _ = mean_ci([r[k] for r in sel_a])
            mb, lb, hb, _ = mean_ci([r[k] for r in sel_b])
            ratio = mb / ma if ma else float("nan")
            same = abs(ratio - 1) <= args.tolerance and intervals_overlap(la, ha, lb, hb)
            judged = k != "stuck_s_per_min"
            verdict = ("same" if same else "**differs**") if judged else "(not judged)"
            if judged and not same and scope == "all maps":
                all_same = False
            lines.append(f"| {k} | {ma:.3f} [{la:.3f}–{ha:.3f}] | {mb:.3f} [{lb:.3f}–{hb:.3f}] "
                         f"| {ratio:.2f} | {verdict} |")
    lines += ["", f"**Overall (all maps): {'same within tolerance' if all_same else 'DIFFERS'}**"]

    text = "\n".join(lines)
    print(text)
    if args.write:
        out = os.path.abspath(args.write)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "w") as f:
            f.write(text + "\n")
        print(f"\nwritten to {out}", file=sys.stderr)


# -------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    run = sub.add_parser("run", help="play A/B matches on headless servers")
    run.add_argument("--maps", action="append", required=True,
                     help="map names and/or sets (" + ", ".join(MAP_SETS) + "), comma separated; repeatable")
    run.add_argument("--a", required=True, help="feature set A")
    run.add_argument("--b", required=True, help="feature set B")
    run.add_argument("--rounds", type=int, default=10, help="rounds per map per side (default 10)")
    run.add_argument("--instances", type=int, default=4, help="parallel servers (default 4)")
    run.add_argument("--timescale", type=float, default=1.0, help="host_timescale (default 1)")
    run.add_argument("--difficulty", type=int, default=1, help="tf_bot_difficulty for both teams (default 1)")
    run.add_argument("--bots", type=int, default=24, help="total bots / maxplayers (default 24, i.e. 12v12)")
    run.add_argument("--round-timeout-min", type=float, default=30.0,
                     help="wall-clock budget per round in game minutes before a job is killed (default 30)")
    run.add_argument("--override", action="append",
                     help="per-map convar override MAP:CVAR=VALUE (repeatable; recorded in the report)")
    run.add_argument("--label", help="run id (default: timestamp)")
    run.add_argument("--base-port", type=int, default=27100)
    run.add_argument("--srcds", default=os.path.expanduser("~/srcds/sdk2013"))
    run.add_argument("--views", default=os.path.expanduser("~/srcds/views"))
    run.set_defaults(func=cmd_run)

    rep = sub.add_parser("report", help="summarise a run")
    rep.add_argument("run_dir")
    rep.add_argument("--write", help="also write the markdown to this file")
    rep.set_defaults(func=cmd_report)

    cmp_ = sub.add_parser("compare", help="compare per-game-minute rates of two runs (e.g. timescale check)")
    cmp_.add_argument("run_a")
    cmp_.add_argument("run_b")
    cmp_.add_argument("--tolerance", type=float, default=0.10, help="allowed relative difference (default 0.10)")
    cmp_.add_argument("--write", help="also write the markdown to this file")
    cmp_.set_defaults(func=cmd_compare)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
