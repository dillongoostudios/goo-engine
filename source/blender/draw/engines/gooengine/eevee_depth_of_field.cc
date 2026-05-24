/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Depth of field post process effect.
 *
 * There are 2 methods to achieve this effect.
 * - The first uses projection matrix offsetting and sample accumulation to give reference quality
 *   depth of field. But this needs many samples to hide the under-sampling.
 * - The second one is a post-processing based one. It follows the implementation described in
 *   the presentation "Life of a Bokeh - SIGGRAPH 2018" from Guillaume Abadie. There are some
 *   difference with our actual implementation that prioritize quality.
 */

#include "DRW_render.hh"
#include "draw_handle.hh"

#include "DNA_camera_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"
#include "DNA_world_types.h"

#include "BKE_camera.h"

#include "BLI_string_utils.hh"
#include "BLI_math_vector.h"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_query.hh"

#include "GPU_framebuffer.hh"
#include "GPU_texture.hh"
#include "gooengine_private.hh"

namespace blender::gooengine {

#define CAMERA_JITTER_RING_DENSITY 6

static float coc_radius_from_camera_depth(bool is_ortho, EEVEE_EffectsInfo *fx, float camera_depth)
{
  float multiplier = fx->dof_coc_params[0];
  float bias = fx->dof_coc_params[1];
  if (multiplier == 0.0f || bias == 0.0f) {
    return 0.0f;
  }
  if (is_ortho) {
    return (camera_depth + multiplier / bias) * multiplier;
  }
  return multiplier / camera_depth - bias;
}

static float polygon_sides_length(float sides_count)
{
  return 2.0 * sin(M_PI / sides_count);
}

/* Returns intersection ratio between the radius edge at theta and the polygon edge.
 * Start first corners at theta == 0. */
static float circle_to_polygon_radius(float sides_count, float theta)
{
  /* From Graphics Gems from CryENGINE 3 (SIGGRAPH 2013) by Tiago Sousa (slide 36). */
  float side_angle = (2.0f * M_PI) / sides_count;
  return cosf(side_angle * 0.5f) /
         cosf(theta - side_angle * floorf((sides_count * theta + M_PI) / (2.0f * M_PI)));
}

/* Remap input angle to have homogeneous spacing of points along a polygon edge.
 * Expect theta to be in [0..2pi] range. */
static float circle_to_polygon_angle(float sides_count, float theta)
{
  float side_angle = (2.0f * M_PI) / sides_count;
  float halfside_angle = side_angle * 0.5f;
  float side = floorf(theta / side_angle);
  /* Length of segment from center to the middle of polygon side. */
  float adjacent = circle_to_polygon_radius(sides_count, 0.0f);

  /* This is the relative position of the sample on the polygon half side. */
  float local_theta = theta - side * side_angle;
  float ratio = (local_theta - halfside_angle) / halfside_angle;

  float halfside_len = polygon_sides_length(sides_count) * 0.5f;
  float opposite = ratio * halfside_len;

  /* NOTE: atan(y_over_x) has output range [-M_PI_2..M_PI_2]. */
  float final_local_theta = atanf(opposite / adjacent);

  return side * side_angle + final_local_theta;
}

static int dof_jitter_total_sample_count(int ring_density, int ring_count)
{
  return ((ring_count * ring_count + ring_count) / 2) * ring_density + 1;
}

bool EEVEE_depth_of_field_jitter_get(EEVEE_EffectsInfo *fx,
                                     float r_jitter[2],
                                     float *r_focus_distance)
{
  if (fx->dof_jitter_radius == 0.0f) {
    return false;
  }

  int ring_density = CAMERA_JITTER_RING_DENSITY;
  int ring_count = fx->dof_jitter_ring_count;
  int sample_count = dof_jitter_total_sample_count(ring_density, ring_count);

  int s = fx->taa_current_sample - 1;

  int ring = 0;
  int ring_sample_count = 1;
  int ring_sample = 1;

  s = s * (ring_density - 1);
  s = s % sample_count;

  int samples_passed = 1;
  while (s >= samples_passed) {
    ring++;
    ring_sample_count = ring * ring_density;
    ring_sample = s - samples_passed;
    ring_sample = (ring_sample + 1) % ring_sample_count;
    samples_passed += ring_sample_count;
  }

  r_jitter[0] = float(ring) / ring_count;
  r_jitter[1] = float(ring_sample) / ring_sample_count;

  {
    /* Bokeh shape parameterization. */
    float r = r_jitter[0];
    float T = r_jitter[1] * 2.0f * M_PI;

    if (fx->dof_jitter_blades >= 3.0f) {
      T = circle_to_polygon_angle(fx->dof_jitter_blades, T);
      r *= circle_to_polygon_radius(fx->dof_jitter_blades, T);
    }

    T += fx->dof_bokeh_rotation;

    r_jitter[0] = r * cosf(T);
    r_jitter[1] = r * sinf(T);

    mul_v2_v2(r_jitter, fx->dof_bokeh_aniso);
  }

  mul_v2_fl(r_jitter, fx->dof_jitter_radius);

  *r_focus_distance = fx->dof_jitter_focus;
  return true;
}

int EEVEE_depth_of_field_sample_count_get(EEVEE_EffectsInfo *effects,
                                          int sample_count,
                                          int *r_ring_count)
{
  if (effects->dof_jitter_radius == 0.0f) {
    if (r_ring_count != nullptr) {
      *r_ring_count = 0;
    }
    return 1;
  }

  if (sample_count == TAA_MAX_SAMPLE) {
    /* Special case for viewport continuous rendering. We clamp to a max sample to avoid the
     * jittered dof never converging. */
    sample_count = 1024;
  }
  /* Inversion of dof_jitter_total_sample_count. */
  float x = 2.0f * (sample_count - 1.0f) / CAMERA_JITTER_RING_DENSITY;
  /* Solving polynomial. We only search positive solution. */
  float discriminant = 1.0f + 4.0f * x;
  int ring_count = ceilf(0.5f * (sqrt(discriminant) - 1.0f));

  sample_count = dof_jitter_total_sample_count(CAMERA_JITTER_RING_DENSITY, ring_count);

  if (r_ring_count != nullptr) {
    *r_ring_count = ring_count;
  }
  return sample_count;
}

int EEVEE_depth_of_field_init(EEVEE_ViewLayerData * /*sldata*/, Instance *inst, Object *camera)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  const DRWContext *draw_ctx = DRW_context_get();
  const Scene *scene_eval = DEG_get_evaluated_scene(draw_ctx->depsgraph);

