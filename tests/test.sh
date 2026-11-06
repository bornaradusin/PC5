#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Host checks, no console or PS5 SDK needed. Run tools/build.sh sources first (it fetches
# the pinned prospero-win the checks build against).
#
#   tests/test.sh [--shots]      --shots: also draws every screen into .build/tests/shots/
#                                (PC5_SHOTS_LANG=de, ja, ...: in that language, tests/shots.c)
#
# PC5_BUILD_DIR names the build's folder when it isn't .build/, as for tools/build.sh.
set -euo pipefail
here=$(realpath -- "$(dirname -- "$0")/..")
B=$(realpath -m -- "${PC5_BUILD_DIR:-$here/.build}")
PW=$B/prospero-win
out=$B/tests
[[ -f $PW/native/pc5_ui.c ]] || { echo "run tools/build.sh sources first" >&2; exit 2; }
rm -rf "$out" && mkdir -p "$out/work"
cd "$here"

# sce_sys/param.json's contentVersion is PC5's version (2.2.6 as 02.002.006): the console and the
# store see an update by it, and 2.2.5 was published with 2.2.4's.
version=$(sed -n 's/^#define PC5_VERSION "\(.*\)"/\1/p' title/pc5.h)
content=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["contentVersion"])' sce_sys/param.json)
IFS=. read -r major minor patch <<<"${version%%-*}"   # a test version's suffix: 2.3.1-programs-test is 2.3.1
[[ $content == "$(printf '%02d.%03d.%03d' "$major" "$minor" "$patch")" ]] ||
    { echo "sce_sys/param.json's contentVersion $content isn't PC5 $version's" >&2; exit 1; }

# What the tests read: Windows programs (MinGW), made-up covers, the font atlas.
python3 tests/make-test-programs.py "$out/programs" >/dev/null
python3 tests/make-sample-covers.py "$out/covers" >/dev/null
# The translations' template is the sources' and every lang/*.po is merged with it; the font
# atlas has their letters, and Chinese, Japanese and Korean their own files where the build
# fetched their fonts (tools/build.sh sources); the .po files beside it, as the app has them.
python3 tools/update-po.py --check
# The issue Actions' scripts (.github/scripts).
python3 tests/test_github_scripts.py >/dev/null
cjk=()
[[ -f $B/downloads/DroidSansFallbackFull.ttf ]] && cjk+=(--cjk "$B/downloads/DroidSansFallbackFull.ttf")
[[ -f $B/downloads/NotoSansKR-Regular.otf && -f $B/downloads/NotoSansKR-Bold.otf ]] &&
    cjk+=(--korean "$B/downloads/NotoSansKR-Regular.otf" "$B/downloads/NotoSansKR-Bold.otf")
