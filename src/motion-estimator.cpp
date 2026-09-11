/*
VR Head Stabilizer for OBS - global rigid motion estimator
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

#include "motion-estimator.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vrstab {

void MotionEstimator::configure(int width, int height, const Config &cfg)
{
	cfg_ = cfg;
	cfg_.levels = std::max(1, std::min(cfg.levels, 5));
	cfg_.iterations = std::max(1, std::min(cfg.iterations, 20));
	cfg_.step0 = std::max(1, std::min(cfg.step0, 4));

	if (width != w_ || height != h_) {
		w_ = width;
		h_ = height;
		prev_.clear();
		cur_.clear();
		has_prev_ = false;
	}
	/* make sure the coarsest level is still large enough */
	while (cfg_.levels > 1 && ((w_ >> (cfg_.levels - 1)) < 24 || (h_ >> (cfg_.levels - 1)) < 16))
		cfg_.levels--;
	hist_.assign(256, 0);

	map_.clear();
	map_w_.clear();
	map_h_.clear();
	if (cfg_.use_map && w_ > 0 && h_ > 0) {
		int lw = w_, lh = h_;
		for (int l = 0; l < cfg_.levels; l++) {
			map_.emplace_back((size_t)lw * lh, 1.0f);
			map_w_.push_back(lw);
			map_h_.push_back(lh);
			lw /= 2;
			lh /= 2;
		}
	}
}

void MotionEstimator::reset()
{
	has_prev_ = false;
	sigma_ = 8.0;
	for (auto &m : map_)
		std::fill(m.begin(), m.end(), 1.0f);
}

void MotionEstimator::update_map(const Level &prev, const Level &cur, double tx, double ty, double rot)
{
	if (map_.empty())
		return;
	std::vector<float> &map = map_[0];
	const int w = prev.w, h = prev.h;
	const double cx = (w - 1) * 0.5, cy = (h - 1) * 0.5;
	const double cs = std::cos(rot), sn = std::sin(rot);
	/*
	 * The residual is normalized by the local gradient so it measures the
	 * misfit in pixels rather than in gray levels: a textured region that
	 * moves independently of the rigid model shows a misfit of several
	 * pixels, while the cockpit stays well below one pixel. Flat regions
	 * carry no information and keep their weight.
	 */
	const float sigma_px = (float)cfg_.map_sigma;
	const float inv2s2 = 1.0f / (2.0f * sigma_px * sigma_px);
	const float rate = (float)cfg_.map_rate;
	const float floor_w = (float)cfg_.map_floor;
	const float min_grad = 3.0f;

	for (int y = 1; y < h - 1; y++) {
		const float *prow = &prev.img[(size_t)y * w];
		const float *gxr = &prev.gx[(size_t)y * w];
		const float *gyr = &prev.gy[(size_t)y * w];
		float *mrow = &map[(size_t)y * w];
		const double ry = y - cy;
		for (int x = 1; x < w - 1; x++) {
			const float gmag = std::sqrt(gxr[x] * gxr[x] + gyr[x] * gyr[x]);
			if (gmag < min_grad)
				continue;
			const double rx = x - cx;
			const double wx = cs * rx - sn * ry + cx + tx;
			const double wy = sn * rx + cs * ry + cy + ty;
			if (wx < 1.0 || wy < 1.0 || wx >= w - 2.0 || wy >= h - 2.0)
				continue;
			const int ix = (int)wx, iy = (int)wy;
			const float fx = (float)(wx - ix), fy = (float)(wy - iy);
			const size_t i00 = (size_t)iy * w + ix;
			const float I1 = cur.img[i00] * (1 - fx) * (1 - fy) + cur.img[i00 + 1] * fx * (1 - fy) +
					 cur.img[i00 + w] * (1 - fx) * fy + cur.img[i00 + w + 1] * fx * fy;
			const float r = I1 - prow[x];
			float d = std::fabs(r) / gmag; /* misfit in pixels */
			if (d > 8.0f)
				d = 8.0f;
			const float s = std::exp(-d * d * inv2s2);
			float m = mrow[x] + (s - mrow[x]) * rate;
			mrow[x] = m < floor_w ? floor_w : m;
		}
	}
}

void MotionEstimator::build_map_pyramid()
{
	for (size_t l = 1; l < map_.size(); l++) {
		const std::vector<float> &src = map_[l - 1];
		std::vector<float> &dst = map_[l];
		const int sw = map_w_[l - 1], dw = map_w_[l], dh = map_h_[l];
		for (int y = 0; y < dh; y++) {
			const float *r0 = &src[(size_t)(2 * y) * sw];
			const float *r1 = r0 + sw;
			float *d = &dst[(size_t)y * dw];
			for (int x = 0; x < dw; x++) {
				const int sx = 2 * x;
				d[x] = 0.25f * (r0[sx] + r0[sx + 1] + r1[sx] + r1[sx + 1]);
			}
		}
	}
}

