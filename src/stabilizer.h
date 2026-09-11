/*
VR Head Stabilizer for OBS - motion filter
Copyright (C) 2026 Raul Arcos

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

namespace vrstab {

/*
 * Per-axis filter. Reference implementation: tools/stabfilter.py (keep in sync).
 *
 *   p = actual trajectory (cumulative measured motion)
 *   d = displayed trajectory = p + c        (c is the correction we apply)
 *
 * Displayed velocity = smoothed actual velocity + spring pulling d back to p.
 * Smooth (intentional) motion passes through, tremor and capture judder are
 * cancelled, and the correction never drifts away thanks to the spring.
 */
struct AxisFilter {
	double tau_v = 0.3;   /* velocity smoothing time constant at rest [s] */
	double v0 = 4.0;      /* speed [units/frame] at which smoothing halves */
	double tau_c = 0.6;   /* correction relaxation time constant [s] */
	double margin = 40.0; /* hard limit of the correction [units] */
	double knee = 0.5;    /* fraction of margin where the soft limit starts */

	double v_s = 0.0; /* smoothed velocity */
	double c = 0.0;   /* current correction */

	void reset()
	{
		v_s = 0.0;
		c = 0.0;
	}

	/* m: measured motion this frame [units], dt: frame time [s]. Returns c. */
	double step(double m, double dt);

	/* no measurement available: relax the correction toward zero */
	double relax(double dt);
};

double soft_limit(double x, double margin, double knee);

struct StabilizerParams {
	double zoom = 1.05;       /* >= 1.0, crop factor that provides the margin */
	double strength = 1.0;    /* 0..1 scales the applied correction */
	double tau_v = 0.3;       /* [s] */
	double v0 = 4.0;          /* [px/frame] */
	double tau_c = 0.6;       /* [s] */
	double max_rot_deg = 0.4; /* max rotation correction, 0 disables rotation */
};

struct Correction {
	double x = 0.0;   /* px, full resolution */
	double y = 0.0;   /* px, full resolution */
	double rot = 0.0; /* radians, positive = x axis toward y axis (y down) */
};

class Stabilizer {
public:
	/* (re)compute margins for a frame size; keeps filter state */
	void configure(int width, int height, const StabilizerParams &p);
	void reset();

	/* feed one measured frame-to-frame motion; returns the correction to apply */
	Correction step(double dx, double dy, double drot, double dt);
	/* no reliable measurement this frame */
	Correction relax(double dt);

	const StabilizerParams &params() const { return p_; }
	double margin_x() const { return fx_.margin; }
	double margin_y() const { return fy_.margin; }
	Correction current() const;

private:
	Correction scaled() const;

	AxisFilter fx_, fy_, fr_;
	StabilizerParams p_;
	int w_ = 0, h_ = 0;
};

} // namespace vrstab
