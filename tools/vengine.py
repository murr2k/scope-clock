"""
Vector engine -- reference implementation of the point emitter.

A *display list* (list of ops) is rasterized into a flat list of beam samples
(x, y, z).  This is exactly what the STM32 streams to the dual DAC by DMA:
  x, y  -> DAC1 / DAC2  (world coords here; DAC counts on the target)
  z     -> beam on(1)/blank(0), driven onto a GPIO by the second DMA stream

The C engine in firmware/stm32/src/vector_engine.c mirrors this logic; keeping
the reference here lets us eyeball geometry, brightness and the point budget on
a PC before touching a scope.

World space is [-1, +1] on both axes = full usable deflection.  Scenes keep
their geometry inside a ~0.92 radius so the buffered output stage has headroom.

Ops (tuples):
  ("move", x, y)          pen-up jump to (x, y)          -> blanked samples
  ("line", x, y)          pen-down draw to (x, y)        -> lit samples
  ("dwell", n)            n extra lit samples at cur     -> brightens a node
  ("poly", [(x,y), ...])  move to first, line the rest
  ("text", x, y, s, scale, tracking)   baseline-left text
"""

import math
from vectorfont import glyph

# --- board targets -------------------------------------------------------
# max_code mirrors the C world_to_dac()/world_to_ccr() range exactly, so the
# Python reference quantizes identically to the firmware.
TARGETS = {
    # STM32F407: true dual 12-bit DAC, TIM6 at 200 kSa/s.  The DAC settles in
    # well under one sample period, so no extra settling is needed.
    "f407": dict(max_code=4095, step=0.010, max_points=4096,
                 sample_rate=200_000, move_settle=0, pwm_div=0,
                 minute_ticks=True,
                 note="dual 12-bit DAC via DHR12RD"),
    # STM32F401: no DAC -- 8-bit PWM pair (CCR 0..256) filtered by 2-pole RC.
    # 84 MHz / (3 * 256) = 109375 Sa/s.  move_settle covers the RC filter's
    # lag: the beam must arrive AND settle before a stroke is allowed to light,
    # otherwise it paints a tail from wherever it came from.
    "f401": dict(max_code=256, step=0.015, max_points=2560,
                 sample_rate=109_375, move_settle=4, pwm_div=3,
                 minute_ticks=True,
                 note="TIM3 PWM pair + external RC filters"),
    # STM32G491RE: same DAC/DMAMUX as the G431 but 96 KB of contiguous SRAM,
    # so the full 4096-point budget and the complete 60-tick face fit.
    "g491": dict(max_code=4095, step=0.010, max_points=4096,
                 sample_rate=200_000, move_settle=1, pwm_div=0,
                 minute_ticks=True,
                 note="dual 12-bit DAC, buffered, 96 KB contiguous SRAM"),
    # STM32G431KB: dual 12-bit DAC with buffered outputs (no op-amp needed),
    # but only 32 KB SRAM (22 KB of it contiguous).  A double-buffered frame
    # costs 16 bytes/point, so 1536 points = 24 KB is the ceiling and the
    # analog face drops its 48 in-between minute ticks.  170 MHz/1360 = 125 kSa/s.
    "g431": dict(max_code=4095, step=0.013, max_points=1536,
                 sample_rate=125_000, move_settle=1, pwm_div=0,
                 minute_ticks=False,
                 note="dual 12-bit DAC, buffered, 32 KB SRAM"),
}
DEFAULT_TARGET = "f407"

# Back-compat module-level defaults (f407).
DAC_MAX = TARGETS["f407"]["max_code"]
MAX_POINTS = TARGETS["f407"]["max_points"]
SAMPLE_RATE = TARGETS["f407"]["sample_rate"]


class Config:
    """Rasterizer tuning.  step controls lit-sample density == brightness."""
    def __init__(self, step=None, move_step=0.05, move_min=3, move_max=14,
                 quantize=True, target=DEFAULT_TARGET, max_code=None,
                 move_settle=None):
        t = TARGETS[target]
        self.target = target
        self.step = t["step"] if step is None else step
        self.move_step = move_step
        self.move_min = move_min
        self.move_max = move_max
        # blanked samples held AT the destination after a pen-up move, so a
        # slow output stage is settled before the next stroke lights up
        self.move_settle = t["move_settle"] if move_settle is None else move_settle
        self.quantize = quantize
        self.max_code = t["max_code"] if max_code is None else max_code
        self.minute_ticks = t["minute_ticks"]
        self.max_points = t["max_points"]
        self.sample_rate = t["sample_rate"]


