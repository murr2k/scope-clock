"""
Host-side scope simulator.

Rasterizes each scene with the reference vector engine and renders the beam the
way an X-Y CRT would show it: lit samples are connected in phosphor green with a
glow; blanked (z=0) retrace moves are invisible by default (that is what the
Z-blanking hardware buys us).  Also prints the per-frame point budget so you can
confirm every scene refreshes >= 50 Hz before flashing anything.

Usage:
  python preview.py                     # default scenes -> tools/out/*.png,*.svg
  python preview.py --time 13:37:00 --message "HELLO SCOPE"
  python preview.py --now               # use the real clock
  python preview.py --show-retrace      # draw blanked moves faintly (debug)
  python preview.py --dots              # overlay discrete beam samples

Requires Pillow for PNG; SVG is written regardless.
"""

import argparse
import math
import os

import scenes as SC
from vengine import rasterize, stats, Config, TARGETS
import analog

OUT = os.path.join(os.path.dirname(__file__), "out")

GREEN_CORE = (200, 255, 205)
GREEN_GLOW = (30, 210, 90)
BG = (4, 10, 6)


def to_px(x, y, size, margin=0.06):
    s = size * (1 - 2 * margin)
    px = margin * size + (x * 0.5 + 0.5) * s
    py = margin * size + (1 - (y * 0.5 + 0.5)) * s
    return (px, py)


def runs(samples):
    """Group consecutive lit samples into polylines; return (lit_runs, blank_runs)."""
    lit, blank = [], []
    cur, curz = [], None
    for (x, y, z) in samples:
        if z != curz:
            if cur:
                (lit if curz else blank).append(cur)
            cur, curz = [], z
        cur.append((x, y))
    if cur:
        (lit if curz else blank).append(cur)
    return lit, blank


def render_png(samples, size=720, show_retrace=False, dots=False,
               retrace_beam=False):
    from PIL import Image, ImageDraw, ImageChops, ImageFilter

    lit, blank = runs(samples)
    glow = Image.new("RGB", (size, size), (0, 0, 0))
    core = Image.new("RGB", (size, size), (0, 0, 0))
    gd, cd = ImageDraw.Draw(glow), ImageDraw.Draw(core)

    def poly(dr, pts, color, width):
        p = [to_px(x, y, size) for (x, y) in pts]
        if len(p) == 1:
            r = max(1.0, width * 0.9)
            dr.ellipse([p[0][0] - r, p[0][1] - r, p[0][0] + r, p[0][1] + r],
                       fill=color)
        else:
            dr.line(p, fill=color, width=width, joint="curve")

    if retrace_beam:
        # No Z-blank hardware: the beam stays lit through pen-up moves.  It
        # travels them ~7x faster than a drawn stroke (move_step 0.05 vs step
        # 0.010), so it deposits roughly 1/7 the energy per unit length --
        # visible, but clearly dimmer than the artwork.
        for r in blank:
            poly(gd, r, (12, 60, 26), 3)
        for r in blank:
            poly(cd, r, (34, 74, 40), 1)
    elif show_retrace:
        for r in blank:
            poly(gd, r, (40, 18, 18), 1)

    for r in lit:
        poly(gd, r, GREEN_GLOW, 5)     # glow layer (blurred below)
    for r in lit:
        poly(cd, r, GREEN_CORE, 2)     # sharp core

    glow = glow.filter(ImageFilter.GaussianBlur(4))
    out = ImageChops.add(glow, core)
    out = ImageChops.add(out, Image.new("RGB", (size, size), BG))

    if dots:
        dd = ImageDraw.Draw(out)
        for (x, y, z) in samples:
            if z:
                px, py = to_px(x, y, size)
                dd.point((px, py), fill=(230, 255, 235))
    return out


def render_svg(samples, size=720, show_retrace=False):
    lit, blank = runs(samples)
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" '
             f'height="{size}" viewBox="0 0 {size} {size}">',
             f'<rect width="{size}" height="{size}" fill="rgb{BG}"/>',
             '<g fill="none" stroke-linecap="round" stroke-linejoin="round">']

    def path(pts):
        d = "M " + " L ".join(f"{to_px(x, y, size)[0]:.1f},"
                              f"{to_px(x, y, size)[1]:.1f}" for (x, y) in pts)
        return d

    if show_retrace:
        for r in blank:
            if len(r) > 1:
                parts.append(f'<path d="{path(r)}" stroke="#2a1010" '
                             f'stroke-width="1"/>')
    for r in lit:
        if len(r) == 1:
            px, py = to_px(r[0][0], r[0][1], size)
            parts.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="2.4" '
                         f'fill="rgb{GREEN_CORE}"/>')
        else:
            parts.append(f'<path d="{path(r)}" stroke="rgb{GREEN_GLOW}" '
                         f'stroke-width="5" opacity="0.5"/>')
            parts.append(f'<path d="{path(r)}" stroke="rgb{GREEN_CORE}" '
                         f'stroke-width="1.6"/>')
    parts.append("</g></svg>")
    return "\n".join(parts)


