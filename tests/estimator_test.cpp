/*
Offline test harness for the motion estimator and the stabilizer filter.
Builds without libobs.

  vrstab-test synth <raw_gray_file> <w> <h>
      Takes the first frame, shifts/rotates it by known amounts and checks
      that the estimator recovers them.

  vrstab-test video <raw_gray_file> <w> <h> <full_w> [csv_out]
      Runs the estimator over all frames (8-bit gray, tightly packed, as
      produced by: ffmpeg -i in.mp4 -vf scale=W:H -pix_fmt gray -f rawvideo out.gray)
      and prints per-frame tx, ty (scaled to full_w) and rot, plus timing.
*/

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../src/motion-estimator.h"
#include "../src/stabilizer.h"

using namespace vrstab;

static MotionEstimator::Config config_from_env()
{
	MotionEstimator::Config cfg;
	if (const char *v = getenv("VRSTAB_LEVELS"))
		cfg.levels = atoi(v);
	if (const char *v = getenv("VRSTAB_ITERS"))
		cfg.iterations = atoi(v);
	if (const char *v = getenv("VRSTAB_STEP0"))
		cfg.step0 = atoi(v);
	if (const char *v = getenv("VRSTAB_MAP"))
		cfg.use_map = atoi(v) != 0;
	return cfg;
}

static std::vector<uint8_t> read_file(const char *path)
{
	std::vector<uint8_t> buf;
	FILE *f = fopen(path, "rb");
	if (!f)
		return buf;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf.resize((size_t)n);
	if (fread(buf.data(), 1, (size_t)n, f) != (size_t)n)
		buf.clear();
	fclose(f);
	return buf;
}

/* out(x) = in(R(-rot)(x - c) + c - t) : content moved by t and rotated by rot */
static void warp_image(const uint8_t *in, uint8_t *out, int w, int h, double tx, double ty, double rot)
{
	const double cx = (w - 1) * 0.5, cy = (h - 1) * 0.5;
	const double cs = std::cos(rot), sn = std::sin(rot);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const double qx = x - cx - tx, qy = y - cy - ty;
			const double sx = cs * qx + sn * qy + cx;
			const double sy = -sn * qx + cs * qy + cy;
			int ix = (int)std::floor(sx), iy = (int)std::floor(sy);
			double fx = sx - ix, fy = sy - iy;
			auto px = [&](int xx, int yy) {
				xx = xx < 0 ? 0 : (xx >= w ? w - 1 : xx);
				yy = yy < 0 ? 0 : (yy >= h ? h - 1 : yy);
				return (double)in[(size_t)yy * w + xx];
			};
			const double v = px(ix, iy) * (1 - fx) * (1 - fy) + px(ix + 1, iy) * fx * (1 - fy) +
					 px(ix, iy + 1) * (1 - fx) * fy + px(ix + 1, iy + 1) * fx * fy;
			out[(size_t)y * w + x] = (uint8_t)(v + 0.5);
		}
	}
}

static int run_synth(const char *path, int w, int h)
{
	std::vector<uint8_t> data = read_file(path);
	if (data.size() < (size_t)w * h) {
		fprintf(stderr, "cannot read %s\n", path);
		return 1;
	}
	std::vector<uint8_t> a(data.begin(), data.begin() + (size_t)w * h), b((size_t)w * h);

	const double cases[][3] = {{0.0, 0.0, 0.0},   {1.0, 0.0, 0.0},    {2.3, -1.7, 0.0},      {-6.5, 3.2, 0.0},
				   {0.0, 0.0, 0.006}, {3.1, 2.2, -0.004}, {12.0, -7.0, 0.0},     {25.0, 10.0, 0.01},
				   {0.4, 0.3, 0.0},   {-0.6, 0.2, 0.002}};
	int failures = 0;
	for (const auto &c : cases) {
		MotionEstimator est;
		MotionEstimator::Config cfg = config_from_env();
		est.configure(w, h, cfg);
		MotionEstimator::Result r;
		est.estimate(a.data(), w, r);
		warp_image(a.data(), b.data(), w, h, c[0], c[1], c[2]);
		const bool ok = est.estimate(b.data(), w, r);
		const double ex = std::fabs(r.tx - c[0]), ey = std::fabs(r.ty - c[1]), er = std::fabs(r.rot - c[2]);
		const bool pass = ok && ex < 0.15 && ey < 0.15 && er < 0.0005;
		if (!pass)
			failures++;
		printf("%s expected (%6.2f %6.2f %8.5f) got (%6.2f %6.2f %8.5f) conf %.2f res %.1f it %d\n",
		       pass ? "PASS" : "FAIL", c[0], c[1], c[2], r.tx, r.ty, r.rot, r.confidence, r.residual,
		       r.iterations);
	}

	/* cut detection: unrelated frames should yield low confidence */
	{
		MotionEstimator est;
		MotionEstimator::Config cfg;
		est.configure(w, h, cfg);
		MotionEstimator::Result r;
		est.estimate(a.data(), w, r);
		for (size_t i = 0; i < b.size(); i++)
			b[i] = (uint8_t)((i * 7919u) & 0xff);
		est.estimate(b.data(), w, r);
		printf("%s noise frame: conf %.2f res %.1f\n", r.confidence < 0.4 ? "PASS" : "FAIL", r.confidence,
		       r.residual);
		if (r.confidence >= 0.4)
			failures++;
	}
	printf("%d failures\n", failures);
	return failures ? 1 : 0;
}

