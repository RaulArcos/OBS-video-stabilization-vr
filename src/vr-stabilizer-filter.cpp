/*
VR Head Stabilizer for OBS - the OBS video filter
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

#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/platform.h>
#include <plugin-support.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "motion-estimator.h"
#include "stabilizer.h"

#define S_ZOOM "zoom_percent"
#define S_STRENGTH "strength"
#define S_SMOOTHING "smoothing"
#define S_FOLLOW "follow_speed"
#define S_RECENTER "recenter_time"
#define S_MAX_ROT "max_rotation"
#define S_ANALYSIS_W "analysis_width"
#define S_BYPASS "bypass"
#define S_DEBUG "debug_log"

#define T_(k) obs_module_text(k)

using namespace vrstab;

struct vrstab_filter {
	obs_source_t *context = nullptr;

	/* GPU resources */
	gs_texrender_t *full = nullptr;
	gs_texrender_t *small = nullptr;
	gs_stagesurf_t *stage = nullptr;
	gs_effect_t *luma_effect = nullptr;
	gs_effect_t *warp_effect = nullptr;
	gs_eparam_t *p_luma_image = nullptr, *p_luma_texel = nullptr, *p_luma_taps = nullptr;
	gs_eparam_t *p_warp_image = nullptr, *p_warp_size = nullptr, *p_warp_offset = nullptr, *p_warp_cos_sin = nullptr,
		    *p_warp_inv_zoom = nullptr, *p_warp_multiplier = nullptr;
	enum gs_color_space full_space = GS_CS_SRGB;

	/* frame geometry */
	uint32_t cx = 0, cy = 0; /* target size */
	uint32_t sw = 0, sh = 0; /* analysis size */
	bool target_valid = false;
	bool processed_frame = false;
	float tick_dt = 1.0f / 60.0f;

	/* settings */
	double zoom = 1.05;
	double strength = 1.0;
	double smoothing = 0.3;
	double follow = 4.0;
	double recenter = 0.6;
	double max_rot = 0.4;
	int analysis_width = 320;
	bool bypass = false;
	bool debug = false;

	/* processing */
	std::vector<uint8_t> gray;
	MotionEstimator est;
	Stabilizer stab;
	Correction cur;
	bool params_dirty = true;

	/* stats */
	double acc_est_ms = 0.0, acc_map_ms = 0.0, acc_conf = 0.0;
	double acc_cx = 0.0, acc_cy = 0.0;
	int acc_n = 0, acc_lowconf = 0;
	double last_log = 0.0;
};

static const char *vrstab_get_name(void *)
{
	return T_("VRStabilizer.Name");
}

static void vrstab_free_gpu(vrstab_filter *f)
{
	obs_enter_graphics();
	if (f->full)
		gs_texrender_destroy(f->full);
	if (f->small)
		gs_texrender_destroy(f->small);
	if (f->stage)
		gs_stagesurface_destroy(f->stage);
	if (f->luma_effect)
		gs_effect_destroy(f->luma_effect);
	if (f->warp_effect)
		gs_effect_destroy(f->warp_effect);
	f->full = f->small = nullptr;
	f->stage = nullptr;
	f->luma_effect = f->warp_effect = nullptr;
	obs_leave_graphics();
}

static gs_effect_t *load_effect(const char *name)
{
	char *path = obs_module_file(name);
	if (!path) {
		obs_log(LOG_ERROR, "effect file not found: %s", name);
		return nullptr;
	}
	char *err = nullptr;
	gs_effect_t *effect = gs_effect_create_from_file(path, &err);
	if (!effect)
		obs_log(LOG_ERROR, "failed to load effect %s: %s", name, err ? err : "unknown error");
	bfree(err);
	bfree(path);
	return effect;
}

static void vrstab_update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<vrstab_filter *>(data);
	f->zoom = 1.0 + obs_data_get_double(settings, S_ZOOM) / 100.0;
	f->strength = obs_data_get_double(settings, S_STRENGTH) / 100.0;
	f->smoothing = obs_data_get_double(settings, S_SMOOTHING);
	f->follow = obs_data_get_double(settings, S_FOLLOW);
	f->recenter = obs_data_get_double(settings, S_RECENTER);
	f->max_rot = obs_data_get_double(settings, S_MAX_ROT);
	f->analysis_width = (int)obs_data_get_int(settings, S_ANALYSIS_W);
	f->bypass = obs_data_get_bool(settings, S_BYPASS);
	f->debug = obs_data_get_bool(settings, S_DEBUG);
	f->params_dirty = true;
}

