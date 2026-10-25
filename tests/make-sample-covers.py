#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Draws made-up game covers for the host tests and the screenshots.

    tests/make-sample-covers.py OUT_DIR

Each is an invented game with an invented title, drawn from simple shapes: none is, or looks
like, a real game's cover. Also writes pictures in other shapes (as GOG and Wikipedia have).
"""
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONTS = Path(__file__).resolve().parents[1] / "fonts"
GAMES = [
    ("Neon Drift", "city"), ("Hollow Peaks", "mountains"), ("Iron Tide", "sea"), ("Starfall", "space"),
    ("Lantern Road", "forest"), ("Dust and Dawn", "desert"), ("Orbital Nine", "space"), ("Quiet Harbor", "sea"),
    ("Red Signal", "city"), ("Ember Vale", "forest"), ("Frost Line", "mountains"), ("Glass Atlas", "desert"),
    ("Night Courier", "city"), ("Pale Comet", "space"), ("Rust Belt Rally", "desert"), ("Tidewater", "sea"),
    ("Undergrove", "forest"), ("Velvet Static", "city"), ("White Summit", "mountains"), ("Zero Meridian", "space"),
    ("Amber Circuit", "city"), ("Black Lagoon Bay", "sea"), ("Copper Saints", "desert"), ("Deep Orchard", "forest"),
    ("Echo Valley", "mountains"), ("Faraway Lights", "space"), ("Granite Kings", "mountains"), ("Harvest Moon Road", "forest"),
    ("Indigo Run", "city"), ("Jade Coast", "sea"), ("Kilns of Morrow", "desert"), ("Lumen", "space"),
    ("Midnight Ferry", "sea"), ("North Wind", "mountains"), ("Outpost Seven", "desert"), ("Paper Planets", "space"),
    ("Quarry", "desert"), ("River of Glass", "forest"), ("Silver Hours", "city"), ("Twin Suns", "space"),
]
PALETTES = {
    "city": [((20, 10, 50), (230, 60, 140)), ((10, 20, 60), (40, 200, 230)), ((30, 5, 30), (250, 120, 40))],
    "mountains": [((20, 40, 80), (240, 200, 160)), ((10, 30, 50), (150, 210, 240)), ((40, 20, 60), (250, 150, 120))],
    "sea": [((5, 30, 60), (60, 180, 200)), ((10, 15, 40), (200, 120, 90)), ((0, 40, 50), (220, 230, 200))],
    "space": [((2, 2, 15), (90, 60, 200)), ((5, 0, 20), (220, 80, 180)), ((0, 5, 25), (60, 160, 255))],
    "forest": [((10, 30, 20), (220, 200, 120)), ((5, 20, 25), (120, 220, 160)), ((20, 25, 10), (250, 170, 80))],
    "desert": [((60, 20, 10), (250, 200, 120)), ((40, 10, 30), (250, 140, 90)), ((70, 40, 20), (255, 230, 170))],
}


def gradient(size, top, bottom):
    w, h = size
    image = Image.new("RGB", size)
    draw = ImageDraw.Draw(image)
    for y in range(h):
        t = y / (h - 1)
        draw.line([(0, y), (w, y)], fill=tuple(int(top[k] + (bottom[k] - top[k]) * t) for k in range(3)))
    return image


def cover(title, theme, seed, size=(600, 900)):
    rnd = random.Random(seed)
    w, h = size
    dark, light = rnd.choice(PALETTES[theme])
    image = gradient(size, dark, light)
    draw = ImageDraw.Draw(image, "RGBA")
    horizon = int(h * rnd.uniform(0.55, 0.7))
    sun = (int(w * rnd.uniform(0.25, 0.75)), int(horizon * rnd.uniform(0.45, 0.8)))
    r = int(w * rnd.uniform(0.12, 0.22))
    glow = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((sun[0] - r * 2, sun[1] - r * 2, sun[0] + r * 2, sun[1] + r * 2), fill=light + (110,))
    image.paste(glow.filter(ImageFilter.GaussianBlur(60)), (0, 0), glow.filter(ImageFilter.GaussianBlur(60)))
    draw = ImageDraw.Draw(image, "RGBA")
    draw.ellipse((sun[0] - r, sun[1] - r, sun[0] + r, sun[1] + r), fill=tuple(min(255, c + 40) for c in light) + (255,))
    if theme == "space":
        for _ in range(160):
            x, y, s = rnd.randrange(w), rnd.randrange(h), rnd.choice([1, 1, 2, 3])
            draw.ellipse((x, y, x + s, y + s), fill=(255, 255, 255, rnd.randrange(80, 255)))
        cx, cy, pr = int(w * rnd.uniform(0.2, 0.8)), int(h * 0.75), int(w * 0.6)
        draw.ellipse((cx - pr, cy - pr // 3, cx + pr, cy + pr), fill=dark + (255,))
    elif theme == "city":
        x = 0
        while x < w:
            bw, bh = rnd.randint(40, 110), rnd.randint(int(h * 0.15), int(h * 0.55))
            draw.rectangle((x, horizon - bh, x + bw, h), fill=(8, 8, 18, 255))
            for wy in range(horizon - bh + 12, h, 22):
                for wx in range(x + 8, x + bw - 8, 16):
                    if rnd.random() < 0.35:
                        draw.rectangle((wx, wy, wx + 6, wy + 9), fill=light + (200,))
            x += bw + rnd.randint(2, 10)
    elif theme == "mountains":
        for layer in range(3):
            points, x = [(0, h)], 0
            base = horizon + layer * 70
            while x <= w:
                points.append((x, base - rnd.randint(60, 320 - layer * 70)))
                x += rnd.randint(60, 160)
            points += [(w, base - 100), (w, h)]
            shade = tuple(int(dark[k] * (1 - layer * 0.25)) for k in range(3))
            draw.polygon(points, fill=shade + (255,))
    elif theme == "sea":
        draw.rectangle((0, horizon, w, h), fill=dark + (235,))
        for y in range(horizon + 8, h, 14):
            for _ in range(6):
                x = rnd.randrange(w)
                draw.line((x, y, x + rnd.randint(20, 90), y), fill=light + (90,), width=2)
        bx = rnd.randint(80, w - 200)
        draw.polygon([(bx, horizon + 30), (bx + 160, horizon + 30), (bx + 130, horizon + 60), (bx + 30, horizon + 60)], fill=(10, 10, 20, 255))
        draw.polygon([(bx + 80, horizon - 120), (bx + 80, horizon + 25), (bx + 150, horizon + 25)], fill=(240, 240, 230, 255))
    elif theme == "forest":
        for layer in range(4):
            base = horizon + layer * 60
            for _ in range(14):
                x, th = rnd.randrange(-40, w + 40), rnd.randint(140, 320)
                shade = tuple(int(dark[k] * (1.1 - layer * 0.25)) for k in range(3))
                draw.polygon([(x, base - th), (x - 50, base + 20), (x + 50, base + 20)], fill=shade + (255,))
            draw.rectangle((0, base + 10, w, h), fill=tuple(int(dark[k] * (1.1 - layer * 0.25)) for k in range(3)) + (255,))
    elif theme == "desert":
        for layer in range(3):
            points = [(0, h)]
            for x in range(0, w + 40, 40):
                points.append((x, horizon + layer * 90 + int(30 * math.sin(x / 90 + layer + seed))))
            points.append((w, h))
            shade = tuple(int(light[k] * (0.75 - layer * 0.18)) for k in range(3))
            draw.polygon(points, fill=shade + (255,))
    # The title, in the font PC5 itself uses.
    font = ImageFont.truetype(str(FONTS / "InterDisplay-Bold.ttf"), 78 if len(title) < 12 else 62)
    lines, line = [], ""
    for word in title.upper().split():
        test = (line + " " + word).strip()
        if font.getlength(test) > w - 80 and line:
            lines.append(line)
            line = word
        else:
            line = test
    lines.append(line)
    y = int(h * 0.09)
    for text in lines:
        tw = font.getlength(text)
        draw.text(((w - tw) / 2 + 3, y + 3), text, font=font, fill=(0, 0, 0, 140))
        draw.text(((w - tw) / 2, y), text, font=font, fill=(255, 255, 255, 255))
        y += int(font.size * 1.08)
    return image


def main() -> int:
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for i, (title, theme) in enumerate(GAMES):
        cover(title, theme, i).save(out / f"cover-{i:02d}.png")
    # Other shapes: a box (GOG-like) and a wide picture (Wikipedia can give one).
    cover("Neon Drift", "city", 0, (342, 482)).convert("RGB").save(out / "box.jpg", quality=90)
    cover("Neon Drift", "city", 0, (900, 600)).save(out / "wide.png")
    (out / "titles.txt").write_text("".join(f"{t}\n" for t, _ in GAMES))
    print(f"make-sample-covers: {len(GAMES)} covers in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
