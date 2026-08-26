"""
Scene builders: (time / text) -> display list (list of vengine ops).

Geometry lives in world space [-1, +1].  The STM32 firmware
(firmware/stm32/src/scenes.c) reimplements these same constructions in C; this
module is the reference used by preview.py.

Clock-angle convention: 0 at 12 o'clock, increasing clockwise, so a point at
angle `a` (radians) is (sin a, cos a) * radius.
"""

import math
from vectorfont import arc, text_width, CAP_HEIGHT

MONTHS = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN",
          "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]


# --- small helpers --------------------------------------------------------

def _dir(a):
    return (math.sin(a), math.cos(a))


def _perp(a):
    return (math.cos(a), -math.sin(a))


def _hand(a, length, width, tail=0.12):
    """A slim kite from the hub to the tip; width=0 gives a plain line."""
    dx, dy = _dir(a)
    px, py = _perp(a)
    tip = (dx * length, dy * length)
    back = (-dx * tail, -dy * tail)
    if width <= 0.0:
        return ("poly", [back, tip])
    left = (px * width, py * width)
    right = (-px * width, -py * width)
    return ("poly", [back, left, tip, right, back])


def _centered_text_ops(s, scale, cy=0.0):
    """Text ops centered horizontally at x=0, vertically about cy."""
    w = text_width(s) * scale
    x0 = -w / 2.0
    baseline = cy - (CAP_HEIGHT * 0.5) * scale
    return [("text", x0, baseline, s, scale)]


def _fit_scale(s, max_w, max_scale):
    w = text_width(s)
    return min(max_scale, max_w / w) if w else max_scale


# --- analog clock ---------------------------------------------------------

def scene_analog(h, m, s, frac=0.0, numerals="quarter", minute_ticks=True):
    """numerals: 'none' | 'quarter' | 'all'.

    minute_ticks=False draws only the 12 five-minute ticks, saving ~380 points
    for SRAM-tight boards (mirrors SCENE_MINUTE_TICKS in the board headers).
    """
    ops = []
    R = 0.90

    # face ring
    ops.append(("poly", arc(0, 0, R, R, 0, 360, 72)))

    # hour ticks (quarter ticks longer), minute ticks short
    for i in range(60):
        if not minute_ticks and (i % 5) != 0:
            continue
        a = 2 * math.pi * i / 60.0
        dx, dy = _dir(a)
        if i % 15 == 0:
            r_in = R - 0.13
        elif i % 5 == 0:
            r_in = R - 0.09
        else:
            r_in = R - 0.045
        ops.append(("move", dx * R, dy * R))
        ops.append(("line", dx * r_in, dy * r_in))

    # numerals
    if numerals != "none":
        hours = [12, 3, 6, 9] if numerals == "quarter" else list(range(1, 13))
        for hh in hours:
            a = 2 * math.pi * (hh % 12) / 12.0
            dx, dy = _dir(a)
            rr = R - 0.24
            label = str(hh)
            sc = 0.011
            w = text_width(label) * sc
            ops.append(("text", dx * rr - w / 2.0,
                        dy * rr - (CAP_HEIGHT * 0.5) * sc, label, sc))

    # angles
    a_h = 2 * math.pi * ((h % 12) / 12.0 + m / 720.0 + s / 43200.0)
    a_m = 2 * math.pi * (m / 60.0 + s / 3600.0)
    a_s = 2 * math.pi * ((s + frac) / 60.0)

    # hands (draw hour, then minute, then second on top)
    ops.append(_hand(a_h, 0.46, 0.045))
    ops.append(_hand(a_m, 0.74, 0.032))
    ops.append(_hand(a_s, 0.82, 0.0, tail=0.20))

    # hub
    ops.append(("poly", arc(0, 0, 0.03, 0.03, 0, 360, 10)))
    ops.append(("move", 0, 0))
    ops.append(("dwell", 6))
    return ops


# --- digital clock --------------------------------------------------------

def scene_digital(h, m, s, date_str=None, message=None):
    ops = []
    hhmmss = f"{h:02d}:{m:02d}:{s:02d}"
    have_extra = bool(date_str) or bool(message)
    main_scale = _fit_scale(hhmmss, 1.75, 0.016)
    cy = 0.28 if have_extra else 0.0
    ops += _centered_text_ops(hhmmss, main_scale, cy=cy)

    if date_str:
        sc = _fit_scale(date_str, 1.4, 0.010)
        ops += _centered_text_ops(date_str, sc, cy=-0.28)
    if message:
        sc = _fit_scale(message, 1.7, 0.011)
        ops += _centered_text_ops(message, sc, cy=(-0.62 if date_str else -0.35))
    return ops


# --- scrolling / static message ------------------------------------------

def scene_message(text, scroll=0.0):
    """Large centered message; `scroll` shifts x (world units) for a marquee."""
    sc = _fit_scale(text, 1.8, 0.020) if scroll == 0.0 else 0.020
    w = text_width(text) * sc
    x0 = (-w / 2.0) + scroll
    baseline = -(CAP_HEIGHT * 0.5) * sc
    return [("text", x0, baseline, text, sc)]


# --- bring-up test pattern ------------------------------------------------

def scene_testpattern():
    """Circle + crosshair + border box: for DAC/DMA/scope-gain bring-up."""
    ops = []
    ops.append(("poly", [(-0.95, -0.95), (0.95, -0.95), (0.95, 0.95),
                         (-0.95, 0.95), (-0.95, -0.95)]))
    ops.append(("poly", arc(0, 0, 0.8, 0.8, 0, 360, 64)))
    ops.append(("move", -0.9, 0)); ops.append(("line", 0.9, 0))
    ops.append(("move", 0, -0.9)); ops.append(("line", 0, 0.9))
    return ops