static void *vrstab_create(obs_data_t *settings, obs_source_t *context)
{
	auto *f = new vrstab_filter();
	f->context = context;

	obs_enter_graphics();
	f->luma_effect = load_effect("shaders/luma-downsample.effect");
	f->warp_effect = load_effect("shaders/warp.effect");
	if (f->luma_effect) {
		f->p_luma_image = gs_effect_get_param_by_name(f->luma_effect, "image");
		f->p_luma_texel = gs_effect_get_param_by_name(f->luma_effect, "texel");
		f->p_luma_taps = gs_effect_get_param_by_name(f->luma_effect, "taps");
	}
	if (f->warp_effect) {
		f->p_warp_image = gs_effect_get_param_by_name(f->warp_effect, "image");
		f->p_warp_size = gs_effect_get_param_by_name(f->warp_effect, "size");
		f->p_warp_offset = gs_effect_get_param_by_name(f->warp_effect, "offset");
		f->p_warp_cos_sin = gs_effect_get_param_by_name(f->warp_effect, "cos_sin");
		f->p_warp_inv_zoom = gs_effect_get_param_by_name(f->warp_effect, "inv_zoom");
		f->p_warp_multiplier = gs_effect_get_param_by_name(f->warp_effect, "multiplier");
	}
	f->full = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	f->small = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();

	if (!f->luma_effect || !f->warp_effect) {
		obs_log(LOG_ERROR, "VR stabilizer: effects failed to load, the filter will pass video through");
	}

	vrstab_update(f, settings);
	return f;
}

static void vrstab_destroy(void *data)
{
	auto *f = static_cast<vrstab_filter *>(data);
	vrstab_free_gpu(f);
	delete f;
}

static void vrstab_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, S_ZOOM, 5.0);
	obs_data_set_default_double(settings, S_STRENGTH, 100.0);
	obs_data_set_default_double(settings, S_SMOOTHING, 0.3);
	obs_data_set_default_double(settings, S_FOLLOW, 4.0);
	obs_data_set_default_double(settings, S_RECENTER, 0.6);
	obs_data_set_default_double(settings, S_MAX_ROT, 0.4);
	obs_data_set_default_int(settings, S_ANALYSIS_W, 320);
	obs_data_set_default_bool(settings, S_BYPASS, false);
	obs_data_set_default_bool(settings, S_DEBUG, false);
}

