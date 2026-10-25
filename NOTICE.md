# Notice

PC5's own code (`title/`, `tools/`, `tests/`, `art/`, `patches/`) is under the GNU LGPL 2.1
or later. Parts from elsewhere, each under its own licence:

| Part | From | Licence | Where |
| --- | --- | --- | --- |
| prospero-win v0.2.0 (644e32e) | <https://github.com/mpereiraesaa/prospero-win> | LGPL 2.1 or later | fetched by `tools/build.sh`, patched by `patches/prospero-win/` |
| Wine, RADV/Mesa, Mesa Zink, the PS5 SDK and boilerplate, Lapy | as prospero-win's `THIRD_PARTY.md` and the app's `SOURCES.txt` list them | their own | fetched by `tools/build.sh` at pinned commits |
| DXVK 2.6.2-prospero2 (a6f6dcc) | <https://github.com/mpereiraesaa/dxvk> | zlib | fetched by `tools/build.sh`, patched by `patches/dxvk/` (marked as changed by PC5) and built for the Windows template |
| PS5 Forwarder Format (22e8762) | <https://github.com/heydemoura/ps5-forwarder-format> | GPL 3.0 or later | `title/third_party/psfwd/` (psfwd, unchanged, built into the eboot); its `template/eboot.bin` and `sce_module/libc.prx`, unchanged, in the app's `pc5/forwarder/` for the home screen tiles |
| FFmpeg 8.1.3 (1041abd) | <https://github.com/FFmpeg/FFmpeg> | LGPL 2.1 or later (only its LGPL parts are built) | fetched by `tools/build.sh`, built into `winedmo.prx` and `winegstreamer.prx` for games' videos and sound |
| ps5-framegen (6436210) | <https://github.com/Swordpdf/ps5-framegen> | GPL 3.0 or later; its shaders (from AMD's FSR3) MIT | fetched by `tools/build.sh`, built into `libvulkan.prx` for Frame generation |
| VKD3D-Proton 2.14.1 | <https://github.com/HansKristian-Work/vkd3d-proton> | LGPL 2.1 or later | its release's DLLs, unchanged, pinned by SHA-256 |
| Mbed TLS 3.6.7 | <https://github.com/Mbed-TLS/mbedtls> | Apache 2.0 | its release archive, unchanged, pinned by SHA-256 and built into the eboot with `title/pc5_mbedtls_config.h` |
| Mozilla's root certificates | Debian's `ca-certificates` package (`/usr/share/ca-certificates/mozilla`) on the build machine | MPL 2.0 | the app's `pc5/ca-certificates.crt`, unchanged |
| stb_image 2.30 and stb_image_write 1.16 (a7d4c21) | <https://github.com/nothings/stb> | public domain or MIT | `title/third_party/stb_image.h` and `stb_image_write.h`, unchanged |
| Inter 4.1 | <https://github.com/rsms/inter> | SIL Open Font License 1.1 | `fonts/` (three of its files, unchanged, and `OFL.txt`) |
| Wine patches of PS5_MihawkOS v3.0.0-alpha-3, by mihawk-99 | <https://github.com/mihawk-99/PS5_MihawkOS> (`wine/patches/`) | LGPL 2.1 or later, as Wine | its patches 0910, 0913, 0914, 0965 and 1170, unchanged but for their context, as prospero-win's Wine patches 0915 and 0917 to 0920 (in `patches/prospero-win/`); patch 0914's free-memory count follows its patch 0912 |
| NFS Black Box's prospero-win patch 0004, by lavavex | <https://github.com/lavavex/nfs-blackbox-ps5> | LGPL 2.1 or later, as prospero-win | `patches/prospero-win/` (its author kept): the audio driver logs streams whose buffer runs short |

Blur5 (<https://github.com/bornaradusin/Blur5>) and sahin (<https://github.com/7tg/sahin>)
were looked at for ideas only. None of their code is in PC5. PS5_MihawkOS's own code (GPL
3.0) is not in PC5 either: only the Wine patches above, which are under Wine's licence.

Game covers are not part of PC5. On the console it downloads them, for the games the player
installs, from Steam's store, GOG's catalogue and Wikipedia, and keeps them with each game.
The sample covers in the tests and screenshots are drawn by `tests/make-sample-covers.py`
for made-up games.