static int run_video(const char *path, int w, int h, int full_w, const char *csv_out)
{
	std::vector<uint8_t> data = read_file(path);
	const size_t frame_size = (size_t)w * h;
	const size_t n = data.size() / frame_size;
	if (n < 2) {
		fprintf(stderr, "cannot read %s\n", path);
		return 1;
	}
	FILE *csv = csv_out ? fopen(csv_out, "w") : nullptr;
	if (csv)
		fprintf(csv, "frame,dx,dy,rot_rad,confidence,residual,iterations\n");

	MotionEstimator est;
	MotionEstimator::Config cfg = config_from_env();
	est.configure(w, h, cfg);
	Stabilizer stab;
	StabilizerParams sp;
	stab.configure(full_w, full_w * h / w, sp);
	const double k = (double)full_w / w;

	double total_ms = 0.0, max_ms = 0.0, conf_sum = 0.0;
	int lowconf = 0;
	for (size_t i = 0; i < n; i++) {
		MotionEstimator::Result r;
		const auto t0 = std::chrono::steady_clock::now();
		const bool ok = est.estimate(data.data() + i * frame_size, w, r);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		if (i > 0) {
			total_ms += ms;
			max_ms = std::max(max_ms, ms);
			conf_sum += r.confidence;
			if (!ok || r.confidence < 0.25)
				lowconf++;
			if (csv)
				fprintf(csv, "%zu,%.4f,%.4f,%.6f,%.3f,%.2f,%d\n", i, r.tx * k, r.ty * k, r.rot,
					r.confidence, r.residual, r.iterations);
			if (ok)
				stab.step(r.tx * k, r.ty * k, r.rot, 1.0 / 60.0);
			else
				stab.relax(1.0 / 60.0);
		}
	}
	if (csv)
		fclose(csv);
	if (const char *mp = getenv("VRSTAB_MAP_OUT")) {
		const std::vector<float> &m = est.consistency_map();
		if (!m.empty()) {
			FILE *pf = fopen(mp, "wb");
			if (pf) {
				fprintf(pf, "P5\n%d %d\n255\n", w, h);
				for (float v : m)
					fputc((int)(std::max(0.0f, std::min(1.0f, v)) * 255.0f), pf);
				fclose(pf);
			}
		}
	}
	printf("frames %zu | mean %.3f ms | max %.3f ms | mean confidence %.3f | low-confidence frames %d\n", n,
	       total_ms / (double)(n - 1), max_ms, conf_sum / (double)(n - 1), lowconf);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 5 && strcmp(argv[1], "synth") == 0)
		return run_synth(argv[2], atoi(argv[3]), atoi(argv[4]));
	if (argc >= 6 && strcmp(argv[1], "video") == 0)
		return run_video(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), argc >= 7 ? argv[6] : nullptr);
	fprintf(stderr, "usage:\n  vrstab-test synth <raw_gray> <w> <h>\n  vrstab-test video <raw_gray> <w> <h> <full_w> [csv]\n");
	return 2;
}
