# Notice

PC5's own code (`title/`, `tools/`, `tests/`, `art/`, `patches/`) is under the GNU LGPL 2.1
or later. Parts from elsewhere, each under its own licence:

| Part | From | Licence | Where |
| --- | --- | --- | --- |
| prospero-win v0.2.0 (644e32e) | <https://github.com/mpereiraesaa/prospero-win> | LGPL 2.1 or later | fetched by `tools/build.sh`, patched by `patches/prospero-win/`; its later commits 2ccffbb, f0c5ddd, 91615eb, 22f0d48, d393828, 404cac6, 959b2d1 and df3e85f are that folder's 0087 to 0094 (their authors kept; its Wine patch 0986 is 0975 there) |
| Wine, RADV/Mesa, Mesa Zink, the PS5 SDK and boilerplate, Lapy | as prospero-win's `THIRD_PARTY.md` and the app's `SOURCES.txt` list them | their own | fetched by `tools/build.sh` at pinned commits; RADV's (PS5_Mesa, MIT) patched by `patches/radv/` |
| DXVK 2.6.2-prospero3 (4228f2e) | <https://github.com/mpereiraesaa/dxvk> | zlib | fetched by `tools/build.sh`, patched by `patches/dxvk/` (marked as changed by PC5) and built for the Windows template |
| PS5 Forwarder Format (22e8762) | <https://github.com/heydemoura/ps5-forwarder-format> | GPL 3.0 or later | `title/third_party/psfwd/` (psfwd, unchanged, built into the eboot); its `template/eboot.bin` and `sce_module/libc.prx`, unchanged, in the app's `pc5/forwarder/` for the home screen tiles |
| FFmpeg 8.1.3 (1041abd) | <https://github.com/FFmpeg/FFmpeg> | LGPL 2.1 or later (only its LGPL parts are built) | fetched by `tools/build.sh`, built into `winedmo.prx` and `winegstreamer.prx` for games' videos and sound |
| ps5-framegen (6436210) | <https://github.com/Swordpdf/ps5-framegen> | GPL 3.0 or later; its shaders (from AMD's FSR3) MIT | fetched by `tools/build.sh`, built into `libvulkan.prx` for Frame generation |
| PS5 MihawkGen (c57232a), by Mihawk | <https://github.com/mihawk-99/PS5_MihawkGen> | GPL 3.0 or later | vendored by prospero-win (345a71b) in `third_party/mihawkgen/` and brought in by `patches/prospero-win/` 0097, unchanged, with its shim as `wine/ps5/pw_mihawkgen.cpp`; built into `libvulkan.prx` for Frame generation's MihawkGen choice |
| VKD3D-Proton 3.0.1 | <https://github.com/HansKristian-Work/vkd3d-proton> | LGPL 2.1 or later | built by `tools/build.sh` from its release's commit with `patches/vkd3d-proton` (Direct3D 12 feature level 12_0 with tiled resources tier 1, and shared resources without Win32 memory export); its source is in `PPSA11018-sources.tar.xz` |
| Wine Mono 11.3.0 | <https://gitlab.winehq.org/mono/wine-mono> (its release on <https://github.com/madewokherd/wine-mono>) | MIT for its own code; Mono's parts LGPL or MIT X11 (some GPL), FNA Ms-PL and MIT, its libraries zlib and MIT, as its `COPYING` says | its release's MSI, unchanged, pinned by the SHA-256 Wine's appwiz.cpl names, installed into the template by `tools/make-prefix.py`; its runtime in the app's `pc5/mono`, its `COPYING` in `LICENSES/wine-mono/`, its source `wine-mono-11.3.0-src.tar.xz`, released beside the app and its sources archive |
| gbe_fork (release-2026_09_27, 7103add) | <https://github.com/Detanup01/gbe_fork> | LGPL 3.0 | its release's `emu-win-release-vs22.7z`, pinned by SHA-256, its regular `steam_api.dll` and `steam_api64.dll` unchanged in the app's `pc5/steam` (PC5 puts one in place of Valve's in a game that has it, `title/pc5_steam.c`); its `LICENSE` in `LICENSES/gbe_fork/`; its source in `PPSA11018-sources.tar.xz` |
| Mbed TLS 3.6.7 | <https://github.com/Mbed-TLS/mbedtls> | Apache 2.0 | its release archive, unchanged, pinned by SHA-256 and built into the eboot with `title/pc5_mbedtls_config.h` |
| Mozilla's root certificates | Debian's `ca-certificates` package (`/usr/share/ca-certificates/mozilla`) on the build machine | MPL 2.0 | the app's `pc5/ca-certificates.crt`, unchanged |
| QR Code generator, C (3c6d0b3) | <https://github.com/nayuki/QR-Code-generator> | MIT | `title/third_party/qrcodegen/` (`qrcodegen.c` and `.h`, unchanged, built into the eboot for Report on GitHub; its notice in `LICENSE` there) |
| stb_image 2.30 and stb_image_write 1.16 (a7d4c21) | <https://github.com/nothings/stb> | public domain or MIT | `title/third_party/stb_image.h` and `stb_image_write.h`, unchanged |
| Liberation 2.1.5 | <https://github.com/liberationfonts/liberation-fonts> | SIL Open Font License 1.1 | its 12 fonts as Ubuntu 24.04's `fonts-liberation` 1:2.1.5-3 ships them (pinned by SHA-256), unchanged, in the app's `win/wine/share/wine/fonts`; its `LICENSE` in `LICENSES/liberation-fonts/` |
| Droid Sans Fallback | <https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-7.1.2_r39/data/fonts/> | Apache 2.0 | `DroidSansFallbackFull.ttf` from Android 7.1.2, pinned by SHA-256, unchanged, in the app's `win/wine/share/wine/fonts`, and the Chinese and Japanese letters PC5's text uses rendered from it into `pc5/fonts-ja.bin`, `fonts-zh-Hans.bin` and `fonts-zh-Hant.bin`; its `NOTICE` in `LICENSES/droid-sans-fallback/` |
| Inter 4.1 | <https://github.com/rsms/inter> | SIL Open Font License 1.1 | `fonts/` (three of its files, unchanged, and `OFL.txt`) |
| Noto Sans KR 2.004 | <https://github.com/notofonts/noto-cjk> (tag Sans2.004, `Sans/SubsetOTF/KR`) | SIL Open Font License 1.1 | its regular and bold OTFs, pinned by SHA-256, read by `tools/make-font.py` at build time: the Korean letters PC5's text uses, rendered into the app's `pc5/fonts-ko.bin`; its `LICENSE` in `LICENSES/noto-sans-kr/` |
| Wine patches of PS5_MihawkOS v3.0.0-alpha-3, by mihawk-99 | <https://github.com/mihawk-99/PS5_MihawkOS> (`wine/patches/`) | LGPL 2.1 or later, as Wine | its patches 0910, 0913, 0914, 0965 and 1170, unchanged but for their context, as prospero-win's Wine patches 0915 and 0917 to 0920 (in `patches/prospero-win/`); patch 0914's free-memory count follows its patch 0912 |
| NFS Black Box's prospero-win patch 0004, by lavavex | <https://github.com/lavavex/nfs-blackbox-ps5> | LGPL 2.1 or later, as prospero-win | `patches/prospero-win/` (its author kept): the audio driver logs streams whose buffer runs short |
| Wine patches of PS5_MihawkOS 5.0.1-alpha-5 (a2ce6eb), by mihawk-99 | <https://github.com/mihawk-99/PS5_MihawkOS> (`wine/patches/`) | LGPL 2.1 or later, as Wine | its patches 0953, 2080 and 2081 (as Wine patches 0953 to 0955, their author kept) and 2082 (as 0945, its author kept, placed in this series' NtReadFile), in `patches/prospero-win/` 0067 and 0068: a 32-bit allocation below 4 GiB, faster file opens and reads. Wine patch 0944 (the instructions UMIP refuses) is PC5's own code, after its patch 2102's finding |
| Wine patches of PS5_MihawkOS 6.0.0-alpha-6 (6c920bf), by mihawk-99 | <https://github.com/mihawk-99/PS5_MihawkOS> (`wine/patches/`) | LGPL 2.1 or later, as Wine | its patches 2085 and 2152, unchanged, as Wine patches 0976 and 0977 (their author kept), in `patches/prospero-win/` 0095; and 0924 and 1188, unchanged, as Wine patches 0946 and 0947, in 0096 (120 Hz output) |
| DXVK patch of PS5_MihawkOS 5.0.1-alpha-5, by mihawk-99 | <https://github.com/mihawk-99/PS5_MihawkOS> (`platform/dxvk/patches/`) | zlib, as DXVK | its patch 0006 as `patches/dxvk/` 0004 (its author kept): a Direct3D 9 reset onto a window that was never hooked doesn't crash |
| Wine patches of Leo Lobato's prospero-win (1bd9163), as Playden uses them | <https://github.com/leolobato/prospero-win> (`wine/patches/`) | LGPL 2.1 or later, as Wine | its patches 0871, 0956, 0958 to 0963, 0971, 0981 and 0984 (0984 placed in this series' driver), and its `pw_wine_pad_changes` in `wine/ps5/pw_wine_sink.c`, in `patches/prospero-win/` 0066; its patch 0993 (3926b27) in 0073 |
| PC5 pull request #78, by SuperLeiyan | <https://github.com/bornaradusin/PC5/pull/78> | LGPL 2.1 or later; its DXVK patch zlib, as DXVK | `patches/prospero-win/` 0036 and 0037 (Wine patch 0491) and `patches/dxvk/` 0003, their author kept: a movie's reads answered at their own offset, the touchpad as the Xbox controller's Back, a shared render target that can be sampled |
| PC5 pull request #171, by SuperLeiyan | <https://github.com/bornaradusin/PC5/pull/171> | LGPL 2.1 or later | `patches/prospero-win/` 0056 to 0059 (their author kept), `title/pc5_tile.c`, `title/pc5_dds.c` and `tools/lapy-title.py`: home screen tiles that are apps of their own |
| FAudio a3b49d6 and 2ead980, by Ethan Lee, as prospero-win 0b0f0e3 by Leo Lobato backports them | <https://github.com/FNA-XNA/FAudio>, <https://github.com/mpereiraesaa/prospero-win> | zlib, as FAudio | `patches/prospero-win/` 0035 (Wine patch 0972, its author kept): FAudio's resampler stays inside its output buffer |

A home screen tile that is an app of its own (`title/pc5_tile.c`) is a few files PC5 copies
from its own folder on the console: its eboot.bin (prospero-win's, LGPL 2.1 or later),
`sce_module/libc.prx` (the PS5 boilerplate's, GPL 3.0 or later) and `lapy.elf` (Lapy's, MIT),
changed only where it names the app's title ID, so that it serves the tile. Their source is
the app's, in PPSA11018-sources.tar.xz.

PS5_MihawkOS's own code (GPL 3.0) is not in PC5: only the Wine and DXVK patches above, which
are under Wine's and DXVK's licences. PS5 MihawkGen, Mihawk's frame generator, is a project of its
own, in `libvulkan.prx` beside ps5-framegen, which is a GPL 3.0 work as a whole already. Nor is Playden's own code (GPL 3.0): only the Wine patches
of the prospero-win it uses, above.

Game covers are not part of PC5. On the console it downloads them, for the games the player
installs, from Steam's store, SteamGridDB (with the player's own key), GOG's catalogue,
PCGamingWiki and (for the Steam game it names) Wikidata, and keeps them with each game.
The sample covers in the tests and screenshots are drawn by `tests/make-sample-covers.py`
for made-up games.
