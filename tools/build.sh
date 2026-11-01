#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Builds PC5 (PPSA11018) from source: prospero-win at its v0.2.0 release with this repo's
# patch and title/, its Wine, RADV and Mesa Zink for the console, VKD3D-Proton, and the app.
#
#   tools/build.sh [PIECE...]      build these pieces, in the order below (default: all)
#   tools/build.sh key PIECE...    each piece's key: a fingerprint of all it is built from
#   tools/build.sh paths PIECE...  what each piece leaves for the pieces after it
#   tools/build.sh work PIECE...   what a piece keeps to build again in less time
#   tools/build.sh ci              every piece's key and paths, for GitHub's build ($GITHUB_OUTPUT)
#   tools/build.sh toolchain       a fingerprint of the compilers installed here
#
# The pieces, in order. Each fetches what it is built from, so any one runs on its own once
# the pieces it uses are there, and one whose key is the same as when it was built here is
# skipped.
#   sources   every project and archive the app is built from, fetched in parallel (what
#             bundle archives)
#   title     the PS5 payload SDK, the Lapy helper for PPSA11018, mbedTLS for the console,
#             the eboot with PC5 in it
#   radv      RADV, the console's Vulkan driver (PS5_Vulkan, PS5_Mesa with patches/radv)
#   tools     Wine's own tools for this computer (winebuild, widl, winegcc and the rest),
#             which the PS5 Wine is built with: minutes, where the whole of hostwine is the
#             longest piece
#   hostwine  Wine's Windows side, built for this computer: the PE modules the console runs
#             (every DLL, 32- and 64-bit), its NLS files, and the Wine that makes PC5's empty
#             Windows template; the 32-bit CPUs' Windows halves (wowprospero.dll, wow64native.dll)
#   wineps5   Wine's PS5 side (PRX modules), with FFmpeg and FreeType; the native WoW64 CPU's
#             PS5 module (uses radv and tools)
#   dxvk      DXVK 2.6.2-prospero3 with patches/dxvk, 64- and 32-bit, for the Windows template
#   vkd3d     VKD3D-Proton 3.0.1 with patches/vkd3d-proton, 64- and 32-bit, for the Windows template
#   zink      Mesa's WGL/Zink, OpenGL for the games that draw with it
#   package   prospero-win's tools/package_release.sh, with Zink (uses title and the pieces above)
#   app       release/PPSA11018/ (art, fonts, the Windows template), release/PPSA11018.zip and
#             Wine Mono's source archive beside it
#   bundle    release/PPSA11018-sources.tar.xz: the source of everything in the app, at the
#             commits it was built from (the GPL and LGPL ask for it beside the binaries)
#
# PC5_RELEASE=DIR puts the app there instead of release/ (on a shared build machine, say
# /mnt/media/builder/releases/PC5); PC5_BUILD_DIR=DIR builds there instead of .build/.
# PC5_ZINK_JOBS sets Zink's jobs (default 2).
#
# On Debian 13 or Ubuntu 24.04 (x86-64) with tools/packages.txt installed it runs here, as
# root or a user who can write the repo; anywhere else it runs itself inside
# tools/Dockerfile's image (Docker needed), the image GitHub's build runs in. Each piece
# writes its log to .build/logs/<piece>.log and prints one line.
set -euo pipefail
here=$(realpath -- "$(dirname -- "$0")/..")
release=$(realpath -m -- "${PC5_RELEASE:-$here/release}")
B=$(realpath -m -- "${PC5_BUILD_DIR:-$here/.build}")

# ---- the projects, each at a fixed commit ------------------------------------------
PW_URL=https://github.com/mpereiraesaa/prospero-win.git
PW_COMMIT=644e32e400c555e340d507383398c5d21b39b0b0        # v0.2.0 (2026-10-10)
VULKAN_URL=https://github.com/mpereiraesaa/PS5_Vulkan.git
VULKAN_COMMIT=8aee3afb37bcfad06f0c7d263b901040f08ebbd0    # RADV from PS5_Mesa 01d7057
MESA_URL=https://github.com/mpereiraesaa/PS5_Mesa.git
SDK_FORK_URL=https://github.com/mihawk-99/PS5_PayloadSDK.git
BOILERPLATE_URL=https://github.com/mpereiraesaa/ps5-native-app-boilerplate.git
LAPY_URL=https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon.git
LAPY_COMMIT=b48b7d7236eca25c7b9dfd6c040c763ae05dde80      # v0.3.2-experimental
WINE_URL=https://github.com/wine-mirror/wine.git
# Read from the pinned projects themselves, after prospero-win is fetched: Wine
# (build_wine_ps5.sh), the title's boilerplate (build_native.sh), the PRX
# boilerplate (build_wine_ps5.sh), PS5_Mesa for RADV (build-radv.sh) and for Zink
# (build_mesa_zink.sh, with its llvm-mingw), the SDK fork (setup-native-dependencies.sh).
VKD3D_VERSION=3.0.1                                       # needs DXVK 2.1's dxgi or newer (its CHANGELOG)
MBEDTLS_VERSION=3.6.7                                     # LTS: the covers' HTTPS
# Wine Mono, .NET Framework for the games: the version and SHA-256 Wine's appwiz.cpl names
# (dlls/appwiz.cpl/addons.c, MONO_VERSION and MONO_SHA), from its release on GitHub.
MONO_VERSION=11.3.0
MONO_URL=https://github.com/madewokherd/wine-mono/releases/download/wine-mono-$MONO_VERSION/wine-mono-$MONO_VERSION-x86.msi
MONO_SHA256=df2dfc1665c2511882e7cabd56eafd0c0a3d94e5a7e86f969277f6c189d418d3
MONO_SOURCE_URL=https://github.com/madewokherd/wine-mono/releases/download/wine-mono-$MONO_VERSION/wine-mono-$MONO_VERSION-src.tar.xz
MONO_SOURCE_SHA256=9f4f03285fbeed484980efbb1eba1c19069244aea401d2ff1585e3990782edf7
MONO_COPYING_URL=https://raw.githubusercontent.com/madewokherd/wine-mono/wine-mono-$MONO_VERSION/COPYING
MONO_COPYING_SHA256=0265443ffc97c9450dbcf402c2430bf9fe87d7a6c92d5d7684fcecfe749bf75d
# Fonts in Wine's own fonts folder, which every game finds: Liberation 2.1.5 (Arial, Times New
# Roman and Courier New's metrics) as Ubuntu 24.04 packages it, and Droid Sans Fallback
# (Chinese, Japanese and Korean) from Android 7.1.2. make-prefix.py names them for Windows.
LIBERATION_VERSION=2.1.5
LIBERATION_URL=https://archive.ubuntu.com/ubuntu/pool/main/f/fonts-liberation/fonts-liberation_$LIBERATION_VERSION-3_all.deb
LIBERATION_SHA256=065c2ab1abc9108b17d401016dc594b79750904390f095845c93bb06e1153acc
LIBERATION_LICENSE_URL=https://raw.githubusercontent.com/liberationfonts/liberation-fonts/$LIBERATION_VERSION/LICENSE
LIBERATION_LICENSE_SHA256=93fed46019c38bbe566b479d22148e2e8a1e85ada614accb0211c37b2c61c19b
DROID_FALLBACK_URL=https://raw.githubusercontent.com/aosp-mirror/platform_frameworks_base/android-7.1.2_r39/data/fonts/DroidSansFallbackFull.ttf
DROID_FALLBACK_SHA256=acb6440a713d880a13a21b468ba7cd43f5a2b2934972e51be791c880730777b8
DROID_FALLBACK_NOTICE_URL=https://raw.githubusercontent.com/aosp-mirror/platform_frameworks_base/android-7.1.2_r39/data/fonts/NOTICE
DROID_FALLBACK_NOTICE_SHA256=38751245389e1e23f73e6f5384b5cbe7fa972cc4410c5adc9c04b082a0b9561a
# PC5's own text in Korean (pc5/fonts-ko.bin, tools/make-font.py): Droid Sans Fallback has no
# Hangul, so Noto Sans KR 2.004 (SIL OFL 1.1), the subset OTFs of Noto CJK's release, regular and
# bold (4.6 and 4.8 MB, read at build time only: the app gets the letters its ko.po uses).
NOTO_KR_URL=https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004
NOTO_KR_REGULAR_SHA256=69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68
NOTO_KR_BOLD_SHA256=5a6ceb287ed2fc6cfc6213144ebea68cbd94b20fc9eb873d8486493bf02d9bda
NOTO_KR_LICENSE_SHA256=6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2
MBEDTLS_URL=https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VERSION/mbedtls-$MBEDTLS_VERSION.tar.bz2
MBEDTLS_SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
# DXVK: the release prospero-win's pw_install.py pins (2.6.2-prospero3, since prospero-win
# df9802f: prospero2 with Direct3D 9 texture packs, d3d9.texturePack and d3d9.textureDump, and
# the HUD's "menu" item), built here with patches/dxvk; app/pc5/dxvk.txt names the result, and
# PC5 gives installed games its DLLs.
DXVK_URL=https://github.com/mpereiraesaa/dxvk.git
DXVK_COMMIT=4228f2e06b410df7ea3f552f07c2fd7c9d368df6      # tag v2.6.2-prospero3
DXVK_NAME=2.6.2-prospero3-pc5.1
# gbe_fork, a Steamworks emulator (LGPL 3.0), its release build unchanged: PC5 puts its
# steam_api.dll or steam_api64.dll in place of Valve's in a game that has it
# (title/pc5_steam.c). The DLLs are pinned by SHA-256 too; its source, at the release's
# commit, goes in the source bundle.
GBE_RELEASE=release-2026_09_27
GBE_URL=https://github.com/Detanup01/gbe_fork/releases/download/$GBE_RELEASE/emu-win-release-vs22.7z
GBE_SHA256=300186c34f49a9ea75b33ce2af299267cc1dad0225cf45e81a229578d4e11c85
GBE_DLL64_SHA256=70d9826a402d3a78b0c07e9f7ea7c6832468a1113013f31ea570fccaa5445ea9
GBE_DLL32_SHA256=cae4372bceda997d6c73cebc1c14eb27d39b74a7999d8f28384bdfd8d746a79c
GBE_SOURCE_URL=https://github.com/Detanup01/gbe_fork.git
GBE_COMMIT=7103add7ca6ef3ef8353279219880068f2494fc8       # tag release-2026_09_27
GBE_LICENSE_URL=https://raw.githubusercontent.com/Detanup01/gbe_fork/$GBE_COMMIT/LICENSE
GBE_LICENSE_SHA256=e3a994d82e644b03a792a930f574002658412f62407f5fee083f2555c5f23118
# The PS5 Forwarder Format: the home screen tiles' program (template/) and psfwd, which
# title/third_party/psfwd holds unchanged.
FORWARDER_URL=https://github.com/heydemoura/ps5-forwarder-format.git
FORWARDER_COMMIT=22e8762ad7dbd01e18bd8c0e95f86461fdf8aaf4
# Project Nayuki's QR Code generator (C): Report on GitHub's codes, which
# title/third_party/qrcodegen holds unchanged.
QRCODEGEN_URL=https://github.com/nayuki/QR-Code-generator.git
QRCODEGEN_COMMIT=3c6d0b3cefb4e049dc337e82237c9644399716a8
VKD3D_SOURCE_URL=https://github.com/HansKristian-Work/vkd3d-proton.git
VKD3D_SOURCE_COMMIT=3b10bd7a7ec6a7347e616cf8bea59333afec2255   # tag v3.0.1, the release's build
# Built here with patches/vkd3d-proton (piece vkd3d); app/pc5/dxvk.txt names the result with
# DXVK's, and PC5 gives installed games its DLLs when the name changes.
VKD3D_NAME=3.0.1-pc5.1