static obs_properties_t *vrstab_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	p = obs_properties_add_float_slider(props, S_ZOOM, T_("VRStabilizer.Zoom"), 0.0, 15.0, 0.5);
	obs_property_float_set_suffix(p, " %");
	obs_property_set_long_description(p, T_("VRStabilizer.Zoom.Desc"));

	p = obs_properties_add_float_slider(props, S_STRENGTH, T_("VRStabilizer.Strength"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, " %");
	obs_property_set_long_description(p, T_("VRStabilizer.Strength.Desc"));

	p = obs_properties_add_float_slider(props, S_SMOOTHING, T_("VRStabilizer.Smoothing"), 0.05, 1.0, 0.01);
	obs_property_float_set_suffix(p, " s");
	obs_property_set_long_description(p, T_("VRStabilizer.Smoothing.Desc"));

	p = obs_properties_add_float_slider(props, S_MAX_ROT, T_("VRStabilizer.MaxRotation"), 0.0, 2.0, 0.05);
	obs_property_float_set_suffix(p, " °");
	obs_property_set_long_description(p, T_("VRStabilizer.MaxRotation.Desc"));

	obs_properties_t *adv = obs_properties_create();

	p = obs_properties_add_float_slider(adv, S_FOLLOW, T_("VRStabilizer.Follow"), 0.5, 16.0, 0.5);
	obs_property_float_set_suffix(p, " px/frame");
	obs_property_set_long_description(p, T_("VRStabilizer.Follow.Desc"));

	p = obs_properties_add_float_slider(adv, S_RECENTER, T_("VRStabilizer.Recenter"), 0.1, 3.0, 0.05);
	obs_property_float_set_suffix(p, " s");
	obs_property_set_long_description(p, T_("VRStabilizer.Recenter.Desc"));

	p = obs_properties_add_list(adv, S_ANALYSIS_W, T_("VRStabilizer.AnalysisWidth"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, "240", 240);
	obs_property_list_add_int(p, "320", 320);
	obs_property_list_add_int(p, "400", 400);
	obs_property_list_add_int(p, "480", 480);
	obs_property_set_long_description(p, T_("VRStabilizer.AnalysisWidth.Desc"));

	p = obs_properties_add_bool(adv, S_BYPASS, T_("VRStabilizer.Bypass"));
	obs_property_set_long_description(p, T_("VRStabilizer.Bypass.Desc"));

	obs_properties_add_bool(adv, S_DEBUG, T_("VRStabilizer.Debug"));

	obs_properties_add_group(props, "advanced", T_("VRStabilizer.Group.Advanced"), OBS_GROUP_NORMAL, adv);
	return props;
}

static void vrstab_check_size(vrstab_filter *f)
{
	obs_source_t *target = obs_filter_get_target(f->context);
	const uint32_t cx = target ? obs_source_get_base_width(target) : 0;
	const uint32_t cy = target ? obs_source_get_base_height(target) : 0;

	f->target_valid = cx > 0 && cy > 0;
	if (!f->target_valid)
		return;

	uint32_t sw = (uint32_t)std::max(64, std::min(f->analysis_width, (int)cx));
	uint32_t sh = std::max(36u, (uint32_t)std::lround((double)cy * sw / cx));
	sw &= ~1u;
	sh &= ~1u;

	if (cx != f->cx || cy != f->cy || sw != f->sw || sh != f->sh) {
		f->cx = cx;
		f->cy = cy;
		f->sw = sw;
		f->sh = sh;

		obs_enter_graphics();
		if (f->stage)
			gs_stagesurface_destroy(f->stage);
		f->stage = gs_stagesurface_create(sw, sh, GS_RGBA);
		obs_leave_graphics();

		f->gray.assign((size_t)sw * sh, 0);
		MotionEstimator::Config cfg;
		cfg.levels = 4;
		cfg.iterations = 8;
		cfg.step0 = 2;
		f->est.configure((int)sw, (int)sh, cfg);
		f->est.reset();
		f->stab.reset();
		f->params_dirty = true;
		obs_log(LOG_INFO, "VR stabilizer: target %ux%u, analysis %ux%u", cx, cy, sw, sh);
	}

	if (f->params_dirty) {
		StabilizerParams p;
		p.zoom = f->zoom;
		p.strength = f->strength;
		p.tau_v = f->smoothing;
		p.v0 = f->follow;
		p.tau_c = f->recenter;
		p.max_rot_deg = f->max_rot;
		f->stab.configure((int)cx, (int)cy, p);
		f->params_dirty = false;
	}
}

static void vrstab_tick(void *data, float seconds)
{
	auto *f = static_cast<vrstab_filter *>(data);
	f->tick_dt = std::max(1.0f / 240.0f, std::min(seconds, 0.25f));
	f->processed_frame = false;
	vrstab_check_size(f);
}

/* render the filter target into f->full (mirrors obs-filters/gpu-delay.c) */
static bool vrstab_capture_target(vrstab_filter *f)
{
	obs_source_t *target = obs_filter_get_target(f->context);
	obs_source_t *parent = obs_filter_get_parent(f->context);
	if (!target || !parent)
		return false;

	const enum gs_color_space preferred_spaces[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED};
	const enum gs_color_space space =
		obs_source_get_color_space(target, OBS_COUNTOF(preferred_spaces), preferred_spaces);
	const enum gs_color_format format = gs_get_format_from_space(space);
	if (gs_texrender_get_format(f->full) != format) {
		gs_texrender_destroy(f->full);
		f->full = gs_texrender_create(format, GS_ZS_NONE);
	}

	gs_texrender_reset(f->full);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	bool ok = false;
	if (gs_texrender_begin_with_color_space(f->full, f->cx, f->cy, space)) {
		const uint32_t flags = obs_source_get_output_flags(target);
		const bool custom_draw = (flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		const bool async = (flags & OBS_SOURCE_ASYNC) != 0;

		struct vec4 clear_color;
		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, (float)f->cx, 0.0f, (float)f->cy, -100.0f, 100.0f);

		if (target == parent && !custom_draw && !async)
			obs_source_default_render(target);
		else
			obs_source_video_render(target);

		gs_texrender_end(f->full);
		f->full_space = space;
		ok = true;
	}

	gs_blend_state_pop();
	return ok;
}

/* downsample luma to f->small, read it back to f->gray. */
static bool vrstab_readback(vrstab_filter *f, gs_texture_t *full_tex)
{
	gs_texrender_reset(f->small);

	const bool prev_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	bool drawn = false;
	if (gs_texrender_begin(f->small, f->sw, f->sh)) {
		gs_ortho(0.0f, (float)f->sw, 0.0f, (float)f->sh, -100.0f, 100.0f);

		struct vec2 texel;
		vec2_set(&texel, 1.0f / (float)f->cx, 1.0f / (float)f->cy);
		const int ratio = (int)std::lround((double)f->cx / f->sw);
		const int taps = std::max(1, std::min(8, (ratio + 1) / 2));

		gs_effect_set_texture(f->p_luma_image, full_tex);
		gs_effect_set_vec2(f->p_luma_texel, &texel);
		gs_effect_set_int(f->p_luma_taps, taps);

		while (gs_effect_loop(f->luma_effect, "Draw"))
			gs_draw_sprite(full_tex, 0, f->sw, f->sh);

		gs_texrender_end(f->small);
		drawn = true;
	}

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(prev_srgb);

	if (!drawn)
		return false;

	gs_texture_t *small_tex = gs_texrender_get_texture(f->small);
	if (!small_tex || !f->stage)
		return false;

	const auto t0 = std::chrono::steady_clock::now();
	gs_stage_texture(f->stage, small_tex);

	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(f->stage, &data, &linesize))
		return false;

	for (uint32_t y = 0; y < f->sh; y++) {
		const uint8_t *src = data + (size_t)y * linesize;
		uint8_t *dst = &f->gray[(size_t)y * f->sw];
		for (uint32_t x = 0; x < f->sw; x++)
			dst[x] = src[x * 4];
	}
	gs_stagesurface_unmap(f->stage);

	f->acc_map_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	return true;
}

