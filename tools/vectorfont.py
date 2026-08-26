"""
Single-stroke vector font for the scope clock.

This is the ONE source of truth for glyph geometry. Two consumers:
  * preview.py / vengine.py  -- host-side rendering & simulation
  * gen_font.py              -- emits firmware/stm32/src/font_vec.{c,h}

Design / coordinate system
--------------------------
Each glyph lives in a cell with the baseline at y = 0 and the cap height at
y = 24, x increasing right, y increasing UP (screen/scope convention, not
image convention).  A glyph is a list of *strokes*; each stroke is a polyline
[(x, y), ...].  The beam is ON along a stroke and BLANKED on the jump between
strokes, so fewer strokes == fewer blanked retrace moves == cleaner image.
That is why this is a "single-stroke" font: letters are drawn the way a pen
plotter (or an electron beam) would draw them, not as filled outlines.

Round glyphs (0, 8, O, C, D, G, Q ...) are generated from `arc()` so the
curves are accurate and the data stays compact.  Everything is plain Python
data, so it is trivially portable to a C table.

Font units are integers-ish (floats allowed); the renderer scales them.
"""

import math

CAP_HEIGHT = 24.0     # y of a capital's top
BASELINE = 0.0        # y of the baseline
DESCENT = -6.0        # lowest y used (comma tail)
DEFAULT_ADVANCE = 18.0


def arc(cx, cy, rx, ry, a0, a1, n=14):
    """Elliptical arc as a polyline, angles in degrees (0 deg = +x, CCW).

    a0 -> a1 may increase or decrease; the sweep follows that direction, so
    arc(.., 90, 450) is a full CCW loop and arc(.., 60, 300) is an open 'C'.
    """
    pts = []
    for i in range(n + 1):
        t = math.radians(a0 + (a1 - a0) * i / n)
        pts.append((cx + rx * math.cos(t), cy + ry * math.sin(t)))
    return pts


# --- glyph geometry -------------------------------------------------------
# Box conventions for most glyphs: x in [2, 14], y in [1, 23], mid x = 8.