TITLE=PPSA11018
STEPS="sources title radv tools hostwine wineps5 dxvk vkd3d zink package app bundle"
# The pieces with a key: built again only when it changes. GitHub's build keeps what each
# leaves (paths) under its key, and builds only the pieces whose key it hasn't kept.
KEYED="title radv tools hostwine wineps5 dxvk vkd3d zink"
COMMANDS="key paths work ci toolchain"

# ---- here on Debian 13 or Ubuntu 24.04, else in the build image ----------------------
supported() {
    local id version
    id=$(. /etc/os-release 2>/dev/null && echo "$ID") version=$(. /etc/os-release 2>/dev/null && echo "$VERSION_ID")
    [[ $(uname -m) == x86_64 && ( ($id == debian && $version == 13) || ($id == ubuntu && $version == 24.04) ) ]]
}
if [[ -z ${PC5_IN_IMAGE:-} ]] && ! supported; then
    command -v docker >/dev/null || { echo "build.sh: this is neither Debian 13 nor Ubuntu 24.04 (x86-64), and there is no Docker" >&2; exit 2; }
    mkdir -p "$B" "$release"
    docker build -q -t pc5-build "$here/tools" >/dev/null
    exec docker run --rm -v "$here:/PC5" -v "$release:$release" -v "$B:$B" -w /PC5 -e PC5_IN_IMAGE=1 \
        -e PC5_RELEASE="$release" -e PC5_BUILD_DIR="$B" -e PC5_ZINK_JOBS="${PC5_ZINK_JOBS:-2}" pc5-build tools/build.sh "$@"
fi

export LC_ALL=C TZ=UTC
# The time every build says it was made at (2026-10-22, tools/make-app.py's STAMP), so two
# builds of one commit give the same files: strip and the linkers date the PE modules with
# it, tools/make-prefix.py the Windows template's registry. An update downloads only the
# files that changed (tools/make-update.py).
export SOURCE_DATE_EPOCH=1792627200
PW=$B/prospero-win
jobs=$(nproc)
mkdir -p "$B/logs" "$B/downloads" "$B/stamps"
pin() { sed -n "s/^$2=\\([0-9a-f]\\{40\\}\\).*/\\1/p" "$1" | head -1; }
value() { sed -n "s/^$2=\\([^ ]*\\).*/\\1/p" "$1" | head -1; }
wine_commit() { sed -n 's/^WINE_COMMIT=//p' "$PW/tools/build_wine_ps5.sh"; }

# A clone of url in dir at commit (only that commit is fetched).
fetch() { # dir url commit
    [[ -d $1/.git ]] || git init -q "$1"
    if ! git -C "$1" cat-file -e "$3^{commit}" 2>/dev/null; then
        git -C "$1" fetch -q --depth 1 "$2" "$3" || git -C "$1" fetch -q "$2"
    fi
    git -C "$1" -c advice.detachedHead=false checkout -q --force "$3"
}
# A file downloaded once and checked against its SHA-256.
download() { # file url sha256
    if [[ ! -f $1 ]] || ! echo "$3  $1" | sha256sum --check --status; then
        curl -fsSL --retry 3 -o "$1.part" "$2"
        echo "$3  $1.part" | sha256sum --check --status || { echo "$2 does not match its pinned SHA-256" >&2; rm -f "$1.part"; return 1; }
        mv "$1.part" "$1"
    fi
}
# Functions run at once, each in the background with a log of its own; fails when one did.
parallel() { # function...
    local f i status=0 pids=()
    for f; do
        ( set -euo pipefail; "$f" ) > "$B/logs/fetch-$f.log" 2>&1 &
        pids+=($!)
    done
    for i in "${!pids[@]}"; do
        if ! wait "${pids[$i]}"; then
            status=1
            echo "${*:i+1:1} failed, the end of .build/logs/fetch-${*:i+1:1}.log:"
            tail -20 "$B/logs/fetch-${*:i+1:1}.log"
        fi
    done
    return $status
}

# ---- the sources, each fetched by the pieces built from it ---------------------------

