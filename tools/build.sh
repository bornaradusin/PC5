#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Builds PC5 (PPSA11018) from source: prospero-win at its v0.2.0 release with this repo's
# patch and title/, its Wine, RADV and Mesa Zink for the console, VKD3D-Proton, and the app.
#
#   tools/build.sh [STEP...]
#
# Steps, in order (default: all):
#   sources   clone the pinned projects below into .build/, patch prospero-win, download
#             the pinned archives (llvm-mingw for Zink, VKD3D-Proton, mbedTLS)
#   title     the PS5 payload SDK, the Lapy helper for PPSA11018, mbedTLS for the console,
#             the eboot with PC5 in it
#   radv      RADV, the console's Vulkan driver (PS5_Vulkan, PS5_Mesa)
#   hostwine  Wine for this computer: the PE modules the console runs, and the Wine that
#             makes PC5's empty Windows template
#   wineps5   Wine's PS5 side (PRX modules)
#   cpu       wowprospero and the native WoW64 CPU (32-bit games)
#   dxvk      DXVK 2.6.2-prospero2 with patches/dxvk, 64- and 32-bit, for the Windows template
#   zink      Mesa's WGL/Zink, OpenGL for the games that draw with it
#   package   prospero-win's tools/package_release.sh, with Zink
#   app       release/PPSA11018/ (art, fonts, the Windows template) and release/PPSA11018.zip
#   bundle    release/PPSA11018-sources.tar.xz: the source of everything in the app, at the
#             commits it was built from (the GPL and LGPL ask for it beside the binaries)
#
# PC5_RELEASE=DIR puts the app there instead of release/ (on a shared build machine, say
# /mnt/media/builder/releases/PC5). PC5_ZINK_JOBS sets Zink's jobs (default 2).
#
# On Debian 13 or Ubuntu 24.04 (x86-64) with tools/packages.txt installed it runs here, as
# root or a user who can write the repo; anywhere else it runs itself inside
# tools/Dockerfile's image (Docker needed). Each step writes its log to
# .build/logs/<step>.log and prints one line. A step run again picks up where it was.
set -euo pipefail
here=$(realpath -- "$(dirname -- "$0")/..")
release=$(realpath -m -- "${PC5_RELEASE:-$here/release}")

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
# Read from the pinned projects themselves, after the sources step: Wine
# (build_wine_ps5.sh), the title's boilerplate (build_native.sh), the PRX
# boilerplate (build_wine_ps5.sh), PS5_Mesa for RADV (build-radv.sh) and for Zink
# (build_mesa_zink.sh, with its llvm-mingw), the SDK fork (setup-native-dependencies.sh).
VKD3D_VERSION=2.14.1                                      # pairs with DXVK 2.6.2's dxgi
VKD3D_URL=https://github.com/HansKristian-Work/vkd3d-proton/releases/download/v$VKD3D_VERSION/vkd3d-proton-$VKD3D_VERSION.tar.zst
VKD3D_SHA256=ac70ccfe01d610b51ca67a0a44e4f24f5da29c665facc0c2faf47e0818d23168
MBEDTLS_VERSION=3.6.7                                     # LTS: the covers' HTTPS
MBEDTLS_URL=https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VERSION/mbedtls-$MBEDTLS_VERSION.tar.bz2
MBEDTLS_SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
# DXVK: the release prospero-win's pw_install.py pins (2.6.2-prospero2), built here with
# patches/dxvk; app/pc5/dxvk.txt names the result, and PC5 gives installed games its DLLs.
DXVK_URL=https://github.com/mpereiraesaa/dxvk.git
DXVK_COMMIT=a6f6dcc1eaa67946482ebdded0d1d5dc13293133      # tag v2.6.2-prospero2
DXVK_NAME=2.6.2-prospero2-pc5.2
# The PS5 Forwarder Format: the home screen tiles' program (template/) and psfwd, which
# title/third_party/psfwd holds unchanged.
FORWARDER_URL=https://github.com/heydemoura/ps5-forwarder-format.git
FORWARDER_COMMIT=22e8762ad7dbd01e18bd8c0e95f86461fdf8aaf4
VKD3D_SOURCE_URL=https://github.com/HansKristian-Work/vkd3d-proton.git
VKD3D_SOURCE_COMMIT=0d66699b1b1e250ef19d793f2649d9288b0808ff   # tag v2.14.1, the release's build

