/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */
#include "DRW_render.hh"

#include "BKE_camera.h"
#include "BKE_studiolight.h"

#include "BLI_math_rotation.h"
#include "BLI_rand.h"
#include "BLI_rect.h"
#include "BLI_math_vector.h"

#include "DNA_screen_types.h"
#include "DNA_world_types.h"

#include "DEG_depsgraph_query.hh"

#include "ED_screen.hh"

#include "GPU_material.hh"

#include "UI_resources.hh"

#include "eevee_lightcache.h"
#include "eevee_private.hh"

#include "draw_common_c.hh"

namespace blender::gooengine {

static void eevee_lookdev_lightcache_delete(Instance *inst)
{
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;

  MEM_SAFE_FREE(inst->goo_data->lookdev_lightcache);
  MEM_SAFE_FREE(inst->goo_data->lookdev_grid_data);
  MEM_SAFE_FREE(inst->goo_data->lookdev_cube_data);
  DRW_TEXTURE_FREE_SAFE(inst->goo_data->lookdev_grid_tx);
  DRW_TEXTURE_FREE_SAFE(inst->goo_data->lookdev_cube_tx);
  g_data->studiolight_index = -1;
  g_data->studiolight_rot_z = 0.0f;
}

static void eevee_lookdev_hdri_preview_init(Instance *inst, EEVEE_ViewLayerData *sldata)
{
  const DRWContext *draw_ctx = DRW_context_get();
  Scene *scene = draw_ctx->scene;
  DRWShadingGroup *grp;

  const EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  blender::gpu::Batch *sphere = DRW_cache_sphere_get(effects->sphere_lod);
  int mat_options = VAR_MAT_MESH | VAR_MAT_LOOKDEV;

  DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_ALWAYS |
                   DRW_STATE_CULL_BACK;

  {
    Material *ma = EEVEE_material_default_diffuse_get();
    printf("GET LOOK DEV MAT 1 \n");
    GPUMaterial *gpumat = EEVEE_material_get(inst, scene, ma, nullptr, mat_options);
    GPUShader *sh = GPU_material_get_shader(gpumat);

    DRW_PASS_CREATE(inst->goo_data->lookdev_diffuse_pass, state);
    grp = DRW_shgroup_create(sh, inst->goo_data->lookdev_diffuse_pass);
    EEVEE_material_bind_resources(
        grp, gpumat, sldata, inst, nullptr, nullptr, -1.0f, false, false);
    DRW_shgroup_add_material_resources(grp, gpumat);
    DRW_shgroup_call(grp, sphere, nullptr);
  }
  {
    Material *ma = EEVEE_material_default_glossy_get();
    
    printf("GET LOOK DEV MAT 2 \n");
    GPUMaterial *gpumat = EEVEE_material_get(inst, scene, ma, nullptr, mat_options);
    GPUShader *sh = GPU_material_get_shader(gpumat);

    DRW_PASS_CREATE(inst->goo_data->lookdev_glossy_pass, state);
    grp = DRW_shgroup_create(sh, inst->goo_data->lookdev_glossy_pass);
    EEVEE_material_bind_resources(
        grp, gpumat, sldata, inst, nullptr, nullptr, -1.0f, false, false);
    DRW_shgroup_add_material_resources(grp, gpumat);
    DRW_shgroup_call(grp, sphere, nullptr);
  }
}

void EEVEE_lookdev_init(Instance *inst)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  const DRWContext *draw_ctx = DRW_context_get();
  /* The view will be nullptr when rendering previews. */
  const View3D *v3d = draw_ctx->v3d;

