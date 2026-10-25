#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Host checks, no console or PS5 SDK needed. Run tools/build.sh sources first (it fetches
# the pinned prospero-win the checks build against).
#
#   tests/test.sh [--shots]      --shots: also draws every screen into .build/tests/shots/
set -euo pipefail
here=$(realpath -- "$(dirname -- "$0")/..")
PW=$here/.build/prospero-win
out=$here/.build/tests
[[ -f $PW/native/pc5_ui.c ]] || { echo "run tools/build.sh sources first" >&2; exit 2; }
rm -rf "$out" && mkdir -p "$out/work"
cd "$here"

# What the tests read: Windows programs (MinGW), made-up covers, the font atlas.
python3 tests/make-test-programs.py "$out/programs" >/dev/null
python3 tests/make-sample-covers.py "$out/covers" >/dev/null
python3 tools/make-font.py "$out/fonts.bin" >/dev/null
upstream=("$PW/src/pw_game_profile.c" "$PW/src/pw_app_profile.c" "$PW/src/pw_profile_catalog.c"
          "$PW/native/pw_wine_library.c" "$PW/src/pw_result.c")
common=(-std=c11 -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE -Ititle -I"$PW/src" -I"$PW/native" -I"$PW/include"
        -I"$PW/wine/ps5")

# PC5's code, with every buffer on the heap so the sanitizers see it, against
# prospero-win's own profile parser and library loader.
cc "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DPC5_HEAP_ONLY \
    tests/test_pc5.c title/pc5_base.c title/pc5_controls.c title/pc5_cover.c title/pc5_draw.c title/pc5_game.c title/pc5_image.c title/pc5_json.c \
    title/pc5_library.c title/pc5_logs.c title/pc5_matrix.c title/pc5_pe.c title/pc5_settings.c title/pc5_source.c \
    title/pc5_stb.c title/pc5_ui.c title/pc5_update.c title/pc5_home.c title/pc5_psfwd.c \
    "${upstream[@]}" -lm \
    -o "$out/test_pc5"
"$out/test_pc5" "$out/work" tests/fixtures "$out/programs" "$out/covers" "$out/fonts.bin"

# The HTTPS client, with mbedTLS built for this computer, against a local server: a
# certificate from a test CA (trusted), one from nobody (refused), redirects, chunks.
mbedtls=$(ls -d "$here"/.build/mbedtls-*/ | head -1)
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

# Clang 18, as the console build uses it, finds nothing to warn about either.
for file in title/pc5*.c; do
    case $file in *_ps5.c|*console.c) continue ;; esac   # the console's own: built by tools/build.sh
    clang-18 "${common[@]}" -I"$mbedtls/include" "$config" -O2 -fsyntax-only "$file"
done

if [[ ${1:-} == --shots ]]; then
    cc "${common[@]}" -O2 tests/shots.c title/pc5_base.c title/pc5_controls.c title/pc5_cover.c title/pc5_draw.c title/pc5_game.c \
        title/pc5_image.c title/pc5_json.c title/pc5_library.c title/pc5_logs.c title/pc5_matrix.c title/pc5_pe.c \
        title/pc5_settings.c title/pc5_source.c title/pc5_stb.c title/pc5_ui.c title/pc5_update.c title/pc5_home.c \
        title/pc5_psfwd.c "${upstream[@]}" -lm -o "$out/draw-shots"
    mkdir -p "$out/shots" "$out/shotwork"
    "$out/draw-shots" "$out/shots" "$out/shotwork" "$out/covers" "$out/programs" "$out/fonts.bin"
    python3 -c "import sys, pathlib; from PIL import Image
for p in sorted(pathlib.Path(sys.argv[1]).glob('*.ppm')): Image.open(p).save(p.with_suffix('.png')); p.unlink()" "$out/shots"
fi

# The patched prospero-win still passes its own checks on the files the patch changes.
(cd "$PW" && python3 tests/test_title_identity.py && python3 tests/test_native_contract.py &&
    python3 tests/test_package_release.py)

# The art is the one the script draws.
python3 art/make-art.py "$out/art" >/dev/null
cmp art/icon0.png "$out/art/icon0.png"
cmp art/pic0.png "$out/art/pic0.png"
python3 -m py_compile tools/make-prefix.py tools/make-app.py tools/make-font.py art/make-art.py \
    tests/make-test-programs.py tests/make-sample-covers.py
bash -n tools/build.sh
echo "host checks passed"