  const Camera *cam = static_cast<const Camera *>(
      (camera != nullptr && camera->type == OB_CAMERA) ? camera->data : nullptr);

  if (cam && (cam->dof.flag & CAM_DOF_ENABLED)) {
    RegionView3D *rv3d = draw_ctx->rv3d;
    const float2 viewport_size = draw_ctx->viewport_size_get();

    effects->dof_hq_slight_focus = (scene_eval->eevee.flag & SCE_EEVEE_DOF_HQ_SLIGHT_FOCUS) != 0;

    /* Retrieve Near and Far distance */
    effects->dof_coc_params[2] = -cam->clip_start;
    effects->dof_coc_params[3] = -cam->clip_end;

    /* Parameters */
    bool is_ortho = cam->type == CAM_ORTHO;
    float fstop = cam->dof.aperture_fstop;
    float blades = cam->dof.aperture_blades;
    float rotation = cam->dof.aperture_rotation;
    float ratio = 1.0f / max_ff(cam->dof.aperture_ratio, 0.00001f);
    float sensor = BKE_camera_sensor_size(cam->sensor_fit, cam->sensor_x, cam->sensor_y);
    float focus_dist = BKE_camera_object_dof_distance(camera);
    float focal_len = cam->lens;

    if (is_ortho) {
      /* (fclem) A bit of black magic here. I don't know if this is correct. */
      fstop *= 1.3f;
      focal_len = 1.0f;
      sensor = cam->ortho_scale;
    }

    const float scale_camera = (is_ortho) ? 1.0 : 0.001f;
    /* We want radius here for the aperture number. */
    float aperture = 0.5f * scale_camera * focal_len / fstop;
    float focal_len_scaled = scale_camera * focal_len;
    float sensor_scaled = scale_camera * sensor;

    if (rv3d != nullptr) {
      sensor_scaled *= rv3d->viewcamtexcofac[0];
    }

    if (ratio > 1.0) {
      /* If ratio is scaling the bokeh outwards, we scale the aperture so that the gather
       * kernel size will encompass the maximum axis. */
      aperture *= ratio;
    }

    effects->dof_coc_params[1] = -aperture *
                                 fabsf(focal_len_scaled / (focus_dist - focal_len_scaled));
    /* FIXME(@fclem): This is broken for vertically fit sensor. */
    effects->dof_coc_params[1] *= viewport_size[0] / sensor_scaled;

    if ((scene_eval->eevee.flag & SCE_EEVEE_DOF_JITTER) != 0) {
      effects->dof_jitter_radius = effects->dof_coc_params[1];
      effects->dof_jitter_focus = focus_dist;
      effects->dof_jitter_blades = blades;

      int sample_count = EEVEE_temporal_sampling_sample_count_get(scene_eval, inst);
      sample_count = EEVEE_depth_of_field_sample_count_get(
          effects, sample_count, &effects->dof_jitter_ring_count);

      if (effects->dof_jitter_ring_count == 0) {
        effects->dof_jitter_radius = 0.0f;
      }
      else {
        /* Compute a minimal overblur radius to fill the gaps between the samples.
         * This is just the simplified form of dividing the area of the bokeh
         * by the number of samples. */
        float minimal_overblur = 1.0f / sqrtf(sample_count);
        float user_overblur = scene_eval->eevee.bokeh_overblur / 100.0f;

        minimal_overblur *= effects->dof_coc_params[1];
        user_overblur *= effects->dof_coc_params[1];

        effects->dof_coc_params[1] = minimal_overblur + user_overblur;
        /* Avoid dilating the shape. Over-blur only soften. */
        effects->dof_jitter_radius -= minimal_overblur + user_overblur * 0.5f;
      }
    }
    else {
      effects->dof_jitter_radius = 0.0f;
    }

    if (is_ortho) {
      /* (fclem) A bit of black magic here. Needed to match cycles. */
      effects->dof_coc_params[1] *= 0.225;
    }

    effects->dof_coc_params[0] = -focus_dist * effects->dof_coc_params[1];

    effects->dof_bokeh_blades = blades;
    effects->dof_bokeh_rotation = rotation;
    effects->dof_bokeh_aniso[0] = min_ff(ratio, 1.0f);
    effects->dof_bokeh_aniso[1] = min_ff(1.0f / ratio, 1.0f);
    effects->dof_bokeh_max_size = scene_eval->eevee.bokeh_max_size;

    copy_v2_v2(effects->dof_bokeh_aniso_inv, effects->dof_bokeh_aniso);
    invert_v2(effects->dof_bokeh_aniso_inv);

    effects->dof_scatter_color_threshold = scene_eval->eevee.bokeh_threshold;
    effects->dof_scatter_neighbor_max_color = scene_eval->eevee.bokeh_neighbor_max;
    effects->dof_denoise_factor = clamp_f(scene_eval->eevee.bokeh_denoise_fac, 0.0f, 1.0f);

    float max_abs_fg_coc, max_abs_bg_coc;
    if (is_ortho) {
      max_abs_fg_coc = fabsf(coc_radius_from_camera_depth(true, effects, -cam->clip_start));
      max_abs_bg_coc = fabsf(coc_radius_from_camera_depth(true, effects, -cam->clip_end));
    }
    else {
      max_abs_fg_coc = fabsf(coc_radius_from_camera_depth(false, effects, -cam->clip_start));
      /* Background is at infinity so maximum CoC is the limit of the function at -inf. */
      max_abs_bg_coc = fabsf(effects->dof_coc_params[1]);
    }

    float max_coc = max_ff(max_abs_bg_coc, max_abs_fg_coc);
    /* Clamp with user defined max. */
    effects->dof_fx_max_coc = min_ff(scene_eval->eevee.bokeh_max_size, max_coc);

    if (effects->dof_fx_max_coc < 0.5f) {
      return 0;
    }

    return EFFECT_DOF | EFFECT_POST_BUFFER;
  }

  effects->dof_jitter_radius = 0.0f;

  /* Cleanup to release memory */
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_setup_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_flatten_tiles_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_dilate_tiles_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_reduce_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_reduce_copy_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_gather_fg_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_gather_bg_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_scatter_bg_fb);
  DRW_TEXTURE_FREE_SAFE(inst->goo_data->dof_reduced_color);
  DRW_TEXTURE_FREE_SAFE(inst->goo_data->dof_reduced_coc);

  return 0;
}

