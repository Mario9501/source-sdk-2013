#!/usr/bin/env python3
"""Batch-generate nav meshes for installed TF2 maps that have none (M5).

  nav_batch.py list                      # which maps have a nav, and from where
  nav_batch.py run [--maps a,b] [--instances 3] [--write docs/tfbot/results/M5-navs.md]

A map "has a nav" if one ships in tf2_misc_dir.vpk, is embedded in the BSP's
pakfile, or was already generated into game/mod_tf/maps/. For each map
without one, `run` starts a headless srcds with tf_bot_nav_autogen 1 and
tf_bot_nav_autogen_quit 1: the server seeds generation from spawns and
objectives, generates and analyses, saves maps/<map>.nav into the mod folder,
reloads, validates (objectives reachable from both teams' spawns, how much of
the mesh is cut off) and quits. The verdict line is parsed from the log.

Requires tools/tfbot/ds_setup.py (re-run it once so the view links maps/).
See docs/tfbot/DESIGN.md M5.
"""
import argparse
import concurrent.futures
import os
import re
import signal
import struct
import subprocess
import sys
import threading
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MOD_MAPS = os.path.join(REPO, "game", "mod_tf", "maps")
STEAM_COMMON = os.path.expanduser("~/.local/share/Steam/steamapps/common")
TF_DIR = os.path.join(STEAM_COMMON, "Team Fortress 2", "tf")

VERDICT_RE = re.compile(r"TFBot nav: (\S+) (generated|shipped) nav, (\d+) areas, (\d+) \((\d+)%\) cut off from spawns, "
                        r"(\d+) missing spawn-to-objective routes: (VERIFIED|UNVERIFIED)")


def vpk_navs(vpk_dir_path):
    """Map names with a .nav in a VPK v2 directory file."""
    navs = set()
    with open(vpk_dir_path, "rb") as f:
        sig, version, tree_size = struct.unpack("<III", f.read(12))
        if sig != 0x55AA1234:
            return navs
        if version == 2:
            f.read(16)
        tree = f.read(tree_size)

    pos = 0

    def read_str():
        nonlocal pos
        end = tree.index(b"\0", pos)
        s = tree[pos:end].decode("utf-8", "replace")
        pos = end + 1
        return s

    while True:
        ext = read_str()
        if not ext:
            break
        while True:
            path = read_str()
            if not path:
                break
            while True:
                name = read_str()
                if not name:
                    break
                preload = struct.unpack("<H", tree[pos + 4:pos + 6])[0]
                pos += 18 + preload
                if ext == "nav" and path.strip("/").lower() == "maps":
                    navs.add(name.lower())
    return navs


def bsp_has_embedded_nav(bsp_path):
    """True if the BSP's pakfile lump holds maps/<map>.nav or the embed name."""
    map_nav = (os.path.basename(bsp_path)[:-4] + ".nav").lower().encode()
    try:
        with open(bsp_path, "rb") as f:
            header = f.read(8 + 64 * 16)
            if header[:4] != b"VBSP":
                return False
            ofs, length = struct.unpack("<ii", header[8 + 40 * 16:8 + 40 * 16 + 8])
            if length <= 0:
                return False
            f.seek(ofs)
            pak = f.read(length)
    except OSError:
        return False
    for name in re.findall(rb"[\w/\-]+\.nav", pak):
        name = name.lower()
        if name.endswith(map_nav) or name.endswith(b"embed.nav"):
            return True
    return False


def survey(maps_filter=None):
    vpk = os.path.join(TF_DIR, "tf2_misc_dir.vpk")
    shipped = vpk_navs(vpk) if os.path.exists(vpk) else set()
    rows = []
    for fname in sorted(os.listdir(os.path.join(TF_DIR, "maps"))):
        if not fname.endswith(".bsp"):
            continue
        name = fname[:-4]
        if maps_filter and name not in maps_filter:
            continue
        if name.lower() in shipped:
            source = "vpk"
        elif bsp_has_embedded_nav(os.path.join(TF_DIR, "maps", fname)):
            source = "bsp"
        elif os.path.exists(os.path.join(MOD_MAPS, name + ".nav")):
            source = "generated"
        else:
            source = None
        rows.append((name, source))
    return rows


