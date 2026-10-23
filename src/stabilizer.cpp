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

	/* Only release outward motion near the edge; leave central tremor unchanged.
	 * Smoothstep has zero slope at both ends, avoiding a follow-mode switch. */
	const double residual = m - v_s;
	if (margin > 0.0 && c * residual < 0.0) {
		const double t = std::clamp((std::fabs(c) / margin - knee) / (1.0 - knee), 0.0, 1.0);
		v_s += residual * t * t * (3.0 - 2.0 * t);
	}

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

	zoom_ = std::clamp(zoom_, std::max(1.0, p.zoom), std::max(1.0, p.zoom) + 0.05);
	const double zoom = std::max(1.0, p.zoom) + 0.05;
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
	zoom_ = std::max(1.0, p_.zoom);
}

/* Exact coverage for the inverse rigid transform used by warp.effect. */
static double required_zoom(const Correction &c, int width, int height)
{
	const double co = std::cos(c.rot), si = std::sin(c.rot);
	const double dx = std::fabs(co * c.x + si * c.y);
	const double dy = std::fabs(-si * c.x + co * c.y);
	const double avail_x = width - 2.0 * dx, avail_y = height - 2.0 * dy;
	if (avail_x <= 0.0 || avail_y <= 0.0)
		return 1e9;
	return std::max((std::fabs(co) * width + std::fabs(si) * height) / avail_x,
			(std::fabs(si) * width + std::fabs(co) * height) / avail_y);
}

Correction Stabilizer::scaled() const
{
	Correction c;
	c.x = fx_.c * p_.strength;
	c.y = fy_.c * p_.strength;
	c.rot = fr_.c * p_.strength;
	const double cap = std::max(1.0, p_.zoom) + 0.05;
	if (w_ > 0 && h_ > 0 && required_zoom(c, w_, h_) > cap) {
		/* Combined roll and translation can exhaust the same crop budget.
		 * Reduce the rigid correction, never stretch pixels or exceed the cap. */
		double lo = 0.0, hi = 1.0;
		for (int i = 0; i < 32; ++i) {
			const double mid = (lo + hi) * 0.5;
			Correction candidate = c;
			candidate.x *= mid;
			candidate.y *= mid;
			candidate.rot *= mid;
			if (required_zoom(candidate, w_, h_) <= cap)
				lo = mid;
			else
				hi = mid;
		}
		c.x *= lo;
		c.y *= lo;
		c.rot *= lo;
	}
	c.zoom = zoom_;
	return c;
}

void Stabilizer::update_zoom(double dt)
{
	const double baseline = std::max(1.0, p_.zoom);
	const double needed = w_ > 0 && h_ > 0 ? std::max(baseline, required_zoom(scaled(), w_, h_)) : baseline;
	/* Immediate attack guarantees coverage; slow release avoids pumping. */
	zoom_ = std::clamp(std::max(needed, zoom_ + (needed - zoom_) * dt / (0.8 + dt)), baseline,
			   baseline + 0.05);
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
	update_zoom(dt);
	return scaled();
}

Correction Stabilizer::relax(double dt)
{
	fx_.relax(dt);
	fy_.relax(dt);
	fr_.relax(dt);
	update_zoom(dt);
	return scaled();
}

} // namespace vrstab