static const GPUSamplerState WITH_FILTERING = {GPU_SAMPLER_FILTERING_MIPMAP |
                                               GPU_SAMPLER_FILTERING_LINEAR};
static const GPUSamplerState NO_FILTERING = {GPU_SAMPLER_FILTERING_MIPMAP};

#define COLOR_FORMAT fx->dof_color_format
#define FG_TILE_FORMAT GPU_RGBA16F
#define BG_TILE_FORMAT GPU_R11F_G11F_B10F

/**
 * Create bokeh texture.
 */
static void dof_bokeh_pass_init(Instance *inst,
                                EEVEE_EffectsInfo *fx)
{
  if ((fx->dof_bokeh_aniso[0] == 1.0f) && (fx->dof_bokeh_aniso[1] == 1.0f) &&
      (fx->dof_bokeh_blades == 0.0))
  {
    fx->dof_bokeh_gather_lut_tx = nullptr;
    fx->dof_bokeh_scatter_lut_tx = nullptr;
    fx->dof_bokeh_resolve_lut_tx = nullptr;
    return;
  }

  const int2 res = {DOF_BOKEH_LUT_SIZE, DOF_BOKEH_LUT_SIZE};

  blender::draw::PassSimple &pass = inst->goo_data->dof_bokeh;
  pass.init();
  pass.state_set(DRW_STATE_WRITE_COLOR);
  pass.shader_set(inst->shaders.static_shader_get(DEPTH_OF_FIELD_BOKEH));
  pass.push_constant("bokehSides", fx->dof_bokeh_blades);
  pass.push_constant("bokehRotation", fx->dof_bokeh_rotation);
  pass.push_constant("bokehAnisotropyInv", fx->dof_bokeh_aniso_inv);
  pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);

  eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
  fx->dof_bokeh_gather_lut_tx = DRW_texture_pool_query_2d_ex(
      UNPACK2(res), GPU_RG16F, usage, 0);
  fx->dof_bokeh_scatter_lut_tx = DRW_texture_pool_query_2d_ex(
      UNPACK2(res), GPU_R16F, usage, 0);
  fx->dof_bokeh_resolve_lut_tx = DRW_texture_pool_query_2d_ex(
      UNPACK2(res), GPU_R16F, usage, 0);

  GPU_framebuffer_ensure_config(&inst->goo_data->dof_bokeh_fb,
                                {
                                    GPU_ATTACHMENT_NONE,
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_bokeh_gather_lut_tx),
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_bokeh_scatter_lut_tx),
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_bokeh_resolve_lut_tx),
                                });
}

/**
 * Outputs halfResColorBuffer and halfResCocBuffer.
 */
static void dof_setup_pass_init(Instance *inst,
                                EEVEE_EffectsInfo *fx)
{
  const DRWContext *draw_ctx = DRW_context_get();
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();

  const float2 fullres = draw_ctx->viewport_size_get();
  const int2 res = {int(divide_ceil_u(fullres[0], 2)), int(divide_ceil_u(fullres[1], 2))};

  blender::draw::PassSimple &pass = inst->goo_data->dof_setup;   
  pass.init();
  pass.state_set(DRW_STATE_WRITE_COLOR);
  pass.shader_set(inst->shaders.static_shader_get(DEPTH_OF_FIELD_SETUP));
  pass.bind_texture("colorBuffer", &fx->source_buffer, NO_FILTERING);
  pass.bind_texture("depthBuffer", &dtxl->depth, NO_FILTERING);
  pass.push_constant("cocParams", fx->dof_coc_params);
  pass.push_constant("bokehMaxSize", fx->dof_bokeh_max_size);
  pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);

  eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
  fx->dof_half_res_color_tx = DRW_texture_pool_query_2d_ex(
      UNPACK2(res), COLOR_FORMAT, usage, 0);
  fx->dof_half_res_coc_tx = DRW_texture_pool_query_2d_ex(
      UNPACK2(res), GPU_RG16F, usage, 0);

  GPU_framebuffer_ensure_config(&inst->goo_data->dof_setup_fb,
                                {
                                    GPU_ATTACHMENT_NONE,
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_half_res_color_tx),
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_half_res_coc_tx),
                                });
}

/**
 * Outputs min & max COC in each 8x8 half res pixel tiles (so 1/16th of full resolution).
 */
static void dof_flatten_tiles_pass_init(Instance *inst,
                                        EEVEE_EffectsInfo *fx)
{
  const DRWContext *draw_ctx = DRW_context_get();
  const float2 fullres = draw_ctx->viewport_size_get();
  const int2 res = {int(divide_ceil_u(fullres[0], DOF_TILE_DIVISOR)),
                      int(divide_ceil_u(fullres[1], DOF_TILE_DIVISOR))};

  blender::draw::PassSimple &pass = inst->goo_data->dof_flatten_tiles;   
  pass.init();
  pass.state_set(DRW_STATE_WRITE_COLOR);
  pass.shader_set(inst->shaders.static_shader_get(DEPTH_OF_FIELD_FLATTEN_TILES));
  pass.bind_texture("halfResCocBuffer", &fx->dof_half_res_coc_tx, NO_FILTERING);
  pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);

  eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
  fx->dof_coc_tiles_fg_tx = DRW_texture_pool_query_2d_ex(
      UNPACK2(res), FG_TILE_FORMAT, usage, 0);
  fx->dof_coc_tiles_bg_tx = DRW_texture_pool_query_2d_ex(
      UNPACK2(res), BG_TILE_FORMAT, usage, 0);

  GPU_framebuffer_ensure_config(&inst->goo_data->dof_flatten_tiles_fb,
                                {
                                    GPU_ATTACHMENT_NONE,
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_coc_tiles_fg_tx),
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_coc_tiles_bg_tx),
                                });
}

/**
 * Dilates the min & max COCS to cover maximum COC values.
 * Output format/dimensions should be the same as coc_flatten_pass as they are swapped for
 * doing multiple dilation passes.
 */
