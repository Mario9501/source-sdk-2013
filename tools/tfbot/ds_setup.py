#!/usr/bin/env python3
"""Prepare the Source SDK 2013 dedicated server (srcds) to run this mod headless.

The dedicated server can't load game/mod_tf as-is, so this builds a
"server view" of the mod plus a few aliases, without touching the srcds
install or the repo:

  <views>/content/{tf2,sdkbase}  space-free symlinks to the Steam installs
                                 (paths with spaces fail to mount)
  <views>/lib/                   libtier0.so / libvstdlib.so -> the engine's
                                 *_srv.so (so only one copy loads), plus
                                 soundemittersystem.so / scenefilecache.so
                                 aliases and libsteam_api.so
  <views>/mod_tf/                gameinfo.txt with |appid_N| rewritten to
                                 absolute paths (srcds has no SteamApps()),
                                 symlinks to everything in game/mod_tf, and
                                 bin/linux64/server_srv.so -> server.so
  <srcds>/srcds_linux64          copied from the SDK Base client install
                                 (the dedicated server app ships 32-bit only)

Run it again after adding top-level folders to game/mod_tf. Binaries are
symlinked, so rebuilding the mod needs no re-run.

See docs/tfbot/DESIGN.md section 4.
"""
import argparse
import os
import re
import shutil
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
STEAM_ROOT = os.path.expanduser("~/.local/share/Steam")
TF2_DIR_NAME = "Team Fortress 2"
SDKBASE_DIR_NAME = "Source SDK Base 2013 Multiplayer"


def steam_libraries():
    """All steamapps/common folders listed in libraryfolders.vdf."""
    libs = [os.path.join(STEAM_ROOT, "steamapps", "common")]
    vdf = os.path.join(STEAM_ROOT, "steamapps", "libraryfolders.vdf")
    if os.path.exists(vdf):
        with open(vdf, encoding="utf-8", errors="replace") as f:
            for m in re.finditer(r'"path"\s+"([^"]+)"', f.read()):
                libs.append(os.path.join(m.group(1), "steamapps", "common"))
    seen, out = set(), []
    for lib in libs:
        real = os.path.realpath(lib)
        if real not in seen and os.path.isdir(real):
            seen.add(real)
            out.append(lib)
    return out


def find_install(name, override):
    if override:
        return os.path.abspath(override)
    for lib in steam_libraries():
        cand = os.path.join(lib, name)
        if os.path.isdir(cand):
            return cand
    sys.exit(f"could not find '{name}' in any Steam library; pass it explicitly")


def relink(target, link):
    if os.path.islink(link) or os.path.isfile(link):
        os.remove(link)
    elif os.path.isdir(link):
        shutil.rmtree(link)
    os.symlink(target, link)