void MotionEstimator::downsample(const Level &src, Level &dst)
{
	dst.w = src.w / 2;
	dst.h = src.h / 2;
	dst.img.resize((size_t)dst.w * dst.h);
	for (int y = 0; y < dst.h; y++) {
		const float *r0 = &src.img[(size_t)(2 * y) * src.w];
		const float *r1 = r0 + src.w;
		float *d = &dst.img[(size_t)y * dst.w];
		for (int x = 0; x < dst.w; x++) {
			const int sx = 2 * x;
			d[x] = 0.25f * (r0[sx] + r0[sx + 1] + r1[sx] + r1[sx + 1]);
		}
	}
}

void MotionEstimator::gradients(Level &l)
{
	const size_t n = (size_t)l.w * l.h;
	l.gx.resize(n);
	l.gy.resize(n);
	for (int y = 0; y < l.h; y++) {
		const int ym = std::max(y - 1, 0), yp = std::min(y + 1, l.h - 1);
		const float *row = &l.img[(size_t)y * l.w];
		const float *rm = &l.img[(size_t)ym * l.w];
		const float *rp = &l.img[(size_t)yp * l.w];
		float *gx = &l.gx[(size_t)y * l.w];
		float *gy = &l.gy[(size_t)y * l.w];
		for (int x = 0; x < l.w; x++) {
			const int xm = std::max(x - 1, 0), xp = std::min(x + 1, l.w - 1);
			gx[x] = 0.5f * (row[xp] - row[xm]);
			gy[x] = 0.5f * (rp[x] - rm[x]);
		}
	}
}

void MotionEstimator::build_pyramid(const uint8_t *gray, int stride, std::vector<Level> &pyr)
{
	pyr.resize(cfg_.levels);
	Level &l0 = pyr[0];
	l0.w = w_;
	l0.h = h_;
	l0.img.resize((size_t)w_ * h_);
	for (int y = 0; y < h_; y++) {
		const uint8_t *s = gray + (size_t)y * stride;
		float *d = &l0.img[(size_t)y * w_];
		for (int x = 0; x < w_; x++)
			d[x] = (float)s[x];
	}
	for (int i = 1; i < cfg_.levels; i++)
		downsample(pyr[i - 1], pyr[i]);
	for (int i = 0; i < cfg_.levels; i++)
		gradients(pyr[i]);
}

static inline bool solve3(const double H[6], const double b[3], double x[3])
{
	/* H is symmetric: [h00 h01 h02; h01 h11 h12; h02 h12 h22] */
	const double a = H[0], bb = H[1], c = H[2], d = H[3], e = H[4], f = H[5];
	const double det = a * (d * f - e * e) - bb * (bb * f - e * c) + c * (bb * e - d * c);
	if (std::fabs(det) < 1e-12)
		return false;
	const double inv = 1.0 / det;
	const double i00 = (d * f - e * e) * inv;
	const double i01 = (c * e - bb * f) * inv;
	const double i02 = (bb * e - c * d) * inv;
	const double i11 = (a * f - c * c) * inv;
	const double i12 = (bb * c - a * e) * inv;
	const double i22 = (a * d - bb * bb) * inv;
	x[0] = i00 * b[0] + i01 * b[1] + i02 * b[2];
	x[1] = i01 * b[0] + i11 * b[1] + i12 * b[2];
	x[2] = i02 * b[0] + i12 * b[1] + i22 * b[2];
	return true;
}