static void dof_dilate_tiles_pass_init(Instance *inst,
                                       EEVEE_EffectsInfo *fx)
{
  const DRWContext *draw_ctx = DRW_context_get();
  const float2 fullres = draw_ctx->viewport_size_get();
  const int2 res = {int(divide_ceil_u(fullres[0], DOF_TILE_DIVISOR)),
                      int(divide_ceil_u(fullres[1], DOF_TILE_DIVISOR))};

  GPUShader *sh;
  for (int pass = 0; pass < 2; pass++) {
    blender::draw::PassSimple &drw_pass = (pass == 0) ? inst->goo_data->dof_dilate_tiles_minmax : inst->goo_data->dof_dilate_tiles_minabs;
    if (pass == 0) {
      sh = inst->shaders.static_shader_get(DEPTH_OF_FIELD_DILATE_TILES_MINMAX);
    }
    else {
      sh = inst->shaders.static_shader_get(DEPTH_OF_FIELD_DILATE_TILES_MINABS);
    }
    drw_pass.init();
    drw_pass.state_set(DRW_STATE_WRITE_COLOR);
    drw_pass.shader_set(sh);
    drw_pass.bind_texture("cocTilesFgBuffer", &fx->dof_coc_tiles_fg_tx);
    drw_pass.bind_texture("cocTilesBgBuffer", &fx->dof_coc_tiles_bg_tx);
    drw_pass.push_constant("dilateSlightFocus", &fx->dof_dilate_slight_focus);
    drw_pass.push_constant("ringCount", &fx->dof_dilate_ring_count);
    drw_pass.push_constant("ringWidthMultiplier", &fx->dof_dilate_ring_width_multiplier);
    drw_pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);
  }
  eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
  fx->dof_coc_dilated_tiles_fg_tx = DRW_texture_pool_query_2d_ex(
      UNPACK2(res), FG_TILE_FORMAT, usage, 0);
  fx->dof_coc_dilated_tiles_bg_tx = DRW_texture_pool_query_2d_ex(
      UNPACK2(res), BG_TILE_FORMAT, usage, 0);

  GPU_framebuffer_ensure_config(&inst->goo_data->dof_dilate_tiles_fb,
                                {
                                    GPU_ATTACHMENT_NONE,
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_coc_dilated_tiles_fg_tx),
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_coc_dilated_tiles_bg_tx),
                                });
}

static void dof_dilate_tiles_pass_draw(Instance *inst,
                                       EEVEE_EffectsInfo *fx)
{
  blender::draw::View &drw_view = blender::draw::View::default_get();
  blender::draw::Manager *manager = DRW_manager_get();

  for (int pass = 0; pass < 2; pass++) {
    blender::draw::PassSimple &drw_pass = (pass == 0) ? inst->goo_data->dof_dilate_tiles_minmax : inst->goo_data->dof_dilate_tiles_minabs;

    /* Error introduced by gather center jittering. */
    const float error_multiplier = 1.0f + 1.0f / (DOF_GATHER_RING_COUNT + 0.5f);
    int dilation_end_radius = ceilf((fx->dof_fx_max_coc * error_multiplier) / DOF_TILE_DIVISOR);

    /* This algorithm produce the exact dilation radius by dividing it in multiple passes. */
    int dilation_radius = 0;
    while (dilation_radius < dilation_end_radius) {
      /* Dilate slight focus only on first iteration. */
      fx->dof_dilate_slight_focus = (dilation_radius == 0) ? 1 : 0;

      int remainder = dilation_end_radius - dilation_radius;
      /* Do not step over any unvisited tile. */
      int max_multiplier = dilation_radius + 1;

      int ring_count = min_ii(DOF_DILATE_RING_COUNT, ceilf(remainder / float(max_multiplier)));
      int multiplier = min_ii(max_multiplier, floor(remainder / float(ring_count)));

      dilation_radius += ring_count * multiplier;

      fx->dof_dilate_ring_count = ring_count;
      fx->dof_dilate_ring_width_multiplier = multiplier;

      GPU_framebuffer_bind(inst->goo_data->dof_dilate_tiles_fb);
      manager->submit(drw_pass, drw_view);

      std::swap(inst->goo_data->dof_dilate_tiles_fb, inst->goo_data->dof_flatten_tiles_fb);
      std::swap(fx->dof_coc_dilated_tiles_bg_tx, fx->dof_coc_tiles_bg_tx);
      std::swap(fx->dof_coc_dilated_tiles_fg_tx, fx->dof_coc_tiles_fg_tx);
    }
  }
  /* Swap again so that final textures are dof_coc_dilated_tiles_*_tx. */
  std::swap(inst->goo_data->dof_dilate_tiles_fb, inst->goo_data->dof_flatten_tiles_fb);
  std::swap(fx->dof_coc_dilated_tiles_bg_tx, fx->dof_coc_tiles_bg_tx);
  std::swap(fx->dof_coc_dilated_tiles_fg_tx, fx->dof_coc_tiles_fg_tx);
}

/**
 * Create mipmapped color & COC textures for gather passes.
 */
