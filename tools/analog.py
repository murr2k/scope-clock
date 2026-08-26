"""
Analog output-stage model for the PWM "DAC" build (Nucleo-F401RE).

The F401 has no DAC, so each axis is a PWM duty cycle smoothed by an external
two-stage RC low-pass.  That filter is the whole ball game: too high a corner
frequency and carrier ripple thickens the beam, too low and the filter cannot
follow the beam so corners round off and the figure shrinks.  This module lets
you see both effects on a PC before soldering anything.

Network (unbuffered cascade, which is what you actually breadboard):

    PWM --[R1]--+--[R2]--+--> scope
                |        |
               C1       C2
                |        |
               GND      GND

Transfer function of the loaded cascade (note the R1*C2 cross term -- the
stages interact, which a naive "two independent poles" model misses):

    H(s) = 1 / (1 + s*(R1C1 + R2C2 + R1C2) + s^2*(R1C1*R2C2))
"""

import math

# Defaults: a deliberately *fast* filter.  A slower one (10nF/2.2nF) gets the
# ripple down to 0.3% but its lag is so long that each stroke needs many blanked
# settling samples before the beam is allowed to light, which blows the frame
# budget.  These values trade ripple up to ~1.3% of full scale (still thinner
# than the trace itself) for a third of the lag.  All common E12 values.
R1_DEF, C1_DEF = 1000.0, 4.7e-9
R2_DEF, C2_DEF = 4700.0, 1.0e-9

VDD = 3.3


class RC2:
    """Two-stage loaded RC low-pass."""

    def __init__(self, r1=R1_DEF, c1=C1_DEF, r2=R2_DEF, c2=C2_DEF):
        self.r1, self.c1, self.r2, self.c2 = r1, c1, r2, c2
        # denominator coefficients: 1 + b1 s + b2 s^2
        self.b1 = r1 * c1 + r2 * c2 + r1 * c2
        self.b2 = (r1 * c1) * (r2 * c2)

    def gain_at(self, f_hz):
        """|H(j2*pi*f)|"""
        w = 2.0 * math.pi * f_hz
        re = 1.0 - self.b2 * w * w
        im = self.b1 * w
        return 1.0 / math.hypot(re, im)

    def corner_hz(self):
        """-3 dB frequency, found by bisection (the cascade is not a clean 2-pole)."""
        lo, hi = 1.0, 1e9
        for _ in range(200):
            mid = math.sqrt(lo * hi)
            if self.gain_at(mid) > 0.70710678:
                lo = mid
            else:
                hi = mid
        return math.sqrt(lo * hi)

    def group_delay_dc(self):
        """Low-frequency group delay == b1; the lag that rounds corners."""
        return self.b1

    def ripple_pp(self, f_carrier, vdd=VDD):
        """Worst-case (50% duty) carrier ripple at the output, volts peak-to-peak.

        The PWM fundamental at D=0.5 has amplitude 2*Vdd/pi; higher harmonics
        are attenuated much harder and are ignored.
        """
        fundamental_peak = 2.0 * vdd / math.pi
        return 2.0 * fundamental_peak * self.gain_at(f_carrier)

    def step_state_update(self, state, u, dt):
        """One RK4 step of the network. state = (v1, v2), u = input volts."""
        r1, c1, r2, c2 = self.r1, self.c1, self.r2, self.c2

        def deriv(s):
            v1, v2 = s
            i1 = (u - v1) / r1
            i2 = (v1 - v2) / r2
            return ((i1 - i2) / c1, i2 / c2)

        def add(s, d, h):
            return (s[0] + d[0] * h, s[1] + d[1] * h)

        k1 = deriv(state)
        k2 = deriv(add(state, k1, dt / 2))
        k3 = deriv(add(state, k2, dt / 2))
        k4 = deriv(add(state, k3, dt))
        return (state[0] + dt / 6 * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]),
                state[1] + dt / 6 * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]))

    def filter_samples(self, values, sample_rate, substeps=8, v_settle=None):
        """Run a piecewise-constant sample stream through the network.

        `values` are in world units [-1, +1]; the DAC holds each for one sample
        period.  Returns the filtered output in the same units.  The filter
        state is pre-settled by running the frame once and carrying the state
        over, so the result is the steady-state loop the scope actually shows.
        """
        dt = 1.0 / sample_rate / substeps
        state = (v_settle, v_settle) if v_settle is not None else (0.0, 0.0)
        # two warm-up passes so the periodic steady state is reached
        for _pass in range(2):
            out = []
            for v in values:
                for _ in range(substeps):
                    state = self.step_state_update(state, v, dt)
                out.append(state[1])
        return out


def filter_beam(samples, sample_rate, net):
    """Apply the network to a [(x, y, z), ...] beam stream, per axis."""
    xs = [s[0] for s in samples]
    ys = [s[1] for s in samples]
    fx = net.filter_samples(xs, sample_rate)
    fy = net.filter_samples(ys, sample_rate)
    return [(fx[i], fy[i], samples[i][2]) for i in range(len(samples))]


def report(net, carrier_hz, sample_rate, step, vdd=VDD):
    """Human-readable summary of what this filter will do to the picture."""
    ripple_v = net.ripple_pp(carrier_hz, vdd)
    # Beam speed in world units/sec -> how far the filter lag drags a corner.
    velocity = step * sample_rate
    lag_world = velocity * net.group_delay_dc()
    return {
        "corner_hz": net.corner_hz(),
        "carrier_atten_db": 20.0 * math.log10(net.gain_at(carrier_hz)),
        "ripple_v_pp": ripple_v,
        "ripple_pct_fs": 100.0 * ripple_v / vdd,
        "group_delay_us": net.group_delay_dc() * 1e6,
        "corner_round_world": lag_world,
        "corner_round_pct": 100.0 * lag_world / 2.0,   # screen spans 2.0 units
    }