  if (eevee_hdri_preview_overlay_enabled(v3d)) {
    /* Viewport / Spheres size. */
    const rcti *rect;
    rcti fallback_rect;
    if (draw_ctx->is_viewport_image_render()) {
      const float2 vp_size = draw_ctx->viewport_size_get();
      fallback_rect.xmax = vp_size[0];
      fallback_rect.ymax = vp_size[1];
      fallback_rect.xmin = fallback_rect.ymin = 0;
      rect = &fallback_rect;
    }
    else {
      rect = ED_region_visible_rect(draw_ctx->region);
    }

    /* Make the viewport width scale the lookdev spheres a bit.
     * Scale between 1000px and 2000px. */
    const float viewport_scale = clamp_f(
        BLI_rcti_size_x(rect) / (2000.0f * UI_SCALE_FAC), 0.5f, 1.0f);
    const int sphere_size = U.lookdev_sphere_size * UI_SCALE_FAC * viewport_scale;

    if (sphere_size != effects->sphere_size || rect->xmax != effects->anchor[0] ||
        rect->ymin != effects->anchor[1])
    {
      /* Make sphere resolution adaptive to viewport_scale, DPI and #U.lookdev_sphere_size. */
      float res_scale = clamp_f(
          (U.lookdev_sphere_size / 400.0f) * viewport_scale * UI_SCALE_FAC, 0.1f, 1.0f);

      if (res_scale > 0.7f) {
        effects->sphere_lod = DRW_LOD_HIGH;
      }
      else if (res_scale > 0.25f) {
        effects->sphere_lod = DRW_LOD_MEDIUM;
      }
      else {
        effects->sphere_lod = DRW_LOD_LOW;
      }
      /* If sphere size or anchor point moves, reset TAA to avoid ghosting issue.
       * This needs to happen early because we are changing taa_current_sample. */
      effects->sphere_size = sphere_size;
      effects->anchor[0] = rect->xmax;
      effects->anchor[1] = rect->ymin;
      inst->goo_data->g_data->valid_double_buffer = false;
      EEVEE_temporal_sampling_reset(inst);
    }
  }
}