static void dof_reduce_pass_init(Instance *inst,
                                 EEVEE_EffectsInfo *fx)
{
  const DRWContext *draw_ctx = DRW_context_get();
  const float2 fullres = draw_ctx->viewport_size_get();

  /* Divide by 2 because dof_fx_max_coc is in fullres CoC radius and the reduce texture begins at
   * half resolution. */
  float max_space_between_sample = fx->dof_fx_max_coc * 0.5f / DOF_GATHER_RING_COUNT;

  int mip_count = max_ii(1, log2_ceil_u(max_space_between_sample));

  fx->dof_reduce_steps = mip_count - 1;
  /* This ensure the mipmaps are aligned for the needed 4 mip levels.
   * Starts at 2 because already at half resolution. */
  int multiple = 2 << (mip_count - 1);
  const int2 res = {
      (multiple * int(divide_ceil_u(fullres[0], multiple))) / 2,
      (multiple * int(divide_ceil_u(fullres[1], multiple))) / 2,
  };

  const int2 quater_res = {int(divide_ceil_u(fullres[0], 4)), int(divide_ceil_u(fullres[1], 4))};

  /* TODO(fclem): Make this dependent of the quality of the gather pass. */
  fx->dof_scatter_coc_threshold = 4.0f;

  {
    blender::draw::PassSimple &pass = inst->goo_data->dof_downsample;
    pass.init();
    pass.state_set(DRW_STATE_WRITE_COLOR);
    pass.shader_set(inst->shaders.static_shader_get(DEPTH_OF_FIELD_DOWNSAMPLE));
    pass.bind_texture("colorBuffer", &fx->dof_reduce_input_color_tx, NO_FILTERING);
    pass.bind_texture("cocBuffer", &fx->dof_reduce_input_coc_tx, NO_FILTERING);
    pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);

    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
    fx->dof_downsample_tx = DRW_texture_pool_query_2d_ex(
        UNPACK2(quater_res), COLOR_FORMAT, usage, 0);

    GPU_framebuffer_ensure_config(&inst->goo_data->dof_downsample_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(fx->dof_downsample_tx),
                                  });
  }

  {
    blender::draw::PassSimple &pass = inst->goo_data->dof_reduce_copy;
    pass.init();
    pass.state_set(DRW_STATE_WRITE_COLOR);
    pass.shader_set(inst->shaders.static_shader_get(DEPTH_OF_FIELD_REDUCE_COPY));
    pass.bind_texture("colorBuffer", &fx->dof_reduce_input_color_tx, NO_FILTERING);
    pass.bind_texture("cocBuffer", &fx->dof_reduce_input_coc_tx, NO_FILTERING);
    pass.bind_texture("downsampledBuffer", &fx->dof_downsample_tx, NO_FILTERING);
    pass.push_constant("scatterColorThreshold", fx->dof_scatter_color_threshold);
    pass.push_constant("scatterColorNeighborMax", fx->dof_scatter_neighbor_max_color);
    pass.push_constant("scatterCocThreshold", fx->dof_scatter_coc_threshold);
    pass.push_constant("colorNeighborClamping", fx->dof_denoise_factor);
    pass.push_constant("bokehAnisotropy", fx->dof_bokeh_aniso);
    pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);

    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
    fx->dof_scatter_src_tx = DRW_texture_pool_query_2d_ex(
        UNPACK2(res), GPU_R11F_G11F_B10F, usage, 0);
  }

  {
    blender::draw::PassSimple &dof_reduce_ = inst->goo_data->dof_reduce;
    dof_reduce_.init();
    dof_reduce_.state_set(DRW_STATE_WRITE_COLOR);
    dof_reduce_.shader_set(inst->shaders.static_shader_get(DEPTH_OF_FIELD_REDUCE));
    dof_reduce_.bind_texture("colorBuffer", &fx->dof_reduce_input_color_tx, NO_FILTERING);
    dof_reduce_.bind_texture("cocBuffer", &fx->dof_reduce_input_coc_tx, NO_FILTERING);
    dof_reduce_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
  }

  if (inst->goo_data->dof_reduced_color) {
    /* TODO(@fclem): In the future, we need to check if mip_count did not change.
     * For now it's ok as we always define all mip level. */
    if (res[0] != GPU_texture_width(inst->goo_data->dof_reduced_color) ||
        res[1] != GPU_texture_width(inst->goo_data->dof_reduced_color))
    {
      DRW_TEXTURE_FREE_SAFE(inst->goo_data->dof_reduced_color);
      DRW_TEXTURE_FREE_SAFE(inst->goo_data->dof_reduced_coc);
    }
  }

  if (inst->goo_data->dof_reduced_color == nullptr) {
    /* Color needs to be signed format here. See note in shader for explanation. */
    /* Do not use texture pool because of needs mipmaps. */
    eGPUTextureUsage tex_flags = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
    inst->goo_data->dof_reduced_color = GPU_texture_create_2d(
        "dof_reduced_color", UNPACK2(res), mip_count, GPU_RGBA16F, tex_flags, nullptr);
    inst->goo_data->dof_reduced_coc = GPU_texture_create_2d(
        "dof_reduced_coc", UNPACK2(res), mip_count, GPU_R16F, tex_flags, nullptr);
  }

  GPU_framebuffer_ensure_config(&inst->goo_data->dof_reduce_fb,
                                {
                                    GPU_ATTACHMENT_NONE,
                                    GPU_ATTACHMENT_TEXTURE(inst->goo_data->dof_reduced_color),
                                    GPU_ATTACHMENT_TEXTURE(inst->goo_data->dof_reduced_coc),
                                });

  GPU_framebuffer_ensure_config(&inst->goo_data->dof_reduce_copy_fb,
                                {
                                    GPU_ATTACHMENT_NONE,
                                    GPU_ATTACHMENT_TEXTURE(inst->goo_data->dof_reduced_color),
                                    GPU_ATTACHMENT_TEXTURE(inst->goo_data->dof_reduced_coc),
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_scatter_src_tx),
                                });
}

/**
 * Do the gather convolution. For each pixels we gather multiple pixels in its neighborhood
 * depending on the min & max CoC tiles.
 */
