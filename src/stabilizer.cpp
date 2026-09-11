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

#include "stabilizer.h"

#include <algorithm>
#include <cmath>

namespace vrstab {

double soft_limit(double x, double margin, double knee)
{
	if (margin <= 0.0)
		return 0.0;
	const double k = margin * knee;
	const double ax = std::fabs(x);
	if (ax <= k)
		return x;
	const double r = margin - k;
	const double y = k + r * (1.0 - std::exp(-(ax - k) / r));
	return std::copysign(y, x);
}

double AxisFilter::step(double m, double dt)
{
	/* adaptive velocity smoothing: fast motion -> less smoothing (no lag) */
	const double tau = tau_v / (1.0 + std::fabs(v_s) / v0);
	const double a = dt / (tau + dt);
	v_s += (m - v_s) * a;

	/* cancel the non-smooth part of the motion */
	c -= (m - v_s);

	/* spring back toward the true position */
	const double k = dt / (tau_c + dt);
	c *= (1.0 - k);

	c = soft_limit(c, margin, knee);
	return c;
}

double AxisFilter::relax(double dt)
{
	/* without a measurement we cannot know the motion: assume the smoothed
	 * velocity continues (keeps a turn smooth) and relax faster than usual */
	const double k = dt / (tau_c * 0.5 + dt);
	c *= (1.0 - k);
	v_s *= (1.0 - k);
	c = soft_limit(c, margin, knee);
	return c;
}

void Stabilizer::configure(int width, int height, const StabilizerParams &p)
{
	p_ = p;
	w_ = width;
	h_ = height;

	const double zoom = std::max(1.0, p.zoom);
	const double margin_x = (1.0 - 1.0 / zoom) * width / 2.0;
	const double margin_y = (1.0 - 1.0 / zoom) * height / 2.0;

	/* reserve part of the margin for the rotation (corner displacement) */
	const double max_rot = p.max_rot_deg * 3.14159265358979323846 / 180.0;
	const double rot_px = max_rot * std::hypot((double)width, (double)height) / 2.0;
	const double rot_frac = std::min(0.25, rot_px / std::max(margin_y, 1e-6));

	fx_.tau_v = fy_.tau_v = fr_.tau_v = p.tau_v;
	fx_.tau_c = fy_.tau_c = fr_.tau_c = p.tau_c;
	fx_.v0 = fy_.v0 = p.v0;
	fr_.v0 = p.v0 * 0.002;
	fx_.margin = margin_x * (1.0 - rot_frac);
	fy_.margin = margin_y * (1.0 - rot_frac);
	fr_.margin = max_rot;

	/* keep state inside the (possibly smaller) new margins */
	fx_.c = soft_limit(fx_.c, fx_.margin, fx_.knee);
	fy_.c = soft_limit(fy_.c, fy_.margin, fy_.knee);
	fr_.c = soft_limit(fr_.c, fr_.margin, fr_.knee);
}

void Stabilizer::reset()
{
	fx_.reset();
	fy_.reset();
	fr_.reset();
}

Correction Stabilizer::scaled() const
{
	Correction c;
	c.x = fx_.c * p_.strength;
	c.y = fy_.c * p_.strength;
	c.rot = fr_.c * p_.strength;
	return c;
}

Correction Stabilizer::current() const
{
	return scaled();
}

Correction Stabilizer::step(double dx, double dy, double drot, double dt)
{
	fx_.step(dx, dt);
	fy_.step(dy, dt);
	if (fr_.margin > 0.0)
		fr_.step(drot, dt);
	else
		fr_.c = 0.0;
	return scaled();
}

Correction Stabilizer::relax(double dt)
{
	fx_.relax(dt);
	fy_.relax(dt);
	fr_.relax(dt);
	return scaled();
}

} // namespace vrstab
