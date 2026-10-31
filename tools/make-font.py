#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Renders PC5's text faces from the Inter fonts (fonts/, SIL OFL 1.1) into one atlas, and for
Chinese, Japanese and Korean an atlas of each one's own letters.

    tools/make-font.py OUT.bin [--lang DIR] [--cjk FONT] [--korean REGULAR BOLD]

OUT.bin has the letters of English and of every language in DIR (lang/ by default: each .po's
msgstr) that Inter has (Latin, Greek, Cyrillic, Vietnamese), and every language's name in its own
letters (for Settings › System › Language; Chinese, Japanese and Korean's from their fonts). With --cjk (Droid Sans
Fallback, Apache 2.0) it also writes fonts-ja.bin, fonts-zh-Hans.bin and fonts-zh-Hant.bin
beside OUT.bin: the letters that language's .po (and its fallback's) uses, with the punctuation
and, for Japanese, the kana; with --korean (Noto Sans KR, SIL OFL 1.1: Droid Sans Fallback has no
Hangul) fonts-ko.bin. Those have no glyphs for the game-title faces (hero, card), which show
game names, not PC5's text: the console takes them from the title face.

The console draws text from these files (title/pc5_draw.c), so it needs no font engine.
Layout, little-endian:

    "PC5F" u32 version (1) u32 faces u32 atlas_width u32 atlas_height
    per face:  u32 pixel_size i32 ascent i32 descent i32 line_height u32 glyphs u32 first_glyph
    per glyph: u32 codepoint u16 x u16 y u16 w u16 h i16 left i16 top u16 advance_64ths u16 0
    the atlas: atlas_width * atlas_height bytes of coverage (0-255)