def cmd_list(args):
    rows = survey(set(args.maps.split(",")) if args.maps else None)
    counts = {}
    for name, source in rows:
        counts[source or "none"] = counts.get(source or "none", 0) + 1
        if args.verbose or not source:
            print(f"  {name:<40} {source or '-'}")
    print("totals: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))


def load_ab():
    """tools/tfbot/ab.py as a module (for its binary snapshot helper)."""
    import importlib.util
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ab.py")
    spec = importlib.util.spec_from_file_location("tfbot_ab", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def generate(args, name, port, lock):
    view = args.view
    lib = os.path.join(args.views, "lib")
    env = dict(os.environ, LD_LIBRARY_PATH=f"{os.path.join(args.srcds, 'bin', 'linux64')}:{lib}")
    log_dir = os.path.join(REPO, "tfbot_runs", "navgen")
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, f"{name}.log")
    cmd = ["./srcds_linux64", "-game", view, "-console", "-norestart", "-insecure", "-port", str(port),
           "+sv_lan", "1", "+maxplayers", "2", "+tf_allow_server_hibernation", "0", "+tf_bot_quota", "0",
           "+tf_bot_nav_autogen", "1", "+tf_bot_nav_autogen_quit", "1", "+map", name]
    start = time.time()
    with open(log_path, "w") as log:
        proc = subprocess.Popen(cmd, cwd=args.srcds, env=env, stdin=subprocess.DEVNULL, stdout=log,
                                stderr=subprocess.STDOUT, start_new_session=True)
        timed_out = False
        try:
            proc.wait(timeout=args.timeout_min * 60)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
    verdict = None
    with open(log_path, errors="replace") as f:
        for line in f:
            m = VERDICT_RE.search(line)
            if m and m.group(2) == "generated":
                verdict = m
    result = {"map": name, "seconds": round(time.time() - start), "timed_out": timed_out,
              "nav_saved": os.path.exists(os.path.join(MOD_MAPS, name + ".nav"))}
    if verdict:
        result.update(areas=int(verdict.group(3)), cut_off_pct=int(verdict.group(5)),
                      missing_routes=int(verdict.group(6)), verified=verdict.group(7) == "VERIFIED")
    with lock:
        status = "TIMEOUT" if timed_out else ("VERIFIED" if result.get("verified") else
                                              ("unverified" if verdict else "no verdict"))
        print(f"  {name:<36} {status:<11} {result['seconds']:>5}s", flush=True)
    return result


def cmd_run(args):
    if not os.path.islink(os.path.join(args.views, "mod_tf", "maps")):
        sys.exit("the server view has no maps/ link: re-run tools/tfbot/ds_setup.py")
    args.view = os.path.join(args.views, "mod_tf")
    if args.server_so:
        # snapshot with that binary; maps/ stays a link, so navs land in game/mod_tf/maps
        snap = os.path.join(REPO, "tfbot_runs", "navgen", time.strftime("view-%Y%m%d-%H%M%S"))
        os.makedirs(snap)
        args.view = load_ab().snapshot_view(args.view, snap, args.server_so)
    rows = survey(set(args.maps.split(",")) if args.maps else None)
    todo = [name for name, source in rows if not source or (args.revalidate and source == "generated")]
    what = "maps without a nav" + (" or with a generated one to re-validate" if args.revalidate else "")
    print(f"{len(todo)} {what}; running {args.instances} servers", flush=True)

    lock = threading.Lock()
    ports = [args.base_port + 10 * i for i in range(args.instances)]
    free = list(ports)
    port_lock = threading.Lock()

    def worker(name):
        with port_lock:
            port = free.pop()
        try:
            return generate(args, name, port, lock)
        finally:
            with port_lock:
                free.append(port)

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.instances) as pool:
        results = list(pool.map(worker, todo))

    lines = ["# Generated nav meshes (M5)", "",
             f"{len(results)} maps without a shipped nav. Verified = every objective reachable from both teams' "
             "spawns and under 25% of the mesh cut off from them.", "",
             "| Map | Verdict | Areas | Cut off | Missing routes | Time |", "|---|---|---|---|---|---|"]
    for r in results:
        verdict = "timeout" if r["timed_out"] else ("verified" if r.get("verified") else
                                                  ("**unverified**" if "verified" in r else "no verdict"))
        lines.append(f"| {r['map']} | {verdict} | {r.get('areas', '')} | "
                     f"{str(r['cut_off_pct']) + '%' if 'cut_off_pct' in r else ''} | {r.get('missing_routes', '')} | {r['seconds']}s |")
    ok = sum(1 for r in results if r.get("verified"))
    lines += ["", f"**{ok} of {len(results)} verified.**"]
    text = "\n".join(lines)
    print(text)
    if args.write:
        with open(os.path.abspath(args.write), "w") as f:
            f.write(text + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    ls = sub.add_parser("list")
    ls.add_argument("--maps")
    ls.add_argument("--verbose", action="store_true", help="also list maps that have a nav")
    ls.set_defaults(func=cmd_list)
    run = sub.add_parser("run")
    run.add_argument("--maps", help="comma list (default: every map without a nav)")
    run.add_argument("--instances", type=int, default=3)
    run.add_argument("--timeout-min", type=float, default=25.0)
    run.add_argument("--base-port", type=int, default=27400)
    run.add_argument("--srcds", default=os.path.expanduser("~/srcds/sdk2013"))
    run.add_argument("--views", default=os.path.expanduser("~/srcds/views"))
    run.add_argument("--write", help="write the markdown report here")
    run.add_argument("--server-so", help="use this server.so instead of game/mod_tf/bin's")
    run.add_argument("--revalidate", action="store_true", help="also load maps whose nav we generated, to re-run validation")
    run.set_defaults(func=cmd_run)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