void EEVEE_lookdev_cache_init(Instance *inst,
                              EEVEE_ViewLayerData *sldata,
                              DRWPass *pass,
                              EEVEE_LightProbesInfo *pinfo,
                              DRWShadingGroup **r_shgrp)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;
  const DRWContext *draw_ctx = DRW_context_get();
  /* The view will be nullptr when rendering previews. */
  const View3D *v3d = draw_ctx->v3d;
  const Scene *scene = draw_ctx->scene;

  const bool probe_render = pinfo != nullptr;

  effects->lookdev_view = nullptr;

  if (eevee_hdri_preview_overlay_enabled(v3d)) {
    eevee_lookdev_hdri_preview_init(inst, sldata);
  }

  if (LOOK_DEV_STUDIO_LIGHT_ENABLED(v3d)) {
    const View3DShading *shading = &v3d->shading;
    StudioLight *sl = BKE_studiolight_find(shading->lookdev_light,
                                           STUDIOLIGHT_ORIENTATIONS_MATERIAL_MODE);
    if (sl == nullptr || (sl->flag & STUDIOLIGHT_TYPE_WORLD) == 0) {
      return;
    }

    GPUShader *shader = probe_render ? inst->shaders.static_shader_get(STUDIOLIGHT_PROBE) :
                                       inst->shaders.static_shader_get(STUDIOLIGHT_BACKGROUND);

    const Scene *scene_eval = DEG_get_evaluated_scene(draw_ctx->depsgraph);
    int cube_res = scene_eval->eevee.gi_cubemap_resolution;

    /* If one of the component is missing we start from scratch. */
    if ((inst->goo_data->lookdev_grid_data == nullptr) || (inst->goo_data->lookdev_cube_data == nullptr) ||
        (inst->goo_data->lookdev_grid_tx == nullptr) || (inst->goo_data->lookdev_cube_tx == nullptr) ||
        (g_data->light_cache && g_data->light_cache->ref_res != cube_res))
    {
      eevee_lookdev_lightcache_delete(inst);
    }

    if (inst->goo_data->lookdev_lightcache == nullptr) {
#if defined(IRRADIANCE_SH_L2)
      int grid_res = 4;
#elif defined(IRRADIANCE_HL2)
      int grid_res = 4;
#endif

      inst->goo_data->lookdev_lightcache = EEVEE_lightcache_create(
          1, 1, cube_res, 8, blender::int3{grid_res, grid_res, 1});

      /* XXX: Fix memleak. TODO: find out why. */
      MEM_SAFE_FREE(inst->goo_data->lookdev_cube_mips);

      /* We do this to use a special light cache for lookdev.
       * This light-cache needs to be per viewport. But we need to
       * have correct freeing when the viewport is closed. So we
       * need to reference all textures to the txl and the memblocks
       * to the stl. */
      inst->goo_data->lookdev_grid_data = inst->goo_data->lookdev_lightcache->grid_data;
      inst->goo_data->lookdev_cube_data = inst->goo_data->lookdev_lightcache->cube_data;
      inst->goo_data->lookdev_cube_mips = inst->goo_data->lookdev_lightcache->cube_mips;
      inst->goo_data->lookdev_grid_tx = inst->goo_data->lookdev_lightcache->grid_tx.tex;
      inst->goo_data->lookdev_cube_tx = inst->goo_data->lookdev_lightcache->cube_tx.tex;
    }

    g_data->light_cache = inst->goo_data->lookdev_lightcache;

    DRWShadingGroup *grp = DRW_shgroup_create(shader, pass);
    axis_angle_to_mat3_single(g_data->studiolight_matrix, 'Z', shading->studiolight_rot_z);

    float studiolight_matrix[3][3] = {{0.0f}};
    if (shading->flag & V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION) {
      float view_matrix[4][4];
      float view_rot_matrix[3][3];
      float x_rot_matrix[3][3];
      DRW_view_viewmat_get(nullptr, view_matrix, false);
      copy_m3_m4(view_rot_matrix, view_matrix);
      axis_angle_to_mat3_single(x_rot_matrix, 'X', M_PI_2);
      mul_m3_m3m3(view_rot_matrix, x_rot_matrix, view_rot_matrix);
      mul_m3_m3m3(view_rot_matrix, g_data->studiolight_matrix, view_rot_matrix);
      copy_m3_m3(studiolight_matrix, view_rot_matrix);
    }

    DRW_shgroup_uniform_mat3(grp, "StudioLightMatrix", g_data->studiolight_matrix);

    if (probe_render) {
      /* Avoid artifact with equirectangular mapping. */
      GPUSamplerState state = {GPU_SAMPLER_FILTERING_LINEAR,
                               GPU_SAMPLER_EXTEND_MODE_REPEAT,
                               GPU_SAMPLER_EXTEND_MODE_EXTEND};
      DRW_shgroup_uniform_float_copy(grp, "studioLightIntensity", shading->studiolight_intensity);
      BKE_studiolight_ensure_flag(sl, STUDIOLIGHT_EQUIRECT_RADIANCE_GPUTEXTURE);
      DRW_shgroup_uniform_texture_ex(grp, "studioLight", sl->equirect_radiance_gputexture, state);
      /* Do not fade-out when doing probe rendering, only when drawing the background. */
      DRW_shgroup_uniform_float_copy(grp, "backgroundAlpha", 1.0f);
      DRW_shgroup_uniform_float_copy(grp, "studioLightBlur", 0.0f);
    }
    else {
      float background_alpha = g_data->background_alpha * shading->studiolight_background;
      float studiolight_blur = powf(shading->studiolight_blur, 2.5f);
      DRW_shgroup_uniform_float_copy(grp, "backgroundAlpha", background_alpha);
      DRW_shgroup_uniform_float_copy(grp, "studioLightBlur", studiolight_blur);
      DRW_shgroup_uniform_texture(grp, "probeCubes", inst->goo_data->lookdev_cube_tx);
      DRW_shgroup_uniform_float_copy(grp, "studioLightIntensity", 1.0f);
    }

    /* Common UBOs are setup latter. */
    *r_shgrp = grp;

    /* Do we need to recalc the lightprobes? */
    if (g_data->studiolight_index != sl->index ||
        (shading->flag & V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION &&
         !equals_m3m3(g_data->studiolight_matrix, studiolight_matrix)) ||
        g_data->studiolight_rot_z != shading->studiolight_rot_z ||
        g_data->studiolight_intensity != shading->studiolight_intensity ||
        g_data->studiolight_cubemap_res != scene->eevee.gi_cubemap_resolution ||
        g_data->studiolight_glossy_clamp != scene->eevee.gi_glossy_clamp ||
        g_data->studiolight_filter_quality != scene->eevee.gi_filter_quality)
    {
      inst->goo_data->lookdev_lightcache->flag |= LIGHTCACHE_UPDATE_WORLD;
      g_data->studiolight_index = sl->index;
      copy_m3_m3(g_data->studiolight_matrix, studiolight_matrix);
      g_data->studiolight_rot_z = shading->studiolight_rot_z;
      g_data->studiolight_intensity = shading->studiolight_intensity;
      g_data->studiolight_cubemap_res = scene->eevee.gi_cubemap_resolution;
      g_data->studiolight_glossy_clamp = scene->eevee.gi_glossy_clamp;
      g_data->studiolight_filter_quality = scene->eevee.gi_filter_quality;
    }
  }
}

