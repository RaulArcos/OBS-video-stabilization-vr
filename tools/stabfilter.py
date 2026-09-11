"""Reference implementation of the real-time stabilization filter.

This is the exact algorithm implemented in C++ inside the OBS plugin
(src/stabilizer.cpp). Keep both in sync.

Model (per axis x, y and rotation):
  p  = actual trajectory (cumulative measured motion)
  d  = displayed trajectory = p + c   (c is the correction we apply)
  Displayed velocity = smoothed actual velocity + spring pulling d back to p.
  => everything that is smooth (intentional) passes through, everything that
     is not (tremor, capture judder) is cancelled, and the correction never
     drifts away thanks to the spring.
"""
import math


def soft_limit(x, m, knee):
    if m <= 0:
        return 0.0
    k = m * knee
    ax = abs(x)
    if ax <= k:
        return x
    r = m - k
    y = k + r * (1.0 - math.exp(-(ax - k) / r))
    return math.copysign(y, x)


class AxisFilter:
    def __init__(self, tau_v=0.25, v0=2.0, tau_c=0.6, margin=40.0, knee=0.5):
        self.tau_v = tau_v      # velocity smoothing time constant at rest [s]
        self.v0 = v0            # speed (units/frame) at which smoothing halves
        self.tau_c = tau_c      # correction relaxation time constant [s]
        self.margin = margin    # hard limit of the correction [units]
        self.knee = knee        # fraction of margin where the soft limit starts
        self.v_s = 0.0
        self.c = 0.0

    def reset(self):
        self.v_s = 0.0
        self.c = 0.0

    def step(self, m, dt):
        # adaptive velocity smoothing: fast motion -> less smoothing (no lag)
        tau = self.tau_v / (1.0 + abs(self.v_s) / self.v0)
        a = dt / (tau + dt)
        self.v_s += (m - self.v_s) * a
        # cancel the non-smooth part of the motion
        self.c -= (m - self.v_s)
        # spring back toward the true position
        k = dt / (self.tau_c + dt)
        self.c *= (1.0 - k)
        # soft limit
        self.c = soft_limit(self.c, self.margin, self.knee)
        return self.c


class Stabilizer:
    """x/y in pixels, rotation in radians."""

    def __init__(self, width, height, zoom=1.04, strength=1.0, tau_v=0.25, v0=2.0, tau_c=0.6,
                 max_rot_deg=0.4):
        self.w, self.h = width, height
        self.zoom = zoom
        self.strength = strength
        margin_x = (1.0 - 1.0 / zoom) * width / 2.0
        margin_y = (1.0 - 1.0 / zoom) * height / 2.0
        # reserve part of the margin for rotation (corner displacement)
        rot_px = math.radians(max_rot_deg) * math.hypot(width, height) / 2.0
        rot_frac = min(0.25, rot_px / max(margin_y, 1e-6))
        self.fx = AxisFilter(tau_v, v0, tau_c, margin_x * (1.0 - rot_frac))
        self.fy = AxisFilter(tau_v, v0, tau_c, margin_y * (1.0 - rot_frac))
        self.fr = AxisFilter(tau_v, v0 * 0.002, tau_c, math.radians(max_rot_deg))

    def step(self, dx, dy, drot, dt):
        cx = self.fx.step(dx, dt) * self.strength
        cy = self.fy.step(dy, dt) * self.strength
        cr = self.fr.step(drot, dt) * self.strength
        return cx, cy, cr