bool MotionEstimator::align_level(const Level &prev, const Level &cur, const float *map, int step, double &tx,
				  double &ty, double &rot, Result &out, bool final_level)
{
	const int w = prev.w, h = prev.h;
	const double cx = (w - 1) * 0.5, cy = (h - 1) * 0.5;
	const int border = 2;
	bool ok = false;

	for (int it = 0; it < cfg_.iterations; it++) {
		const double cs = std::cos(rot), sn = std::sin(rot);
		double H[6] = {0, 0, 0, 0, 0, 0};
		double b[3] = {0, 0, 0};
		double sum_w = 0.0, sum_abs = 0.0, sum_map = 0.0;
		int n = 0;
		std::fill(hist_.begin(), hist_.end(), 0u);
		const double inv_sigma2 = 1.0 / (sigma_ * sigma_);

		for (int y = border; y < h - border; y += step) {
			const float *prow = &prev.img[(size_t)y * w];
			const double ry = y - cy;
			for (int x = border; x < w - border; x += step) {
				const double rx = x - cx;
				/* warped position in the current frame */
				const double wx = cs * rx - sn * ry + cx + tx;
				const double wy = sn * rx + cs * ry + cy + ty;
				if (wx < 1.0 || wy < 1.0 || wx >= w - 2.0 || wy >= h - 2.0)
					continue;
				const int ix = (int)wx, iy = (int)wy;
				const float fx = (float)(wx - ix), fy = (float)(wy - iy);
				const size_t i00 = (size_t)iy * w + ix;
				const float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy,
					    w11 = fx * fy;
				const float I1 = cur.img[i00] * w00 + cur.img[i00 + 1] * w10 + cur.img[i00 + w] * w01 +
						 cur.img[i00 + w + 1] * w11;
				const float Ix = cur.gx[i00] * w00 + cur.gx[i00 + 1] * w10 + cur.gx[i00 + w] * w01 +
						 cur.gx[i00 + w + 1] * w11;
				const float Iy = cur.gy[i00] * w00 + cur.gy[i00 + 1] * w10 + cur.gy[i00 + w] * w01 +
						 cur.gy[i00 + w + 1] * w11;
				const double r = (double)I1 - (double)prow[x];

				/* robust weight (Geman-McClure style) times the consistency map */
				const double mw = map ? (double)map[(size_t)y * w + x] : 1.0;
				const double wt = mw / (1.0 + r * r * inv_sigma2);

				/* jacobian of W wrt (tx, ty, rot) */
				const double jr = Ix * (-sn * rx - cs * ry) + Iy * (cs * rx - sn * ry);
				const double j0 = Ix, j1 = Iy, j2 = jr;

				H[0] += wt * j0 * j0;
				H[1] += wt * j0 * j1;
				H[2] += wt * j0 * j2;
				H[3] += wt * j1 * j1;
				H[4] += wt * j1 * j2;
				H[5] += wt * j2 * j2;
				b[0] += wt * j0 * r;
				b[1] += wt * j1 * r;
				b[2] += wt * j2 * r;

				sum_w += wt;
				sum_map += mw;
				const double ar = std::fabs(r);
				sum_abs += ar;
				hist_[(size_t)std::min(255.0, ar)]++;
				n++;
			}
		}

		if (n < 64) {
			out.confidence = 0.0;
			return false;
		}

		/* robust scale from the median absolute residual */
		{
			int acc = 0, med = 0;
			for (; med < 256; med++) {
				acc += (int)hist_[med];
				if (acc * 2 >= n)
					break;
			}
			const double s = 1.4826 * (med + 0.5);
			sigma_ = std::max(cfg_.sigma_min, std::min(cfg_.sigma_max, s));
		}

		double d[3];
		if (!solve3(H, b, d)) {
			out.confidence = 0.0;
			return false;
		}
		/* Gauss-Newton step: minimize sum w (r + J dp)^2 -> dp = -H^-1 b */
		tx -= d[0];
		ty -= d[1];
		rot -= d[2];
		out.iterations++;
		ok = true;

		if (final_level) {
			out.residual = sum_abs / n;
			out.valid_pixels = n;
			/* mean robust weight (over the trusted region) tells how much of
			 * the image agrees with the model */
			double conf = sum_w / std::max(1e-9, sum_map);
			/* penalize low texture (weak normal equations) */
			const double texture = (H[0] + H[3]) / std::max(1.0, sum_w);
			if (texture < 4.0)
				conf *= texture / 4.0;
			out.confidence = std::max(0.0, std::min(1.0, conf));
		}

		if (std::fabs(d[0]) < 0.01 && std::fabs(d[1]) < 0.01 && std::fabs(d[2]) < 2e-5)
			break;
	}
	return ok;
}

bool MotionEstimator::estimate(const uint8_t *gray, int stride, Result &out, double prior_tx, double prior_ty,
			       double prior_rot)
{
	out = Result();
	if (w_ <= 0 || h_ <= 0)
		return false;

	build_pyramid(gray, stride, cur_);

	if (!has_prev_) {
		std::swap(prev_, cur_);
		has_prev_ = true;
		return false;
	}

	const int top = cfg_.levels - 1;
	const double scale_top = (double)(1 << top);
	double tx = prior_tx / scale_top, ty = prior_ty / scale_top, rot = prior_rot;

	bool ok = true;
	for (int l = top; l >= 0; l--) {
		const int step = (l == 0) ? cfg_.step0 : 1;
		const float *map = map_.empty() ? nullptr : map_[l].data();
		if (!align_level(prev_[l], cur_[l], map, step, tx, ty, rot, out, l == 0)) {
			ok = false;
			break;
		}
		if (l > 0) {
			tx *= 2.0;
			ty *= 2.0;
		}
	}

	if (ok && (!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(rot)))
		ok = false;

	/* implausible motion for consecutive frames -> treat as a cut */
	if (ok && (std::fabs(tx) > w_ * 0.5 || std::fabs(ty) > h_ * 0.5 || std::fabs(rot) > 0.35))
		ok = false;

	if (ok) {
		out.tx = tx;
		out.ty = ty;
		out.rot = rot;
		if (!map_.empty()) {
			update_map(prev_[0], cur_[0], tx, ty, rot);
			build_map_pyramid();
		}
	} else {
		out.tx = out.ty = out.rot = 0.0;
		out.confidence = 0.0;
		/* scene cut or garbage: forget what we learned */
		for (auto &m : map_)
			std::fill(m.begin(), m.end(), 1.0f);
	}

	std::swap(prev_, cur_);
	return ok;
}

} // namespace vrstab