def rewrite_gameinfo(src_text, mod_dir, appid_dirs):
    """Rewrite SearchPaths entries to absolute, quoted, space-free paths."""
    out, in_paths = [], False
    for line in src_text.splitlines():
        stripped = line.strip()
        if re.match(r"SearchPaths\b", stripped):
            in_paths = True
        elif in_paths and stripped == "}":
            in_paths = False
        elif in_paths and stripped and not stripped.startswith(("//", "{")):
            m = re.match(r"^(\s*)(\S+)\s+(.+)$", line)
            if m:
                indent, key, value = m.group(1), m.group(2), m.group(3).strip().strip('"')
                am = re.match(r"\|appid_(\d+)\|(.*)", value)
                if am:
                    base = appid_dirs.get(am.group(1))
                    if base is None:
                        sys.exit(f"gameinfo.txt mounts appid {am.group(1)}, which ds_setup doesn't know")
                    value = os.path.join(base, am.group(2))
                elif value.startswith("mod_tf/"):
                    value = os.path.join(mod_dir, value[len("mod_tf/"):])
                line = f'{indent}{key}\t"{value}"'
        out.append(line)
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--srcds", default=os.path.expanduser("~/srcds/sdk2013"),
                    help="Source SDK Base 2013 Dedicated Server install (app 244310)")
    ap.add_argument("--views", default=os.path.expanduser("~/srcds/views"),
                    help="where to build the server view, content links and lib aliases")
    ap.add_argument("--tf2", help="Team Fortress 2 install (auto-detected)")
    ap.add_argument("--sdkbase", help="Source SDK Base 2013 Multiplayer install (auto-detected)")
    args = ap.parse_args()

    mod_dir = os.path.join(REPO, "game", "mod_tf")
    srcds = os.path.abspath(args.srcds)
    views = os.path.abspath(args.views)
    tf2 = find_install(TF2_DIR_NAME, args.tf2)
    sdkbase = find_install(SDKBASE_DIR_NAME, args.sdkbase)

    for path, what in ((os.path.join(srcds, "bin", "linux64", "dedicated_srv.so"), "dedicated server libs"),
                       (os.path.join(mod_dir, "bin", "linux64", "server.so"), "built server.so (run src/buildallprojects)")):
        if not os.path.exists(path):
            sys.exit(f"missing {what}: {path}")

    # 64-bit launcher.
    launcher = os.path.join(srcds, "srcds_linux64")
    if not os.path.exists(launcher):
        shutil.copy2(os.path.join(sdkbase, "srcds_linux64"), launcher)
        print(f"copied srcds_linux64 into {srcds}")

    # Space-free content links.
    content = os.path.join(views, "content")
    os.makedirs(content, exist_ok=True)
    relink(tf2, os.path.join(content, "tf2"))
    relink(sdkbase, os.path.join(content, "sdkbase"))

    # Shared-library aliases: one copy of tier0/vstdlib (the engine's _srv),
    # and the client-style module names our server.so asks for.
    lib = os.path.join(views, "lib")
    os.makedirs(lib, exist_ok=True)
    srv = os.path.join(srcds, "bin", "linux64")
    for alias, target in (("libtier0.so", os.path.join(srv, "libtier0_srv.so")),
                          ("libvstdlib.so", os.path.join(srv, "libvstdlib_srv.so")),
                          ("soundemittersystem.so", os.path.join(srv, "soundemittersystem_srv.so")),
                          ("scenefilecache.so", os.path.join(srv, "scenefilecache_srv.so")),
                          ("libsteam_api.so", os.path.join(REPO, "game", "bin", "linux64", "libsteam_api.so"))):
        if not os.path.exists(target):
            sys.exit(f"missing library for alias {alias}: {target}")
        relink(target, os.path.join(lib, alias))

    # Server view of the mod.
    view = os.path.join(views, "mod_tf")
    os.makedirs(view, exist_ok=True)
    for entry in os.listdir(mod_dir):
        if entry in ("gameinfo.txt", "bin"):
            continue
        relink(os.path.join(mod_dir, entry), os.path.join(view, entry))

    bin_src = os.path.join(mod_dir, "bin", "linux64")
    bin_view = os.path.join(view, "bin", "linux64")
    if os.path.islink(os.path.join(view, "bin")):
        os.remove(os.path.join(view, "bin"))
    os.makedirs(bin_view, exist_ok=True)
    for f in os.listdir(bin_src):
        relink(os.path.join(bin_src, f), os.path.join(bin_view, f))
    relink(os.path.join(bin_src, "server.so"), os.path.join(bin_view, "server_srv.so"))

    with open(os.path.join(mod_dir, "gameinfo.txt"), encoding="utf-8") as f:
        text = rewrite_gameinfo(f.read(), mod_dir, {
            "440": os.path.join(content, "tf2"),
            "243750": os.path.join(content, "sdkbase"),
        })
    with open(os.path.join(view, "gameinfo.txt"), "w", encoding="utf-8") as f:
        f.write(text)

    print(f"server view ready: {view}")
    print("test it with:")
    print(f"  cd {srcds} && LD_LIBRARY_PATH={srv}:{lib} ./srcds_linux64 -game {view} "
          "-console -insecure +sv_lan 1 +maxplayers 24 +tf_allow_server_hibernation 0 "
          "+tf_bot_join_after_player 0 +tf_bot_quota 12 +map ctf_2fort")


if __name__ == "__main__":
    main()