# prospero-win v0.2.0 + patches/prospero-win + title/: committed with fixed names and dates,
# so the same repo gives the same prospero-win commit (SOURCES.txt names it). Made again only
# when one of them changed or the tree was.
src_pw() {
    local id=(-c user.name="PC5 build" -c user.email="pc5@invalid") made want
    want=$(cd "$here" && { echo "$PW_COMMIT"
        sha256sum patches/prospero-win/*.patch title/pc5*.c title/pc5*.h title/third_party/stb_image.h \
            title/third_party/stb_image_write.h $(find title/third_party/psfwd title/third_party/qrcodegen -type f | sort)
    } | sha256sum | cut -c1-64)
    made=$(git -C "$PW" rev-parse -q --verify HEAD 2>/dev/null || true)
    if [[ -n $made && $(cat "$B/stamps/prospero-win" 2>/dev/null) == "$want $made" ]] && git -C "$PW" diff --quiet HEAD; then
        return 0
    fi
    fetch "$PW" "$PW_URL" "$PW_COMMIT"
    git -C "$PW" reset -q --hard "$PW_COMMIT"
    git -C "$PW" clean -q -fd
    git -C "$PW" "${id[@]}" am -q --committer-date-is-author-date "$here"/patches/prospero-win/*.patch
    cp "$here"/title/pc5*.c "$here"/title/pc5*.h "$PW/native/"
    mkdir -p "$PW/native/third_party"
    cp "$here/title/third_party/stb_image.h" "$here/title/third_party/stb_image_write.h" "$PW/native/third_party/"
    cp -r "$here/title/third_party/psfwd" "$here/title/third_party/qrcodegen" "$PW/native/third_party/"
    git -C "$PW" add native/pc5* native/third_party
    GIT_AUTHOR_DATE="2026-10-22T17:00:00+0100" GIT_COMMITTER_DATE="2026-10-22T17:00:00+0100" \
        git -C "$PW" "${id[@]}" commit -q -m "PC5: its screens (title/ of the PC5 repo)"
    git -C "$PW" log --oneline -3
    echo "$want $(git -C "$PW" rev-parse HEAD)" > "$B/stamps/prospero-win"
}
src_wine() { fetch "$PW/.deps/wine/source" "$WINE_URL" "$(wine_commit)"; }
# FFmpeg for the games' videos and sound (winedmo.prx, winegstreamer.prx).
src_ffmpeg() {
    fetch "$PW/.deps/ffmpeg/source" "$(value "$PW/tools/build_ffmpeg_ps5.sh" FFMPEG_URL)" \
        "$(pin "$PW/tools/build_ffmpeg_ps5.sh" FFMPEG_COMMIT)"
}
# ps5-framegen's interpolator, linked into libvulkan.prx for Frame generation.
src_framegen() {
    fetch "$B/ps5-framegen" "$(value "$PW/tools/link_radv_prx.sh" FRAMEGEN_URL)" "$(pin "$PW/tools/link_radv_prx.sh" FRAMEGEN_COMMIT)"
}
src_foundation() { fetch "$PW/.deps/ps5-native-app-boilerplate" "$BOILERPLATE_URL" "$(pin "$PW/tools/build_native.sh" pin)"; }
src_prxfoundation() { fetch "$B/prx-foundation" "$BOILERPLATE_URL" "$(pin "$PW/tools/build_wine_ps5.sh" MODULE_EXPORTS_COMMIT)"; }
src_vulkan() {
    fetch "$B/PS5_Vulkan" "$VULKAN_URL" "$VULKAN_COMMIT"
    fetch "$B/PS5_Mesa" "$MESA_URL" "$(pin "$B/PS5_Vulkan/tools/build-radv.sh" mesa_revision)"
    fetch "$B/PS5_PayloadSDK" "$SDK_FORK_URL" "$(pin "$B/PS5_Vulkan/tools/setup-native-dependencies.sh" sdk_revision)"
}
src_lapy() { fetch "$B/lapy" "$LAPY_URL" "$LAPY_COMMIT"; }
# A project at a commit with PC5's patches on it, committed with fixed names and dates, with
# its submodules: made again only when the commit or the patches changed, or the tree did.
patched() { # dir url commit patches-dir
    local id=(-c user.name="PC5 build" -c user.email="pc5@invalid") made want stamp
    stamp=$B/stamps/$(basename "$1")
    want=$( { echo "$3"; cat "$4"/*.patch; } | sha256sum | cut -c1-64)
    made=$(git -C "$1" rev-parse -q --verify HEAD 2>/dev/null || true)
    if [[ -n $made && $(cat "$stamp" 2>/dev/null) == "$want $made" ]] && git -C "$1" diff --quiet HEAD; then
        return 0
    fi
    fetch "$1" "$2" "$3"
    git -C "$1" reset -q --hard "$3"
    git -C "$1" clean -q -fdx
    git -C "$1" submodule -q update --init --recursive
    git -C "$1" "${id[@]}" am -q --committer-date-is-author-date "$4"/*.patch
    echo "$want $(git -C "$1" rev-parse HEAD)" > "$stamp"
}
src_dxvk() { patched "$B/dxvk-source" "$DXVK_URL" "$DXVK_COMMIT" "$here/patches/dxvk"; }
src_vkd3d() { patched "$B/vkd3d-proton-source" "$VKD3D_SOURCE_URL" "$VKD3D_SOURCE_COMMIT" "$here/patches/vkd3d-proton"; }
# The home screen tiles: psfwd in title/third_party must be the format's own, unchanged.
src_forwarder() {
    local pair
    fetch "$B/ps5-forwarder-format" "$FORWARDER_URL" "$FORWARDER_COMMIT"
    for pair in src/psfwd.c:psfwd.c include/psfwd.h:psfwd.h LICENSE:LICENSE; do
        cmp -s "$B/ps5-forwarder-format/${pair%%:*}" "$here/title/third_party/psfwd/${pair##*:}" ||
            { echo "title/third_party/psfwd/${pair##*:} differs from the PS5 Forwarder Format at $FORWARDER_COMMIT" >&2
              return 1; }
    done
}
# Report on GitHub's QR codes: qrcodegen in title/third_party must be Project Nayuki's, unchanged.
src_qrcodegen() {
    local pair
    fetch "$B/qrcodegen" "$QRCODEGEN_URL" "$QRCODEGEN_COMMIT"
    for pair in c/qrcodegen.c:qrcodegen.c c/qrcodegen.h:qrcodegen.h; do
        cmp -s "$B/qrcodegen/${pair%%:*}" "$here/title/third_party/qrcodegen/${pair##*:}" ||
            { echo "title/third_party/qrcodegen/${pair##*:} differs from the QR Code generator at $QRCODEGEN_COMMIT" >&2
              return 1; }
    done
}
# Zink's Mesa, as tools/build_mesa_zink.sh pins it.
src_zink() { fetch "$B/mesa-zink-source" "$MESA_URL" "$(pin "$PW/tools/build_mesa_zink.sh" MESA_COMMIT)"; }
src_mbedtls() {
    download "$B/downloads/mbedtls-$MBEDTLS_VERSION.tar.bz2" "$MBEDTLS_URL" "$MBEDTLS_SHA256"
    [[ -f $B/mbedtls-$MBEDTLS_VERSION/include/mbedtls/ssl.h ]] || tar -xjf "$B/downloads/mbedtls-$MBEDTLS_VERSION.tar.bz2" -C "$B"
}
src_mono() { download "$B/downloads/wine-mono-$MONO_VERSION-x86.msi" "$MONO_URL" "$MONO_SHA256"; }
# Its source, unchanged, released beside the app (parts of Mono are under the LGPL and GPL).
src_mono_source() {
    download "$B/downloads/wine-mono-$MONO_VERSION-src.tar.xz" "$MONO_SOURCE_URL" "$MONO_SOURCE_SHA256"
    download "$B/downloads/wine-mono-$MONO_VERSION-COPYING" "$MONO_COPYING_URL" "$MONO_COPYING_SHA256"
}
# gbe_fork's release build and its licence, for the app; its source, for the bundle.
src_gbe() {
    download "$B/downloads/gbe_fork-$GBE_RELEASE-emu-win-release-vs22.7z" "$GBE_URL" "$GBE_SHA256"
    download "$B/downloads/gbe_fork-$GBE_RELEASE-LICENSE" "$GBE_LICENSE_URL" "$GBE_LICENSE_SHA256"
}
src_gbe_source() { fetch "$B/gbe_fork" "$GBE_SOURCE_URL" "$GBE_COMMIT"; }
src_fonts() {
    download "$B/downloads/fonts-liberation_$LIBERATION_VERSION-3_all.deb" "$LIBERATION_URL" "$LIBERATION_SHA256"
    download "$B/downloads/liberation-fonts-$LIBERATION_VERSION-LICENSE" "$LIBERATION_LICENSE_URL" "$LIBERATION_LICENSE_SHA256"
    download "$B/downloads/DroidSansFallbackFull.ttf" "$DROID_FALLBACK_URL" "$DROID_FALLBACK_SHA256"
    download "$B/downloads/DroidSansFallbackFull-NOTICE" "$DROID_FALLBACK_NOTICE_URL" "$DROID_FALLBACK_NOTICE_SHA256"
    download "$B/downloads/NotoSansKR-Regular.otf" "$NOTO_KR_URL/Sans/SubsetOTF/KR/NotoSansKR-Regular.otf" "$NOTO_KR_REGULAR_SHA256"
    download "$B/downloads/NotoSansKR-Bold.otf" "$NOTO_KR_URL/Sans/SubsetOTF/KR/NotoSansKR-Bold.otf" "$NOTO_KR_BOLD_SHA256"
    download "$B/downloads/NotoSansCJK-LICENSE" "$NOTO_KR_URL/LICENSE" "$NOTO_KR_LICENSE_SHA256"
}
# FreeType's release archive, as build_wine_ps5.sh pins it (PROSPERO_FREETYPE_TARBALL names a
# local copy), unpacked for the source bundle.
src_freetype() {
    local ft=$PW/tools/build_wine_ps5.sh version tarball
    version=$(sed -n 's/^FREETYPE_VERSION=//p' "$ft")
    tarball=${PROSPERO_FREETYPE_TARBALL:-$B/downloads/freetype-$version.tar.xz}
    [[ -n ${PROSPERO_FREETYPE_TARBALL:-} ]] ||
        download "$tarball" "$(sed -n 's/^FREETYPE_URL=//p' "$ft" | sed "s/\$FREETYPE_VERSION/$version/")" \
            "$(sed -n 's/^FREETYPE_SHA256=//p' "$ft")"
    rm -rf "$B/freetype-src" && mkdir -p "$B/freetype-src"
    tar -xJf "$tarball" -C "$B/freetype-src" --strip-components=1
}
# Python tools for Mesa's builds (meson 1.9 and the rest, as prospero-win pins them).
src_venv() {
    [[ -x $B/venv/bin/meson ]] || {
        python3 -m venv "$B/venv"
        "$B/venv/bin/pip" install -q -r "$PW/tools/mesa_zink_requirements.txt"
    }
}
# What bundle archives, besides prospero-win.
SOURCES="src_wine src_ffmpeg src_framegen src_foundation src_prxfoundation src_vulkan src_lapy src_dxvk
 src_vkd3d src_forwarder src_qrcodegen src_zink src_mbedtls src_freetype src_gbe_source"

# The PS5 payload SDK the title's boilerplate pins, and the module tool the Wine PS5 build
# signs its PRX modules with (from the PRX boilerplate).
need_sdk() {
    src_foundation
    bash "$PW/.deps/ps5-native-app-boilerplate/tools/setup-native-dependencies.sh"
}
need_native_tool() {
    local native=$B/prx-foundation/tooling/native tool=$B/prx-foundation/build/host/ps5-native-tool
    src_prxfoundation
    if [[ ! -x $tool ]]; then
        mkdir -p "${tool%/*}"
        c++ -std=c++20 -O2 "$native/native_app_builder.cpp" "$native/self_container.cpp" \
            "$native/elf_object.cpp" "$native/sce_module_writer.cpp" -lz -o "$tool"
    fi
}
# Another piece's outputs, there before this one runs.
need() { # piece
    local path missing=
    for path in $(paths_of "$1"); do
        compgen -G "$path" >/dev/null || missing+=" $path"
    done
    [[ -z $missing ]] || { echo "build.sh: $1 is not built (missing:$missing): run tools/build.sh $1 first" >&2; return 1; }
}

# ---- the pieces ----------------------------------------------------------------------

step_sources() {
    src_pw
    parallel $SOURCES
}

step_title() {
    stage "its sources and the payload SDK"
    src_pw; src_lapy; src_mbedtls; need_sdk
    local foundation=$PW/.deps/ps5-native-app-boilerplate sources
    make -C "$foundation" libc
    local sdk=$foundation/.deps/native/ps5-payload-sdk
    # The /data helper, built for this title: a helper only elevates the title it was built for.
    PS5_PAYLOAD_SDK="$sdk" make -C "$B/lapy" owned-helper TARGET_TITLE="$TITLE" LOGGING_CLIENT="$PW/native/ps5log"
    local helper=$B/lapy/build/owned_root_helper-$TITLE
    printf '{\n  "published_at": null,\n  "release_url": "%s/tree/%s",\n  "repository": "mpereiraesaa/PS5-Lapy-JB-Daemon",\n  "tag_name": "v0.3.2-experimental (%s), built for %s"\n}\n' \
        "${LAPY_URL%.git}" "$LAPY_COMMIT" "${LAPY_COMMIT:0:7}" "$TITLE" > "$helper/release.json"
    # mbedTLS for the console, with PC5's settings (title/pc5_mbedtls_config.h).
    local tls=$B/mbedtls-ps5 mbedtls=$B/mbedtls-$MBEDTLS_VERSION config='-DMBEDTLS_USER_CONFIG_FILE="pc5_mbedtls_config.h"'
    rm -rf "$tls" && mkdir -p "$tls"
    local pids=() pid
    for source in "$mbedtls"/library/*.c; do
        env PS5_PAYLOAD_SDK="$sdk" sh "$foundation/tooling/prospero-clang18" -std=c99 -O2 -ffunction-sections \
            -fdata-sections -I"$mbedtls/include" -I"$mbedtls/library" -I"$here/title" "$config" \
            -c "$source" -o "$tls/$(basename "$source" .c).o" &
        pids+=($!)
        if (( ${#pids[@]} >= jobs )); then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
    done
    for pid in "${pids[@]}"; do wait "$pid"; done
    /usr/lib/llvm-18/bin/llvm-ar rcs "$tls/libmbedtls-pc5.a" "$tls"/*.o
    sources=$(cd "$PW" && ls native/pc5*.c | tr '\n' ' ')
    (cd "$PW" && PW_TITLE_ID=$TITLE PW_LAPY_DIR=$helper PW_EXTRA_SOURCES="$sources" \
        PW_EXTRA_CFLAGS="-DPW_PC5=1 -I$PW/wine/ps5 -I$mbedtls/include $config" PW_EXTRA_LIBS="$tls/libmbedtls-pc5.a" \
        PS5_NATIVE_FOUNDATION="$foundation" PW_FOUNDATION_READY=1 tools/build_native.sh)
    # The eboot's imports, each from a library a title gets. A title's imports from
    # libkernel_sys, libkernel_web and libScePosixForWebKit stay empty, and a call to one
    # jumps to nothing; the console's statvfs and fstatvfs go through libkernel_sys.
    local stubs=$sdk/target/lib imports=$PW/build/native/PW_DYNAMIC_IMPORTS.txt nm=/usr/lib/llvm-18/bin/llvm-nm bad
    defined() { for lib; do "$nm" -D --defined-only "$stubs/$lib.so" | awk 'NF {print $NF}'; done | sort -u; }
    local gets lacks
    gets=$(defined $(cd "$stubs" && ls *.so | sed 's/\.so$//' | grep -vxE 'libkernel_sys|libkernel_web|libScePosixForWebKit|.*_stub_weak'))
    lacks=$(defined libkernel_sys libkernel_web libScePosixForWebKit)
    bad=$( (comm -12 <(sort -u "$imports") <(echo "$lacks") | comm -23 - <(echo "$gets"); grep -xE 'f?statv?fs|getfsstat|getmntinfo' "$imports") | sort -u || true)
    [[ -z $bad ]] || { echo "the eboot imports what a title doesn't get: $(echo $bad)" >&2; return 1; }
}

step_radv() {
    src_pw; src_vulkan; src_venv
    cd "$B/PS5_Vulkan"
    bash tools/setup-native-dependencies.sh
    # PC5's patches/radv on the pinned revision, as a commit with fixed names and a fixed
    # date, made without a checkout: the same patches always give the same revision, which
    # build-radv.sh (a copy naming it) builds incrementally.
    local pin rev tree index=$B/radv-pc5.index
    pin=$(pin tools/build-radv.sh mesa_revision)
    rm -f "$index"
    GIT_INDEX_FILE=$index git -C "$B/PS5_Mesa" read-tree "$pin"
    GIT_INDEX_FILE=$index git -C "$B/PS5_Mesa" apply --cached "$here"/patches/radv/*.patch
    tree=$(GIT_INDEX_FILE=$index git -C "$B/PS5_Mesa" write-tree)
    rm -f "$index"
    rev=$(GIT_AUTHOR_NAME=PC5 GIT_AUTHOR_EMAIL=pc5@invalid GIT_AUTHOR_DATE=2026-10-25T00:00:00Z \
          GIT_COMMITTER_NAME=PC5 GIT_COMMITTER_EMAIL=pc5@invalid GIT_COMMITTER_DATE=2026-10-25T00:00:00Z \
          git -C "$B/PS5_Mesa" commit-tree "$tree" -p "$pin" -m "PC5's patches/radv")
    sed "s/^mesa_revision=.*/mesa_revision=$rev/" tools/build-radv.sh > tools/build-radv-pc5.sh
    echo "RADV: $pin with patches/radv is $rev"
    MESON=$B/venv/bin/meson PATH="$B/venv/bin:$PATH" bash tools/build-radv-pc5.sh release
}

step_tools() {
    src_pw; src_wine
    stage "Wine's tools"
    cd "$PW"
    tools/build_host_wine.sh --source "$PW/.deps/wine/source" --work "$PW/.deps/wine-tools" --jobs "$jobs" --tools-only
}

step_hostwine() {
    src_pw; src_wine
    cd "$PW"
    local build=$PW/.deps/wine-host/build source=$PW/.deps/wine-host/source
    stage "Wine's Windows side"
    tools/build_host_wine.sh --source "$PW/.deps/wine/source" --work "$PW/.deps/wine-host" --jobs "$jobs"
    stage "the 32-bit CPUs' Windows halves"
    # The 32-bit CPUs' Windows halves, built against this Wine. The copy is made without
    # .git, and both scripts check the Wine commit there: it points at the checkout it came
    # from, which is at WINE_COMMIT.
    [[ -e $source/.git ]] || echo "gitdir: $PW/.deps/wine/source/.git" > "$source/.git"
    tools/build_wowprospero.sh --build "$build" --source "$source" --jobs "$jobs"
    rm -rf "$PW/.deps/wow64native/x86_64-windows" "$PW/.deps/wow64native/x86_64-unix"
    tools/build_wow64native.sh --source "$source" --build "$build" --output "$PW/.deps/wow64native"
}

step_wineps5() {
    stage "its sources"
    src_pw; src_wine; src_ffmpeg; src_framegen; src_vulkan; src_venv
    stage "the payload SDK"
    need_sdk; need_native_tool
    need tools; need radv
    cd "$PW"
    local sdk=$PW/.deps/ps5-native-app-boilerplate/.deps/native/ps5-payload-sdk work=$PW/.deps/wine-ps5
    stage "Wine for the PS5"
    PATH="$B/venv/bin:$PATH" tools/build_wine_ps5.sh --source "$PW/.deps/wine/source" \
        --host-tools "$PW/.deps/wine-tools/build" --prx-foundation "$B/prx-foundation" \
        --radv "$B/PS5_Vulkan" --ffmpeg-source "$PW/.deps/ffmpeg/source" --framegen "$B/ps5-framegen" --jobs "$jobs" ||
        { # what make stopped on, last, where the log's end shows it
          echo "-- the errors in .deps/wine-ps5/make.log:"
          grep -E "error:|\] Error [0-9]|No rule to make|undefined (symbol|reference)" "$work/make.log" | tail -25
          return 1; }
    # The link is skipped, not failed, when something it needs is missing: check what it made.
    for module in ntdll win32u winevulkan libvulkan winedmo winegstreamer; do
        [[ -f $work/prx/sce_module/$module.prx ]] ||
            { echo "no $module.prx: see the prx section of .deps/wine-ps5/report.json" >&2; return 1; }
    done
    grep -q '"framegen": "[0-9a-f]\{40\}"' "$work/report.json" ||
        { echo "libvulkan.prx was linked without frame generation: see .deps/wine-ps5/prx/libvulkan.link.log" >&2; return 1; }
    ls "$work/prx/sce_module"
    # The native WoW64 CPU's PS5 module, against this Wine, with the title's import stubs less
    # the two WebKit libraries it must not import.
    [[ -e $work/source/.git ]] || echo "gitdir: $PW/.deps/wine/source/.git" > "$work/source/.git"
    local stubs=$B/title-stubs
    rm -rf "$stubs" && mkdir -p "$stubs"
    find "$sdk/target/lib" -maxdepth 1 -name '*.so' ! -name libkernel_web.so ! -name libScePosixForWebKit.so \
        -exec cp {} "$stubs/" \;
    stage "the native CPU's PS5 module"
    rm -rf "$PW/.deps/wow64native/ps5"
    tools/build_wow64native.sh --ps5-only --output "$PW/.deps/wow64native" --ps5-sdk "$sdk" --ps5-wine "$work" \
        --ps5-ntdll "$work/prx/ntdll.shared.elf" --ps5-foundation "$B/prx-foundation" --ps5-stubs "$stubs"
}

step_dxvk() {
    # 64- and 32-bit, each into $B/dxvk/x64 and $B/dxvk/x32, as DXVK's own releases lay them out.
    src_pw; src_dxvk; src_venv
    local arch out=$B/dxvk
    rm -rf "$out"
    for arch in 64 32; do
        stage "DXVK $arch-bit ($([[ $arch == 64 ]] && echo 1 || echo 2) of 2)"
        rm -rf "$B/dxvk-build.w$arch"
        (cd "$B/dxvk-source" && "$B/venv/bin/meson" setup --cross-file "build-win$arch.txt" --buildtype release --strip \
            --prefix "$out" --bindir "x$arch" --libdir "x$arch" -Dbuild_id=false "$B/dxvk-build.w$arch")
        ninja -C "$B/dxvk-build.w$arch" -j"$jobs" install
    done
    for arch in 64 32; do
        for dll in d3d8 d3d9 d3d10core d3d11 dxgi; do
            [[ -f $out/x$arch/$dll.dll ]] || { echo "no x$arch/$dll.dll" >&2; return 1; }
        done
    done
    rm -f "$out"/x*/*.dll.a
    cp "$B/dxvk-source/LICENSE" "$out/LICENSE"
    echo "$DXVK_NAME" > "$out/NAME"
    ls -l "$out"/x64 "$out"/x32
}

step_vkd3d() {
    # 64- and 32-bit into $B/vkd3d/vkd3d-proton-<version>.tar.zst, laid out as VKD3D-Proton's own
    # releases are (its package-release.sh: x64/ and x86/).
    src_pw; src_vkd3d; src_venv
    local arch out=$B/vkd3d stage
    rm -rf "$out"
    stage=$out/vkd3d-proton-$VKD3D_VERSION
    for arch in 64 86; do
        stage "VKD3D-Proton $([[ $arch == 64 ]] && echo '64-bit (1' || echo '32-bit (2') of 2)"
        rm -rf "$B/vkd3d-build.x$arch"
        (cd "$B/vkd3d-proton-source" && "$B/venv/bin/meson" setup --cross-file "build-win${arch/86/32}.txt" \
            --buildtype release --strip --prefix "$stage" --bindir "x$arch" --libdir "x$arch" "$B/vkd3d-build.x$arch")
        ninja -C "$B/vkd3d-build.x$arch" -j"$jobs" install
        find "$stage/x$arch" -type f ! -name '*.dll' -delete
        for dll in d3d12 d3d12core; do
            [[ -f $stage/x$arch/$dll.dll ]] || { echo "no x$arch/$dll.dll" >&2; return 1; }
        done
    done
    tar -C "$out" --sort=name --owner=0 --group=0 --numeric-owner --mtime=2026-10-22 \
        -c "vkd3d-proton-$VKD3D_VERSION" | zstd -q -19 -T0 -o "$out/vkd3d-proton-$VKD3D_VERSION.tar.zst"
    rm -rf "$stage"
    echo "$VKD3D_NAME" > "$out/NAME"
    ls -l "$out"
}

step_zink() {
    src_pw; src_zink; src_venv
    cd "$PW"
    local llvm
    llvm=$(value tools/build_mesa_zink.sh LLVM_MINGW_VERSION)
    # Its compiler, as tools/build_mesa_zink.sh pins it.
    download "$B/downloads/llvm-mingw-$llvm-ubuntu-22.04-x86_64.tar.xz" \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/${llvm%-ucrt}/llvm-mingw-$llvm-ubuntu-22.04-x86_64.tar.xz" \
        "$(value tools/build_mesa_zink.sh LLVM_MINGW_SHA256)"
    # Its own memory note: the release link wants about 8 GiB a job.
    MESON=$B/venv/bin/meson PATH="$B/venv/bin:$PATH" bash tools/build_mesa_zink.sh --work "$B/mesa-zink" \
        --mesa "$B/mesa-zink-source" --llvm-mingw "$B/downloads/llvm-mingw-$llvm-ubuntu-22.04-x86_64.tar.xz" \
        --jobs "${PC5_ZINK_JOBS:-2}"
    ls "$B/mesa-zink/artifacts"/*-windows
}

step_package() {
    src_pw
    stage "putting the pieces together"
    need title; need hostwine; need wineps5; need zink
    cd "$PW"
    rm -rf "$B/package"
    tools/package_release.sh --title "dist/$TITLE" --wine-ps5 .deps/wine-ps5 \
        --host-wine .deps/wine-host/install/usr \
        --cpu-dll .deps/wine-host/build/dlls/wowprospero/x86_64-windows/wowprospero.dll \
        --native-cpu .deps/wow64native --mesa-zink "$B/mesa-zink/artifacts" \
        --lapy-release build/native/lapy-helper-release.json --out "$B/package"
}

step_app() {
    src_pw; src_forwarder; src_mono; src_mono_source; src_fonts; src_gbe
    need hostwine; need dxvk; need vkd3d
    local app=$release/$TITLE
    [[ -d $B/package/PPSA99995 ]] || { echo "build.sh: package is not built: run tools/build.sh package first" >&2; return 1; }
    stage "copying the package"
    rm -rf "$app" "$release/$TITLE.zip"
    cp -a "$B/package/PPSA99995" "$app"
    # Built from a local FreeType copy (PROSPERO_FREETYPE_TARBALL): SOURCES.txt names it, as PARTS.txt does.
    if [[ -n ${PROSPERO_FREETYPE_TARBALL:-} ]]; then
        python3 - "$app/SOURCES.txt" "$(basename "$PROSPERO_FREETYPE_TARBALL")" \
            "$(sha256sum "$PROSPERO_FREETYPE_TARBALL" | cut -c1-64)" <<'EOF_SOURCES'
import re, sys
path, name, sha = sys.argv[1:]
text = open(path).read()
text, count = re.subn(r"^(FreeType \S+)  \S+\n  SHA-256 [0-9a-f]{64}\n",
                      lambda m: f"{m.group(1)}  from {name}, not the release archive\n  SHA-256 {sha}\n", text, flags=re.M)
if count != 1:
    sys.exit("build.sh: SOURCES.txt has no FreeType lines to correct")
open(path, "w").write(text)
EOF_SOURCES
    fi
    stage "art, the tiles' program, fonts and sounds"
    cp "$here/sce_sys/param.json" "$here/art/icon0.png" "$here/art/pic0.png" "$app/sce_sys/"
    # The home screen's background while PC5's tile is chosen (pic0) and while PC5 starts
    # (pic1): the background at 4K as BC7 in a DDS (tools/make-dds.c), as the shell reads it.
    mkdir -p "$B/art"
    python3 "$here/art/make-art.py" "$B/art" --background-4k "$B/art/background-4k.ppm" >/dev/null
    cc -std=c11 -O2 -o "$B/art/make-dds" "$here/tools/make-dds.c" "$here/title/pc5_dds.c" -lm
    "$B/art/make-dds" "$B/art/background-4k.ppm" "$app/sce_sys/pic0.dds" >/dev/null
    cp "$app/sce_sys/pic0.dds" "$app/sce_sys/pic1.dds"
    # The music the home screen plays while PC5's tile is chosen (ATRAC9), when there is one.
    if [[ -f $here/art/snd0.at9 ]]; then cp "$here/art/snd0.at9" "$app/sce_sys/"; fi
    mkdir -p "$app/pc5"
    # The home screen tiles' program, which PC5 copies into each tile (pc5_home.c), and its licence.
    mkdir -p "$app/pc5/forwarder/sce_module" "$app/LICENSES/ps5-forwarder-format"
    cp "$B/ps5-forwarder-format/template/eboot.bin" "$app/pc5/forwarder/"
    cp "$B/ps5-forwarder-format/template/sce_module/libc.prx" "$app/pc5/forwarder/sce_module/"
    chmod 755 "$app/pc5/forwarder/eboot.bin" "$app/pc5/forwarder/sce_module/libc.prx"
    cp "$B/ps5-forwarder-format/LICENSE" "$app/LICENSES/ps5-forwarder-format/LICENSE"
    mkdir -p "$app/LICENSES/qrcodegen"
    cp "$here/title/third_party/qrcodegen/LICENSE" "$app/LICENSES/qrcodegen/LICENSE"
    # PC5's text faces, with the letters of every language in lang/, and Chinese, Japanese and
    # Korean's own (fonts-<code>.bin, only the language in use is loaded); the languages' text,
    # which players may edit (pc5_i18n.h).
    python3 "$here/tools/make-font.py" "$app/pc5/fonts.bin" --lang "$here/lang" --cjk "$B/downloads/DroidSansFallbackFull.ttf" \
        --korean "$B/downloads/NotoSansKR-Regular.otf" "$B/downloads/NotoSansKR-Bold.otf"
    mkdir -p "$app/pc5/lang" "$app/LICENSES/noto-sans-kr"
    cp "$here"/lang/*.po "$app/pc5/lang/"
    cp "$B/downloads/NotoSansCJK-LICENSE" "$app/LICENSES/noto-sans-kr/LICENSE"
    # The startup sounds to choose from, on the controller's speaker when PC5 is opened (pc5_sound.h).
    mkdir -p "$app/pc5/sounds"
    cp "$here"/art/sounds/*.wav "$app/pc5/sounds/"
    # PC5's own pictures: its logo, the controller, the backdrop and the cover a game without one gets (pc5_ui.c).
    mkdir -p "$app/pc5/ui"
    cp "$here"/art/ui/* "$app/pc5/ui/"
    # Mozilla's roots as Debian ships them, for the covers' HTTPS (the console's own come first).
    for root in /usr/share/ca-certificates/mozilla/*.crt; do cat "$root"; echo; done > "$app/pc5/ca-certificates.crt"
    # The same roots for the games, where Wine's crypt32 reads them (prospero-win's Wine patch 0878):
    # without them a game's own HTTPS (an Unreal game's login, its servers) trusts nothing.
    mkdir -p "$app/win/wine/share/wine"
    cp "$app/pc5/ca-certificates.crt" "$app/win/wine/share/wine/ca-certificates.crt"
    python3 "$here/tools/make-prefix.py" --pw "$PW" --wine "$PW/.deps/wine-host/install/usr/bin/wine" \
        --cpu-dll "$PW/.deps/wine-host/build/dlls/wowprospero/x86_64-windows/wowprospero.dll" \
        --vkd3d "$B/vkd3d/vkd3d-proton-$VKD3D_VERSION.tar.zst" --dxvk "$B/dxvk" \
        --mono "$B/downloads/wine-mono-$MONO_VERSION-x86.msi" --mono-version "$MONO_VERSION" --out "$app"
    # The DXVK the template has, which PC5 gives games installed with an earlier one (pc5_game.c).
    # DXVK and VKD3D-Proton by name: a game installed before either changed gets both again.
    printf '%s vkd3d-proton-%s\n' "$(cat "$B/dxvk/NAME")" "$(cat "$B/vkd3d/NAME")" > "$app/pc5/dxvk.txt"
    mkdir -p "$app/LICENSES/dxvk" "$app/LICENSES/wine-mono"
    cp "$B/dxvk/LICENSE" "$app/LICENSES/dxvk/LICENSE"
    cp "$B/downloads/wine-mono-$MONO_VERSION-COPYING" "$app/LICENSES/wine-mono/COPYING"
    # The fonts, beside Wine's own: Wine loads every font in that folder for every game.
    local fonts=$app/win/wine/share/wine/fonts unpacked=$B/fonts-liberation
    rm -rf "$unpacked" && mkdir -p "$unpacked"
    dpkg-deb -x "$B/downloads/fonts-liberation_$LIBERATION_VERSION-3_all.deb" "$unpacked"
    [[ $(ls "$unpacked"/usr/share/fonts/truetype/liberation/Liberation*.ttf | wc -l) == 12 ]] ||
        { echo "build.sh: fonts-liberation $LIBERATION_VERSION doesn't hold Liberation's 12 fonts" >&2; return 1; }
    cp "$unpacked"/usr/share/fonts/truetype/liberation/Liberation*.ttf "$B/downloads/DroidSansFallbackFull.ttf" "$fonts/"
    rm -rf "$unpacked"
    mkdir -p "$app/LICENSES/liberation-fonts" "$app/LICENSES/droid-sans-fallback"
    cp "$B/downloads/liberation-fonts-$LIBERATION_VERSION-LICENSE" "$app/LICENSES/liberation-fonts/LICENSE"
    cp "$B/downloads/DroidSansFallbackFull-NOTICE" "$app/LICENSES/droid-sans-fallback/NOTICE"
    # The Steam emulator: gbe_fork's regular (not experimental) steam_api DLLs, as its release
    # has them, and its licence.
    stage "the Steam emulator"
    rm -rf "$B/gbe" && mkdir -p "$B/gbe" "$app/pc5/steam" "$app/LICENSES/gbe_fork"
    7z x -y -o"$B/gbe" "$B/downloads/gbe_fork-$GBE_RELEASE-emu-win-release-vs22.7z" \
        release/regular/x64/steam_api64.dll release/regular/x86/steam_api.dll >/dev/null
    cp "$B/gbe/release/regular/x64/steam_api64.dll" "$B/gbe/release/regular/x86/steam_api.dll" "$app/pc5/steam/"
    { echo "$GBE_DLL64_SHA256  $app/pc5/steam/steam_api64.dll"; echo "$GBE_DLL32_SHA256  $app/pc5/steam/steam_api.dll"; } |
        sha256sum --check --status || { echo "build.sh: gbe_fork's DLLs aren't its $GBE_RELEASE release's" >&2; return 1; }
    cp "$B/downloads/gbe_fork-$GBE_RELEASE-LICENSE" "$app/LICENSES/gbe_fork/LICENSE"
    rm -rf "$B/gbe"
    # XAudio2 and XACT come from the PS5 Wine build (its FAudio fix) with their debug data in,
    # five times their size: stripped as Wine's own install strips its modules. Wine's
    # builtin mark (the DOS stub's "Wine builtin DLL") stays.
    stage "stripping XAudio2 and XACT"
    local dll format
    while IFS= read -r -d '' dll; do
        format=$(x86_64-w64-mingw32-objdump -f "$dll" | sed -n 's/.*file format //p')
        case $format in
            pei-x86-64) x86_64-w64-mingw32-strip "$dll" ;;
            pei-i386) i686-w64-mingw32-strip "$dll" ;;
            *) echo "build.sh: $dll is $format, not a PE module" >&2; return 1 ;;
        esac
        [[ $(head -c 80 "$dll" | tail -c 16) == "Wine builtin DLL" ]] ||
            { echo "build.sh: $dll lost Wine's builtin mark" >&2; return 1; }
    done < <(find "$app" -type f \( -name 'xaudio2_*.dll' -o -name 'xactengine*.dll' \) -print0)
    # The tiles PC5 makes as apps of their own change the title ID in a copy of lapy.elf
    # (title/pc5_tile.c): it must be where PC5 looks for it.
    python3 "$here/tools/lapy-title.py" "$app/lapy.elf" PPSA99437
    stage "zipping the app"
    python3 "$here/tools/make-app.py" "$app" "$release/$TITLE.zip" --repo "$here" --pw "$PW"
    # Wine Mono's source (300 MB) is released beside the app, as it came: a file of its own.
    ln -f "$B/downloads/wine-mono-$MONO_VERSION-src.tar.xz" "$release/" 2>/dev/null ||
        cp "$B/downloads/wine-mono-$MONO_VERSION-src.tar.xz" "$release/"
}

step_bundle() {
    # Every part at the commit it was built from, as the build fetched it, in one archive.
    step_sources
    need radv
    local stage=$B/bundle/PPSA11018-sources out=$release/$TITLE-sources.tar.xz zink=$PW/tools/build_mesa_zink.sh
    rm -rf "$B/bundle" && mkdir -p "$stage"
    part() { # name repo commit [what]
        mkdir -p "$stage/$1"
        git -C "$2" archive --format=tar "$3" | tar -x -C "$stage/$1"
        printf '%-28s %s at %s\n' "$1/" "${4:-$(git -C "$2" remote get-url origin 2>/dev/null || echo "$2")}" "$3" >> "$stage/PARTS.txt"
    }
    printf 'The source of PC5 (%s) as built: each folder below is one project at the commit used.\n\n' "$TITLE" \
        > "$stage/PARTS.txt"
    if [[ -d $here/.git ]]; then
        part PC5 "$here" HEAD "https://github.com/bornaradusin/PC5"
    else
        mkdir -p "$stage/PC5"
        tar -C "$here" --exclude=./.build --exclude=./release -cf - . | tar -x -C "$stage/PC5"
        printf '%-28s %s\n' "PC5/" "https://github.com/bornaradusin/PC5 (this build's own folder)" >> "$stage/PARTS.txt"
    fi
    part prospero-win "$PW" HEAD "prospero-win v0.2.0 with PC5's patch and title/ (git log names them)"
    part wine "$PW/.deps/wine/source" "$(wine_commit)" "$WINE_URL"
    part ps5-native-app-boilerplate "$PW/.deps/ps5-native-app-boilerplate" "$(pin "$PW/tools/build_native.sh" pin)" "$BOILERPLATE_URL"
    part prx-foundation "$B/prx-foundation" "$(pin "$PW/tools/build_wine_ps5.sh" MODULE_EXPORTS_COMMIT)" "$BOILERPLATE_URL"
    part ffmpeg "$PW/.deps/ffmpeg/source" "$(pin "$PW/tools/build_ffmpeg_ps5.sh" FFMPEG_COMMIT)" \
        "$(value "$PW/tools/build_ffmpeg_ps5.sh" FFMPEG_URL) (FFmpeg $(value "$PW/tools/build_ffmpeg_ps5.sh" FFMPEG_VERSION), its LGPL parts built: winedmo.prx and winegstreamer.prx)"
    part ps5-framegen "$B/ps5-framegen" "$(pin "$PW/tools/link_radv_prx.sh" FRAMEGEN_COMMIT)" \
        "$(value "$PW/tools/link_radv_prx.sh" FRAMEGEN_URL) (its interpolator, in libvulkan.prx for Frame generation)"
    part PS5_Vulkan "$B/PS5_Vulkan" "$VULKAN_COMMIT" "$VULKAN_URL"
    part PS5_Mesa-radv "$B/PS5_Mesa" "$(pin "$B/PS5_Vulkan/tools/build-radv.sh" mesa_revision)" \
        "$MESA_URL (RADV, built with PC5's patches/radv applied)"
    part PS5_Mesa-zink "$B/mesa-zink-source" "$(pin "$zink" MESA_COMMIT)" "$MESA_URL"
    part PS5_PayloadSDK "$B/PS5_PayloadSDK" "$(pin "$B/PS5_Vulkan/tools/setup-native-dependencies.sh" sdk_revision)" "$SDK_FORK_URL"
    part PS5-Lapy-JB-Daemon "$B/lapy" "$LAPY_COMMIT" "$LAPY_URL"
    part ps5-forwarder-format "$B/ps5-forwarder-format" "$FORWARDER_COMMIT" \
        "$FORWARDER_URL (the home screen tiles' program and psfwd)"
    part QR-Code-generator "$B/qrcodegen" "$QRCODEGEN_COMMIT" "$QRCODEGEN_URL (Report on GitHub's QR codes)"
    part gbe_fork "$B/gbe_fork" "$GBE_COMMIT" \
        "$GBE_SOURCE_URL (tag $GBE_RELEASE; the app's pc5/steam is its release build, unchanged)"
    mkdir -p "$stage/vkd3d-proton"
    tar -C "$B/vkd3d-proton-source" --exclude=.git -cf - . | tar -x -C "$stage/vkd3d-proton"
    printf '%-28s %s at %s (tag v%s), with its submodules and PC5'"'"'s patches/vkd3d-proton applied\n' \
        "vkd3d-proton/" "$VKD3D_SOURCE_URL" "$VKD3D_SOURCE_COMMIT" "$VKD3D_VERSION" >> "$stage/PARTS.txt"
    mkdir -p "$stage/freetype"
    cp -a "$B/freetype-src/." "$stage/freetype/"
    local ft_from="its release archive"
    # A local copy in place of the release archive (PROSPERO_FREETYPE_TARBALL): named as it is.
    [[ -n ${PROSPERO_FREETYPE_TARBALL:-} ]] &&
        ft_from="from $(basename "$PROSPERO_FREETYPE_TARBALL"), SHA-256 $(sha256sum "$PROSPERO_FREETYPE_TARBALL" | cut -c1-64), not the release archive"
    printf '%-28s %s\n' "freetype/" "FreeType $(sed -n 's/^FREETYPE_VERSION=//p' "$PW/tools/build_wine_ps5.sh") ($ft_from; libfreetype.prx was built from it)" >> "$stage/PARTS.txt"
    mkdir -p "$stage/zlib"
    cp -a "$B/PS5_Vulkan/.deps/work/radv-src/subprojects/"zlib-*/. "$stage/zlib/"
    printf '%-28s %s\n' "zlib/" "zlib, the subproject RADV's shader cache was built with" >> "$stage/PARTS.txt"
    cp "$B/downloads/mbedtls-$MBEDTLS_VERSION.tar.bz2" "$stage/"
    printf '%-28s %s (the source of the app'"'"'s pc5/mono, Wine Mono %s: released beside this archive)\n' \
        "wine-mono-$MONO_VERSION-src.tar.xz" "$MONO_SOURCE_URL" "$MONO_VERSION" >> "$stage/PARTS.txt"
    printf '%-28s %s\n' "mbedtls-$MBEDTLS_VERSION.tar.bz2" "$MBEDTLS_URL" >> "$stage/PARTS.txt"
    mkdir -p "$stage/dxvk"
    tar -C "$B/dxvk-source" --exclude=.git -cf - . | tar -x -C "$stage/dxvk"
    printf '%-28s %s at %s (tag v2.6.2-prospero3), with its submodules and PC5'"'"'s patches/dxvk applied\n' \
        "dxvk/" "$DXVK_URL" "$DXVK_COMMIT" >> "$stage/PARTS.txt"
    printf 'PC5/README.md says how to build the app from these with PC5/tools/build.sh.\n' >> "$stage/PARTS.txt"
    rm -f "$out" "$out.part"
    # xz on every CPU, in the 24 MiB blocks it used on two (three times -6's dictionary).
    tar -C "$B/bundle" --sort=name --owner=0 --group=0 --numeric-owner --mtime=2026-10-22 -cf - PPSA11018-sources |
        xz -6 -T0 --block-size=$((24 << 20)) > "$out.part"
    mv "$out.part" "$out"
    rm -rf "$B/bundle"
    ls -l "$out"
}

# ---- keys, paths and the work kept --------------------------------------------------

# The commands of a piece, as its key takes them: the names of its stages are only what its
# line a minute says, so changing them builds nothing again.
commands() { declare -f "$@" | grep -v '^ *stage '; }
# A file's or a folder's id in prospero-win as patched (git's own hash of its content).
pwid() { git -C "$PW" rev-parse "HEAD:$1"; }
# The patches in one of PC5's patch folders.
patches_id() { cat "$here/patches/$1"/*.patch | sha256sum | cut -c1-64; }
key_of() { # piece
    local hash
    case $1 in
    # Zink as GitHub's build has kept it since 1.6: Mesa and llvm-mingw as prospero-win pins them.
    zink) echo "zink-$(cat "$PW/tools/build_mesa_zink.sh" "$PW/tools/mesa_zink_manifest.py" | sha256sum | cut -c1-16)"; return ;;
    title) hash=$(printf '%s\n' "$(git -C "$PW" rev-parse 'HEAD^{tree}')" "$BOILERPLATE_URL" "$LAPY_URL" "$LAPY_COMMIT" \
                  "$MBEDTLS_VERSION" "$(commands step_title need_sdk)") ;;
    radv) hash=$(printf '%s\n' "$VULKAN_COMMIT" "$(patches_id radv)" "$(pwid tools/mesa_zink_requirements.txt)" \
                 "$(commands step_radv)") ;;
    tools) hash=$(printf '%s\n' "$WINE_URL" "$(pwid wine)" "$(pwid tools)" "$(commands step_tools)") ;;
    hostwine) hash=$(printf '%s\n' "$WINE_URL" "$(pwid wine)" "$(pwid tools)" "$(commands step_hostwine)") ;;
    wineps5) hash=$(printf '%s\n' "$WINE_URL" "$(pwid wine)" "$(pwid tools)" "$(key_of radv)" "$(key_of tools)" \
                    "$BOILERPLATE_URL" "$(commands step_wineps5 need_sdk need_native_tool)") ;;
    dxvk) hash=$(printf '%s\n' "$DXVK_URL" "$DXVK_COMMIT" "$DXVK_NAME" "$(patches_id dxvk)" \
                 "$(pwid tools/mesa_zink_requirements.txt)" "$(commands step_dxvk)") ;;
    vkd3d) hash=$(printf '%s\n' "$VKD3D_SOURCE_URL" "$VKD3D_SOURCE_COMMIT" "$VKD3D_VERSION" "$VKD3D_NAME" \
                  "$(patches_id vkd3d-proton)" "$(pwid tools/mesa_zink_requirements.txt)" "$(commands step_vkd3d)") ;;
    *) echo "build.sh: $1 has no key (pieces with one: $KEYED)" >&2; return 2 ;;
    esac
    echo "$1-$(printf '%s\n' "$hash" "$(cat "$here/tools/packages.txt")" | sha256sum | cut -c1-16)"
}
# What a piece leaves for the pieces after it: GitHub's build keeps these under its key.
paths_of() { # piece
    local w=$PW/.deps/wine-ps5
    case $1 in
    title) printf '%s\n' "$PW/dist/$TITLE" "$PW/build/native/lapy-helper-release.json" ;;
    radv) printf '%s\n' "$B/PS5_Vulkan/.deps/native" "$B/PS5_Vulkan/.deps/work/radv-src/subprojects/zlib-*" ;;
    tools) printf '%s\n' "$PW/.deps/wine-tools/build" ;;
    hostwine) printf '%s\n' "$PW/.deps/wine-host/install" "$PW/.deps/wine-host/build/dlls/wowprospero" \
                  "$PW/.deps/wow64native/x86_64-windows" "$PW/.deps/wow64native/x86_64-unix" ;;
    # The modules, and the licence texts packaging takes from Wine's and FreeType's sources.
    wineps5) printf '%s\n' "$w/prx" "$w/pe" "$w/report.json" "$w/source/LICENSE" "$w/source/COPYING.LIB" \
                 "$w/source/AUTHORS" "$w/source/NOTICES.md" "$w/source/libs/*/LICENSE*" "$w/source/libs/*/COPYING*" \
                 "$w/source/libs/*/COPYRIGHT*" "$w/freetype/src/LICENSE.TXT" "$w/freetype/src/docs/FTL.TXT" \
                 "$PW/.deps/wow64native/ps5" ;;
    dxvk) printf '%s\n' "$B/dxvk" ;;
    vkd3d) printf '%s\n' "$B/vkd3d" ;;
    zink) printf '%s\n' "$B/mesa-zink/artifacts" ;;
    *) echo "build.sh: $1 leaves nothing kept (pieces that do: $KEYED)" >&2; return 2 ;;
    esac
}
# What a piece keeps to build again incrementally: Wine's trees and objects, which make
# rebuilds only where a change of the series reaches (the scripts keep a file that is the
# same as before at its old time). Wine's tools too: everything the PS5 Wine generates and
# links depends on them, so a tool made again, even the same, would make it all again.
work_of() { # piece
    case $1 in
    tools) printf '%s\n' "$PW/.deps/wine-tools/source" "$PW/.deps/wine-tools/build" ;;
    hostwine) printf '%s\n' "$PW/.deps/wine-host/source" "$PW/.deps/wine-host/build" ;;
    wineps5) printf '%s\n' "$PW/.deps/wine-ps5/source" "$PW/.deps/wine-ps5/build" "$PW/.deps/wine-ps5/ffmpeg" \
                 "$PW/.deps/wine-ps5/freetype" ;;
    *) ;;
    esac
}
# The compilers installed: work made with others is not built on.
toolchain() {
    { dpkg-query -W -f '${Package} ${Version}\n' 'gcc*' 'g++*' 'binutils*' 'clang*' 'lld*' 'llvm*' 'mingw-w64*' \
        2>/dev/null || true; } | awk 'NF == 2' | sort | sha256sum | cut -c1-16
}
# prospero-win as patched, quietly: the keys read it.
quiet_pw() {
    src_pw > "$B/logs/prospero-win.log" 2>&1 || { tail -25 "$B/logs/prospero-win.log" >&2; exit 1; }
}