static void vrstab_process(vrstab_filter *f)
{
	const auto t0 = std::chrono::steady_clock::now();

	/* the analysis image is 1/k of the full frame; convert px units */
	const double k = (double)f->cx / (double)f->sw;
	const Correction prev = f->stab.current();
	(void)prev;

	MotionEstimator::Result r;
	/* prior: the motion we expect if the smoothed velocity continues (0 is fine too) */
	const bool ok = f->est.estimate(f->gray.data(), (int)f->sw, r);

	const double dt = (double)f->tick_dt;
	if (ok && r.confidence > 0.25) {
		f->cur = f->stab.step(r.tx * k, r.ty * k, r.rot, dt);
	} else {
		f->cur = f->stab.relax(dt);
		if (f->est.has_previous())
			f->acc_lowconf++;
	}

	f->acc_est_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	f->acc_conf += r.confidence;
	f->acc_cx += std::fabs(f->cur.x);
	f->acc_cy += std::fabs(f->cur.y);
	f->acc_n++;

	if (f->debug) {
		const double now = (double)os_gettime_ns() * 1e-9;
		if (now - f->last_log > 5.0 && f->acc_n > 0) {
			obs_log(LOG_INFO,
				"VR stabilizer: %d frames | estimate %.2f ms | readback %.2f ms | confidence %.2f | "
				"low-confidence %d | mean |corr| x %.1f y %.1f px | margins x %.0f y %.0f px",
				f->acc_n, f->acc_est_ms / f->acc_n, f->acc_map_ms / f->acc_n, f->acc_conf / f->acc_n,
				f->acc_lowconf, f->acc_cx / f->acc_n, f->acc_cy / f->acc_n, f->stab.margin_x(),
				f->stab.margin_y());
			f->last_log = now;
			f->acc_est_ms = f->acc_map_ms = f->acc_conf = f->acc_cx = f->acc_cy = 0.0;
			f->acc_n = f->acc_lowconf = 0;
		}
	}
}