static void dof_gather_pass_init(Instance *inst,
                                 EEVEE_EffectsInfo *fx)
{
  const DRWContext *draw_ctx = DRW_context_get();
  const float2 fullres = draw_ctx->viewport_size_get();
  const int2 res = {int(divide_ceil_u(fullres[0], 2)), int(divide_ceil_u(fullres[1], 2))};
  int2 input_size;
  GPU_texture_get_mipmap_size(inst->goo_data->dof_reduced_color, 0, input_size);
  const float2 uv_correction_fac = {res[0] / float(input_size[0]),
                                      res[1] / float(input_size[1])};
  const float2 output_texel_size = {1.0f / res[0], 1.0f / res[1]};
  const bool use_bokeh_tx = (fx->dof_bokeh_gather_lut_tx != nullptr);

  {
    blender::draw::PassSimple &pass = inst->goo_data->dof_gather_fg_holefill;
    pass.init();
    pass.state_set(DRW_STATE_WRITE_COLOR);
    pass.shader_set(inst->shaders.static_shader_get(DEPTH_OF_FIELD_GATHER_HOLEFILL));
    pass.bind_texture("colorBufferBilinear", &inst->goo_data->dof_reduced_color, WITH_FILTERING);
    pass.bind_texture("colorBuffer", &inst->goo_data->dof_reduced_color, NO_FILTERING);
    pass.bind_texture("cocBuffer", &inst->goo_data->dof_reduced_coc, NO_FILTERING);
    pass.bind_texture("cocTilesFgBuffer", &fx->dof_coc_dilated_tiles_fg_tx);
    pass.bind_texture("cocTilesBgBuffer", &fx->dof_coc_dilated_tiles_bg_tx);
    pass.bind_texture("utilTex", EEVEE_materials_get_util_tex());
    pass.push_constant("gatherInputUvCorrection", uv_correction_fac);
    pass.push_constant("gatherOutputTexelSize", output_texel_size);
    pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);

    /* Reuse textures from the setup pass. */
    /* NOTE: We could use the texture pool do that for us but it does not track usage and it might
     * backfire (it does in practice). */
    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
    fx->dof_fg_holefill_color_tx = fx->dof_half_res_color_tx;
    fx->dof_fg_holefill_weight_tx = DRW_texture_pool_query_2d_ex(
        UNPACK2(res), GPU_R16F, usage, 0);

    GPU_framebuffer_ensure_config(&inst->goo_data->dof_gather_fg_holefill_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(fx->dof_fg_holefill_color_tx),
                                      GPU_ATTACHMENT_TEXTURE(fx->dof_fg_holefill_weight_tx),
                                  });
  }
  {
    GPUShader *sh;
    if (use_bokeh_tx) {
      sh = inst->shaders.static_shader_get(DEPTH_OF_FIELD_GATHER_FG_BOKEH);
    }
    else {
      sh = inst->shaders.static_shader_get(DEPTH_OF_FIELD_GATHER_FG);
    }

    blender::draw::PassSimple &pass = inst->goo_data->dof_gather_fg;
    pass.init();
    pass.state_set(DRW_STATE_WRITE_COLOR);
    pass.shader_set(sh);
    pass.bind_texture("colorBufferBilinear", &inst->goo_data->dof_reduced_color, WITH_FILTERING);
    pass.bind_texture("colorBuffer", &inst->goo_data->dof_reduced_color, NO_FILTERING);
    pass.bind_texture("cocBuffer", &inst->goo_data->dof_reduced_coc, NO_FILTERING);
    pass.bind_texture("cocTilesFgBuffer", &fx->dof_coc_dilated_tiles_fg_tx);
    pass.bind_texture("cocTilesBgBuffer", &fx->dof_coc_dilated_tiles_bg_tx);
    pass.bind_texture("utilTex", EEVEE_materials_get_util_tex());
    pass.push_constant("gatherInputUvCorrection", uv_correction_fac);
    pass.push_constant("gatherOutputTexelSize", output_texel_size);

    if (use_bokeh_tx) {
      /* Negate to flip bokeh shape. Mimics optical phenomenon. */
      negate_v2(fx->dof_bokeh_aniso);
      pass.push_constant("bokehAnisotropy", fx->dof_bokeh_aniso);
      pass.bind_texture("bokehLut", &fx->dof_bokeh_gather_lut_tx);
      /* Restore. */
      negate_v2(fx->dof_bokeh_aniso);
    }
    pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);

    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
    fx->dof_fg_color_tx = DRW_texture_pool_query_2d_ex(
        UNPACK2(res), COLOR_FORMAT, usage, 0);
    fx->dof_fg_weight_tx = DRW_texture_pool_query_2d_ex(
        UNPACK2(res), GPU_R16F, usage, 0);
    /* Reuse textures from the setup pass. */
    /* NOTE: We could use the texture pool do that for us but it does not track usage and it might
     * backfire (it does in practice). */
    fx->dof_fg_occlusion_tx = fx->dof_half_res_coc_tx;

    /* NOTE: First target is holefill texture so we can use the median filter on it.
     * See the filter function. */
    GPU_framebuffer_ensure_config(&inst->goo_data->dof_gather_fg_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(fx->dof_fg_holefill_color_tx),
                                      GPU_ATTACHMENT_TEXTURE(fx->dof_fg_holefill_weight_tx),
                                      GPU_ATTACHMENT_TEXTURE(fx->dof_fg_occlusion_tx),
                                  });
  }
  {
    GPUShader *sh;
    if (use_bokeh_tx) {
      sh = inst->shaders.static_shader_get(DEPTH_OF_FIELD_GATHER_BG_BOKEH);
    }
    else {
      sh = inst->shaders.static_shader_get(DEPTH_OF_FIELD_GATHER_BG);
    }

    blender::draw::PassSimple &pass = inst->goo_data->dof_gather_bg;
    pass.init();
    pass.state_set(DRW_STATE_WRITE_COLOR);
    pass.shader_set(sh);
    pass.bind_texture("colorBufferBilinear", &inst->goo_data->dof_reduced_color, WITH_FILTERING);
    pass.bind_texture("colorBuffer", &inst->goo_data->dof_reduced_color, NO_FILTERING);
    pass.bind_texture("cocBuffer", &inst->goo_data->dof_reduced_coc, NO_FILTERING);
    pass.bind_texture("cocTilesFgBuffer", &fx->dof_coc_dilated_tiles_fg_tx);
    pass.bind_texture("cocTilesBgBuffer", &fx->dof_coc_dilated_tiles_bg_tx);
    pass.bind_texture("utilTex", EEVEE_materials_get_util_tex());
    pass.push_constant("gatherInputUvCorrection", uv_correction_fac);
    pass.push_constant("gatherOutputTexelSize", output_texel_size);
    if (use_bokeh_tx) {
      pass.push_constant("bokehAnisotropy", fx->dof_bokeh_aniso);
      pass.bind_texture("bokehLut", &fx->dof_bokeh_gather_lut_tx);
    }
    pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);

    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
    fx->dof_bg_color_tx = DRW_texture_pool_query_2d_ex(
        UNPACK2(res), COLOR_FORMAT, usage, 0);
    fx->dof_bg_weight_tx = DRW_texture_pool_query_2d_ex(
        UNPACK2(res), GPU_R16F, usage, 0);
    /* Reuse, since only used for scatter. Foreground is processed before background. */
    fx->dof_bg_occlusion_tx = fx->dof_fg_occlusion_tx;

    /* NOTE: First target is holefill texture so we can use the median filter on it.
     * See the filter function. */
    GPU_framebuffer_ensure_config(&inst->goo_data->dof_gather_bg_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(fx->dof_fg_holefill_color_tx),
                                      GPU_ATTACHMENT_TEXTURE(fx->dof_fg_holefill_weight_tx),
                                      GPU_ATTACHMENT_TEXTURE(fx->dof_bg_occlusion_tx),
                                  });
  }
}

/**
 * Filter an input buffer using a median filter to reduce noise.
 * NOTE: We use the holefill texture as our input to reduce memory usage.
 * Thus, the holefill pass cannot be filtered.
 */