TITLE=PPSA11018
STEPS="sources title radv hostwine wineps5 cpu dxvk zink package app bundle"

# ---- here on Debian 13 or Ubuntu 24.04, else in the build image ----------------------
supported() {
    local id version
    id=$(. /etc/os-release 2>/dev/null && echo "$ID") version=$(. /etc/os-release 2>/dev/null && echo "$VERSION_ID")
    [[ $(uname -m) == x86_64 && ( ($id == debian && $version == 13) || ($id == ubuntu && $version == 24.04) ) ]]
}
if [[ -z ${PC5_IN_IMAGE:-} ]] && ! supported; then
    command -v docker >/dev/null || { echo "build.sh: this is neither Debian 13 nor Ubuntu 24.04 (x86-64), and there is no Docker" >&2; exit 2; }
    docker build -q -t pc5-build "$here/tools" >/dev/null
    exec docker run --rm -v "$here:/PC5" -v "$release:$release" -w /PC5 -e PC5_IN_IMAGE=1 -e PC5_RELEASE="$release" \
        -e PC5_ZINK_JOBS="${PC5_ZINK_JOBS:-2}" pc5-build tools/build.sh "$@"
fi
missing=$(for package in $(cat "$here/tools/packages.txt"); do dpkg -s "$package" >/dev/null 2>&1 || echo "$package"; done)
[[ -z $missing ]] || { echo "build.sh: missing packages: $(echo $missing). Install them with: apt-get install -y \$(cat tools/packages.txt)" >&2; exit 2; }

export LC_ALL=C TZ=UTC
# Clang 18 for every PS5 build: the payload SDK otherwise takes the newest LLVM it finds
# (Debian 13's default is 19), and the boilerplate's wrapper wants 18.
export LLVM_CONFIG=/usr/lib/llvm-18/bin/llvm-config PS5_CLANG=/usr/lib/llvm-18/bin/clang
[[ -x $LLVM_CONFIG && -x $PS5_CLANG ]] || { echo "build.sh: no Clang 18 in /usr/lib/llvm-18 (packages clang-18, llvm-18)" >&2; exit 2; }
B=$here/.build
PW=$B/prospero-win
jobs=$(nproc)
mkdir -p "$B/logs" "$B/downloads" "$release"
steps=" ${*:-$STEPS} "
for s in $steps; do [[ " $STEPS " == *" $s "* ]] || { echo "unknown step: $s (steps: $STEPS)" >&2; exit 2; }; done
want() { [[ $steps == *" $1 "* ]]; }
pin() { sed -n "s/^$2=\\([0-9a-f]\\{40\\}\\).*/\\1/p" "$1" | head -1; }
value() { sed -n "s/^$2=\\([^ ]*\\).*/\\1/p" "$1" | head -1; }

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
# One step: its output in its log, one line on screen, the log's end when it fails.
run() { # step command...
    local step=$1 started=$SECONDS; shift
    local status=0
    printf '==> %-9s ' "$step"
    # In a subshell of its own, so a failing command stops the step (set -e) and a cd stays in it.
    set +e
    ( set -euo pipefail; "$@" ) > "$B/logs/$step.log" 2>&1
    status=$?
    set -e
    if (( status == 0 )); then
        printf 'done (%d min)\n' $(( (SECONDS - started + 59) / 60 ))
    else
        printf 'FAILED, the end of .build/logs/%s.log:\n' "$step"
        tail -25 "$B/logs/$step.log"
        exit 1
    fi
}

# ---- the steps ---------------------------------------------------------------------

