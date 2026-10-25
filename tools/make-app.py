#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Checks the PC5 app folder, adds its notice, and zips it.

    tools/make-app.py APP_DIR OUT.zip --repo REPO --pw PROSPERO_WIN

The folder is prospero-win's package with this repo's title, identity, art, fonts and empty
Windows. Its PC5.md says what it is and where each part comes from; the package's own
LICENSE, THIRD_PARTY.md, LICENSES/ and SOURCES.txt stay as they are. The zip holds
APP_DIR's name as its top folder, every file with its mode (eboot.bin and the .prx modules
executable).
"""
import argparse
import json
import subprocess
import sys
import zipfile
from pathlib import Path

TITLE = "PPSA11018"
STAMP = (2026, 10, 22, 0, 0, 0)


def git(repo: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True).stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("app", type=Path)
    parser.add_argument("zip", type=Path)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--pw", type=Path, required=True)
    args = parser.parse_args()
    app = args.app

    problems = []
    param = json.loads((app / "sce_sys" / "param.json").read_text())
    if param.get("titleId") != TITLE:
        problems.append(f"sce_sys/param.json is {param.get('titleId')}, not {TITLE}")
    for needed in ("eboot.bin", "lapy.elf", "sce_sys/icon0.png", "sce_sys/pic0.png", "sce_module/libc.prx",
                   "pc5/fonts.bin", "pc5/ca-certificates.crt", "pc5/prefix.list", "pc5/prefix/system.reg",
                   "pc5/prefix-media.reg", "win/wine/lib/wine/x86_64-unix/winedmo.prx",
                   "win/wine/lib/wine/x86_64-unix/winegstreamer.prx", "LICENSES/ps5-framegen/COPYING",
                   "win/wine/lib/wine/x86_64-windows/winegstreamer.dll",
                   "win/wine/lib/wine/i386-windows/winegstreamer.dll",
                   "pc5/prefix/drive_c/windows/system32/d3d12core.dll",
                   "pc5/prefix/drive_c/windows/syswow64/d3d12core.dll",
                   "pc5/prefix/drive_c/windows/system32/d3d11.dll",
                   "win/wine/lib/wine/x86_64-unix/ntdll.prx", "win/wine/lib/wine/x86_64-unix/libvulkan.prx",
                   "win/mesa-zink/x86_64-windows/opengl32.dll", "win/mesa-zink/i386-windows/opengl32.dll",
                   "pc5/forwarder/eboot.bin", "pc5/forwarder/sce_module/libc.prx",
                   "LICENSES/ps5-forwarder-format/LICENSE", "pc5/dxvk.txt", "LICENSES/dxvk/LICENSE",
                   "pc5/mono.list", "pc5/mono-version.txt", "pc5/prefix-dotnet.reg",
                   "pc5/mono/mono-2.0/bin/libmono-2.0-x86_64.dll", "pc5/mono/mono-2.0/bin/libmono-2.0-x86.dll",
                   "LICENSES/wine-mono/COPYING", "pc5/prefix-fonts.reg",
                   "win/wine/share/wine/fonts/LiberationSans-Regular.ttf",
                   "win/wine/share/wine/fonts/LiberationMono-BoldItalic.ttf",
                   "win/wine/share/wine/fonts/DroidSansFallbackFull.ttf",
                   "LICENSES/liberation-fonts/LICENSE", "LICENSES/droid-sans-fallback/NOTICE"):
        if not (app / needed).is_file():
            problems.append(f"no {needed}")
    if (app / "dev.conf").exists():
        problems.append("dev.conf should not be in the app")
    eboot = (app / "eboot.bin").read_bytes() if (app / "eboot.bin").is_file() else b""
    for text in (b"pc5/fonts.bin", b"/mnt/usb0", b"store.steampowered.com", TITLE.encode()):
        if text not in eboot:
            problems.append(f"the eboot doesn't hold {text.decode()} (was PC5 built in?)")
    bundle = app / "pc5" / "ca-certificates.crt"
    roots = bundle.read_text(errors="replace").count("-----BEGIN CERTIFICATE-----") if bundle.is_file() else 0
    if bundle.is_file() and roots < 100:
        problems.append(f"pc5/ca-certificates.crt holds {roots} certificates, not Mozilla's set")
    for name, folder in (("prefix.list", "prefix"), ("mono.list", "mono")):
        listed = (app / "pc5" / name).read_text().split("\n") if (app / "pc5" / name).is_file() else []
        for line in filter(None, listed):
            if not (app / "pc5" / folder / line.rstrip("/")).exists():
                problems.append(f"pc5/{name} names {line}, which isn't there")
                break
    if any(line.startswith("drive_c/windows/mono/") for line in
           ((app / "pc5" / "prefix.list").read_text().split("\n") if (app / "pc5/prefix.list").is_file() else [])):
        problems.append("pc5/prefix.list holds Wine Mono's runtime, which every game would copy")
    if problems:
        sys.exit("make-app: " + "; ".join(problems))

    mono_note = app / "pc5" / "mono-version.txt"
    mono_version = mono_note.read_text().strip() if mono_note.is_file() else "?"
    ca_version = subprocess.run(["dpkg-query", "-W", "-f=${Version}", "ca-certificates"], capture_output=True,
                                text=True).stdout.strip() or "(version not recorded)"
    version = (args.repo / "title" / "pc5.h").read_text().split('#define PC5_VERSION "', 1)[1].split('"', 1)[0]
    repo_commit = git(args.repo, "rev-parse", "HEAD") or "not recorded"
    if git(args.repo, "status", "--porcelain", "--untracked-files=no"):
        repo_commit += " (with uncommitted changes)"
    (app / "PC5.md").write_text(f"""# PC5 ({TITLE}) {version}