static void eevee_lookdev_apply_taa(const EEVEE_EffectsInfo *effects,
                                    int sphere_size,
                                    float winmat[4][4])
{
  const DRWContext *draw_ctx = DRW_context_get();
  if (draw_ctx->is_image_render() || ((effects->enabled_effects & EFFECT_TAA) != 0)) {
    double ht_point[2];
    double ht_offset[2] = {0.0, 0.0};
    const uint ht_primes[2] = {2, 3};
    float ofs[2];

    BLI_halton_2d(ht_primes, ht_offset, effects->taa_current_sample, ht_point);
    EEVEE_temporal_sampling_offset_calc(ht_point, 1.5f, ofs);
    winmat[3][0] += ofs[0] / sphere_size;
    winmat[3][1] += ofs[1] / sphere_size;
  }
}

void EEVEE_lookdev_draw(Instance *inst)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  EEVEE_ViewLayerData *sldata = EEVEE_view_layer_data_ensure();

  const DRWContext *draw_ctx = DRW_context_get();

  if (inst->goo_data->lookdev_diffuse_pass && eevee_hdri_preview_overlay_enabled(draw_ctx->v3d)) {
    /* Config renderer. */
    EEVEE_CommonUniformBuffer *common = &sldata->common_data;
    common->la_num_light = 0;
    common->prb_num_planar = 0;
    common->prb_num_render_cube = 1;
    common->prb_num_render_grid = 1;
    common->ao_dist = 0.0f;
    common->ao_factor = 0.0f;
    common->ao_settings = 0.0f;
    GPU_uniformbuf_update(sldata->common_ubo, common);

    /* override matrices */
    float winmat[4][4], viewmat[4][4];
    unit_m4(winmat);
    /* Look through the negative Z. */
    negate_v3(winmat[2]);

    eevee_lookdev_apply_taa(effects, effects->sphere_size, winmat);

    /* "Remove" view matrix location. Leaving only rotation. */
    DRW_view_viewmat_get(nullptr, viewmat, false);
    zero_v3(viewmat[3]);

    if (effects->lookdev_view) {
      /* When rendering just update the view. This avoids recomputing the culling. */
      DRW_view_update_sub(effects->lookdev_view, viewmat, winmat);
    }
    else {
      /* Using default view bypasses the culling. */
      const DRWView *default_view = DRW_view_default_get();
      effects->lookdev_view = DRW_view_create_sub(default_view, viewmat, winmat);
    }

    DRW_view_set_active(effects->lookdev_view);

    /* Find the right frame-buffers to render to. */
    GPUFrameBuffer *fb = (effects->target_buffer == inst->goo_data->effect_color_fb) ? inst->goo_data->main_fb :
                                                                            inst->goo_data->effect_fb;

    GPU_framebuffer_bind(fb);

    const int sphere_margin = effects->sphere_size / 6.0f;
    float offset[2] = {0.0f, float(sphere_margin)};

    offset[0] = effects->sphere_size + sphere_margin;
    GPU_framebuffer_viewport_set(fb,
                                 effects->anchor[0] - offset[0],
                                 effects->anchor[1] + offset[1],
                                 effects->sphere_size,
                                 effects->sphere_size);

    DRW_draw_pass(inst->goo_data->lookdev_diffuse_pass);

    offset[0] = (effects->sphere_size + sphere_margin) +
                (sphere_margin + effects->sphere_size + sphere_margin);
    GPU_framebuffer_viewport_set(fb,
                                 effects->anchor[0] - offset[0],
                                 effects->anchor[1] + offset[1],
                                 effects->sphere_size,
                                 effects->sphere_size);

    DRW_draw_pass(inst->goo_data->lookdev_glossy_pass);

    GPU_framebuffer_viewport_reset(fb);

    DRW_view_set_active(nullptr);
  }
}

}  // namespace blender::gooengine