static void dof_filter_pass_init(Instance *inst,
                                 EEVEE_EffectsInfo *fx)
{
  blender::draw::PassSimple &pass = inst->goo_data->dof_filter;
  pass.init();
  pass.state_set(DRW_STATE_WRITE_COLOR);
  pass.shader_set(inst->shaders.static_shader_get(DEPTH_OF_FIELD_FILTER));
  pass.bind_texture("colorBuffer", &fx->dof_fg_holefill_color_tx, NO_FILTERING);
  pass.bind_texture("weightBuffer", &fx->dof_fg_holefill_weight_tx, NO_FILTERING);
  pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);

  GPU_framebuffer_ensure_config(&inst->goo_data->dof_filter_fg_fb,
                                {
                                    GPU_ATTACHMENT_NONE,
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_fg_color_tx),
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_fg_weight_tx),
                                });

  GPU_framebuffer_ensure_config(&inst->goo_data->dof_filter_bg_fb,
                                {
                                    GPU_ATTACHMENT_NONE,
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_bg_color_tx),
                                    GPU_ATTACHMENT_TEXTURE(fx->dof_bg_weight_tx),
                                });
}

/**
 * Do the Scatter convolution. A sprite is emitted for every 4 pixels but is only expanded if the
 * pixels are bright enough to be scattered.
 */
static void dof_scatter_pass_init(Instance *inst,
                                  EEVEE_EffectsInfo *fx)
{
  int2 input_size, target_size;
  GPU_texture_get_mipmap_size(fx->dof_half_res_color_tx, 0, input_size);
  GPU_texture_get_mipmap_size(fx->dof_bg_color_tx, 0, target_size);
  /* Draw a sprite for every four half-res pixels. */
  int sprite_count = (input_size[0] / 2) * (input_size[1] / 2);
  const float2 target_texel_size = {1.0f / target_size[0], 1.0f / target_size[1]};
  const bool use_bokeh_tx = (fx->dof_bokeh_gather_lut_tx != nullptr);

  {
    GPUShader *sh;
    if (use_bokeh_tx) {
      sh = inst->shaders.static_shader_get(DEPTH_OF_FIELD_SCATTER_FG_BOKEH);
    }
    else {
      sh = inst->shaders.static_shader_get(DEPTH_OF_FIELD_SCATTER_FG);
    }
    blender::draw::PassSimple &pass = inst->goo_data->dof_scatter_fg;
    pass.init();
    pass.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ADD_FULL);
    pass.shader_set(sh);
    pass.bind_texture("colorBuffer", &fx->dof_scatter_src_tx, NO_FILTERING);
    pass.bind_texture("cocBuffer", &inst->goo_data->dof_reduced_coc, NO_FILTERING);
    pass.bind_texture("occlusionBuffer", &fx->dof_fg_occlusion_tx);
    pass.push_constant("targetTexelSize", target_texel_size);
    pass.push_constant("spritePerRow", input_size[0] / 2);
    pass.push_constant("bokehAnisotropy", fx->dof_bokeh_aniso);
    if (use_bokeh_tx) {
      /* Negate to flip bokeh shape. Mimics optical phenomenon. */
      negate_v2(fx->dof_bokeh_aniso_inv);
      pass.push_constant("bokehAnisotropyInv", fx->dof_bokeh_aniso_inv);
      pass.bind_texture("bokehLut", &fx->dof_bokeh_scatter_lut_tx);
      /* Restore. */
      negate_v2(fx->dof_bokeh_aniso_inv);
    }
    pass.draw_procedural(GPU_PRIM_TRIS, sprite_count, 3);

    GPU_framebuffer_ensure_config(&inst->goo_data->dof_scatter_fg_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(fx->dof_fg_color_tx),
                                  });
  }
  {
    GPUShader *sh;
    if (use_bokeh_tx) {
      sh = inst->shaders.static_shader_get(DEPTH_OF_FIELD_SCATTER_BG_BOKEH);
    }
    else {
      sh = inst->shaders.static_shader_get(DEPTH_OF_FIELD_SCATTER_BG);
    }
    blender::draw::PassSimple &pass = inst->goo_data->dof_scatter_bg;
    pass.init();
    pass.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ADD_FULL);
    pass.shader_set(sh);
    pass.bind_texture("colorBuffer", &fx->dof_scatter_src_tx, NO_FILTERING);
    pass.bind_texture("cocBuffer", &inst->goo_data->dof_reduced_coc, NO_FILTERING);
    pass.bind_texture("occlusionBuffer", &fx->dof_bg_occlusion_tx);
    pass.push_constant("targetTexelSize", target_texel_size);
    pass.push_constant("spritePerRow", input_size[0] / 2);
    pass.push_constant("bokehAnisotropy", fx->dof_bokeh_aniso);
    if (use_bokeh_tx) {
      pass.push_constant("bokehAnisotropyInv", fx->dof_bokeh_aniso_inv);
      pass.bind_texture("bokehLut", &fx->dof_bokeh_scatter_lut_tx);
    }
    pass.draw_procedural(GPU_PRIM_TRIS, sprite_count, 3);

    GPU_framebuffer_ensure_config(&inst->goo_data->dof_scatter_bg_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(fx->dof_bg_color_tx),
                                  });
  }
}

/**
 * Recombine the result of the foreground and background processing. Also perform a slight out of
 * focus blur to improve geometric continuity.
 */
