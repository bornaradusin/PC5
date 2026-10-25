#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Makes PC5's empty Windows (the prefix each installed game gets a copy of), in the form the
console reads.

    tools/make-prefix.py --pw PROSPERO_WIN --wine HOST_WINE --cpu-dll WOWPROSPERO_DLL \\
                         --vkd3d VKD3D_PROTON_TAR_ZST --dxvk DXVK_DIR --out APP_DIR

1. prospero-win's tools/pw_install.py makes the prefix from profile/template-prefix.yml
   with the host Wine (the console's revision and patches, built for the PC), as the user
   "prospero", with DXVK 2.6.2-prospero2 for 64- and 32-bit programs.
2. VKD3D-Proton's d3d12.dll and d3d12core.dll go beside DXVK (x64 in system32, x86 in
   syswow64): Direct3D 12 on Vulkan. Profiles load them with d3d12,d3d12core=n. DXVK's own
   DLLs are then replaced by the ones tools/build.sh built (DXVK_DIR/x64 and x32): the same
   release with PC5's patches/dxvk.
3. It is written to APP_DIR/pc5/prefix the way prospero-win's tools/pw_prefix.py pushes a
   prefix to the console (its own functions are used for that): links become
   .pw-symlinks tables (a title can't make links), the registry names the console's WoW64
   CPU, wowprospero.dll goes in system32, and the console user's folders are there.
4. APP_DIR/pc5/prefix.list lists every folder (ending in '/') and file: PC5 copies the
   prefix from that list when it installs a game (title/pc5_library.c), so it never has to
   list the app's own folder.
5. APP_DIR/pc5/prefix-media.reg holds what registering winegstreamer.dll (64- and 32-bit)
   adds to system.reg: the sections the prefix has that unregistering it takes away. A game
   installed before winegstreamer was built has none of it, and wineboot cannot run on the
   console, so PC5 appends these sections to that game's system.reg (title/pc5_game.c).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
RECIPE = REPO / "profile" / "template-prefix.yml"
SLUG = "pc5-template"


def vkd3d_files(archive: Path) -> dict:
    """VKD3D-Proton's DLLs from its release archive (.tar.zst), by where they go."""
    unpacked = subprocess.run(["zstd", "-dc", str(archive)], check=True, capture_output=True).stdout
    work = archive.parent / (archive.name + ".unpacked")
    if work.exists():
        shutil.rmtree(work)
    work.mkdir()
    tar_path = work / "vkd3d.tar"
    tar_path.write_bytes(unpacked)
    found = {}
    with tarfile.open(tar_path) as tar:
        for member in tar.getmembers():
            parts = Path(member.name).parts
            if member.isfile() and len(parts) == 3 and parts[1] in ("x64", "x86") and parts[2] in ("d3d12.dll", "d3d12core.dll"):
                where = "drive_c/windows/system32" if parts[1] == "x64" else "drive_c/windows/syswow64"
                target = work / parts[1] / parts[2]
                target.parent.mkdir(exist_ok=True)
                target.write_bytes(tar.extractfile(member).read())
                found[f"{where}/{parts[2]}"] = target
    if len(found) != 4:
        sys.exit(f"make-prefix: {archive} lacks VKD3D-Proton's four DLLs (found {sorted(found)})")
    return found


def registry_sections(text: str) -> dict:
    """system.reg's keys, each with its lines (the key's own time lines left out)."""
    sections, current = {}, None
    for line in text.splitlines():
        if line.startswith("[") and "]" in line:
            current = line[:line.rindex("]") + 1]
            sections[current] = [line]
        elif current is not None and line:
            sections[current].append(line)
    return sections


def media_fragment(wine: Path, made: Path, work: Path, env: dict) -> str:
    """The sections registering winegstreamer.dll adds, from a copy unregistered again."""
    copy = work / "unregistered"
    shutil.copytree(made, copy, symlinks=True)
    unregister = dict(env, WINEPREFIX=str(copy))
    for regsvr32 in ("regsvr32", "C:\\windows\\syswow64\\regsvr32.exe"):
        subprocess.run([str(wine), regsvr32, "/u", "/s", "winegstreamer.dll"], check=True, env=unregister)
    subprocess.run([str(wine.parent / "wineserver"), "-w"], check=True, env=unregister)
    registered = registry_sections((made / "system.reg").read_text(encoding="latin-1"))
    unregistered = registry_sections((copy / "system.reg").read_text(encoding="latin-1"))
    shutil.rmtree(copy)
    def body(lines):
        return [line for line in lines[1:] if not line.startswith("#time=")]
    added = [key for key, lines in registered.items()
             if key not in unregistered or body(lines) != body(unregistered[key])]
    if not any("winegstreamer.dll" in line for key in added for line in registered[key]):
        sys.exit("make-prefix: registering winegstreamer.dll added nothing to system.reg")
    return "".join("\n" + "\n".join(registered[key]) + "\n" for key in added)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--pw", type=Path, required=True, help="the prospero-win checkout")
    parser.add_argument("--wine", type=Path, required=True, help="host Wine (build_host_wine.sh's bin/wine)")
    parser.add_argument("--cpu-dll", type=Path, required=True, help="build_wowprospero.sh's wowprospero.dll")
    parser.add_argument("--vkd3d", type=Path, required=True, help="vkd3d-proton-<version>.tar.zst")
    parser.add_argument("--dxvk", type=Path, required=True, help="DXVK as built, with x64/ and x32/")
    parser.add_argument("--out", type=Path, required=True, help="the app folder being put together")
    args = parser.parse_args()
    for needed in (args.wine, args.cpu_dll, args.vkd3d, args.pw / "tools" / "pw_install.py"):
        if not needed.is_file():
            sys.exit(f"make-prefix: missing {needed}")
    sys.path.insert(0, str(args.pw / "tools"))
    import pw_prefix  # noqa: E402  (prospero-win's own rules for a console prefix)

    work = args.out.parent / ".prefix-work"
    if work.exists():
        shutil.rmtree(work)
    library = work / "library"
    os.makedirs(library)
    env = dict(os.environ, WINEDEBUG="-all", XDG_CACHE_HOME=str(work.parent / ".cache"))
    subprocess.run([sys.executable, str(args.pw / "tools" / "pw_install.py"), str(RECIPE),
                    "--library", str(library), "--wine", str(args.wine)], check=True, env=env)
    made = library / "prefixes" / SLUG
    system32, wow = made / "drive_c" / "windows" / "system32", made / "drive_c" / "windows" / "syswow64"
    if not (made / "system.reg").is_file() or not (wow / "kernel32.dll").is_file():
        sys.exit(f"make-prefix: {made} is not a whole WoW64 prefix")
    for dll in ("d3d9.dll", "d3d11.dll", "dxgi.dll"):
        if not (wow / dll).is_file() or not (system32 / dll).is_file():
            sys.exit(f"make-prefix: DXVK's {dll} is not in both system32 and syswow64")

    media = media_fragment(args.wine, made, work, env)

    files, dirs, links = pw_prefix.local_tree(made)
    files[pw_prefix.CPU_DLL] = args.cpu_dll
    files.update(vkd3d_files(args.vkd3d))
    for bits, folder in (("x64", "system32"), ("x32", "syswow64")):
        for dll in ("d3d8", "d3d9", "d3d10core", "d3d11", "dxgi"):
            key, built = f"drive_c/windows/{folder}/{dll}.dll", args.dxvk / bits / f"{dll}.dll"
            if key not in files or not built.is_file():
                sys.exit(f"make-prefix: no DXVK {dll}.dll to replace in {folder}, or none built at {built}")
            files[key] = built
    user = f"drive_c/users/{pw_prefix.CONSOLE_USER}"
    for folder in pw_prefix.CONSOLE_USER_DIRS:
        path = ""
        for part in f"{user}/{folder}".split("/"):   # the folder and each one above it
            path = f"{path}/{part}" if path else part
            dirs.add(path)
    dirs.add("drive_c/Games")

    target = args.out / "pc5" / "prefix"
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    listed = []
    for folder in sorted(d for d in dirs if d):
        (target / folder).mkdir(parents=True, exist_ok=True)
        listed.append(folder + "/")
    for key in sorted(files):
        data = files[key].read_bytes()
        if key in pw_prefix.REGISTRY:
            data = pw_prefix.to_console(key, data)
        (target / key).parent.mkdir(parents=True, exist_ok=True)
        (target / key).write_bytes(data)
        listed.append(key)
    for folder, table in sorted(links.items()):
        key = f"{folder}/{pw_prefix.LINK_TABLE}" if folder else pw_prefix.LINK_TABLE
        (target / key).write_text("".join(f"{name}\t{link}\n" for name, link in sorted(table.items())))
        listed.append(key)
    if b'@="wowprospero.dll"' not in (target / "system.reg").read_bytes():
        sys.exit("make-prefix: system.reg doesn't name wowprospero.dll as the WoW64 CPU")
    (args.out / "pc5" / "prefix.list").write_text("".join(f"{line}\n" for line in listed))
    (args.out / "pc5" / "prefix-media.reg").write_bytes(media.encode("latin-1"))
    shutil.rmtree(work)
    shutil.rmtree(args.vkd3d.parent / (args.vkd3d.name + ".unpacked"), ignore_errors=True)
    size = sum((target / key).stat().st_size for key in listed if not key.endswith("/"))
    print(f"make-prefix: {target}: {len(listed)} entries, {size >> 20} MB; "
          f"prefix-media.reg: {media.count(chr(10) + '[')} keys")
    return 0


if __name__ == "__main__":
    sys.exit(main())