step_sources() {
    fetch "$PW" "$PW_URL" "$PW_COMMIT"
    # prospero-win v0.2.0 + patches/prospero-win + title/: committed with fixed names
    # and dates, so the same repo gives the same prospero-win commit (SOURCES.txt names it).
    local id=(-c user.name="PC5 build" -c user.email="pc5@invalid")
    git -C "$PW" reset -q --hard "$PW_COMMIT"
    git -C "$PW" clean -q -fd
    git -C "$PW" "${id[@]}" am -q --committer-date-is-author-date "$here"/patches/prospero-win/*.patch
    cp "$here"/title/pc5*.c "$here"/title/pc5*.h "$PW/native/"
    mkdir -p "$PW/native/third_party"
    cp "$here/title/third_party/stb_image.h" "$here/title/third_party/stb_image_write.h" "$PW/native/third_party/"
    cp -r "$here/title/third_party/psfwd" "$PW/native/third_party/"
    git -C "$PW" add native/pc5* native/third_party
    GIT_AUTHOR_DATE="2026-10-22T17:00:00+0100" GIT_COMMITTER_DATE="2026-10-22T17:00:00+0100" \
        git -C "$PW" "${id[@]}" commit -q -m "PC5: its screens (title/ of the PC5 repo)"
    git -C "$PW" log --oneline -3

    fetch "$PW/.deps/wine/source" "$WINE_URL" "$(sed -n 's/^WINE_COMMIT=//p' "$PW/tools/build_wine_ps5.sh")"
    # FFmpeg for the games' videos and sound (winedmo.prx, winegstreamer.prx).
    fetch "$PW/.deps/ffmpeg/source" "$(value "$PW/tools/build_ffmpeg_ps5.sh" FFMPEG_URL)" \
        "$(pin "$PW/tools/build_ffmpeg_ps5.sh" FFMPEG_COMMIT)"
    # ps5-framegen's interpolator, linked into libvulkan.prx for Frame generation.
    fetch "$B/ps5-framegen" "$(value "$PW/tools/link_radv_prx.sh" FRAMEGEN_URL)" \
        "$(pin "$PW/tools/link_radv_prx.sh" FRAMEGEN_COMMIT)"
    fetch "$PW/.deps/ps5-native-app-boilerplate" "$BOILERPLATE_URL" "$(pin "$PW/tools/build_native.sh" pin)"
    fetch "$B/prx-foundation" "$BOILERPLATE_URL" "$(pin "$PW/tools/build_wine_ps5.sh" MODULE_EXPORTS_COMMIT)"
    fetch "$B/PS5_Vulkan" "$VULKAN_URL" "$VULKAN_COMMIT"
    fetch "$B/PS5_Mesa" "$MESA_URL" "$(pin "$B/PS5_Vulkan/tools/build-radv.sh" mesa_revision)"
    fetch "$B/PS5_PayloadSDK" "$SDK_FORK_URL" "$(pin "$B/PS5_Vulkan/tools/setup-native-dependencies.sh" sdk_revision)"
    fetch "$B/lapy" "$LAPY_URL" "$LAPY_COMMIT"
    # DXVK with PC5's patch (step dxvk), committed with fixed names and dates.
    fetch "$B/dxvk-source" "$DXVK_URL" "$DXVK_COMMIT"
    git -C "$B/dxvk-source" reset -q --hard "$DXVK_COMMIT"
    git -C "$B/dxvk-source" clean -q -fdx
    git -C "$B/dxvk-source" submodule -q update --init --recursive
    git -C "$B/dxvk-source" "${id[@]}" am -q --committer-date-is-author-date "$here"/patches/dxvk/*.patch
    # The home screen tiles: psfwd in title/third_party must be the format's own, unchanged.
    fetch "$B/ps5-forwarder-format" "$FORWARDER_URL" "$FORWARDER_COMMIT"
    local pair
    for pair in src/psfwd.c:psfwd.c include/psfwd.h:psfwd.h LICENSE:LICENSE; do
        cmp -s "$B/ps5-forwarder-format/${pair%%:*}" "$here/title/third_party/psfwd/${pair##*:}" ||
            { echo "title/third_party/psfwd/${pair##*:} differs from the PS5 Forwarder Format at $FORWARDER_COMMIT" >&2
              return 1; }
    done
    # Zink's Mesa and compiler, as tools/build_mesa_zink.sh pins them.
    local zink=$PW/tools/build_mesa_zink.sh llvm
    fetch "$B/mesa-zink-source" "$MESA_URL" "$(pin "$zink" MESA_COMMIT)"
    llvm=$(value "$zink" LLVM_MINGW_VERSION)
    download "$B/downloads/llvm-mingw-$llvm-ubuntu-22.04-x86_64.tar.xz" \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/${llvm%-ucrt}/llvm-mingw-$llvm-ubuntu-22.04-x86_64.tar.xz" \
        "$(value "$zink" LLVM_MINGW_SHA256)"
    download "$B/downloads/vkd3d-proton-$VKD3D_VERSION.tar.zst" "$VKD3D_URL" "$VKD3D_SHA256"
    download "$B/downloads/mbedtls-$MBEDTLS_VERSION.tar.bz2" "$MBEDTLS_URL" "$MBEDTLS_SHA256"
    # VKD3D-Proton's source at its release's commit, for the source bundle.
    fetch "$B/vkd3d-proton-source" "$VKD3D_SOURCE_URL" "$VKD3D_SOURCE_COMMIT"
    git -C "$B/vkd3d-proton-source" submodule -q update --init --recursive
    [[ -f $B/mbedtls-$MBEDTLS_VERSION/include/mbedtls/ssl.h ]] || tar -xjf "$B/downloads/mbedtls-$MBEDTLS_VERSION.tar.bz2" -C "$B"
    # Python tools for Mesa's builds (meson 1.9 and the rest, as prospero-win pins them).
    [[ -x $B/venv/bin/meson ]] || {
        python3 -m venv "$B/venv"
        "$B/venv/bin/pip" install -q -r "$PW/tools/mesa_zink_requirements.txt"
    }
}

step_title() {
    local foundation=$PW/.deps/ps5-native-app-boilerplate native tool sources
    bash "$foundation/tools/setup-native-dependencies.sh"
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
    for source in "$mbedtls"/library/*.c; do
        env PS5_PAYLOAD_SDK="$sdk" sh "$foundation/tooling/prospero-clang18" -std=c99 -O2 -ffunction-sections \
            -fdata-sections -I"$mbedtls/include" -I"$mbedtls/library" -I"$here/title" "$config" \
            -c "$source" -o "$tls/$(basename "$source" .c).o"
    done
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
    # The module tool the Wine PS5 build signs its PRX modules with, from the PRX boilerplate.
    native=$B/prx-foundation/tooling/native tool=$B/prx-foundation/build/host/ps5-native-tool
    if [[ ! -x $tool ]]; then
        mkdir -p "${tool%/*}"
        c++ -std=c++20 -O2 "$native/native_app_builder.cpp" "$native/self_container.cpp" \
            "$native/elf_object.cpp" "$native/sce_module_writer.cpp" -lz -o "$tool"
    fi
}

step_radv() {
    cd "$B/PS5_Vulkan"
    bash tools/setup-native-dependencies.sh
    MESON=$B/venv/bin/meson PATH="$B/venv/bin:$PATH" bash tools/build-radv.sh release
}

step_hostwine() {
    cd "$PW"
    tools/build_host_wine.sh --source "$PW/.deps/wine/source" --work "$PW/.deps/wine-host" --jobs "$jobs"
}

step_wineps5() {
    cd "$PW"
    PATH="$B/venv/bin:$PATH" tools/build_wine_ps5.sh --source "$PW/.deps/wine/source" \
        --host-tools "$PW/.deps/wine-host/build" --prx-foundation "$B/prx-foundation" \
        --radv "$B/PS5_Vulkan" --ffmpeg-source "$PW/.deps/ffmpeg/source" --framegen "$B/ps5-framegen" --jobs "$jobs"
    # The link is skipped, not failed, when something it needs is missing: check what it made.
    for module in ntdll win32u winevulkan libvulkan winedmo winegstreamer; do
        [[ -f $PW/.deps/wine-ps5/prx/sce_module/$module.prx ]] ||
            { echo "no $module.prx: see the prx section of .deps/wine-ps5/report.json" >&2; return 1; }
    done
    grep -q '"framegen": "[0-9a-f]\{40\}"' "$PW/.deps/wine-ps5/report.json" ||
        { echo "libvulkan.prx was linked without frame generation: see .deps/wine-ps5/prx/libvulkan.link.log" >&2; return 1; }
    ls "$PW/.deps/wine-ps5/prx/sce_module"
}

step_dxvk() {
    # 64- and 32-bit, each into $B/dxvk/x64 and $B/dxvk/x32, as DXVK's own releases lay them out;
    # not again while the source (the release with patches/dxvk, step sources) is the same.
    local arch out=$B/dxvk tree
    tree=$(git -C "$B/dxvk-source" rev-parse HEAD)
    if [[ -f $out/NAME && $(cat "$out/SOURCE" 2>/dev/null) == "$tree" ]]; then
        echo "$DXVK_NAME" > "$out/NAME"   # the name may have changed without the source
        echo "DXVK $(cat "$out/NAME") already built from $tree"
        return 0
    fi
    rm -rf "$out"
    for arch in 64 32; do
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
    echo "$tree" > "$out/SOURCE"
    echo "$DXVK_NAME" > "$out/NAME"
    ls -l "$out"/x64 "$out"/x32
}

step_cpu() {
    cd "$PW"
    local build=$PW/.deps/wine-host/build sdk=$PW/.deps/ps5-native-app-boilerplate/.deps/native/ps5-payload-sdk
    tools/build_wowprospero.sh --build "$build" --source "$PW/.deps/wine-host/source" --jobs "$jobs"
    # The title's import stubs, less the two WebKit libraries the native CPU must not import.
    local stubs=$B/title-stubs
    rm -rf "$stubs" && mkdir -p "$stubs"
    find "$sdk/target/lib" -maxdepth 1 -name '*.so' ! -name libkernel_web.so ! -name libScePosixForWebKit.so \
        -exec cp {} "$stubs/" \;
    tools/build_wow64native.sh --source "$PW/.deps/wine-host/source" --build "$build" \
        --output "$PW/.deps/wow64native" --ps5-sdk "$sdk" --ps5-wine "$PW/.deps/wine-ps5" \
        --ps5-ntdll "$PW/.deps/wine-ps5/prx/ntdll.shared.elf" --ps5-foundation "$B/prx-foundation" \
        --ps5-stubs "$stubs"
}

step_zink() {
    cd "$PW"
    local llvm
    llvm=$(value tools/build_mesa_zink.sh LLVM_MINGW_VERSION)
    # Its own memory note: the release link wants about 8 GiB a job.
    MESON=$B/venv/bin/meson PATH="$B/venv/bin:$PATH" bash tools/build_mesa_zink.sh --work "$B/mesa-zink" \
        --mesa "$B/mesa-zink-source" --llvm-mingw "$B/downloads/llvm-mingw-$llvm-ubuntu-22.04-x86_64.tar.xz" \
        --jobs "${PC5_ZINK_JOBS:-2}"
    ls "$B/mesa-zink/artifacts"/*-windows
}

step_package() {
    cd "$PW"
    tools/package_release.sh --title "dist/$TITLE" --wine-ps5 .deps/wine-ps5 \
        --host-wine .deps/wine-host/install/usr \
        --cpu-dll .deps/wine-host/build/dlls/wowprospero/x86_64-windows/wowprospero.dll \
        --native-cpu .deps/wow64native --mesa-zink "$B/mesa-zink/artifacts" \
        --lapy-release build/native/lapy-helper-release.json --out "$B/package"
}

step_app() {
    local app=$release/$TITLE
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
    cp "$here/sce_sys/param.json" "$here/art/icon0.png" "$here/art/pic0.png" "$app/sce_sys/"
    mkdir -p "$app/pc5"
    # The home screen tiles' program, which PC5 copies into each tile (pc5_home.c), and its licence.
    mkdir -p "$app/pc5/forwarder/sce_module" "$app/LICENSES/ps5-forwarder-format"
    cp "$B/ps5-forwarder-format/template/eboot.bin" "$app/pc5/forwarder/"
    cp "$B/ps5-forwarder-format/template/sce_module/libc.prx" "$app/pc5/forwarder/sce_module/"
    chmod 755 "$app/pc5/forwarder/eboot.bin" "$app/pc5/forwarder/sce_module/libc.prx"
    cp "$B/ps5-forwarder-format/LICENSE" "$app/LICENSES/ps5-forwarder-format/LICENSE"
    python3 "$here/tools/make-font.py" "$app/pc5/fonts.bin"
    # Mozilla's roots as Debian ships them, for the covers' HTTPS (the console's own come first).
    for root in /usr/share/ca-certificates/mozilla/*.crt; do cat "$root"; echo; done > "$app/pc5/ca-certificates.crt"
    python3 "$here/tools/make-prefix.py" --pw "$PW" --wine "$PW/.deps/wine-host/install/usr/bin/wine" \
        --cpu-dll "$PW/.deps/wine-host/build/dlls/wowprospero/x86_64-windows/wowprospero.dll" \
        --vkd3d "$B/downloads/vkd3d-proton-$VKD3D_VERSION.tar.zst" --dxvk "$B/dxvk" --out "$app"
    # The DXVK the template has, which PC5 gives games installed with an earlier one (pc5_game.c).
    cp "$B/dxvk/NAME" "$app/pc5/dxvk.txt"
    mkdir -p "$app/LICENSES/dxvk"
    cp "$B/dxvk-source/LICENSE" "$app/LICENSES/dxvk/LICENSE"
    python3 "$here/tools/make-app.py" "$app" "$release/$TITLE.zip" --repo "$here" --pw "$PW"
}

step_bundle() {
    # Every part at the commit it was built from, as the build fetched it, in one archive.
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
    part wine "$PW/.deps/wine/source" "$(sed -n 's/^WINE_COMMIT=//p' "$PW/tools/build_wine_ps5.sh")" "$WINE_URL"
    part ps5-native-app-boilerplate "$PW/.deps/ps5-native-app-boilerplate" "$(pin "$PW/tools/build_native.sh" pin)" "$BOILERPLATE_URL"
    part prx-foundation "$B/prx-foundation" "$(pin "$PW/tools/build_wine_ps5.sh" MODULE_EXPORTS_COMMIT)" "$BOILERPLATE_URL"
    part ffmpeg "$PW/.deps/ffmpeg/source" "$(pin "$PW/tools/build_ffmpeg_ps5.sh" FFMPEG_COMMIT)" \
        "$(value "$PW/tools/build_ffmpeg_ps5.sh" FFMPEG_URL) (FFmpeg $(value "$PW/tools/build_ffmpeg_ps5.sh" FFMPEG_VERSION), its LGPL parts built: winedmo.prx and winegstreamer.prx)"
    part ps5-framegen "$B/ps5-framegen" "$(pin "$PW/tools/link_radv_prx.sh" FRAMEGEN_COMMIT)" \
        "$(value "$PW/tools/link_radv_prx.sh" FRAMEGEN_URL) (its interpolator, in libvulkan.prx for Frame generation)"
    part PS5_Vulkan "$B/PS5_Vulkan" "$VULKAN_COMMIT" "$VULKAN_URL"
    part PS5_Mesa-radv "$B/PS5_Mesa" "$(pin "$B/PS5_Vulkan/tools/build-radv.sh" mesa_revision)" "$MESA_URL"
    part PS5_Mesa-zink "$B/mesa-zink-source" "$(pin "$zink" MESA_COMMIT)" "$MESA_URL"
    part PS5_PayloadSDK "$B/PS5_PayloadSDK" "$(pin "$B/PS5_Vulkan/tools/setup-native-dependencies.sh" sdk_revision)" "$SDK_FORK_URL"
    part PS5-Lapy-JB-Daemon "$B/lapy" "$LAPY_COMMIT" "$LAPY_URL"
    part ps5-forwarder-format "$B/ps5-forwarder-format" "$FORWARDER_COMMIT" \
        "$FORWARDER_URL (the home screen tiles' program and psfwd)"
    mkdir -p "$stage/vkd3d-proton"
    tar -C "$B/vkd3d-proton-source" --exclude=.git -cf - . | tar -x -C "$stage/vkd3d-proton"
    printf '%-28s %s at %s, with its submodules\n' "vkd3d-proton/" "$VKD3D_SOURCE_URL" "$VKD3D_SOURCE_COMMIT" >> "$stage/PARTS.txt"
    mkdir -p "$stage/freetype"
    cp -a "$PW/.deps/wine-ps5/freetype/src/." "$stage/freetype/"
    local ft_from="its release archive"
    # A local copy in place of the release archive (PROSPERO_FREETYPE_TARBALL): named as it is.
    [[ -n ${PROSPERO_FREETYPE_TARBALL:-} ]] &&
        ft_from="from $(basename "$PROSPERO_FREETYPE_TARBALL"), SHA-256 $(sha256sum "$PROSPERO_FREETYPE_TARBALL" | cut -c1-64), not the release archive"
    printf '%-28s %s\n' "freetype/" "FreeType $(sed -n 's/^FREETYPE_VERSION=//p' "$PW/tools/build_wine_ps5.sh") ($ft_from; libfreetype.prx was built from it)" >> "$stage/PARTS.txt"
    mkdir -p "$stage/zlib"
    cp -a "$B/PS5_Vulkan/.deps/work/radv-src/subprojects/"zlib-*/. "$stage/zlib/"
    printf '%-28s %s\n' "zlib/" "zlib, the subproject RADV's shader cache was built with" >> "$stage/PARTS.txt"
    cp "$B/downloads/mbedtls-$MBEDTLS_VERSION.tar.bz2" "$stage/"
    printf '%-28s %s\n' "mbedtls-$MBEDTLS_VERSION.tar.bz2" "$MBEDTLS_URL" >> "$stage/PARTS.txt"
    mkdir -p "$stage/dxvk"
    tar -C "$B/dxvk-source" --exclude=.git -cf - . | tar -x -C "$stage/dxvk"
    printf '%-28s %s at %s (tag v2.6.2-prospero2), with its submodules and PC5'"'"'s patches/dxvk applied\n' \
        "dxvk/" "$DXVK_URL" "$DXVK_COMMIT" >> "$stage/PARTS.txt"
    printf 'PC5/README.md says how to build the app from these with PC5/tools/build.sh.\n' >> "$stage/PARTS.txt"
    rm -f "$out" "$out.part"
    tar -C "$B/bundle" --sort=name --owner=0 --group=0 --numeric-owner --mtime=2026-10-22 -cf - PPSA11018-sources |
        xz -6 -T2 > "$out.part"
    mv "$out.part" "$out"
    rm -rf "$B/bundle"
    ls -l "$out"
}

for s in $STEPS; do
    if want "$s"; then run "$s" "step_$s"; fi
done
for made in "$TITLE.zip" "$TITLE-sources.tar.xz"; do
    if { [[ $made == *.zip ]] && want app; } || { [[ $made == *.xz ]] && want bundle; }; then
        echo "==> $release/$made: $(du -h "$release/$made" | cut -f1), SHA-256 $(sha256sum "$release/$made" | cut -c1-16)…"
    fi
done