Glyphs are sorted by code point within each face; top is from the baseline (negative is up).
"""
import argparse
import importlib.util
import re
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONTS = ROOT / "fonts"
# name, file, pixel size, bold: the faces title/pc5_draw.h names, in its order.
FACES = [
    ("small", "Inter-Regular.ttf", 24, False),
    ("body", "Inter-Regular.ttf", 29, False),
    ("label", "Inter-SemiBold.ttf", 29, True),
    ("head", "InterDisplay-Bold.ttf", 40, True),
    ("title", "InterDisplay-Bold.ttf", 54, True),
    ("card", "InterDisplay-Bold.ttf", 46, True),
    ("button", "Inter-SemiBold.ttf", 17, True),
    ("hero", "InterDisplay-Bold.ttf", 92, True),
]
# The faces that show game titles only: English's letters, nothing a translation adds.
TITLE_FACES = {"card", "hero"}
CODEPOINTS = sorted(set(range(0x20, 0x7F)) | set(range(0xA0, 0x180)) |
                    {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2122, 0x2190, 0x2192, 0x203A,
                     0x20AC, 0x0218, 0x0219, 0x021A, 0x021B})
BASE = set(CODEPOINTS)
ATLAS_WIDTH = 2048
CJK = {"ja", "ko", "zh-Hans", "zh-Hant"}
# Where a language's missing text comes from (title/pc5_i18n.c's fallbacks): its letters too.
FALLBACK = {"es-419": "es", "fr-CA": "fr", "pt-BR": "pt-PT", "pt-PT": "pt-BR", "zh-Hans": "zh-Hant", "zh-Hant": "zh-Hans"}
# The languages' names in their own letters (pc5_i18n.c), for the Language setting in any language.


def language_names():
    """Every language's name in its own letters, as title/pc5_i18n.c's list has them."""
    text = (ROOT / "title" / "pc5_i18n.c").read_text(encoding="utf-8")
    table = text[text.index("static const Pc5Language languages[]"):]
    table = table[:table.index("};")]
    names = []
    for entry in re.findall(r'\{ "[^"]+", ((?:"(?:[^"\\]|\\.)*"\s*)+),', table):
        parts = re.findall(r'"((?:[^"\\]|\\.)*)"', entry)
        names.append(b"".join(p.encode("latin-1").decode("unicode_escape").encode("latin-1") for p in parts).decode("utf-8"))
    return names


# Chinese and Japanese punctuation, and Japanese's kana: in every file of theirs.
CJK_PUNCTUATION = {0x3001, 0x3002, 0x3008, 0x3009, 0x300A, 0x300B, 0x300C, 0x300D, 0x300E, 0x300F, 0x3010, 0x3011,
                   0x301C, 0x30FB, 0x30FC, 0xFF01, 0xFF08, 0xFF09, 0xFF0C, 0xFF0E, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF5E}
KANA = set(range(0x3041, 0x3097)) | set(range(0x30A1, 0x30FB))


def read_po():
    spec = importlib.util.spec_from_file_location("update_po", ROOT / "tools" / "update-po.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.read_po


def texts_of(lang_dir, code, read):
    """The translated text of code's .po (and its fallback's): what the console may show."""
    out = []
    for c in (code, FALLBACK.get(code)):
        path = lang_dir / f"{c}.po" if c else None
        if path and path.exists():
            for e in read(path):
                if not e["obsolete"] and e.get("str") and "fuzzy" not in e["flags"] and e["id"]:
                    out.append(e["str"])
    return out


def english_texts(lang_dir, read):
    pot = lang_dir / "pc5.pot"
    return [e["id"] for e in read(pot)] if pot.exists() else []


def render(faces, sources, wanted):
    """faces: (name, size, metrics font); sources(name) -> fonts to take letters from, in turn;
    wanted(name) -> the code points. The file's bytes and its glyph count."""
    out_faces, glyphs, bitmaps = [], [], []
    for name, size, metrics in faces:
        ascent, descent = metrics.getmetrics()
        first = len(glyphs)
        fonts = sources(name)
        for cp in sorted(wanted(name)):
            ch = chr(cp)
            for k, (font, cmap) in enumerate(fonts):
                # Beyond English's letters, a font only for those it maps (its .notdef box isn't one).
                if (k or cp not in BASE) and cp not in cmap and cp != 0x20:
                    continue
                # Only what the font really has: a missing glyph is left out, not drawn as a box.
                if cp != 0x20 and font.getmask(ch).getbbox() is None and not ch.isspace():
                    continue
                advance = font.getlength(ch)
                x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
                w, h = max(0, x1 - x0), max(0, y1 - y0)
                image = Image.new("L", (max(w, 1), max(h, 1)), 0)
                if w and h:
                    ImageDraw.Draw(image).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
                glyphs.append([cp, 0, 0, w, h, x0, y0, int(round(advance * 64))])
                bitmaps.append(image)
                break
        out_faces.append((size, ascent, descent, int(round((ascent + descent) * 1.18)), len(glyphs) - first, first))
    # Shelf packing, tallest first.
    order = sorted(range(len(glyphs)), key=lambda i: -glyphs[i][4])
    x = y = shelf = 0
    for i in order:
        w, h = glyphs[i][3], glyphs[i][4]
        if not w or not h:
            continue
        if x + w + 1 > ATLAS_WIDTH:
            x, y, shelf = 0, y + shelf + 1, 0
        glyphs[i][1], glyphs[i][2] = x, y
        x += w + 1
        shelf = max(shelf, h)
    height = y + shelf + 1
    if height >= 65536:
        sys.exit("make-font: the atlas is too tall for its 16-bit glyph places")
    atlas = Image.new("L", (ATLAS_WIDTH, height), 0)
    for i, glyph in enumerate(glyphs):
        if glyph[3] and glyph[4]:
            atlas.paste(bitmaps[i], (glyph[1], glyph[2]))
    data = bytearray(b"PC5F" + struct.pack("<IIII", 1, len(out_faces), ATLAS_WIDTH, height))
    for face in out_faces:
        data += struct.pack("<IiiiII", *face)
    for cp, gx, gy, w, h, left, top, advance in glyphs:
        data += struct.pack("<IHHHHhhHH", cp, gx, gy, w, h, left, top, advance, 0)
    data += atlas.tobytes()
    return bytes(data), len(glyphs), height


