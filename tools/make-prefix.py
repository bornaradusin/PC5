#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Makes PC5's empty Windows (the prefix each installed game gets a copy of), in the form the
console reads.

    tools/make-prefix.py --pw PROSPERO_WIN --wine HOST_WINE --cpu-dll WOWPROSPERO_DLL \\
                         --vkd3d VKD3D_PROTON_TAR_ZST --dxvk DXVK_DIR \\
                         --mono WINE_MONO_MSI --mono-version VERSION --out APP_DIR

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
6. Wine Mono (.NET Framework) is installed into the prefix from its MSI with the host Wine.
   Its runtime (C:\\windows\\mono, some 230 MB) is not in the template, which every game
   copies: it goes to APP_DIR/pc5/mono, listed in APP_DIR/pc5/mono.list, and PC5 copies it
   once to the console's /data, where every game's Wine finds it (HKCU\\Software\\Wine\\Mono
   RuntimePath, which PC5 sets: title/pc5_game.c). The installer's own copy of the MSI is
   left out. APP_DIR/pc5/prefix-dotnet.reg holds what installing it added to system.reg,
   for games installed before (as prefix-media.reg), and APP_DIR/pc5/mono-version.txt names
   the version.
7. Fonts: tools/build.sh puts Liberation and Droid Sans Fallback in Wine's own fonts folder,
   which Wine loads for every game. HKCU\\Software\\Wine\\Fonts\\Replacements gives them the
   names games ask for (FONT_REPLACEMENTS: Arial, Times New Roman, Courier New, and the
   Chinese, Japanese and Korean fonts of Windows); Wine already falls back to MS UI Gothic,
   SimSun, Gulim and PMingLiU for characters a font lacks, which are then Droid Sans
   Fallback. APP_DIR/pc5/prefix-fonts.reg holds what that adds to user.reg, which PC5 adds
   to the prefix of a game installed before (title/pc5_game.c).
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

# Windows' fonts, by the names games ask for, and the font in Wine's fonts folder that takes
# each one's place. Wine leaves out a replacement whose font is there itself.
CJK = "Droid Sans Fallback"
FONT_REPLACEMENTS = {
    "Arial": "Liberation Sans", "Helvetica": "Liberation Sans",
    "Times New Roman": "Liberation Serif", "Times": "Liberation Serif",
    "Courier New": "Liberation Mono",
    # Japanese, in English and as Japanese Windows names them
    "MS Gothic": CJK, "MS PGothic": CJK, "MS UI Gothic": CJK, "MS Mincho": CJK, "MS PMincho": CJK,
    "Meiryo": CJK, "Meiryo UI": CJK, "Yu Gothic": CJK, "Yu Gothic UI": CJK, "Yu Mincho": CJK,
    "ＭＳ ゴシック": CJK, "ＭＳ Ｐゴシック": CJK, "ＭＳ 明朝": CJK, "ＭＳ Ｐ明朝": CJK, "メイリオ": CJK, "游ゴシック": CJK,
    # Chinese, simplified and traditional
    "SimSun": CJK, "NSimSun": CJK, "SimHei": CJK, "Microsoft YaHei": CJK, "Microsoft YaHei UI": CJK,
    "PMingLiU": CJK, "MingLiU": CJK, "Microsoft JhengHei": CJK, "Microsoft JhengHei UI": CJK,
    "宋体": CJK, "新宋体": CJK, "黑体": CJK, "微软雅黑": CJK, "新細明體": CJK, "細明體": CJK, "微軟正黑體": CJK,
    # Korean
    "Gulim": CJK, "GulimChe": CJK, "Dotum": CJK, "DotumChe": CJK, "Batang": CJK, "BatangChe": CJK,
    "Gungsuh": CJK, "Malgun Gothic": CJK,
    "굴림": CJK, "굴림체": CJK, "돋움": CJK, "돋움체": CJK, "바탕": CJK, "바탕체": CJK, "궁서": CJK, "맑은 고딕": CJK,
}


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


def install_mono(wine: Path, msi: Path, made: Path, env: dict) -> str:
    """Wine Mono installed into made; the sections it added to system.reg."""
    before = registry_sections((made / "system.reg").read_text(encoding="latin-1"))
    installer = made / "drive_c" / "windows" / "Installer"
    cached = set(installer.iterdir()) if installer.is_dir() else set()
    into = dict(env, WINEPREFIX=str(made))
    subprocess.run([str(wine), "msiexec", "/i", str(msi), "/qn"], check=True, env=into)
    subprocess.run([str(wine.parent / "wineserver"), "-w"], check=True, env=into)
    after = registry_sections((made / "system.reg").read_text(encoding="latin-1"))
    def body(lines):
        return [line for line in lines[1:] if not line.startswith("#time=")]
    added = [key for key, lines in after.items() if key not in before or body(lines) != body(before[key])]
    if not any(key.startswith("[Software\\\\Microsoft\\\\NET Framework Setup\\\\NDP\\\\v4\\\\Full]") for key in added):
        sys.exit("make-prefix: installing Wine Mono registered no .NET Framework 4")
    if not (made / "drive_c" / "windows" / "mono" / "mono-2.0").is_dir():
        sys.exit("make-prefix: Wine Mono's runtime is not in C:\\windows\\mono\\mono-2.0")
    for package in set(installer.iterdir()) - cached if installer.is_dir() else ():
        package.unlink()   # the installer's copy of the MSI (80 MB), which nothing runs again
    return "".join("\n" + "\n".join(after[key]) + "\n" for key in added)