def world_to_code(w, max_code=DAC_MAX):
    """world [-1, +1] -> device code [0, max_code], clamped."""
    c = int(round((w * 0.5 + 0.5) * max_code))
    return 0 if c < 0 else max_code if c > max_code else c


def code_to_world(c, max_code=DAC_MAX):
    return (c / max_code) * 2.0 - 1.0


def _q(x, y, quantize, max_code=DAC_MAX):
    if not quantize:
        return (x, y)
    return (code_to_world(world_to_code(x, max_code), max_code),
            code_to_world(world_to_code(y, max_code), max_code))


# --- text -> ops ----------------------------------------------------------

def text_ops(x, y, s, scale, tracking=0.0):
    """Expand a string to move/line ops; returns (ops, end_x)."""
    ops = []
    penx = x
    for ch in s:
        g = glyph(ch)
        for stroke in g.strokes:
            first = True
            for (fx, fy) in stroke:
                wx, wy = penx + fx * scale, y + fy * scale
                ops.append(("move" if first else "line", wx, wy))
                first = False
        penx += g.advance * scale + tracking
    return ops, penx


# --- rasterizer -----------------------------------------------------------

def rasterize(ops, cfg=None):
    """Display list -> [(x, y, z), ...] beam samples."""
    if cfg is None:
        cfg = Config()
    out = []
    cur = (0.0, 0.0)
    pen_down = False

    def emit(x, y, z):
        qx, qy = _q(x, y, cfg.quantize, cfg.max_code)
        out.append((qx, qy, z))

    # ensure we start with a blanked move so the DMA wrap (last->first sample)
    # is also blanked
    started = False

    i = 0
    while i < len(ops):
        op = ops[i]
        i += 1
        kind = op[0]

        if kind == "poly":
            pts = op[1]
            if not pts:
                continue
            expanded = [("move", pts[0][0], pts[0][1])]
            expanded += [("line", px, py) for (px, py) in pts[1:]]
            ops[i:i] = expanded
            continue

        if kind == "text":
            _, x, y, s, scale = op[0], op[1], op[2], op[3], op[4]
            tracking = op[5] if len(op) > 5 else 0.0
            texpanded, _ = text_ops(x, y, s, scale, tracking)
            ops[i:i] = texpanded
            continue

        if kind == "move":
            tx, ty = op[1], op[2]
            dist = math.hypot(tx - cur[0], ty - cur[1])
            n = max(cfg.move_min, min(cfg.move_max,
                                      int(math.ceil(dist / cfg.move_step))))
            for k in range(1, n + 1):
                t = k / n
                emit(cur[0] + (tx - cur[0]) * t,
                     cur[1] + (ty - cur[1]) * t, 0)
            for _ in range(cfg.move_settle):
                emit(tx, ty, 0)         # let the output stage catch up
            cur = (tx, ty)
            pen_down = False
            started = True

        elif kind == "line":
            tx, ty = op[1], op[2]
            if not started:
                emit(cur[0], cur[1], 0)     # safety: blank the wrap
                started = True
            if not pen_down:
                emit(cur[0], cur[1], 1)     # light the starting vertex
                pen_down = True
            dist = math.hypot(tx - cur[0], ty - cur[1])
            n = max(1, int(math.ceil(dist / cfg.step)))
            for k in range(1, n + 1):
                t = k / n
                emit(cur[0] + (tx - cur[0]) * t,
                     cur[1] + (ty - cur[1]) * t, 1)
            cur = (tx, ty)

        elif kind == "dwell":
            for _ in range(int(op[1])):
                emit(cur[0], cur[1], 1)

    return out


def stats(samples, sample_rate=None, max_points=None, cfg=None):
    if cfg is not None:
        sample_rate = sample_rate or cfg.sample_rate
        max_points = max_points or cfg.max_points
    sample_rate = sample_rate or SAMPLE_RATE
    max_points = max_points or MAX_POINTS
    n = len(samples)
    lit = sum(1 for s in samples if s[2])
    return {
        "samples": n,
        "lit": lit,
        "blank": n - lit,
        "refresh_hz": (sample_rate / n) if n else 0.0,
        "over_budget": n > max_points,
    }
