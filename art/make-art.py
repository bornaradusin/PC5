#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Draws PC5's tile icon and its Home-screen background, from simple shapes.

    art/make-art.py [OUT_DIR]        (default: art/)

icon0.png  512x512    a gaming PC tower, glass side, three lit fans, on a lit floor (no lettering:
                      Home shows the app's name under it)
pic0.png   1920x1080  a desk at night: monitor, PC tower, keyboard and mouse, lit up

Everything is drawn here, with no lettering. Both are drawn at four (icon) or two
(background) times their size and scaled down, for smooth edges. The output depends only on
this script and Pillow.
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
CYAN, VIOLET, PINK = (64, 214, 255), (140, 96, 255), (255, 84, 196)


def lerp(a, b, t):
    return tuple(int(round(a[k] + (b[k] - a[k]) * t)) for k in range(len(a)))


def vertical(size, top, bottom):
    w, h = size
    column = Image.new("RGB", (1, h))
    for y in range(h):
        column.putpixel((0, y), lerp(top, bottom, y / max(1, h - 1)))
    return column.resize((w, h))


def glow(size, box, color, blur, strength=255):
    """A blurred ellipse of light, as an RGBA layer."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).ellipse(box, fill=color + (strength,))
    return layer.filter(ImageFilter.GaussianBlur(blur))


def add_light(base, layer):
    """Light adds to what is under it (screen blend)."""
    rgb = layer.convert("RGB")
    alpha = layer.getchannel("A")
    lit = ImageChops.screen(base.convert("RGB"), Image.composite(rgb, Image.new("RGB", base.size), alpha))
    return lit


def fan(draw, cx, cy, r, ring, s):
    """A case fan seen from the side: a lit ring, a dark hub and seven blades."""
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(10, 12, 20))
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline=ring, width=max(2, int(r * 0.13)))
    inner = r * 0.78
    for k in range(7):
        a = k * 2 * math.pi / 7
        points = []
        for t in range(9):
            f = t / 8
            ang = a + f * 0.9
            rad = inner * (0.28 + 0.72 * f)
            points.append((cx + math.cos(ang) * rad, cy + math.sin(ang) * rad))
        for t in range(8, -1, -1):
            f = t / 8
            ang = a + f * 0.9 + 0.38
            rad = inner * (0.28 + 0.72 * f)
            points.append((cx + math.cos(ang) * rad, cy + math.sin(ang) * rad))
        draw.polygon(points, fill=lerp((28, 32, 46), ring, 0.18))
    draw.ellipse((cx - r * 0.26, cy - r * 0.26, cx + r * 0.26, cy + r * 0.26), fill=(18, 20, 30),
                 outline=lerp(ring, (255, 255, 255), 0.3), width=max(1, int(s)))


def tower(size, box, s, rings=(CYAN, VIOLET, PINK)):
    """A PC tower: dark case, glass side, three fans down the front, a lit card across."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    lights = Image.new("RGBA", size, (0, 0, 0, 0))
    draw, light = ImageDraw.Draw(layer), ImageDraw.Draw(lights)
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    r = w * 0.07
    draw.rounded_rectangle(box, r, fill=(20, 23, 34), outline=(56, 62, 84), width=max(2, int(3 * s)))
    glass = (x0 + w * 0.06, y0 + h * 0.05, x1 - w * 0.06, y1 - h * 0.08)
    draw.rounded_rectangle(glass, r * 0.6, fill=(12, 14, 24), outline=(70, 78, 104), width=max(1, int(2 * s)))
    # fans down the front, a card across the middle
    fr = (glass[2] - glass[0]) * 0.21
    fx = glass[2] - fr * 1.35
    for i, ring in enumerate(rings):
        fy = glass[1] + (glass[3] - glass[1]) * (0.2 + 0.3 * i)
        light.ellipse((fx - fr * 1.6, fy - fr * 1.6, fx + fr * 1.6, fy + fr * 1.6), fill=ring + (120,))
        fan(draw, fx, fy, fr, ring, s)
    card = (glass[0] + w * 0.05, glass[1] + (glass[3] - glass[1]) * 0.52, fx - fr * 1.3, glass[1] + (glass[3] - glass[1]) * 0.6)
    draw.rounded_rectangle(card, 4 * s, fill=(34, 38, 54))
    draw.line((card[0] + 6 * s, card[3] - 3 * s, card[2] - 6 * s, card[3] - 3 * s), fill=VIOLET, width=max(2, int(3 * s)))
    light.rectangle((card[0], card[1], card[2], card[3] + 10 * s), fill=VIOLET + (90,))
    # a board, in faint lines behind
    for k in range(5):
        yy = glass[1] + (glass[3] - glass[1]) * (0.08 + 0.08 * k)
        draw.line((glass[0] + w * 0.08, yy, card[2] - w * 0.05, yy), fill=(30, 34, 50), width=max(1, int(2 * s)))
    # feet and a power light
    draw.rectangle((x0 + w * 0.1, y1 - 2, x0 + w * 0.25, y1 + 6 * s), fill=(14, 16, 24))
    draw.rectangle((x1 - w * 0.25, y1 - 2, x1 - w * 0.1, y1 + 6 * s), fill=(14, 16, 24))
    draw.ellipse((x0 + w * 0.08, y0 + h * 0.015, x0 + w * 0.08 + 6 * s, y0 + h * 0.015 + 6 * s), fill=CYAN)
    return layer, lights.filter(ImageFilter.GaussianBlur(26 * s))


def icon():
    S = 4
    size = (512 * S, 512 * S)
    base = vertical(size, (16, 20, 36), (5, 6, 12))
    base = add_light(base, glow(size, (40 * S, -60 * S, 470 * S, 330 * S), (40, 70, 160), 90 * S, 150))
    image = base.convert("RGBA")
    floor = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(floor)
    # a floor of lines, going away
    for k in range(1, 7):
        y = 428 * S + (k ** 1.6) * 2.4 * S
        draw.line((0, y, 512 * S, y), fill=(90, 120, 230, 120), width=S)
    for k in range(-8, 9):
        draw.line((256 * S + k * 20 * S, 428 * S, 256 * S + k * 64 * S, 512 * S), fill=(90, 120, 230, 100), width=S)
    image = Image.alpha_composite(image, floor)
    # the tower fills the tile: the app's name is under it on Home already
    case, lights = tower(size, (136 * S, 40 * S, 376 * S, 426 * S), S)
    image = Image.alpha_composite(image, Image.new("RGBA", size, (0, 0, 0, 0)))
    image = add_light(image, lights).convert("RGBA")
    image = Image.alpha_composite(image, case)
    return image.convert("RGB").resize((512, 512), Image.LANCZOS)


def background():
    S = 2
    W, H = 1920 * S, 1080 * S
    size = (W, H)
    image = vertical(size, (8, 10, 20), (4, 5, 10))
    image = add_light(image, glow(size, (1000 * S, 80 * S, 1900 * S, 760 * S), (40, 90, 200), 160 * S, 170))
    image = add_light(image, glow(size, (1500 * S, 300 * S, 2200 * S, 900 * S), (150, 40, 160), 170 * S, 120))
    image = image.convert("RGBA")
    desk_y = 800 * S
    marks = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(marks)
    # the wall: faint panels
    for k in range(9):
        x = 760 * S + k * 140 * S
        draw.line((x, 0, x, desk_y), fill=(160, 180, 255, 14), width=S)
    image = Image.alpha_composite(image, marks)
    # the monitor's light on the wall
    mx0, my0, mx1, my1 = 900 * S, 250 * S, 1560 * S, 640 * S
    screen_light = glow(size, (mx0 - 120 * S, my0 - 60 * S, mx1 + 120 * S, my1 + 220 * S), (70, 110, 230), 90 * S, 150)
    image = add_light(image, screen_light).convert("RGBA")
    draw = ImageDraw.Draw(image)
    # the desk, then the monitor: its stand, then the screen with a picture of its own (a sunset over a grid)
    draw.rectangle((0, desk_y, W, H), fill=(10, 11, 18, 255))
    draw.line((0, desk_y, W, desk_y), fill=(44, 50, 76, 255), width=2 * S)
    draw.polygon([(1200 * S, my1), (1260 * S, my1), (1280 * S, desk_y - 6 * S), (1180 * S, desk_y - 6 * S)], fill=(22, 25, 36, 255))
    draw.rounded_rectangle((1130 * S, desk_y - 14 * S, 1330 * S, desk_y), 6 * S, fill=(26, 29, 42, 255))
    draw.rounded_rectangle((mx0, my0, mx1, my1), 14 * S, fill=(12, 13, 20, 255), outline=(48, 54, 74, 255), width=3 * S)
    sx0, sy0, sx1, sy1 = mx0 + 14 * S, my0 + 14 * S, mx1 - 14 * S, my1 - 14 * S
    picture = vertical((sx1 - sx0, sy1 - sy0), (26, 18, 70), (230, 90, 120))
    pd = ImageDraw.Draw(picture)
    pw, ph = picture.size
    horizon = int(ph * 0.62)
    sun = (pw * 0.5 - ph * 0.22, horizon - ph * 0.38, pw * 0.5 + ph * 0.22, horizon + ph * 0.06)
    pd.ellipse(sun, fill=(255, 196, 110))
    # the sun's lower half cut by bands of the sky behind it
    for k in range(6):
        y = horizon - ph * 0.04 - k * ph * 0.05
        sky = lerp((26, 18, 70), (230, 90, 120), y / ph)
        pd.rectangle((sun[0], y, sun[2], y + ph * 0.012 * (6 - k) / 2), fill=sky)
    pd.rectangle((0, horizon, pw, ph), fill=(20, 10, 40))
    for k in range(1, 12):
        y = horizon + (k ** 1.7) * ph * 0.006
        pd.line((0, y, pw, y), fill=(255, 80, 200), width=max(1, S))
    for k in range(-14, 15):
        pd.line((pw / 2 + k * pw * 0.02, horizon, pw / 2 + k * pw * 0.12, ph), fill=(255, 80, 200), width=max(1, S))
    # mountains either side
    pd.polygon([(0, horizon), (pw * 0.12, horizon - ph * 0.16), (pw * 0.22, horizon - ph * 0.07), (pw * 0.33, horizon)], fill=(40, 20, 80))
    pd.polygon([(pw, horizon), (pw * 0.86, horizon - ph * 0.19), (pw * 0.75, horizon - ph * 0.06), (pw * 0.64, horizon)], fill=(40, 20, 80))
    mask = Image.new("L", picture.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, pw - 1, ph - 1), 6 * S, fill=255)
    image.paste(picture, (sx0, sy0), mask)
    draw = ImageDraw.Draw(image)
    # the tower, right of the monitor
    case, lights = tower(size, (1640 * S, 330 * S, 1900 * S, desk_y - 8 * S), S * 1.6)
    image = add_light(image, lights).convert("RGBA")
    image = Image.alpha_composite(image, case)
    draw = ImageDraw.Draw(image)
    # the keyboard, lit from under, and the mouse
    kx0, kx1, ky0, ky1 = 930 * S, 1520 * S, 860 * S, 960 * S
    under = Image.new("RGBA", size, (0, 0, 0, 0))
    ud = ImageDraw.Draw(under)
    for k in range(40):
        t = k / 39
        x = kx0 + (kx1 - kx0) * t
        ud.ellipse((x - 40 * S, ky1 - 30 * S, x + 40 * S, ky1 + 40 * S), fill=lerp(CYAN, PINK, t) + (110,))
    image = add_light(image, under.filter(ImageFilter.GaussianBlur(24 * S))).convert("RGBA")
    draw = ImageDraw.Draw(image)
    draw.polygon([(kx0 + 40 * S, ky0), (kx1 - 40 * S, ky0), (kx1, ky1), (kx0, ky1)], fill=(18, 20, 30, 255), outline=(60, 66, 90, 255))
    for row in range(5):
        t0, t1 = row / 5, (row + 1) / 5
        ya, yb = ky0 + (ky1 - ky0) * t0 + 3 * S, ky0 + (ky1 - ky0) * t1 - 3 * S
        inset_a, inset_b = 40 * S * (1 - t0), 40 * S * (1 - t1)
        keys = 15 - (row == 4) * 6
        for k in range(keys):
            fa, fb = k / keys, (k + 1) / keys
            xa = kx0 + inset_a + (kx1 - kx0 - 2 * inset_a) * fa + 3 * S
            xb = kx0 + inset_a + (kx1 - kx0 - 2 * inset_a) * fb - 3 * S
            xc = kx0 + inset_b + (kx1 - kx0 - 2 * inset_b) * fa + 3 * S
            xd = kx0 + inset_b + (kx1 - kx0 - 2 * inset_b) * fb - 3 * S
            key_light = lerp(CYAN, PINK, (fa + fb) / 2)
            draw.polygon([(xa, ya), (xb, ya), (xd, yb), (xc, yb)], fill=lerp((30, 33, 46), key_light, 0.16) + (255,))
    draw.ellipse((1590 * S, 880 * S, 1650 * S, 960 * S), fill=(22, 24, 34, 255), outline=(64, 70, 96, 255), width=2 * S)
    draw.line((1620 * S, 884 * S, 1620 * S, 910 * S), fill=(64, 70, 96, 255), width=2 * S)
    draw.ellipse((1612 * S, 920 * S, 1628 * S, 936 * S), fill=PINK + (255,))
    # the left side stays quiet: the console puts the name and Play there
    shade = Image.new("L", (W, 1))
    for x in range(W):
        shade.putpixel((x, 0), int(max(0.0, 1 - x / (W * 0.62)) ** 1.6 * 200))
    image = Image.composite(Image.new("RGBA", size, (4, 5, 10, 255)), image, shade.resize(size))
    return image.convert("RGB").resize((1920, 1080), Image.LANCZOS)


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE
    out.mkdir(parents=True, exist_ok=True)
    icon().save(out / "icon0.png", optimize=True)
    background().save(out / "pic0.png", optimize=True)
    print(f"make-art: {out / 'icon0.png'} and {out / 'pic0.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
