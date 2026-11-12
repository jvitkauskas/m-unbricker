#!/usr/bin/env python3
"""Rebuild the cable-transfer animation using only Pillow and the fixed scene.

Run: python3 art/web/make_move_cable.py
All drawing is at 2x resolution, followed by a Lanczos downsample. The background
is composed from the existing move_cable.png artwork, with its arrow and loose
cable removed; no generated assets, fonts, or other source images are needed.
The physical USB-C tip is the position reference, so it visibly leaves one
socket and enters the other. The orange unit's plug never moves.
"""

from math import cos, sin, pi
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
BACKGROUND = HERE.parent / "sources" / "move_cable_bg.png"
SIZE = (960, 720)
SCALE = 2
FPS = 20
FRAME_MS = 1000 // FPS
FRAME_COUNT = 80
BG = (245, 245, 242)
GREEN = (109, 229, 162)
ORANGE = (255, 120, 0)
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
ANCHOR = (480, 402)
PHONE = (165.5, 341)
WITHDRAWN = (165.5, 399)
APPROACH = (717, 124)
BROKEN = (717, 181)


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def bezier(p0, p1, p2, p3, t):
    s = 1 - t
    return tuple(s**3 * a + 3 * s*s*t * b + 3 * s*t*t * c + t**3 * d
                 for a, b, c, d in zip(p0, p1, p2, p3))


def scaled(p):
    return tuple(round(x * SCALE) for x in p)


def stroke(draw, points, color=BLACK, width=7):
    """A smooth, round-ended line at the 2x working resolution."""
    pts = [scaled(p) for p in points]
    w = round(width * SCALE)
    draw.line(pts, fill=color, width=w, joint="curve")
    r = w / 2
    for x, y in (pts[0], pts[-1]):
        draw.ellipse((round(x-r), round(y-r), round(x+r), round(y+r)),
                     fill=color)


def arc(t):
    # Depart rightwards below the phone, then rise through the empty space.
    return bezier(WITHDRAWN, (360, 413), (438, 67), APPROACH, t)


def state(frame):
    """16-frame start hold, 7 withdrawal, 27 transfer, 6 insert, 24 end hold."""
    if frame < 16:
        return PHONE, -pi / 2, 0.0, 1.0
    if frame < 23:
        t = ease((frame - 15) / 7)
        return mix(PHONE, WITHDRAWN, t), -pi / 2, 0.0, 1 - t
    if frame < 50:
        t = ease((frame - 22) / 27)
        # One white plug, rotating continuously as the free lead changes shape.
        position = arc(t)
        rotation = ease((position[0] - ANCHOR[0]) / (APPROACH[0] - ANCHOR[0]))
        return position, -pi / 2 + pi * rotation, t, 0.0
    if frame < 56:
        t = ease((frame - 49) / 6)
        return mix(APPROACH, BROKEN, t), pi / 2, 1.0, 0.0
    return BROKEN, pi / 2, 1.0, 0.0


def plug(draw, tip, angle):
    """White outlined USB-C plug: slim metal nose, rounded insulated body.

    Local x is perpendicular to the cable, local y is distance behind the tip.
    Transform each contour into world space, avoiding a raster rotation blur.
    """
    forward = (cos(angle), sin(angle))
    side = (-forward[1], forward[0])

    def transform(x, y):
        return (tip[0] + x * side[0] - y * forward[0],
                tip[1] + x * side[1] - y * forward[1])

    def rounded_rect(box, radius, fill, outline=BLACK, width=5):
        x0, y0, x1, y1 = box
        contour = []
        for cx, cy, start in ((x1-radius, y0+radius, -pi/2),
                              (x1-radius, y1-radius, 0),
                              (x0+radius, y1-radius, pi/2),
                              (x0+radius, y0+radius, pi)):
            for k in range(9):
                a = start + k * pi / 16
                contour.append(transform(cx + radius*cos(a), cy + radius*sin(a)))
        draw.polygon([scaled(p) for p in contour], fill=fill)
        stroke(draw, contour + [contour[0]], outline, width)

    # The nose disappears into the receiving socket; the white body stays out.
    rounded_rect((-8, 0, 8, 17), 4, WHITE, width=4)
    rounded_rect((-15, 14, 15, 45), 7, WHITE, width=5)
    return transform(0, 46), forward