A launcher for your own PC games on a PS5 that runs homebrew. Put each game's installed
folder (the one with its .exe) at the top of an exFAT USB drive, then in PC5 press Options
for Settings and choose Install games from USB: one game, or several ticked with Square.
PC5 copies each once to /data/prospero-win, checks the copy, finds its cover online and
adds it to the library. A game's menu (Triangle) sets its controller and Frame generation,
changes its program or uninstalls it with its saves and settings. In a game, the touchpad and Options held for two seconds bring
you back to PC5. No game is part of this app.

The app as a whole is under the GNU GPL 3.0 or later (the PS5 boilerplate's startup code
and libc.prx are); PC5's own code and prospero-win are under the GNU LGPL 2.1 or later.
The source of everything in it is PPSA11018-sources.tar.xz, released with it.

Built from <https://github.com/bornaradusin/PC5> at commit {repo_commit}:

- prospero-win <https://github.com/mpereiraesaa/prospero-win> v0.2.0 (644e32e) with that
  repository's `patches/prospero-win` and `title/` (PC5's own code), as commit
  {git(args.pw, 'rev-parse', 'HEAD')}. SOURCES.txt names the rest: Wine, RADV, Mesa Zink,
  the boilerplate, FreeType, FFmpeg, ps5-framegen (Frame generation, in libvulkan.prx) and
  the Lapy helper, built for {TITLE}.
- `pc5/prefix`: an empty Wine prefix made by prospero-win's `pw_install.py`, with DXVK
  2.6.2-prospero2 (<https://github.com/mpereiraesaa/dxvk>, zlib licence) built with that
  repository's `patches/dxvk` (textures shared within the game's process; `pc5/dxvk.txt`
  names the build) and VKD3D-Proton 3.0.1 (<https://github.com/HansKristian-Work/vkd3d-proton>,
  LGPL 2.1 or later, its release's DLLs unchanged). Each game PC5 installs gets its own
  copy, and a game installed with an earlier DXVK gets this one's DLLs when it next starts.
  `pc5/prefix-media.reg` is what registering Wine's media decoders adds to it, which PC5
  adds to the prefix of a game installed before they were part of the app.
- `pc5/mono`: Wine Mono {mono_version}
  (<https://gitlab.winehq.org/mono/wine-mono>, .NET Framework for Windows programs; its
  own code MIT, its parts under their own licences, `LICENSES/wine-mono/COPYING`),
  installed into the prefix from its MSI and kept here once: PC5 copies it to the console's
  `/data/prospero-win/pc5/` the first time a .NET game starts, and every game's Wine finds it
  there. `pc5/prefix-dotnet.reg` is what installing it adds to the registry, which PC5 adds
  to the prefix of a game installed before it was part of the app. Its source is
  `wine-mono-*-src.tar.xz`, released beside the app.
- `win/wine/share/wine/fonts`: beside Wine's own fonts, Liberation 2.1.5
  (<https://github.com/liberationfonts/liberation-fonts>, SIL Open Font License 1.1,
  `LICENSES/liberation-fonts/LICENSE`) as Ubuntu 24.04's fonts-liberation 1:2.1.5-3 ships
  it, and Droid Sans Fallback from Android 7.1.2 (Apache 2.0,
  `LICENSES/droid-sans-fallback/NOTICE`), unchanged. Games find them as Arial, Times New
  Roman, Courier New and Windows' Chinese, Japanese and Korean fonts; `pc5/prefix-fonts.reg`
  gives them those names, which PC5 adds to the prefix of a game installed before.
- `pc5/fonts.bin`: Inter 4.1 (<https://github.com/rsms/inter>, SIL Open Font License 1.1)
  drawn into an atlas by `tools/make-font.py`.
- `pc5/ca-certificates.crt`: Mozilla's root certificates as Debian's ca-certificates
  {ca_version} ships them (MPL 2.0), which PC5 trusts for the covers' HTTPS along with the
  console's own; they are fetched over TLS with Mbed TLS 3.6.7
  (<https://github.com/Mbed-TLS/mbedtls>, Apache 2.0), built into the eboot.
- PC5 decodes covers with stb_image 2.30 and writes the home screen tiles' pictures with
  stb_image_write 1.16 (<https://github.com/nothings/stb>, public domain or MIT), built
  into the eboot.
- `pc5/forwarder`: the PS5 Forwarder Format's program (`template/eboot.bin` and
  `sce_module/libc.prx`, <https://github.com/heydemoura/ps5-forwarder-format>, GNU GPL 3.0
  or later), unchanged, which PC5 copies into each home screen tile it makes; its library,
  psfwd, is built into the eboot.
- The icon and the Home background are drawn by `art/make-art.py` in that repository.

Covers are looked up on Steam's store, GOG's catalogue and Wikipedia, which need no
account; they are saved with each game on the console.

PC5 is not made by or connected to Sony, Valve, GOG, GitHub or the Wikimedia Foundation.
""")

    files = sorted(p for p in app.rglob("*") if p.is_file())
    part = args.zip.with_suffix(".zip.part")
    with zipfile.ZipFile(part, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in files:
            info = zipfile.ZipInfo(f"{app.name}/{path.relative_to(app).as_posix()}", STAMP)
            info.create_system = 3
            info.external_attr = (0o100000 | (path.stat().st_mode & 0o777)) << 16
            info.compress_type = archive.compression
            with path.open("rb") as source, archive.open(info, "w") as target:
                while chunk := source.read(1 << 20):
                    target.write(chunk)
    part.replace(args.zip)
    print(f"make-app: {args.zip}: {len(files)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