def loaded(path, size):
    from fontTools.ttLib import TTFont
    font = ImageFont.truetype(str(path), size)
    return font, set(TTFont(str(path), fontNumber=0, lazy=True).getBestCmap())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("out")
    parser.add_argument("--lang", default=str(ROOT / "lang"), help="the .po files (default: lang/)")
    parser.add_argument("--cjk", help="Droid Sans Fallback: also the Chinese and Japanese files")
    parser.add_argument("--korean", nargs=2, metavar=("REGULAR", "BOLD"), help="Noto Sans KR: also the Korean file")
    args = parser.parse_args()
    out, lang_dir = Path(args.out), Path(args.lang)
    read = read_po()
    out.parent.mkdir(parents=True, exist_ok=True)

    cache = {}

    def font(path, size):
        key = (str(path), size)
        if key not in cache:
            cache[key] = loaded(path, size)
        return cache[key]

    sizes = {name: size for name, _, size, _ in FACES}
    bold = {name: b for name, _, _, b in FACES}
    inter = {name: FONTS / file for name, file, _, _ in FACES}
    korean = [Path(p) for p in args.korean] if args.korean else None

    # fonts.bin: English and what the other languages Inter covers use, in every face but the
    # titles'; the CJK languages' names from their fonts.
    extra = set()
    for text in english_texts(lang_dir, read):
        extra |= {ord(c) for c in text}
    for po in sorted(lang_dir.glob("*.po")):
        if po.stem in CJK:
            continue
        for text in texts_of(lang_dir, po.stem, read):
            extra |= {ord(c) for c in text}
    names = "".join(language_names())
    extra |= {ord(c) for c in names}
    extra = {c for c in extra if c >= 0x20 and c != 0x7F}
    autonyms = {ord(c) for c in names if ord(c) >= 0x2E80}

    def main_sources(name):
        fonts = [font(inter[name], sizes[name])]
        if name not in TITLE_FACES:
            if args.cjk:
                fonts.append(font(args.cjk, sizes[name]))
            if korean:
                fonts.append(font(korean[1] if bold[name] else korean[0], sizes[name]))
        return fonts

    def main_wanted(name):
        if name in TITLE_FACES:
            return set(CODEPOINTS)
        return set(CODEPOINTS) | {c for c in extra if c not in autonyms and c < 0x2E80} | \
            (autonyms if args.cjk or korean else set())

    faces = [(name, sizes[name], font(inter[name], sizes[name])[0]) for name, _, _, _ in FACES]
    data, count, height = render(faces, main_sources, main_wanted)
    out.write_bytes(data)
    print(f"make-font: {out}: {len(FACES)} faces, {count} glyphs, {ATLAS_WIDTH}x{height} atlas, {len(data) >> 10} KB")

    # A file of its own for each CJK language: only its letters, only where its text shows.
    main_has = set(CODEPOINTS) | extra | autonyms
    for code in sorted(CJK):
        if code == "ko":
            if not korean:
                continue
            regular, heavy = korean
        else:
            if not args.cjk:
                continue
            regular = heavy = Path(args.cjk)
        letters = set(CJK_PUNCTUATION) | (KANA if code == "ja" else set())
        for text in texts_of(lang_dir, code, read):
            letters |= {ord(c) for c in text}
        letters = {c for c in letters if c >= 0x2E80 or c not in main_has}

        def sources(name, regular=regular, heavy=heavy):
            return [] if name in TITLE_FACES else [font(heavy if bold[name] else regular, sizes[name])]

        def wanted(name, letters=letters):
            return set() if name in TITLE_FACES else letters

        target = out.parent / f"fonts-{code}.bin"
        data, count, height = render(faces, sources, wanted)
        target.write_bytes(data)
        print(f"make-font: {target}: {count} glyphs, {ATLAS_WIDTH}x{height} atlas, {len(data) >> 10} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