_G = {
    " ": (12.0, []),

    "0": (18.0, [arc(8, 12, 6, 11, 90, 450, 18)]),
    "1": (18.0, [[(4, 18), (8, 23), (8, 1)], [(3, 1), (13, 1)]]),
    "2": (18.0, [[(3, 18), (4, 21), (7, 23), (11, 23), (13, 20), (13, 17),
                  (11, 14), (3, 4), (2, 1), (14, 1)]]),
    "3": (18.0, [[(3, 20), (6, 23), (11, 23), (13, 20.5), (13, 17),
                  (10.5, 13.5), (8, 13), (10.5, 12.5), (13, 9), (13, 4),
                  (10, 1), (5, 1), (3, 4)]]),
    "4": (18.0, [[(11, 1), (11, 23), (2, 7), (14, 7)]]),
    "5": (18.0, [[(13, 23), (3, 23), (3, 13), (9, 14), (12, 11), (13, 7),
                  (11, 2), (6, 1), (3, 3)]]),
    "6": (18.0, [[(12, 20), (9, 23), (5, 22), (3, 17), (3, 6), (5, 2),
                  (9, 1), (12, 3), (13, 7), (11, 11), (6, 12), (3, 9)]]),
    "7": (18.0, [[(2, 23), (14, 23), (6, 1)]]),
    "8": (18.0, [arc(8, 17, 5, 6, 90, 450, 14), arc(8, 6, 6, 7, 90, 450, 14)]),
    "9": (18.0, [[(4, 4), (7, 1), (11, 2), (13, 7), (13, 18), (11, 22),
                  (7, 23), (4, 21), (3, 17), (5, 13), (10, 12), (13, 15)]]),

    ":": (9.0, [[(4.5, 6), (4.5, 8)], [(4.5, 15), (4.5, 17)]]),
    ".": (10.0, [[(6, 1), (6, 2.5)]]),
    ",": (10.0, [[(7, 2), (6, 2), (6, 1), (7, 1), (7, 3), (5, -3)]]),
    "!": (8.0, [[(4, 23), (4, 7)], [(4, 1), (4, 2.5)]]),
    "?": (16.0, [[(3, 19), (5, 22), (9, 23), (12, 21), (12, 17), (8, 13),
                  (8, 9)], [(8, 1), (8, 2.5)]]),
    "-": (18.0, [[(4, 12), (14, 12)]]),
    "+": (18.0, [[(8, 17), (8, 7)], [(3, 12), (13, 12)]]),
    "/": (18.0, [[(3, 1), (13, 23)]]),
    "'": (7.0, [[(4, 23), (4, 18)]]),
    "°": (12.0, [arc(6, 19, 4, 4, 0, 360, 10)]),   # degree sign

    "A": (18.0, [[(2, 1), (8, 23), (14, 1)], [(4.7, 8), (11.3, 8)]]),
    "B": (18.0, [[(3, 1), (3, 23), (10, 23), (13, 20), (13, 15), (10, 12),
                  (3, 12)], [(10, 12), (13, 9), (13, 4), (10, 1), (3, 1)]]),
    "C": (18.0, [arc(8, 12, 6, 11, 55, 305, 14)]),
    "D": (18.0, [[(3, 23), (3, 1), (8, 1), (12, 4), (13, 12), (12, 20),
                  (8, 23), (3, 23)]]),
    "E": (18.0, [[(13, 23), (3, 23), (3, 1), (13, 1)], [(3, 12), (10, 12)]]),
    "F": (18.0, [[(13, 23), (3, 23), (3, 1)], [(3, 12), (10, 12)]]),
    "G": (18.0, [arc(8, 12, 6, 11, 30, 330, 15), [(13, 4), (13, 11), (9, 11)]]),
    "H": (18.0, [[(3, 23), (3, 1)], [(13, 23), (13, 1)], [(3, 12), (13, 12)]]),
    "I": (12.0, [[(6, 23), (6, 1)], [(3, 23), (9, 23)], [(3, 1), (9, 1)]]),
    "J": (18.0, [[(12, 23), (12, 6), (10, 2), (6, 1), (3, 3), (3, 7)]]),
    "K": (18.0, [[(3, 23), (3, 1)], [(13, 23), (3, 10)], [(6, 13), (13, 1)]]),
    "L": (18.0, [[(3, 23), (3, 1), (13, 1)]]),
    "M": (20.0, [[(2, 1), (2, 23), (9, 10), (16, 23), (16, 1)]]),
    "N": (18.0, [[(3, 1), (3, 23), (13, 1), (13, 23)]]),
    "O": (18.0, [arc(8, 12, 6, 11, 90, 450, 18)]),
    "P": (18.0, [[(3, 1), (3, 23), (10, 23), (13, 20), (13, 15), (10, 12),
                  (3, 12)]]),
    "Q": (18.0, [arc(8, 12, 6, 11, 90, 450, 18), [(9, 7), (14, 1)]]),
    "R": (18.0, [[(3, 1), (3, 23), (10, 23), (13, 20), (13, 15), (10, 12),
                  (3, 12)], [(8, 12), (14, 1)]]),
    "S": (18.0, [[(13, 20), (10, 23), (5, 23), (3, 20), (3, 16), (5, 13),
                  (11, 11), (13, 8), (13, 4), (11, 1), (6, 1), (3, 4)]]),
    "T": (18.0, [[(2, 23), (14, 23)], [(8, 23), (8, 1)]]),
    "U": (18.0, [[(3, 23), (3, 6), (5, 2), (8, 1), (11, 2), (13, 6),
                  (13, 23)]]),
    "V": (18.0, [[(2, 23), (8, 1), (14, 23)]]),
    "W": (22.0, [[(1, 23), (5, 1), (10, 15), (15, 1), (19, 23)]]),
    "X": (18.0, [[(2, 23), (14, 1)], [(14, 23), (2, 1)]]),
    "Y": (18.0, [[(2, 23), (8, 12), (14, 23)], [(8, 12), (8, 1)]]),
    "Z": (18.0, [[(2, 23), (14, 23), (2, 1), (14, 1)]]),
}


class Glyph:
    __slots__ = ("advance", "strokes")

    def __init__(self, advance, strokes):
        self.advance = float(advance)
        # normalize to lists of (float, float)
        self.strokes = [[(float(x), float(y)) for (x, y) in s] for s in strokes]


GLYPHS = {ch: Glyph(adv, strokes) for ch, (adv, strokes) in _G.items()}

# Lowercase falls back to uppercase geometry (keeps messages legible without
# doubling the table); unknown chars render as a space.
for c in range(ord("a"), ord("z") + 1):
    GLYPHS[chr(c)] = GLYPHS[chr(c - 32)]


def glyph(ch):
    return GLYPHS.get(ch, GLYPHS[" "])


def text_width(s, tracking=0.0):
    return sum(glyph(c).advance + tracking for c in s)


if __name__ == "__main__":
    # quick self-check: bounds + coverage
    miny = min((y for g in GLYPHS.values() for s in g.strokes for (_, y) in s),
               default=0)
    maxy = max((y for g in GLYPHS.values() for s in g.strokes for (_, y) in s),
               default=0)
    print(f"glyphs: {len(set(id(g) for g in GLYPHS.values()))} unique, "
          f"{len(GLYPHS)} mapped")
    print(f"y range: {miny} .. {maxy}")
    print("clock chars present:",
          all(c in GLYPHS for c in "0123456789: "))
