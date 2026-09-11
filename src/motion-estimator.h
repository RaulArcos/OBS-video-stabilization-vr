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

#pragma once

#include <cstdint>
#include <vector>

namespace vrstab {

/*
 * Estimates the rigid 2D motion (translation + rotation about the image
 * center) between the previous and the current grayscale frame using a
 * pyramidal, robustly weighted Lucas-Kanade (Gauss-Newton) alignment.
 *
 * Convention: a point x in the previous frame is found at
 *   W(x) = R(rot) * (x - center) + center + (tx, ty)
 * in the current frame, i.e. (tx, ty, rot) is how the content moved.
 * y axis points down; positive rot rotates the x axis toward the y axis.
 */
class MotionEstimator {
public:
	struct Result {
		double tx = 0.0;         /* px at analysis resolution */
		double ty = 0.0;         /* px at analysis resolution */
		double rot = 0.0;        /* radians */
		double confidence = 0.0; /* 0..1 */
		double residual = 0.0;   /* mean abs gray-level residual after alignment */
		int iterations = 0;
		int valid_pixels = 0;
	};

	struct Config {
		int levels = 4;     /* pyramid levels */
		int iterations = 8; /* max Gauss-Newton iterations per level (early exit on convergence) */
		int step0 = 2;      /* pixel subsampling at the finest level (1 = all pixels) */
		double sigma_min = 4.0;
		double sigma_max = 40.0;
		/*
		 * Temporal consistency map: pixels whose residual after the rigid
		 * alignment stays low over time (the cockpit, which only moves with
		 * the head) gain weight; pixels with independent motion (the world
		 * moving past, mirrors, other cars) lose weight.
		 */
		bool use_map = true;
		double map_rate = 0.05;  /* per-frame learning rate */
		double map_floor = 0.05; /* minimum weight so regions can be re-acquired */
		double map_sigma = 1.0;  /* misfit in pixels considered "consistent" */
	};

	void configure(int width, int height, const Config &cfg);
	void reset();

	int width() const { return w_; }
	int height() const { return h_; }
	bool has_previous() const { return has_prev_; }
	/* consistency map at analysis resolution (empty when disabled) */
	const std::vector<float> &consistency_map() const { return map_.empty() ? empty_map_ : map_[0]; }

	/*
	 * Feed the current frame (8-bit gray, row stride in bytes). The prior is
	 * the expected motion (e.g. last smoothed velocity) used as the starting
	 * point at the coarsest level. Returns false when there is no previous
	 * frame yet (the frame is stored and becomes the reference).
	 */
	bool estimate(const uint8_t *gray, int stride, Result &out, double prior_tx = 0.0, double prior_ty = 0.0,
		      double prior_rot = 0.0);

private:
	struct Level {
		int w = 0, h = 0;
		std::vector<float> img; /* intensity */
		std::vector<float> gx;  /* d/dx */
		std::vector<float> gy;  /* d/dy */
	};

	void build_pyramid(const uint8_t *gray, int stride, std::vector<Level> &pyr);
	static void downsample(const Level &src, Level &dst);
	static void gradients(Level &l);
	bool align_level(const Level &prev, const Level &cur, const float *map, int step, double &tx, double &ty,
			 double &rot, Result &out, bool final_level);
	void update_map(const Level &prev, const Level &cur, double tx, double ty, double rot);
	void build_map_pyramid();

	int w_ = 0, h_ = 0;
	Config cfg_;
	std::vector<Level> prev_, cur_;
	bool has_prev_ = false;
	double sigma_ = 8.0; /* robust scale carried between iterations/frames */
	std::vector<uint32_t> hist_;
	std::vector<std::vector<float>> map_; /* per level, same layout as Level::img */
	std::vector<int> map_w_, map_h_;
	std::vector<float> empty_map_;
};

} // namespace vrstab