static const char *tech_and_multiplier(enum gs_color_space current, enum gs_color_space source, float *multiplier)
{
	/* our warp effect has a single technique; only the multiplier changes */
	*multiplier = 1.0f;
	switch (source) {
	case GS_CS_709_EXTENDED:
		if (current == GS_CS_709_SCRGB)
			*multiplier = obs_get_video_sdr_white_level() / 80.0f;
		break;
	case GS_CS_709_SCRGB:
		if (current != GS_CS_709_SCRGB)
			*multiplier = 80.0f / obs_get_video_sdr_white_level();
		break;
	default:
		break;
	}
	return "Draw";
}

static void vrstab_draw(vrstab_filter *f, gs_texture_t *full_tex)
{
	const Correction c = f->bypass ? Correction() : f->cur;

	float multiplier;
	const char *tech = tech_and_multiplier(gs_get_color_space(), f->full_space, &multiplier);

	struct vec2 size, offset, cos_sin;
	vec2_set(&size, (float)f->cx, (float)f->cy);
	vec2_set(&offset, (float)c.x, (float)c.y);
	vec2_set(&cos_sin, (float)std::cos(c.rot), (float)std::sin(c.rot));

	const bool prev_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	gs_effect_set_texture_srgb(f->p_warp_image, full_tex);
	gs_effect_set_vec2(f->p_warp_size, &size);
	gs_effect_set_vec2(f->p_warp_offset, &offset);
	gs_effect_set_vec2(f->p_warp_cos_sin, &cos_sin);
	gs_effect_set_float(f->p_warp_inv_zoom, (float)(1.0 / std::max(1.0, f->zoom)));
	gs_effect_set_float(f->p_warp_multiplier, multiplier);

	while (gs_effect_loop(f->warp_effect, tech))
		gs_draw_sprite(full_tex, 0, f->cx, f->cy);

	gs_enable_framebuffer_srgb(prev_srgb);
}

static void vrstab_render(void *data, gs_effect_t *)
{
	auto *f = static_cast<vrstab_filter *>(data);

	if (!f->target_valid || !f->luma_effect || !f->warp_effect || !f->full || !f->small) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	if (!f->processed_frame) {
		if (!vrstab_capture_target(f)) {
			obs_source_skip_video_filter(f->context);
			return;
		}
		gs_texture_t *full_tex = gs_texrender_get_texture(f->full);
		if (!full_tex) {
			obs_source_skip_video_filter(f->context);
			return;
		}
		if (vrstab_readback(f, full_tex))
			vrstab_process(f);
		else
			f->cur = f->stab.relax((double)f->tick_dt);
		f->processed_frame = true;
	}

	gs_texture_t *full_tex = gs_texrender_get_texture(f->full);
	if (!full_tex) {
		obs_source_skip_video_filter(f->context);
		return;
	}
	vrstab_draw(f, full_tex);
}

static enum gs_color_space vrstab_get_color_space(void *data, size_t count, const enum gs_color_space *preferred)
{
	auto *f = static_cast<vrstab_filter *>(data);
	obs_source_t *target = obs_filter_get_target(f->context);
	if (!f->target_valid || !target)
		return count > 0 ? preferred[0] : GS_CS_SRGB;
	return obs_source_get_color_space(target, count, preferred);
}

void vrstab_register_filter()
{
	struct obs_source_info info = {};
	info.id = "vr_head_stabilizer_filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	info.get_name = vrstab_get_name;
	info.create = vrstab_create;
	info.destroy = vrstab_destroy;
	info.update = vrstab_update;
	info.get_defaults = vrstab_defaults;
	info.get_properties = vrstab_properties;
	info.video_tick = vrstab_tick;
	info.video_render = vrstab_render;
	info.video_get_color_space = vrstab_get_color_space;
	obs_register_source(&info);
}