case ${1:-} in
key) shift; quiet_pw; for piece; do key_of "$piece"; done; exit 0 ;;
paths) shift; for piece; do paths_of "$piece"; done; exit 0 ;;
work) shift; for piece; do work_of "$piece"; done; exit 0 ;;
toolchain) toolchain; exit 0 ;;
ci)
    quiet_pw
    for piece in $KEYED; do
        echo "$piece=$(key_of "$piece")"
        printf '%s_paths<<EOF_PATHS\n%s\nEOF_PATHS\n' "$piece" "$(paths_of "$piece")"
        printf '%s_work<<EOF_PATHS\n%s\nEOF_PATHS\n' "$piece" "$(work_of "$piece")"
    done
    echo "version=$(sed -n 's/^#define PC5_VERSION "\(.*\)"/\1/p' "$here/title/pc5.h")"
    exit 0 ;;
esac

# ---- the build ------------------------------------------------------------------------
steps=" ${*:-$STEPS} "
for s in $steps; do [[ " $STEPS " == *" $s "* ]] || { echo "unknown piece: $s (pieces: $STEPS; commands: $COMMANDS)" >&2; exit 2; }; done
want() { [[ $steps == *" $1 "* ]]; }
# The packages, but for fetching and archiving the sources, which need only git, curl and xz.
if [[ $steps != " sources " && $steps != " bundle " && $steps != " sources bundle " ]]; then
    missing=$(for package in $(cat "$here/tools/packages.txt"); do dpkg -s "$package" >/dev/null 2>&1 || echo "$package"; done)
    [[ -z $missing ]] || { echo "build.sh: missing packages: $(echo $missing). Install them with: apt-get install -y \$(cat tools/packages.txt)" >&2; exit 2; }
    # Clang 18 for every PS5 build: the payload SDK otherwise takes the newest LLVM it finds
    # (Debian 13's default is 19), and the boilerplate's wrapper wants 18.
    export LLVM_CONFIG=/usr/lib/llvm-18/bin/llvm-config PS5_CLANG=/usr/lib/llvm-18/bin/clang
    [[ -x $LLVM_CONFIG && -x $PS5_CLANG ]] || { echo "build.sh: no Clang 18 in /usr/lib/llvm-18 (packages clang-18, llvm-18)" >&2; exit 2; }
