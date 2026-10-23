#include "../src/stabilizer.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

static void check(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}

int main()
{
	const double dt = 1.0 / 60.0;
	vrstab::AxisFilter axis;
	axis.c = -30.0;
	axis.step(8.0, dt);
	check(axis.v_s > 2.0, "outward turn must follow before reaching the boundary");

	axis.reset();
	double old_v = 0.0, old_c = 0.0;
	for (int i = 0; i < 300; ++i) {
		const double m = std::sin(i * 1.7) * 0.5;
		const double tau = axis.tau_v / (1.0 + std::fabs(old_v) / axis.v0);
		old_v += (m - old_v) * dt / (tau + dt);
		old_c = vrstab::soft_limit((old_c - (m - old_v)) / (1.0 + dt / axis.tau_c),
					 axis.margin, axis.knee);
		check(std::fabs(axis.step(m, dt) - old_c) < 1e-12, "small vibration behavior changed");
	}
	vrstab::Stabilizer stab;
	vrstab::StabilizerParams params;
	stab.configure(1920, 1080, params);
	double peak = params.zoom;
	for (int i = 0; i < 300; ++i) {
		const auto c = stab.step(i < 120 ? 15.0 : -15.0, 8.0, 0.003, dt);
		check(c.zoom >= params.zoom && c.zoom <= params.zoom + 0.05 + 1e-12, "zoom outside bounds");
		peak = std::fmax(peak, c.zoom);
		for (int x : {-1, 1}) for (int y : {-1, 1}) {
			const double qx = x * 960.0 / c.zoom - c.x;
			const double qy = y * 540.0 / c.zoom - c.y;
			check(std::fabs(std::cos(c.rot) * qx + std::sin(c.rot) * qy) <= 960.0 + 1e-8,
			      "horizontal black border");
			check(std::fabs(-std::sin(c.rot) * qx + std::cos(c.rot) * qy) <= 540.0 + 1e-8,
			      "vertical black border");
		}
	}
	check(peak > params.zoom, "turns must activate dynamic zoom");
	const double before = stab.current().zoom;
	const auto after = stab.relax(dt);
	check(after.zoom >= before - 0.002, "zoom release too abrupt");
	for (int i = 0; i < 1200; ++i) stab.step(0, 0, 0, dt);
	check(std::fabs(stab.current().zoom - params.zoom) < 1e-8, "zoom did not recover");
	stab.reset();
	check(stab.current().zoom == params.zoom, "reset must restore baseline zoom");
	params.strength = 0.0;
	stab.configure(1920, 1080, params);
	check(stab.step(100, 100, 0.1, dt).zoom == params.zoom, "zero strength must keep baseline crop");
	std::cout << "stabilizer tests passed\n";
}