def build_scenes(args):
    h, m, s = args.h, args.m, args.s
    date_str = f"{SC.MONTHS[args.mon - 1]} {args.day:02d}"
    return [
        ("testpattern", SC.scene_testpattern()),
        ("analog", SC.scene_analog(h, m, s, frac=0.0, numerals="quarter",
                                   minute_ticks=args.minute_ticks)),
        ("digital", SC.scene_digital(h, m, s)),
        ("digital_full", SC.scene_digital(h, m, s, date_str=date_str,
                                           message=args.message)),
        ("message", SC.scene_message(args.message)),
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--time", default="10:08:42")
    ap.add_argument("--date", default="2026-07-21")
    ap.add_argument("--now", action="store_true")
    ap.add_argument("--message", default="HELLO SCOPE")
    ap.add_argument("--target", choices=sorted(TARGETS), default="f407",
                    help="board: f407 / g431 (12-bit DAC) or f401 (PWM + RC)")
    ap.add_argument("--step", type=float, default=None,
                    help="stroke step in world units (default: per target)")
    ap.add_argument("--size", type=int, default=720)
    ap.add_argument("--show-retrace", action="store_true",
                    help="draw blanked moves in debug red")
    ap.add_argument("--no-zblank", action="store_true",
                    help="show an un-blanked beam: retrace drawn dim green, as on hardware with no Q1 fitted")
    ap.add_argument("--dots", action="store_true")
    ap.add_argument("--no-png", action="store_true")
    # PWM output-stage simulation (f401)
    ap.add_argument("--pwm", action="store_true",
                    help="simulate the RC-filtered PWM output stage")
    ap.add_argument("--r1", type=float, default=analog.R1_DEF)
    ap.add_argument("--c1", type=float, default=analog.C1_DEF)
    ap.add_argument("--r2", type=float, default=analog.R2_DEF)
    ap.add_argument("--c2", type=float, default=analog.C2_DEF)
    args = ap.parse_args()

    if args.now:
        import datetime
        n = datetime.datetime.now()
        args.h, args.m, args.s = n.hour, n.minute, n.second
        args.mon, args.day = n.month, n.day
    else:
        args.h, args.m, args.s = (int(x) for x in args.time.split(":"))
        y, args.mon, args.day = (int(x) for x in args.date.split("-"))

    os.makedirs(OUT, exist_ok=True)
    cfg = Config(step=args.step, target=args.target)
    tgt = TARGETS[args.target]
    args.minute_ticks = cfg.minute_ticks

    net = None
    if args.pwm:
        net = analog.RC2(args.r1, args.c1, args.r2, args.c2)
        carrier = cfg.sample_rate * (tgt["pwm_div"] or 1)
        rep = analog.report(net, carrier, cfg.sample_rate, cfg.step)
        print(f"PWM output stage: R1={args.r1:.0f} C1={args.c1*1e9:.2f}nF  "
              f"R2={args.r2:.0f} C2={args.c2*1e9:.2f}nF")
        print(f"  carrier {carrier/1000:.1f} kHz, filter corner "
              f"{rep['corner_hz']/1000:.1f} kHz "
              f"({rep['carrier_atten_db']:.0f} dB at carrier)")
        print(f"  beam fuzz (ripple) {rep['ripple_v_pp']*1000:.0f} mVpp = "
              f"{rep['ripple_pct_fs']:.2f}% of full scale")
        print(f"  lag {rep['group_delay_us']:.1f} us -> corner rounding "
              f"{rep['corner_round_pct']:.2f}% of screen\n")

    have_pil = False
    if not args.no_png:
        try:
            import PIL  # noqa: F401
            have_pil = True
        except ImportError:
            print("Pillow not installed; writing SVG only "
                  "(pip install pillow for PNG)")

    print(f"{'scene':<14} {'samples':>8} {'lit':>7} {'blank':>7} "
          f"{'refresh':>9}  budget")
    print("-" * 58)
    any_over = False
    tiles = []
    for name, ops in build_scenes(args):
        samples = rasterize(ops, cfg)
        st = stats(samples, cfg=cfg)
        if net is not None:
            samples = analog.filter_beam(samples, cfg.sample_rate, net)
        flag = "OVER" if st["over_budget"] else "ok"
        any_over |= st["over_budget"]
        print(f"{name:<14} {st['samples']:>8} {st['lit']:>7} "
              f"{st['blank']:>7} {st['refresh_hz']:>7.1f}Hz  {flag}")

        svg = render_svg(samples, size=args.size, show_retrace=args.show_retrace)
        with open(os.path.join(OUT, f"{name}.svg"), "w") as f:
            f.write(svg)
        if have_pil:
            img = render_png(samples, size=args.size,
                             show_retrace=args.show_retrace, dots=args.dots,
                             retrace_beam=args.no_zblank)
            img.save(os.path.join(OUT, f"{name}.png"))
            tiles.append((name, img))

    if have_pil and tiles:
        from PIL import Image, ImageDraw
        cols = 3
        rows = (len(tiles) + cols - 1) // cols
        ts = 360
        pad = 10
        sheet = Image.new("RGB", (cols * ts + (cols + 1) * pad,
                                  rows * ts + (rows + 1) * pad), (0, 0, 0))
        dr = ImageDraw.Draw(sheet)
        for i, (name, img) in enumerate(tiles):
            r, c = divmod(i, cols)
            x = pad + c * (ts + pad)
            y = pad + r * (ts + pad)
            sheet.paste(img.resize((ts, ts)), (x, y))
            dr.text((x + 6, y + 6), name, fill=(120, 200, 140))
        sheet.save(os.path.join(OUT, "contact_sheet.png"))
        print(f"\nwrote {len(tiles)} PNG + SVG + contact_sheet.png to {OUT}")
    else:
        print(f"\nwrote SVG to {OUT}")

    print(f"\ntarget {args.target} ({tgt['note']}): budget {cfg.max_points} pts "
          f"@ {cfg.sample_rate/1000:.1f} kSa/s -> "
          f"{cfg.sample_rate/cfg.max_points:.0f} Hz floor, "
          f"{cfg.max_code + 1} levels/axis")
    if any_over:
        print("WARNING: a scene exceeds the point budget (refresh < floor). "
              "Increase --step or trim the scene.")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