static void dof_recombine_pass_init(Instance *inst,
                                    EEVEE_EffectsInfo *fx)
{
  const DRWContext *draw_ctx = DRW_context_get();
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
  const bool use_bokeh_tx = (fx->dof_bokeh_gather_lut_tx != nullptr);

  GPUShader *sh;
  if (fx->dof_hq_slight_focus) {
    sh = (use_bokeh_tx) ? inst->shaders.static_shader_get(DEPTH_OF_FIELD_RESOLVE_HQ_BOKEH) :
                                                inst->shaders.static_shader_get(DEPTH_OF_FIELD_RESOLVE_HQ);
  }
  else {
    sh = (use_bokeh_tx) ? inst->shaders.static_shader_get(DEPTH_OF_FIELD_RESOLVE_LQ_BOKEH) :
                                                inst->shaders.static_shader_get(DEPTH_OF_FIELD_RESOLVE_LQ);
  }
  
  blender::draw::PassSimple &pass = inst->goo_data->dof_resolve;
  pass.init();
  pass.state_set(DRW_STATE_WRITE_COLOR);
  pass.shader_set(sh);
  pass.bind_texture("fullResColorBuffer", &fx->source_buffer, NO_FILTERING);
  pass.bind_texture("fullResDepthBuffer", &dtxl->depth, NO_FILTERING);
  pass.bind_texture("bgColorBuffer", &fx->dof_bg_color_tx);
  pass.bind_texture("bgWeightBuffer", &fx->dof_bg_weight_tx);
  pass.bind_texture("bgTileBuffer", &fx->dof_coc_dilated_tiles_bg_tx);
  pass.bind_texture("fgColorBuffer", &fx->dof_fg_color_tx);
  pass.bind_texture("fgWeightBuffer", &fx->dof_fg_weight_tx);
  pass.bind_texture("holefillColorBuffer", &fx->dof_fg_holefill_color_tx);
  pass.bind_texture("holefillWeightBuffer", &fx->dof_fg_holefill_weight_tx);
  pass.bind_texture("fgTileBuffer", &fx->dof_coc_dilated_tiles_fg_tx);
  pass.bind_texture("utilTex", EEVEE_materials_get_util_tex());
  pass.push_constant("cocParams", fx->dof_coc_params);
  pass.push_constant("bokehMaxSize", fx->dof_bokeh_max_size);
  if (use_bokeh_tx) {
    pass.push_constant("bokehAnisotropyInv", fx->dof_bokeh_aniso_inv);
    pass.bind_texture("bokehLut", &fx->dof_bokeh_resolve_lut_tx);
  }
  pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);
}

void EEVEE_depth_of_field_cache_init(EEVEE_ViewLayerData * /*sldata*/, Instance *inst)
{
  EEVEE_EffectsInfo *fx = inst->goo_data->effects;

  if ((fx->enabled_effects & EFFECT_DOF) != 0) {
    /* GPU_RGBA16F is sufficient now that all scattered bokeh are premultiplied.
     * GPU_R11F_G11F_B10F is not enough when lots of scattered sprites are big and offers
     * relatively small benefits. */
    fx->dof_color_format = GPU_RGBA16F;

    dof_bokeh_pass_init(inst, fx);
    dof_setup_pass_init(inst, fx);
    dof_flatten_tiles_pass_init(inst, fx);
    dof_dilate_tiles_pass_init(inst, fx);
    dof_reduce_pass_init(inst, fx);
    dof_gather_pass_init(inst, fx);
    dof_filter_pass_init(inst, fx);
    dof_scatter_pass_init(inst, fx);
    dof_recombine_pass_init(inst, fx);
  }
}

auto dof_recursive_reduce(void *instance, int /*level*/)
{
  blender::draw::View &drw_view = blender::draw::View::default_get();
  blender::draw::Manager *manager = DRW_manager_get();
  Instance *inst = static_cast<Instance *>(instance);
  EEVEE_EffectsInfo *fx = inst->goo_data->effects;

  fx->dof_reduce_input_color_tx = inst->goo_data->dof_reduced_color;
  fx->dof_reduce_input_coc_tx = inst->goo_data->dof_reduced_coc;

  manager->submit(inst->goo_data->dof_reduce, drw_view);
}

void EEVEE_depth_of_field_draw(Instance *inst)
{
  blender::draw::View &drw_view = blender::draw::View::default_get();
  blender::draw::Manager *manager = DRW_manager_get();
  EEVEE_EffectsInfo *effects = inst->goo_data->effects; /* TODO(fclem): Because of silly SWAP_BUFFERS. */
  EEVEE_EffectsInfo *fx = effects;

  /* Depth Of Field */
  if ((effects->enabled_effects & EFFECT_DOF) != 0) {

    if (fx->dof_bokeh_gather_lut_tx != nullptr) {
      GPU_framebuffer_bind(inst->goo_data->dof_bokeh_fb);
      manager->submit(inst->goo_data->dof_bokeh, drw_view);
    }

    GPU_framebuffer_bind(inst->goo_data->dof_setup_fb);
    manager->submit(inst->goo_data->dof_setup, drw_view);

    GPU_framebuffer_bind(inst->goo_data->dof_flatten_tiles_fb);
    manager->submit(inst->goo_data->dof_flatten_tiles, drw_view);

    dof_dilate_tiles_pass_draw(inst, fx);

    fx->dof_reduce_input_color_tx = fx->dof_half_res_color_tx;
    fx->dof_reduce_input_coc_tx = fx->dof_half_res_coc_tx;

    /* First step is just a copy. */
    GPU_framebuffer_bind(inst->goo_data->dof_downsample_fb);
    manager->submit(inst->goo_data->dof_downsample, drw_view);

    /* First step is just a copy. */
    GPU_framebuffer_bind(inst->goo_data->dof_reduce_copy_fb);
    manager->submit(inst->goo_data->dof_reduce_copy, drw_view);

    GPU_framebuffer_recursive_downsample(
        inst->goo_data->dof_reduce_fb, fx->dof_reduce_steps, dof_recursive_reduce, inst);

    {
      /* Foreground convolution. */
      GPU_framebuffer_bind(inst->goo_data->dof_gather_fg_fb);
      manager->submit(inst->goo_data->dof_gather_fg, drw_view);

      GPU_framebuffer_bind(inst->goo_data->dof_filter_fg_fb);
      manager->submit(inst->goo_data->dof_filter, drw_view);

      GPU_framebuffer_bind(inst->goo_data->dof_scatter_fg_fb);
      manager->submit(inst->goo_data->dof_scatter_fg, drw_view);
    }

    {
      /* Background convolution. */
      GPU_framebuffer_bind(inst->goo_data->dof_gather_bg_fb);
      manager->submit(inst->goo_data->dof_gather_bg, drw_view);

      GPU_framebuffer_bind(inst->goo_data->dof_filter_bg_fb);
      manager->submit(inst->goo_data->dof_filter, drw_view);

      GPU_framebuffer_bind(inst->goo_data->dof_scatter_bg_fb);
      manager->submit(inst->goo_data->dof_scatter_bg, drw_view);
    }

    {
      /* Hole-fill convolution. */
      GPU_framebuffer_bind(inst->goo_data->dof_gather_fg_holefill_fb);
      manager->submit(inst->goo_data->dof_gather_fg_holefill, drw_view);

      /* NOTE: do not filter the hole-fill pass as we use it as out filter input buffer. */
    }

    GPU_framebuffer_bind(fx->target_buffer);
    manager->submit(inst->goo_data->dof_resolve, drw_view);

    SWAP_BUFFERS();
  }
}

}  // namespace blender::gooengine
