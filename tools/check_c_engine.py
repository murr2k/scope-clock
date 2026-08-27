"""
Cross-check the C vector engine against the Python reference.

`CLAUDE.md` states the invariant "the C engine mirrors the Python reference".
This is how that gets verified without hardware: compile the firmware's
board-independent files (vector_engine.c, scenes.c, font_vec.c) with the host's
gcc, run every scene, and compare the resulting sample counts against
tools/vengine.py rasterizing the same scenes with the same target settings.

Exact agreement is not expected: the firmware computes in float and the Python
reference in double, so segment counts can differ by one at
ceil(dist/step) boundaries.  A small percentage tolerance catches real geometry
divergence while ignoring that.

Usage:
    python tools/check_c_engine.py            # both targets
    python tools/check_c_engine.py --tol 0.05
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

import scenes as SC
from vengine import rasterize, Config, TARGETS

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "firmware", "stm32", "src"))
HC = os.path.join(HERE, "hostcheck")
STUB = os.path.join(HC, "stub")

BOARD_DEFINE = {"f407": "BOARD_DISCO_F407VG", "f401": "BOARD_NUCLEO_F401RE",
                "g431": "BOARD_NUCLEO_G431KB",
                "g491": "BOARD_NUCLEO_G491RE",
                "weact_g431": "BOARD_WEACT_G431CB"}

# Must match host_check.c
SCENE_SPECS = [
    ("testpattern",  lambda a: SC.scene_testpattern()),
    ("analog",       lambda c: SC.scene_analog(10, 8, 42,
                                               minute_ticks=c.minute_ticks)),
    ("digital",      lambda a: SC.scene_digital(10, 8, 42)),
    ("digital_full", lambda a: SC.scene_digital(10, 8, 42, date_str="JUL 21",
                                                message="HELLO SCOPE")),
    ("message",      lambda a: SC.scene_message("HELLO SCOPE")),
]


def find_cc():
    for cc in ("gcc", "clang", "cc"):
        p = shutil.which(cc)
        if p:
            return p
    return None


def build_and_run(cc, target, outdir):
    exe = os.path.join(outdir, f"hostcheck_{target}.exe")
    cmd = [cc, "-std=c11", "-Wall", "-Wextra", "-Wno-unused-parameter",
           f"-D{BOARD_DEFINE[target]}", "-I", STUB, "-I", SRC,
           os.path.join(HC, "host_check.c"),
           os.path.join(SRC, "vector_engine.c"),
           os.path.join(SRC, "scenes.c"),
           os.path.join(SRC, "font_vec.c"),
           "-lm", "-o", exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        return None, (r.stdout + r.stderr).strip()
    if r.stderr.strip():
        print("  compiler warnings:\n" + r.stderr.strip())
    out = subprocess.run([exe], capture_output=True, text=True)
    if out.returncode != 0:
        return None, out.stderr.strip()

    counts, meta = {}, {}
    for line in out.stdout.splitlines():
        m = re.match(r"^board (\S+) max_points (\d+)$", line.strip())
        if m:
            meta["board"], meta["max_points"] = m.group(1), int(m.group(2))
            continue
        m = re.match(r"^(\w+) (\d+)$", line.strip())
        if m:
            counts[m.group(1)] = int(m.group(2))
    return (counts, meta), None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tol", type=float, default=0.05,
                    help="allowed fractional difference (default 5%%)")
    ap.add_argument("--targets", nargs="*", default=sorted(TARGETS))
    args = ap.parse_args()

    cc = find_cc()
    if not cc:
        print("no host C compiler (gcc/clang) found; skipping C cross-check")
        return 0
    print(f"host compiler: {cc}\n")

    outdir = os.path.join(HERE, "out")
    os.makedirs(outdir, exist_ok=True)

    failures = 0
    for target in args.targets:
        print(f"=== target {target} ({TARGETS[target]['note']}) ===")
        res, err = build_and_run(cc, target, outdir)
        if res is None:
            print(f"  BUILD/RUN FAILED:\n{err}")
            failures += 1
            continue
        counts, meta = res
        cfg = Config(target=target)
        budget = TARGETS[target]["max_points"]
        if meta.get("max_points") != budget:
            print(f"  MISMATCH: C VE_MAX_POINTS={meta.get('max_points')} "
                  f"but vengine.TARGETS says {budget}")
            failures += 1

        print(f"  {'scene':<14}{'C':>7}{'python':>9}{'delta':>8}   budget")
        for name, build in SCENE_SPECS:
            py = len(rasterize(build(cfg), cfg))
            c = counts.get(name)
            if c is None:
                print(f"  {name:<14}{'--':>7} : missing from C output")
                failures += 1
                continue
            d = abs(c - py) / max(py, 1)
            over = c > budget
            status = []
            if d > args.tol:
                status.append(f"DIVERGENT >{args.tol:.0%}")
                failures += 1
            if over:
                status.append("OVER BUDGET")
                failures += 1
            print(f"  {name:<14}{c:>7}{py:>9}{d:>7.1%}   "
                  f"{'OVER' if over else 'ok'}"
                  f"{'  <-- ' + ', '.join(status) if status else ''}")
        print()

    if failures:
        print(f"FAILED: {failures} problem(s)")
        return 1
    print("all targets agree with the Python reference and fit the budget")
    return 0


if __name__ == "__main__":
    sys.exit(main())