def name_fonts(wine: Path, made: Path, work: Path, env: dict) -> str:
    """FONT_REPLACEMENTS imported into made; the section that added to user.reg."""
    def quoted(text: str) -> str:
        return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'
    lines = ["Windows Registry Editor Version 5.00", "", "[HKEY_CURRENT_USER\\Software\\Wine\\Fonts\\Replacements]"]
    lines += [f"{quoted(name)}={quoted(font)}" for name, font in FONT_REPLACEMENTS.items()]
    reg = work / "fonts.reg"
    reg.write_bytes(b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le"))
    into = dict(env, WINEPREFIX=str(made))
    subprocess.run([str(wine), "reg", "import", str(reg)], check=True, env=into)
    subprocess.run([str(wine.parent / "wineserver"), "-w"], check=True, env=into)
    key = "[Software\\\\Wine\\\\Fonts\\\\Replacements]"
    section = registry_sections((made / "user.reg").read_text(encoding="latin-1")).get(key)
    values = [line for line in section or [] if line.startswith('"')]
    if len(values) != len(FONT_REPLACEMENTS) or '"Arial"="Liberation Sans"' not in values:
        sys.exit(f"make-prefix: user.reg has {len(values)} of the {len(FONT_REPLACEMENTS)} font replacements")
    return "\n" + "\n".join(section) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--pw", type=Path, required=True, help="the prospero-win checkout")
    parser.add_argument("--wine", type=Path, required=True, help="host Wine (build_host_wine.sh's bin/wine)")
    parser.add_argument("--cpu-dll", type=Path, required=True, help="build_wowprospero.sh's wowprospero.dll")
    parser.add_argument("--vkd3d", type=Path, required=True, help="vkd3d-proton-<version>.tar.zst")
    parser.add_argument("--dxvk", type=Path, required=True, help="DXVK as built, with x64/ and x32/")
    parser.add_argument("--mono", type=Path, required=True, help="wine-mono-<version>-x86.msi")
    parser.add_argument("--mono-version", required=True, help="its version, as Wine names it")
    parser.add_argument("--out", type=Path, required=True, help="the app folder being put together")
    args = parser.parse_args()
    for needed in (args.wine, args.cpu_dll, args.vkd3d, args.mono, args.pw / "tools" / "pw_install.py"):
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
    dotnet = install_mono(args.wine, args.mono, made, env)
    fonts = name_fonts(args.wine, made, work, env)

    files, dirs, links = pw_prefix.local_tree(made)
    # Wine Mono's runtime goes beside the template, not in it (6. above).
    runtime = "drive_c/windows/mono/"
    mono_files = {key[len(runtime):]: path for key, path in files.items() if key.startswith(runtime)}
    mono_dirs = {d[len(runtime):] for d in dirs if d.startswith(runtime) and d != runtime.rstrip("/")}
    files = {key: path for key, path in files.items() if not key.startswith(runtime)}
    dirs = {d for d in dirs if not d.startswith(runtime) and d != runtime.rstrip("/")}
    if any(folder.startswith(runtime) or folder == runtime.rstrip("/") for folder in links):
        sys.exit("make-prefix: Wine Mono's runtime has links, which its copy can't keep")
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
    (args.out / "pc5" / "prefix-dotnet.reg").write_bytes(dotnet.encode("latin-1"))
    (args.out / "pc5" / "prefix-fonts.reg").write_bytes(fonts.encode("latin-1"))
    mono =args.out / "pc5" / "mono"
    if mono.exists():
        shutil.rmtree(mono)
    mono_listed = []
    for folder in sorted(d for d in mono_dirs if d):
        (mono / folder).mkdir(parents=True, exist_ok=True)
        mono_listed.append(folder + "/")
    for key in sorted(mono_files):
        (mono / key).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(mono_files[key], mono / key)
        mono_listed.append(key)
    (args.out / "pc5" / "mono.list").write_text("".join(f"{line}\n" for line in mono_listed))
    (args.out / "pc5" / "mono-version.txt").write_text(f"{args.mono_version}\n")
    shutil.rmtree(work)
    shutil.rmtree(args.vkd3d.parent / (args.vkd3d.name + ".unpacked"), ignore_errors=True)
    size = sum((target / key).stat().st_size for key in listed if not key.endswith("/"))
    mono_size = sum((mono / key).stat().st_size for key in mono_listed if not key.endswith("/"))
    print(f"make-prefix: {target}: {len(listed)} entries, {size >> 20} MB; "
          f"prefix-media.reg: {media.count(chr(10) + '[')} keys; prefix-dotnet.reg: {dotnet.count(chr(10) + '[')} keys; "
          f"prefix-fonts.reg: {len(FONT_REPLACEMENTS)} font names; "
          f"Wine Mono {args.mono_version}: {len(mono_listed)} entries, {mono_size >> 20} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
