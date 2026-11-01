#!/usr/bin/env python3
"""The loading screen's picture (art/loading.jpg, 1280x720) as C for pc5_matrix.c:
title/pc5_loading_art.h, its bytes in an array. Usage: make-loading-art.py [out.h]."""
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
source = os.path.join(here, "..", "art", "loading.jpg")
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "title", "pc5_loading_art.h")

data = open(source, "rb").read()
with open(out, "w", encoding="ascii") as f:
    f.write("/* SPDX-License-Identifier: LGPL-2.1-or-later */\n")
    f.write("/* art/loading.jpg, made by tools/make-loading-art.py: don't change it by hand. */\n")
    f.write("#ifndef PC5_LOADING_ART_H\n#define PC5_LOADING_ART_H\n\n")
    f.write("static const unsigned char pc5_loading_jpg[%d] = {\n" % len(data))
    for i in range(0, len(data), 24):
        f.write("    " + ",".join(str(b) for b in data[i:i + 24]) + ",\n")
    f.write("};\n\n#endif\n")
