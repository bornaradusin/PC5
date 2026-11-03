#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Build the vendored, pinned FriBidi for a host or the PS5. No network access.
set -euo pipefail
here=$(realpath -- "$(dirname -- "$0")/..")
destination=${1:?output directory required}
compiler=${2:-cc}
archiver=${3:-ar}
command=("$compiler")
if [[ -f $compiler && ! -x $compiler ]]; then command=(sh "$compiler"); fi
src="$here/third_party/fribidi-1.0.18"
mkdir -p "$destination"
flags=(-std=c11 -O2 -fPIC -ffunction-sections -fdata-sections -DFRIBIDI_BUILD -DFRIBIDI_LIB_STATIC
       -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1 -DHAVE_STRINGIZE=1 -DSTDC_HEADERS=1 -DHAVE_MEMMOVE=1
       -I"$src/lib" -I"$src/pc5-config")
if [[ -n ${4:-} ]]; then flags+=(-I"$4"); fi
if [[ ${PC5_FRIBIDI_PC5_ALLOC:-0} == 1 ]]; then
    # common.h skips stdlib.h when its allocator is overridden; qsort still needs it.
    flags+=(-include stdlib.h -include "$here/title/pc5.h" -Dfribidi_malloc=pc5_alloc -Dfribidi_free=pc5_free)
fi
sources=(fribidi fribidi-arabic fribidi-bidi fribidi-bidi-types fribidi-joining fribidi-joining-types
         fribidi-mirroring fribidi-brackets fribidi-run fribidi-shape fribidi-deprecated)
objects=()
for name in "${sources[@]}"; do
    "${command[@]}" "${flags[@]}" -c "$src/lib/$name.c" -o "$destination/$name.o"
    objects+=("$destination/$name.o")
done
"$archiver" rcs "$destination/libfribidi-pc5.a" "${objects[@]}"
echo "FriBidi 1.0.18 built: $destination/libfribidi-pc5.a"