fi
mkdir -p "$release"

# What a piece is doing now, for its line a minute: the stage it named last (stage), and the
# count its build tool keeps where it keeps one: ninja's own, or the files Wine's make has
# compiled out of the objects its makefile names (the share only when it started from none).
stage() { echo "-- $*"; }
newer() { [[ -f $1 && $(stat -c %Y "$1") -ge $started_at ]]; }
wine_count() { # work dir
    local made total
    made=$(grep -c -- ' -c -o ' "$1/make.log" 2>/dev/null || true)
    total=$(grep -c '^[^ #][^ ]*\.o:' "$1/build/Makefile" 2>/dev/null || true)
    last=$(grep -- ' -c -o ' "$1/make.log" 2>/dev/null | tail -1 | sed -n 's/.* -c -o \([^ ]*\).*/\1/p' | cut -d/ -f1-2)
    if [[ ${fresh_build:-0} == 1 && ${total:-0} -gt 0 ]]; then
        printf 'compiling %6d of %d files (%d%%)  %s' "$made" "$total" $(( made * 100 / total )) "$last"
    else
        printf 'compiling %6d files  %s' "$made" "$last"
    fi
}
ninja_count() { # log...
    local log line
    log=$(ls -t "$@" 2>/dev/null | head -1)
    line=$(grep -ao '^\[[0-9]*/[0-9]*\]' "$log" 2>/dev/null | tail -1 | tr -d '[]')
    [[ -n $line ]] && printf 'ninja %d of %d (%d%%)' "${line%/*}" "${line#*/}" $(( ${line%/*} * 100 / ${line#*/} ))
}
doing() { # piece
    local log=$B/logs/$1.log now w
    now=$(grep -a '^-- ' "$log" 2>/dev/null | tail -1 | cut -c4-)
    case $1 in
    tools|hostwine)
        [[ $1 == tools ]] && w=$PW/.deps/wine-tools || w=$PW/.deps/wine-host
        if [[ $now != "Wine's tools" && $now != "Wine's Windows side" ]]; then :
        elif newer "$w/install.log"; then
            # make install's copies, then (its log quiet) build_host_wine.sh strips the modules.
            if (( $(date +%s) - $(stat -c %Y "$w/install.log") > 20 )); then now="stripping the modules"
            else now="installing  $(grep -c '^tools/install ' "$w/install.log") of $(grep -c "^$(printf '\t')tools/install " "$w/build/Makefile") copies"
            fi
        elif newer "$w/make.log"; then now=$(wine_count "$w")
        elif newer "$w/configure.log"; then now=configuring
        fi ;;
    wineps5)
        w=$PW/.deps/wine-ps5
        if [[ $now == "Wine for the PS5" ]]; then
            if newer "$w/make.log" && ! grep -q '\.prx: built=' "$log"; then now="Wine $(wine_count "$w")"
            elif grep -q '\.prx: built=' "$log"; then now="linking $(grep -a '\.prx: built=' "$log" | tail -1 | cut -d: -f1)"
            elif newer "$w/configure.log"; then now="Wine: configuring"
            elif newer "$w/ffmpeg/make.log"; then now="FFmpeg: compiling $(grep -c '^CC' "$w/ffmpeg/make.log") files"
            fi
        fi ;;
    radv)
        # build-radv.sh builds Mesa's shader compiler for this computer first, then RADV.
        w=$(ls -t "$B"/PS5_Vulkan/.deps/work/*.log 2>/dev/null | head -1)
        if [[ -n $w ]] && newer "$w"; then
            [[ $w == *clc* ]] && now="Mesa's shader compiler (1 of 2)" || now="RADV (2 of 2)"
            now="$now  $(ninja_count "$w")"
        fi ;;
    dxvk|vkd3d|zink) now="${now:-$1}  $(ninja_count "$log")" ;;
    esac
    echo "${now:-starting}"
}
# One piece: its output in its log, a line a minute on screen with what it is doing, and its
# log's end when it fails.
run() { # piece
    local step=$1 started=$SECONDS status=0 pid minutes=0
    started_at=$(date +%s) fresh_build=0
    # Wine for this computer from nothing: its makefile's objects are what it will compile.
    [[ $step != hostwine || -n $(find "$PW/.deps/wine-host/build" -name '*.o' -print -quit 2>/dev/null) ]] || fresh_build=1
    printf '==> %-9s started %s\n' "$step" "$(date +%H:%M)"
    # In a subshell of its own, so a failing command stops the piece (set -e) and a cd stays in it.
    ( set -euo pipefail; "step_$step" ) > "$B/logs/$step.log" 2>&1 &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        sleep 5
        if (( SECONDS - started >= (minutes + 1) * 60 )) && kill -0 "$pid" 2>/dev/null; then
            minutes=$(( (SECONDS - started) / 60 ))
            printf '    %3d min  %s\n' "$minutes" "$(doing "$step" | cut -c1-110)"
        fi
    done
    wait "$pid" || status=$?
    if (( status == 0 )); then
        printf '==> %-9s done (%d min)\n' "$step" $(( (SECONDS - started + 59) / 60 ))
    else
        printf '==> %-9s FAILED, the end of %s/logs/%s.log:\n' "$step" "$B" "$step"
        tail -25 "$B/logs/$step.log"
        exit 1
    fi
}
# Whether a piece was built here from the key it has now, with what it leaves still there.
current() { # piece key
    local path
    [[ $(cat "$B/stamps/$1" 2>/dev/null) == "$2" ]] || return 1
    for path in $(paths_of "$1"); do compgen -G "$path" >/dev/null || return 1; done
}

for s in $STEPS; do
    want "$s" || continue
    if [[ " $KEYED " == *" $s "* ]]; then
        quiet_pw
        key=$(key_of "$s")
        if current "$s" "$key"; then
            printf '==> %-9s up to date (%s)\n' "$s" "$key"
            continue
        fi
        rm -f "$B/stamps/$s"
        run "$s"
        echo "$key" > "$B/stamps/$s"
    else
        run "$s"
    fi
done
for made in "$TITLE.zip" "$TITLE-sources.tar.xz" "wine-mono-$MONO_VERSION-src.tar.xz"; do
    if { [[ $made != "$TITLE-sources.tar.xz" ]] && want app; } || { [[ $made == "$TITLE-sources.tar.xz" ]] && want bundle; }; then
        echo "==> $release/$made: $(du -h "$release/$made" | cut -f1), SHA-256 $(sha256sum "$release/$made" | cut -c1-16)…"
    fi
done