def render_frame(background, frame):
    im = background.copy()
    d = ImageDraw.Draw(im)
    tip, angle, transfer, check = state(frame)

    if check:
        color = tuple(round(g * (1-check)) for g in GREEN)
        stroke(d, [(135, 180), (156, 201), (202, 152)], color, 17)

    forward = (cos(angle), sin(angle))
    tail = (tip[0] - forward[0]*46, tip[1] - forward[1]*46)
    control1 = mix((480, 325), (480, 12), transfer)
    # Keep the curve broad while the free end passes above its anchored end.
    # These bounds avoid a tight fold when the two ends are nearly vertical.
    control1 = (control1[0], min(ANCHOR[1]-85, max(control1[1],
                tail[1] + 60*max(0, -forward[1]))))
    reach = abs(tail[0]-ANCHOR[0]) + abs(tail[1]-ANCHOR[1])
    slack = min(112, max(30, reach*.4))
    control2 = (tail[0] - forward[0]*slack,
                tail[1] - forward[1]*slack)
    cable = [bezier(ANCHOR, control1, control2, tail, k/180)
             for k in range(181)]
    stroke(d, cable, BLACK, 10)

    # Restrained motion strokes hug the free end; nothing points from the phone.
    if 17 <= frame <= 22:
        for x, delta in ((139, 0), (192, 3)):
            stroke(d, [(x, 356+delta), (x, 369+delta)], ORANGE, 4)

    plug(d, tip, angle)

    # A tiny, brief insertion click. The grey screen is never changed.
    if 55 <= frame <= 58:
        strength = (1.0, 1.0, .7, .35)[frame-55]
        color = tuple(round(b + (o-b)*strength) for b, o in zip(BG, ORANGE))
        for p, q in [((682, 163), (672, 154)),
                     ((751, 163), (761, 154)),
                     ((678, 181), (665, 181)),
                     ((756, 181), (769, 181))]:
            stroke(d, [p, q], color, 4)

    return im.resize(SIZE, Image.Resampling.LANCZOS)


def make_palette(frames):
    """One palette sampled across the entire loop; no frame-to-frame shimmer."""
    sheet = Image.new("RGB", (320*8, 240*10))
    for i, frame in enumerate(frames):
        sheet.paste(frame.resize((320, 240), Image.Resampling.LANCZOS),
                    ((i % 8)*320, (i // 8)*240))
    palette = sheet.quantize(colors=96, method=Image.Quantize.MEDIANCUT,
                             dither=Image.Dither.NONE)
    # Median cut can average a flat swatch with its antialias pixels. Pin the
    # original scene colours so the GIF background and devices match the PNGs.
    values = palette.getpalette()
    used = set()
    for color in (BG, BLACK, WHITE, GREEN, ORANGE, (168, 173, 176), (40, 173, 112)):
        index = min((i for i in range(96) if i not in used),
                    key=lambda i: sum((values[i*3+k]-color[k])**2
                                      for k in range(3)))
        values[index*3:index*3+3] = color
        used.add(index)
    # Pillow's palette lookup uses a reduced RGB cache. Collapse neighbouring
    # near-identical entries too, so the cache cannot choose an off-colour twin.
    swatches = (BG, BLACK, WHITE, GREEN, ORANGE, (168, 173, 176), (40, 173, 112))
    for i in range(96):
        rgb = values[i*3:i*3+3]
        closest = min(swatches, key=lambda c: sum((x-y)**2 for x, y in zip(rgb, c)))
        if sum((x-y)**2 for x, y in zip(rgb, closest)) <= 12**2:
            values[i*3:i*3+3] = closest
    palette.putpalette(values)
    return palette


def main():
    background = Image.open(BACKGROUND).convert("RGB")
    if background.size != SIZE:
        raise ValueError(f"Background must be {SIZE}, got {background.size}")
    # Upscale flat elements without introducing colour fringes before drawing.
    background = background.resize((SIZE[0]*SCALE, SIZE[1]*SCALE),
                                   Image.Resampling.NEAREST)
    frames = [render_frame(background, i) for i in range(FRAME_COUNT)]
    frames[0].save(HERE / "move_cable_start.png", optimize=True)
    frames[-1].save(HERE / "move_cable_end.png", optimize=True)
    frames[0].save(HERE / "move_cable.webp", save_all=True,
                   append_images=frames[1:], duration=FRAME_MS, loop=0,
                   lossless=True, quality=100, method=6,
                   minimize_size=True, background=(*BG, 255))
    palette = make_palette(frames)
    indexed = [f.quantize(palette=palette, dither=Image.Dither.NONE)
               for f in frames]
    indexed[0].save(HERE / "move_cable.gif", save_all=True,
                    append_images=indexed[1:], duration=FRAME_MS, loop=0,
                    optimize=True, disposal=1)
    print(f"{FRAME_COUNT} timeline frames, {FPS} fps, "
          f"{FRAME_COUNT*FRAME_MS/1000:.2f} s, {SIZE[0]}x{SIZE[1]}")
    for name in ("move_cable.webp", "move_cable.gif", "move_cable_start.png",
                 "move_cable_end.png"):
        p = HERE / name
        with Image.open(p) as output:
            durations = []
            for i in range(getattr(output, "n_frames", 1)):
                output.seek(i)
                output.load()
                durations.append(output.info.get("duration", 0))
            print(f"{name}: {p.stat().st_size:,} bytes; "
                  f"{len(durations)} encoded frames; "
                  f"{sum(durations)} ms")


if __name__ == "__main__":
    main()
