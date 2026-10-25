#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Builds small Windows programs for the host tests with MinGW (tools/packages.txt).

    tests/make-test-programs.py OUT_DIR

They import what real games import (d3d9, d3d11, d3d12, opengl32, mscoree for .NET), and one carries a
version resource and an icon, so PC5's PE reader is tested on real PE files.
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

PROGRAMS = {
    # name: (compiler, source, libraries, gui, with resources)
    "game64.exe": ("x86_64-w64-mingw32-gcc", "d3d11", ["-ld3d11"], True, True),
    "game32.exe": ("i686-w64-mingw32-gcc", "d3d9", ["-ld3d9"], True, False),
    "launcher.exe": ("x86_64-w64-mingw32-gcc", "plain", [], True, False),
    "engine.dll": ("x86_64-w64-mingw32-gcc", "d3d12", ["-ld3d12", "-ldxgi"], True, False),
    "gl32.exe": ("i686-w64-mingw32-gcc", "opengl", ["-lopengl32"], True, False),
    "tool.exe": ("x86_64-w64-mingw32-gcc", "plain", [], False, False),
    "dotnet.exe": ("x86_64-w64-mingw32-gcc", "dotnet", ["-L.", "-lmscoree"], True, False),
}

SOURCES = {
    "d3d11": "#include <windows.h>\nextern HRESULT WINAPI D3D11CreateDevice();\nint WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d) { return (int)(INT_PTR)&D3D11CreateDevice; }\n",
    "d3d9": "#include <windows.h>\nextern void *WINAPI Direct3DCreate9(UINT);\nint WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d) { return Direct3DCreate9(32) != 0; }\n",
    "d3d12": "#include <windows.h>\nextern HRESULT WINAPI D3D12CreateDevice();\nextern HRESULT WINAPI CreateDXGIFactory1();\n__declspec(dllexport) int engine(void) { return (int)(INT_PTR)&D3D12CreateDevice + (int)(INT_PTR)&CreateDXGIFactory1; }\nBOOL WINAPI DllMain(HINSTANCE a, DWORD b, LPVOID c) { return TRUE; }\n",
    "opengl": "#include <windows.h>\nextern void WINAPI glClear(unsigned);\nint WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d) { glClear(0); return 0; }\n",
    "plain": "#include <windows.h>\nint main(void) { return 0; }\n",
    "dotnet": "#include <windows.h>\nextern int WINAPI _CorExeMain(void);\nint WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d) { return _CorExeMain(); }\n",
}

RC = """1 ICON "app.ico"
1 VERSIONINFO
FILEVERSION 1,2,0,59
PRODUCTVERSION 1,2,0,59
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "FileDescription", "Neon Drift game client"
      VALUE "ProductName", "Neon Drift: Complete Edition"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
"""


def icon(path: Path) -> None:
    big = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    draw = ImageDraw.Draw(big)
    draw.rounded_rectangle((16, 16, 240, 240), 48, fill=(230, 60, 120, 255))
    draw.ellipse((70, 70, 186, 186), fill=(255, 220, 90, 255))
    big.save(path, sizes=[(256, 256), (48, 48), (32, 32)])


def main() -> int:
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    icon(out / "app.ico")
    (out / "app.rc").write_text(RC)
    subprocess.run(["x86_64-w64-mingw32-windres", "app.rc", "-O", "coff", "-o", "app.res"], cwd=out, check=True)
    # MinGW has no mscoree import library: one for _CorExeMain, which a .NET program imports.
    (out / "mscoree.def").write_text("LIBRARY mscoree.dll\nEXPORTS\n_CorExeMain\n")
    subprocess.run(["x86_64-w64-mingw32-dlltool", "-d", "mscoree.def", "-l", "libmscoree.a"], cwd=out, check=True)
    for name, (cc, source, libs, gui, resources) in PROGRAMS.items():
        (out / f"{source}.c").write_text(SOURCES[source])
        args = [cc, "-O1", "-s", f"{source}.c", "-o", name]
        if name.endswith(".dll"):
            args.append("-shared")
        elif gui:
            args.append("-mwindows")
        if resources:
            args.append("app.res")
        subprocess.run(args + libs, cwd=out, check=True)
    print("make-test-programs:", " ".join(PROGRAMS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