python3 tools/make-font.py "$out/fonts.bin" --lang lang "${cjk[@]}" >/dev/null
mkdir -p "$out/lang" && cp lang/*.po "$out/lang/"
# PC5's own pictures beside the fonts, as the app has them (pc5/ui/)
mkdir -p "$out/ui" && cp art/ui/* "$out/ui/"
upstream=("$PW/src/pw_game_profile.c" "$PW/src/pw_app_profile.c" "$PW/src/pw_profile_catalog.c"
          "$PW/native/pw_wine_library.c" "$PW/src/pw_result.c")
common=(-std=c11 -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE -Ititle -I"$PW/src" -I"$PW/native" -I"$PW/include"
        -I"$PW/wine/ps5" -DFRIBIDI_LIB_STATIC -Ithird_party/fribidi-1.0.18/lib -Ithird_party/fribidi-1.0.18/pc5-config)
bash tools/build-fribidi.sh "$out/fribidi" cc ar
# All programs that draw text use the exact same joining and bidi implementation.
text_sources=(title/pc5_bidi.c "$out/fribidi/libfribidi-pc5.a")
cc "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DPC5_HEAP_ONLY \
    tests/test_bidi.c title/pc5_base.c "${text_sources[@]}" -lm -lpthread -o "$out/test_bidi"

cc "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    tests/test_ui_rtl.c title/pc5_base.c title/pc5_draw.c title/pc5_image.c title/pc5_stb.c "${text_sources[@]}" -lm -lpthread -o "$out/test_ui_rtl"
"$out/test_ui_rtl" "$out/fonts.bin"
"$out/test_bidi"

# PC5's code, with every buffer on the heap so the sanitizers see it, against
# prospero-win's own profile parser and library loader.
cc "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DPC5_HEAP_ONLY \
    tests/test_pc5.c title/pc5_base.c title/pc5_controls.c title/pc5_cover.c title/pc5_draw.c title/pc5_game.c title/pc5_image.c title/pc5_json.c \
    title/pc5_library.c title/pc5_logs.c title/pc5_matrix.c title/pc5_pe.c title/pc5_settings.c title/pc5_source.c \
    title/pc5_stb.c title/pc5_ui.c title/pc5_update.c title/pc5_home.c title/pc5_psfwd.c title/pc5_zip.c title/pc5_qr.c title/pc5_report.c \
    title/pc5_upgrade.c title/pc5_sound.c title/pc5_video.c title/pc5_appearance.c title/pc5_compat.c title/pc5_look.c title/pc5_backgrounds.c title/pc5_tile.c title/pc5_dds.c title/pc5_files.c title/pc5_textdoc.c title/pc5_ftp.c title/pc5_web.c title/pc5_cab.c title/pc5_msi.c title/pc5_steam.c title/pc5_identity.c title/pc5_i18n.c title/pc5_child_wire.c tests/pc5_ftp_host.c "${upstream[@]}" "${text_sources[@]}" -lm -lpthread \
    -o "$out/test_pc5"
"$out/test_pc5" "$out/work" tests/fixtures "$out/programs" "$out/covers" "$out/fonts.bin"

# The INSTALL folder's FTP server as a client sees it: Python's ftplib against a server of its own.
cc "${common[@]}" -O2 -DPC5_FTP_SERVE tests/pc5_ftp_host.c title/pc5_ftp.c title/pc5_base.c -lm -lpthread -o "$out/ftp-serve"
timeout 120 python3 -I tests/test_ftp.py "$out/ftp-serve" "$out"
# PC5's page for a browser as one sees it: Python's urllib against a server of its own, on a
# copy of the games test_pc5 installed; the page in C made from its .html as it is now.
python3 tools/make-web-page.py "$out/pc5_web_page.h"
cmp -s "$out/pc5_web_page.h" title/pc5_web_page.h || { echo "title/pc5_web_page.h is older than its .html: run tools/make-web-page.py" >&2; exit 1; }
python3 tools/make-loading-art.py "$out/pc5_loading_art.h"
cmp -s "$out/pc5_loading_art.h" title/pc5_loading_art.h || { echo "title/pc5_loading_art.h is older than art/loading.jpg: run tools/make-loading-art.py" >&2; exit 1; }
cc "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DPC5_WEB_SERVE tests/pc5_ftp_host.c \
    title/pc5_web.c title/pc5_ftp.c title/pc5_base.c title/pc5_controls.c title/pc5_cover.c title/pc5_draw.c title/pc5_game.c \
    title/pc5_image.c title/pc5_json.c title/pc5_library.c title/pc5_logs.c title/pc5_matrix.c title/pc5_pe.c title/pc5_settings.c \
    title/pc5_source.c title/pc5_stb.c title/pc5_update.c title/pc5_home.c title/pc5_psfwd.c title/pc5_zip.c title/pc5_qr.c \
    title/pc5_report.c title/pc5_upgrade.c title/pc5_sound.c title/pc5_video.c title/pc5_appearance.c title/pc5_compat.c title/pc5_look.c title/pc5_backgrounds.c title/pc5_tile.c title/pc5_dds.c \
    title/pc5_files.c title/pc5_textdoc.c title/pc5_cab.c title/pc5_msi.c title/pc5_steam.c title/pc5_identity.c title/pc5_i18n.c "${upstream[@]}" "${text_sources[@]}" -lm -lpthread -o "$out/web-serve"
timeout 300 python3 -I tests/test_web.py "$out/web-serve" "$out" "$out/work/data/prospero-win"
python3 tests/test_arabic_web.py "$out/web-serve" "$out" "$out/work/data/prospero-win" "$out/arabic-web"
# The zip PC5 wrote (pc5_zip.c) opens as any zip does, with what was put in it.
python3 -c "import sys, zipfile
z = zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None
assert z.read('text.log') == bytes(b'PC5 log line, repeated.\\n'[i % 24] for i in range(40000))
assert z.read('game/users/prospero/noise.dmp') == bytes((((i * 2654435761) & 0xffffffff) >> 13) & 255 for i in range(3000))
assert z.read('empty.txt') == b'' and z.read('k\\u00e4ytt\\u00e4j\\u00e4.txt') == b'x'
for kept in sys.argv[2:]: assert zipfile.ZipFile(kept).testzip() is None" "$out/work/zip-test.zip" "$out"/work/logs-root/logs/*.zip

# The HTTPS client, with mbedTLS built for this computer, against a local server: a
# certificate from a test CA (trusted), one from nobody (refused), redirects, chunks.
mbedtls=$(ls -d "$B"/mbedtls-*/ | head -1)
config='-DMBEDTLS_USER_CONFIG_FILE="pc5_mbedtls_config.h"'
mkdir -p "$out/mbedtls" "$out/tls"
for source in "$mbedtls"/library/*.c; do
    cc -std=c99 -O1 -I"$mbedtls/include" -I"$mbedtls/library" -Ititle "$config" -c "$source" \
        -o "$out/mbedtls/$(basename "$source" .c).o"
done
ar rcs "$out/libmbedtls-pc5.a" "$out"/mbedtls/*.o
(
    cd "$out/tls"
    openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout ca.key -out ca.pem -days 2 \
        -subj "/CN=PC5 Test CA" -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" 2>/dev/null
    openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout server.key -out server.csr \
        -subj "/CN=localhost" 2>/dev/null
    printf "subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n" > ext.cnf
    openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial -out server.pem -days 2 \
        -extfile ext.cnf 2>/dev/null
    openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout spare.key -out spare.pem -days 2 \
        -subj "/CN=PC5 Spare CA" -addext "basicConstraints=critical,CA:TRUE" 2>/dev/null
    openssl req -x509 -newkey rsa:2048 -nodes -keyout other.key -out other.pem -days 2 -subj "/CN=localhost" \
        -addext "subjectAltName=DNS:localhost" 2>/dev/null
)
cc "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DPC5_HEAP_ONLY -I"$mbedtls/include" \
    "$config" tests/test_net.c title/pc5_net.c title/pc5_base.c "$out/libmbedtls-pc5.a" -o "$out/test_net"
python3 tests/https_server.py 18443 "$out/tls/server.pem" "$out/tls/server.key" "$out/tls/ready1" 2>/dev/null &
good=$!
python3 tests/https_server.py 18444 "$out/tls/other.pem" "$out/tls/other.key" "$out/tls/ready2" 2>/dev/null &
other=$!
trap 'kill $good $other 2>/dev/null' EXIT
for _ in $(seq 100); do [[ -f $out/tls/ready1 && -f $out/tls/ready2 ]] && break; sleep 0.1; done
"$out/test_net" "$out/tls/spare.pem" "$out/tls/ca.pem" 18443 18444 2>/dev/null

# Wine patch 0935: the MOVs through %fs: that Wine's handler does when the kernel didn't apply
# an FS base repair, taken from Wine's signal_x86_64.c with prospero-win's patches applied
# (tools/build.sh sources fetches Wine; without it the check is left out, and says so).
wine=$PW/.deps/wine/source
if [[ -f $wine/dlls/ntdll/unix/signal_x86_64.c ]]; then
    mkdir -p "$out/wine-fs/dlls/ntdll/unix"
    cp "$wine/dlls/ntdll/unix/signal_x86_64.c" "$wine/dlls/ntdll/unix/unix_private.h" "$out/wine-fs/dlls/ntdll/unix/"
    for patch in "$PW"/wine/patches/*.patch; do
        grep -q '^+++ b/dlls/ntdll/unix/\(signal_x86_64\.c\|unix_private\.h\)' "$patch" || continue
        # Not a repository's: inside one (a build folder in the PC5 repo), git apply would skip
        # every file outside the folder's path in it, quietly.
        GIT_CEILING_DIRECTORIES=$out git -C "$out/wine-fs" apply --include=dlls/ntdll/unix/signal_x86_64.c \
            --include=dlls/ntdll/unix/unix_private.h "$patch"
    done
    awk '/^static BOOL pw_has_modrm/{p=1} /^static BOOL pw_repair_guest_fs/{p=0} p' \
        "$out/wine-fs/dlls/ntdll/unix/signal_x86_64.c" > "$out/wine-fs/fs_funcs.c"
    grep -q pw_emulate_fs_mov "$out/wine-fs/fs_funcs.c" || { echo "test_wine_fs: Wine patch 0935 isn't applied" >&2; exit 1; }
    cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -I"$out/wine-fs" tests/test_wine_fs.c -o "$out/test_wine_fs"
    "$out/test_wine_fs"
else
    echo "test_wine_fs: left out, no Wine source at $wine (tools/build.sh sources fetches it)"
fi

# Wine patches 0985 and 0986: a name missing from a game's system32 or syswow64 is Wine's own
# file, and an open that changes it copies it in first, taken from Wine's file.c with
# prospero-win's patches applied (as for 0935 above).
if [[ -f $wine/dlls/ntdll/unix/file.c ]]; then
    mkdir -p "$out/wine-system/dlls/ntdll/unix"
    cp "$wine/dlls/ntdll/unix/file.c" "$out/wine-system/dlls/ntdll/unix/"
    for patch in "$PW"/wine/patches/*.patch; do
        grep -q '^+++ b/dlls/ntdll/unix/file\.c' "$patch" || continue
        GIT_CEILING_DIRECTORIES=$out git -C "$out/wine-system" apply --include=dlls/ntdll/unix/file.c "$patch"
    done
    for func in "static void ps5_log_missing" "static NTSTATUS ps5_wine_system_file" "static BOOL ps5_is_wine_own" "static BOOL ps5_open_changes" \
                "static NTSTATUS ps5_copy_wine_own"; do
        awk -v head="^$func[(]" '$0 ~ head {p=1} p{print} p&&/^}/{exit}' "$out/wine-system/dlls/ntdll/unix/file.c"
    done > "$out/wine-system/system_func.c"
    grep -q ps5_copy_wine_own "$out/wine-system/system_func.c" ||
        { echo "test_wine_system: Wine patch 0986 isn't applied" >&2; exit 1; }
    grep -q ps5_wine_system_file "$out/wine-system/system_func.c" ||
        { echo "test_wine_system: Wine patch 0985 isn't applied" >&2; exit 1; }
    cc -std=c11 -Wall -Wextra -Werror -I"$out/wine-system" tests/test_wine_system.c -o "$out/test_wine_system"
    "$out/test_wine_system"
else
    echo "test_wine_system: left out, no Wine source at $wine (tools/build.sh sources fetches it)"
fi

# Wine patches 0934 and 0987: NSI answers without nsiproxy.sys, with the loopback interface and
# an unplugged wired adapter, taken from Wine's nsi.c with prospero-win's patches applied.
if [[ -f $wine/dlls/nsi/nsi.c ]]; then
    mkdir -p "$out/wine-nsi/dlls/nsi"
    cp "$wine/dlls/nsi/nsi.c" "$out/wine-nsi/dlls/nsi/"
    for patch in "$PW"/wine/patches/*.patch; do
        grep -q '^+++ b/dlls/nsi/nsi\.c' "$patch" || continue
        GIT_CEILING_DIRECTORIES=$out git -C "$out/wine-nsi" apply --include=dlls/nsi/nsi.c "$patch"
    done
    awk '/^#define NSI_FALLBACK_MS_MODULEID/{p=1} /end of the PS5 fallback/{exit} p' \
        "$out/wine-nsi/dlls/nsi/nsi.c" > "$out/wine-nsi/nsi_fallback.c"
    grep -q ETHERNET_IF_INDEX "$out/wine-nsi/nsi_fallback.c" ||
        { echo "test_wine_nsi: Wine patch 0987 isn't applied" >&2; exit 1; }
    cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
        -Wno-sign-compare -fshort-wchar -D__WINESRC__ -I"$out/wine-nsi" -I"$wine/include" \
        tests/test_wine_nsi.c -o "$out/test_wine_nsi"
    "$out/test_wine_nsi"
else
    echo "test_wine_nsi: left out, no Wine source at $wine (tools/build.sh sources fetches it)"
fi

# Wine patches 0911 and 0979: the windows that take the controller's dialog keys (a dialog box,
# and Spider-Man's launch window), taken from Wine's ps5drv.c with prospero-win's patches applied
# (the driver is prospero-win's own, so no Wine source is needed).
mkdir -p "$out/wine-dialog/dlls/win32u"
for patch in "$PW"/wine/patches/*.patch; do
    grep -q '^+++ b/dlls/win32u/ps5drv\.c' "$patch" || continue
    GIT_CEILING_DIRECTORIES=$out git -C "$out/wine-dialog" apply --include=dlls/win32u/ps5drv.c "$patch"
done
awk '/^static BOOL is_dialog\(/ {p=1} p {print} p && /^}/ {exit}' \
    "$out/wine-dialog/dlls/win32u/ps5drv.c" > "$out/wine-dialog/dialog_func.c"
grep -q GameNxApp "$out/wine-dialog/dialog_func.c" || { echo "test_wine_dialog: Wine patch 0979 isn't applied" >&2; exit 1; }
cc -std=c11 -Wall -Wextra -Werror -I"$out/wine-dialog" tests/test_wine_dialog.c -o "$out/test_wine_dialog"
"$out/test_wine_dialog"

# patches/radv 0003 and 0005: which window's picture has the console's display, with the
# swapchains games' logs showed (tests/test_videoout.c); the rules' header from the patch.
mkdir -p "$out/videoout"
for patch in patches/radv/*.patch; do
    grep -q '^+++ b/src/vulkan/wsi/wsi_common_videoout_takeover\.h' "$patch" || continue
    GIT_CEILING_DIRECTORIES=$out git -C "$out/videoout" apply --include=src/vulkan/wsi/wsi_common_videoout_takeover.h \
        "$(realpath -- "$patch")"
done
[[ -f $out/videoout/src/vulkan/wsi/wsi_common_videoout_takeover.h ]] ||
    { echo "test_videoout: no patches/radv patch adds wsi_common_videoout_takeover.h" >&2; exit 1; }
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -I"$out/videoout/src/vulkan/wsi" tests/test_videoout.c -o "$out/test_videoout"
"$out/test_videoout"

# Clang 18, as the console build uses it, finds nothing to warn about either.
for file in title/pc5*.c; do
    # The console's own system calls (FreeBSD headers): built by tools/build.sh only. pc5_console.c,
    # the rest of the console's side, is checked here too, as the console build's warnings stop it.
    case $file in *_ps5.c) continue ;; esac
    clang-18 "${common[@]}" -I"$mbedtls/include" "$config" -O2 -fsyntax-only "$file"
done

if [[ ${1:-} == --shots ]]; then
    cc "${common[@]}" -O2 tests/shots.c title/pc5_base.c title/pc5_controls.c title/pc5_cover.c title/pc5_draw.c title/pc5_game.c \
        title/pc5_image.c title/pc5_json.c title/pc5_library.c title/pc5_logs.c title/pc5_matrix.c title/pc5_pe.c \
        title/pc5_settings.c title/pc5_source.c title/pc5_stb.c title/pc5_ui.c title/pc5_update.c title/pc5_home.c \
        title/pc5_psfwd.c title/pc5_zip.c title/pc5_qr.c title/pc5_report.c title/pc5_upgrade.c title/pc5_sound.c title/pc5_video.c title/pc5_appearance.c title/pc5_compat.c title/pc5_look.c title/pc5_backgrounds.c \
        title/pc5_tile.c title/pc5_dds.c title/pc5_files.c title/pc5_textdoc.c title/pc5_ftp.c title/pc5_web.c title/pc5_cab.c title/pc5_msi.c title/pc5_steam.c title/pc5_identity.c title/pc5_i18n.c "${upstream[@]}" "${text_sources[@]}" -lm -lpthread -o "$out/draw-shots"
    mkdir -p "$out/shots" "$out/shotwork"
    "$out/draw-shots" "$out/shots" "$out/shotwork" "$out/covers" "$out/programs" "$out/fonts.bin"
    python3 -c "import sys, pathlib; from PIL import Image
for p in sorted(pathlib.Path(sys.argv[1]).glob('*.ppm')): Image.open(p).save(p.with_suffix('.png')); p.unlink()" "$out/shots"
fi

# prospero-win's own host tests of the controllers PC5's patches change (patch 0050: the
# compatible vibration mode set as each one opens).
for t in test_pw_pad_ps5 test_pw_players_ps5; do
    make -s -C "$PW" "build/host/$t" >/dev/null
    "$PW/build/host/$t"
done
echo "controller tests passed"
# The title id read from the app's own param.json (patch 0056: a copy of the app under
# another id, a home screen tile, finds its own sandbox).
make -s -C "$PW" build/host/test_pw_title_id >/dev/null
"$PW/build/host/test_pw_title_id"

# The patched prospero-win still passes its own checks on the files the patch changes.
(cd "$PW" && python3 tests/test_title_identity.py && python3 tests/test_native_contract.py &&
    python3 tests/test_package_release.py)

# The art is the one the script draws. Pillow's versions smooth edges a little differently
# (Ubuntu's 10.2 and a newer one from pip), so it is compared picture to picture, not byte
# for byte: at most 1 % of the pixels a little off, as an edge drawn by another Pillow is,
# and none far off, as a changed picture is.
python3 art/make-art.py "$out/art" >/dev/null
python3 - "$out/art" <<'ART'
import sys
from PIL import Image, ImageChops
for name in ("icon0.png", "pic0.png"):
    kept, made = (Image.open(f"{folder}/{name}").convert("RGB") for folder in ("art", sys.argv[1]))
    if kept.size != made.size:
        sys.exit(f"art/{name} is {kept.size[0]}x{kept.size[1]}; art/make-art.py draws it {made.size[0]}x{made.size[1]}")
    r, g, b = ImageChops.difference(kept, made).split()
    off = ImageChops.lighter(ImageChops.lighter(r, g), b).histogram()   # each pixel's largest difference
    if sum(off[65:]) or sum(off[5:]) * 100 > kept.size[0] * kept.size[1]:
        sys.exit(f"art/{name} isn't what art/make-art.py draws: run it and commit what it makes")
ART
# The home screen's background as the shell reads it (tools/make-dds.c): a DDS as the shell
# wants it (one 2D surface of BC7_UNORM behind a DX10 header, no mipmaps), and the picture in
# it, as Pillow decodes BC7, what went in (a corner of the background, so it is quick).
cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o "$out/make-dds" tools/make-dds.c title/pc5_dds.c -lm
python3 -c "from PIL import Image; Image.open('art/pic0.png').convert('RGB').crop((960, 300, 1216, 444)).save('$out/corner.ppm')"
"$out/make-dds" "$out/corner.ppm" "$out/corner.dds" >/dev/null
python3 - "$out" <<'DDS'
import struct, sys
from PIL import Image, ImageChops, ImageStat
out = sys.argv[1]
data = open(f"{out}/corner.dds", "rb").read()
profile = (data[:4], struct.unpack_from("<I", data, 4)[0], struct.unpack_from("<II", data, 12), struct.unpack_from("<I", data, 28)[0],
           data[84:88], struct.unpack_from("<5I", data, 128), len(data))
if profile != (b"DDS ", 124, (144, 256), 1, b"DX10", (98, 3, 0, 1, 1), 148 + 64 * 36 * 16):
    sys.exit(f"make-dds: not the DDS the home screen reads: {profile}")
decoded = Image.open(f"{out}/corner.dds")
if decoded.mode != "RGBA" or decoded.getchannel("A").getextrema() != (255, 255):
    sys.exit("make-dds: the picture isn't opaque")
rms = ImageStat.Stat(ImageChops.difference(decoded.convert("RGB"), Image.open(f"{out}/corner.ppm").convert("RGB"))).rms
if max(rms) > 4:
    sys.exit(f"make-dds: the picture decoded is far from what went in (rms {rms})")
DDS
# The release's list of its files (tools/make-app.py), its PE modules' one date, the Windows
# template's registry made the same each build (tools/make-prefix.py), and the update zip
# (tools/make-update.py): the files that changed and the list, as they are in the app's zip.
python3 - "$out/release-tools" <<'RELEASE'
import calendar, hashlib, importlib.util, struct, subprocess, sys, zipfile, zlib
from pathlib import Path
def tool(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), f"tools/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
app_tool, prefix_tool = tool("make-app"), tool("make-prefix")
out = Path(sys.argv[1])
def release(folder, files):
    for name, data in files.items():
        (folder / name).parent.mkdir(parents=True, exist_ok=True)
        (folder / name).write_bytes(data)
    (folder / "eboot.bin").chmod(0o755)
    app_tool.write_manifest(folder)
    return folder
def zip_app(folder, target):
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(p for p in folder.rglob("*") if p.is_file()):
            info = zipfile.ZipInfo(f"PPSA11018/{path.relative_to(folder).as_posix()}", app_tool.STAMP)
            info.create_system = 3
            info.external_attr = (0o100000 | (path.stat().st_mode & 0o777)) << 16
            archive.writestr(info, path.read_bytes(), zipfile.ZIP_DEFLATED)
def crc_alike(prefix, target):
    """prefix and 4 bytes whose CRC-32 is target: the CRC is linear in the bits, so solve for them."""
    base = zlib.crc32(prefix + b"\0" * 4)
    rows = [(zlib.crc32(prefix + (1 << bit).to_bytes(4, "little")) ^ base, 1 << bit) for bit in range(32)]
    want, chosen = target ^ base, 0
    for bit in range(32):
        pivot = next((r for r in rows if r[0] >> bit & 1), None)
        if pivot is None:
            continue
        rows.remove(pivot)
        rows = [(r[0] ^ pivot[0], r[1] ^ pivot[1]) if r[0] >> bit & 1 else r for r in rows]
        if want >> bit & 1:
            want, chosen = want ^ pivot[0], chosen ^ pivot[1]
    assert want == 0
    return prefix + chosen.to_bytes(4, "little")
alike_new = b"crc-alike B...."
alike_old = crc_alike(b"crc-alike A", zlib.crc32(alike_new))
assert len(alike_old) == len(alike_new) and zlib.crc32(alike_old) == zlib.crc32(alike_new) and alike_old != alike_new
old_files = {"eboot.bin": b"old eboot", "LICENSE": b"licence 1", "pc5/same.bin": b"same" * 1000,
             "pc5/gone.txt": b"gone", "win/x.dll": alike_old}
new_files = {"eboot.bin": b"new eboot!", "LICENSE": b"licence 2", "pc5/same.bin": b"same" * 1000,
             "pc5/new.txt": b"new", "win/x.dll": alike_new}
old, new = release(out / "old" / "PPSA11018", old_files), release(out / "new" / "PPSA11018", new_files)
# The list: every file but itself, sorted, its size and SHA-256, after a comment.
lines = (new / "pc5/manifest.txt").read_text().splitlines()
assert lines[0].startswith("# "), lines[0]
want = sorted((name, len(data), hashlib.sha256(data).hexdigest()) for name, data in new_files.items())
assert [tuple(line.split("\t")) for line in lines[1:]] == [(n, str(s), h) for n, s, h in want], lines
zip_app(old, out / "old.zip")
zip_app(new, out / "new.zip")
# A file whose size and CRC-32 are the old one's but whose bytes aren't (4 bytes chosen to
# give the CRC): still in the update zip, as the list's SHA-256 says.
made = subprocess.run([sys.executable, "tools/make-update.py", str(out / "old.zip"), str(out / "new.zip"),
                       str(out / "update.zip"), "--markdown", str(out / "summary.md")], capture_output=True, text=True)
assert made.returncode == 0, made.stderr
with zipfile.ZipFile(out / "update.zip") as update, zipfile.ZipFile(out / "new.zip") as full:
    assert update.testzip() is None
    names = sorted(update.namelist())
    assert names == sorted(f"PPSA11018/{n}" for n in ("eboot.bin", "LICENSE", "pc5/new.txt", "win/x.dll",
                                                      "pc5/manifest.txt")), names
    for info in update.infolist():
        other = full.getinfo(info.filename)
        assert (info.date_time, info.external_attr, info.create_system, info.compress_type) == \
            (other.date_time, other.external_attr, other.create_system, zipfile.ZIP_DEFLATED), info.filename
        assert update.read(info) == full.read(other), info.filename
assert "5 of 6 files" in made.stdout and "The update zip" in (out / "summary.md").read_text(), made.stdout
# A new zip without the list, or whose list isn't its files, makes none.
for name, data in (("pc5/manifest.txt", None), ("pc5/manifest.txt", b"# list\nLICENSE\t9\t" + b"0" * 64 + b"\n")):
    with zipfile.ZipFile(out / "new.zip") as full, zipfile.ZipFile(out / "odd.zip", "w") as odd:
        for info in full.infolist():
            if info.filename != "PPSA11018/" + name:
                odd.writestr(info, full.read(info))
            elif data is not None:
                odd.writestr(info, data)
    failed = subprocess.run([sys.executable, "tools/make-update.py", str(out / "old.zip"), str(out / "odd.zip"),
                             str(out / "odd-update.zip")], capture_output=True, text=True)
    assert failed.returncode and "make-update:" in failed.stderr, failed.stderr

# PE modules: one link time and the right checksum; a signed one left as it is.
def pe(stamp, signed=False):
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHI", data, 0x84, 0x8664, 0, stamp)
    struct.pack_into("<H", data, 0x94, 240)
    struct.pack_into("<H", data, 0x98, 0x20b)
    struct.pack_into("<I", data, 0x98 + 64, 1)            # a checksum, made again
    struct.pack_into("<I", data, 0x98 + 108, 16)
    if signed:
        struct.pack_into("<II", data, 0x98 + 112 + 4 * 8, 0x300, 8)
    return bytes(data)
stamp = calendar.timegm(app_tool.STAMP)
for name, first, second in (("a.dll", 1700000000, 1800000000), ("signed.dll", 1700000000, 1800000000)):
    one, two = out / f"1-{name}", out / f"2-{name}"
    one.write_bytes(pe(first, name == "signed.dll"))
    two.write_bytes(pe(second, name == "signed.dll"))
    changed = app_tool.steady_pe(one, stamp), app_tool.steady_pe(two, stamp)
    if name == "signed.dll":
        assert changed == (False, False) and one.read_bytes() != two.read_bytes(), name
        continue
    data = one.read_bytes()
    assert changed == (True, True) and data == two.read_bytes(), name
    assert struct.unpack_from("<I", data, 0x88)[0] == stamp
    assert struct.unpack_from("<I", data, 0x98 + 64)[0] == app_tool.pe_checksum(data, 0x98 + 64) != 1
assert not app_tool.steady_pe(Path("tools/make-app.py"), stamp)

# Two Wines' registries of one build: the same once made steady.
def registry(seconds, guid, host, msi):
    return (f"WINE REGISTRY Version 2\n;; All keys relative to \\\\Machine\n\n#arch=win64\n\n"
            f"[Software\\\\Microsoft\\\\Cryptography] {seconds}\n#time=1dd66{seconds:x}\n\"MachineGuid\"=\"{guid}\"\n\n"
            f"[Software\\\\Wine\\\\Mono] {seconds}\n\"InstallDate\"=\"2026102{seconds % 9}\"\n"
            f"\"LocalPackage\"=\"C:\\\\windows\\\\Installer\\\\{msi}.msi\"\n\n"
            f"[System\\\\Control\\\\ComputerName] {seconds}\n\"ComputerName\"=\"{host.upper()}\"\n"
            f"\"Hostname\"=\"{host}\"\n\"PROCESSOR_LEVEL\"=\"{seconds % 30}\"\n"
            f"\"ContainerId\"=\"{{0001845E-0000-FFFF-{guid[:4].upper()}-1B8F00000000}}\"\n"
            f"@=hex(ffff0010):{','.join(f'{(seconds >> i) & 255:02x}' for i in range(0, 64, 8))}\n")
one = prefix_tool.steady_registry(registry(1793102035, "8d10a469-e550-4f92-9e5e-7582228a161c", "23bd2c9ae97f", "ea81"))
two = prefix_tool.steady_registry(registry(1793174756, "b2d5c60e-791f-4682-b1b0-5091a729be73", "ed6c3038ba7e", "d2a4"))
assert one == two, "\n".join(a + "  |  " + b for a, b in zip(one.splitlines(), two.splitlines()) if a != b)
assert "1793102035" not in one and "23BD2C9AE97F" not in one and '"ComputerName"="PROSPERO"' in one
assert '"MachineGuid"="' in one and "8d10a469" not in one and "{0001845E-0000-FFFF-" in one
RELEASE
python3 -m py_compile tools/make-prefix.py tools/make-app.py tools/make-update.py tools/make-font.py tools/update-po.py art/make-art.py \
    tests/make-test-programs.py tests/make-sample-covers.py tests/make-test-msi.py
bash -n tools/build.sh
echo "host checks passed"
