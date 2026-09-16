/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Screen space reflections and refractions techniques.
 */

#include "DRW_render.hh"
#include "draw_handle.hh"

#include "BLI_dynstr.h"
#include "BLI_string_utils.hh"

#include "DEG_depsgraph_query.hh"

#include "GPU_platform.hh"
#include "GPU_texture.hh"
#include "gooengine_private.hh"

namespace blender::gooengine {

int EEVEE_screen_raytrace_init(EEVEE_ViewLayerData *sldata, Instance *inst)
{
  EEVEE_CommonUniformBuffer *common_data = &sldata->common_data;
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  const DRWContext *draw_ctx = DRW_context_get();
  const float2 viewport_size = draw_ctx->viewport_size_get();

  const Scene *scene_eval = DEG_get_evaluated_scene(draw_ctx->depsgraph);

  if (scene_eval->eevee.flag & SCE_EEVEE_SSR_ENABLED) {
    const bool use_refraction = (scene_eval->eevee.flag & SCE_EEVEE_SSR_REFRACTION) != 0;

    const bool is_persp = DRW_view_is_persp_get(nullptr);
    if (effects->ssr_was_persp != is_persp) {
      effects->ssr_was_persp = is_persp;
      DRW_viewport_request_redraw();
      EEVEE_temporal_sampling_reset(inst);
      inst->goo_data->g_data->valid_double_buffer = false;
    }

    if (!effects->ssr_was_valid_double_buffer) {
      DRW_viewport_request_redraw();
      EEVEE_temporal_sampling_reset(inst);
    }
    effects->ssr_was_valid_double_buffer = inst->goo_data->g_data->valid_double_buffer;

    effects->reflection_trace_full = (scene_eval->eevee.flag & SCE_EEVEE_SSR_HALF_RESOLUTION) == 0;
    common_data->ssr_thickness = scene_eval->eevee.ssr_thickness;
    common_data->ssr_border_fac = scene_eval->eevee.ssr_border_fade;
    common_data->ssr_firefly_fac = scene_eval->eevee.ssr_firefly_fac;
    common_data->ssr_max_roughness = scene_eval->eevee.ssr_max_roughness;
    common_data->ssr_quality = 1.0f - 0.95f * scene_eval->eevee.ssr_quality;
    common_data->ssr_brdf_bias = 0.1f + common_data->ssr_quality * 0.6f; /* Range [0.1, 0.7]. */

    if (common_data->ssr_firefly_fac < 1e-8f) {
      common_data->ssr_firefly_fac = FLT_MAX;
    }

    const int divisor = (effects->reflection_trace_full) ? 1 : 2;
    int tracing_res[2] = {int(viewport_size[0]) / divisor, int(viewport_size[1]) / divisor};
    const int size_fs[2] = {int(viewport_size[0]), int(viewport_size[1])};
    const bool high_qual_input = true; /* TODO: dither low quality input. */
    const eGPUTextureFormat format = (high_qual_input) ? GPU_RGBA16F : GPU_RGBA8;

    tracing_res[0] = max_ii(1, tracing_res[0]);
    tracing_res[1] = max_ii(1, tracing_res[1]);

    common_data->ssr_uv_scale[0] = size_fs[0] / (float(tracing_res[0]) * divisor);
    common_data->ssr_uv_scale[1] = size_fs[1] / (float(tracing_res[1]) * divisor);

    /* MRT for the shading pass in order to output needed data for the SSR pass. */
    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
    effects->ssr_specrough_input = DRW_texture_pool_query_2d_ex(
        UNPACK2(size_fs), format, usage, 0);

    GPU_framebuffer_texture_attach(inst->goo_data->main_fb, effects->ssr_specrough_input, 2, 0);

    /* Ray-tracing output. */
    effects->ssr_hit_output = DRW_texture_pool_query_2d_ex(
        UNPACK2(tracing_res), GPU_RGBA16F, usage, 0);
    effects->ssr_hit_depth = DRW_texture_pool_query_2d_ex(
        UNPACK2(tracing_res), GPU_R16F, usage, 0);

    GPU_framebuffer_ensure_config(&inst->goo_data->screen_tracing_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(effects->ssr_hit_output),
                                      GPU_ATTACHMENT_TEXTURE(effects->ssr_hit_depth),
                                  });

    /* NOTE(Metal): Intel GPUs rendering with Metal require the reflections pass to be split
     * into two separate phases. This reduces the individual complexity of each shader
     * invocation. */
    effects->use_split_ssr_pass = GPU_type_matches_ex(
        GPU_DEVICE_INTEL, GPU_OS_MAC, GPU_DRIVER_ANY, GPU_BACKEND_METAL);

    return EFFECT_SSR | EFFECT_NORMAL_BUFFER | EFFECT_RADIANCE_BUFFER | EFFECT_DOUBLE_BUFFER |
           ((use_refraction) ? EFFECT_REFRACT : 0);
  }

  /* Cleanup to release memory */
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->screen_tracing_fb);
  effects->ssr_specrough_input = nullptr;
  effects->ssr_hit_output = nullptr;

  return 0;
}

void EEVEE_screen_raytrace_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst)
{
  blender::draw::Manager *manager = DRW_manager_get();
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  LightCache *lcache = inst->goo_data->g_data->light_cache;

  if ((effects->enabled_effects & EFFECT_SSR) != 0) {
    
    int3 hitbuf_size;
    GPU_texture_get_mipmap_size(effects->ssr_hit_output, 0, hitbuf_size);

    /**
     * Screen space ray-tracing overview.
     *
     * Following Frostbite stochastic SSR.
     *
     * - First pass Trace rays across the depth buffer. The hit position and PDF are
     *   recorded in a RGBA16F render target for each ray (sample).
     *
     * - We down-sample the previous frame color buffer.
     *
     * - For each final pixel, we gather neighbors rays and choose a color buffer
     *   mipmap for each ray using its PDF. (filtered importance sampling)
     *   We then evaluate the lighting from the probes and mix the results together.
     */
    blender::draw::PassSimple &pass = inst->goo_data->ssr_raytrace;
    pass.init();
    pass.state_set(DRW_STATE_WRITE_COLOR);
    pass.shader_set(inst->shaders.static_shader_get(EFFECT_REFLECTION_TRACE));

    pass.bind_texture("normalBuffer", &effects->ssr_normal_input);
    pass.bind_texture("specroughBuffer", &effects->ssr_specrough_input);
    pass.bind_texture("maxzBuffer", &inst->goo_data->maxzbuffer);
    pass.bind_texture("planarDepth", &inst->goo_data->planar_depth);
    pass.bind_texture("probeCubes", &lcache->cube_tx.tex);
    pass.bind_texture("irradianceGrid", &lcache->grid_tx.tex);
    pass.bind_texture("probePlanars", &inst->goo_data->planar_pool);
    
    pass.bind_texture("utilTex", EEVEE_materials_get_util_tex());
    pass.bind_ubo("grid_block", sldata->grid_ubo);
    pass.bind_ubo("probe_block", sldata->probe_ubo);
    pass.bind_ubo("planar_block", sldata->planar_ubo);
    pass.bind_ubo("common_block", sldata->common_ubo);
    pass.bind_ubo("renderpass_block", sldata->renderpass_ubo.combined);
    pass.push_constant("targetSize", float2{float(hitbuf_size[0]), float(hitbuf_size[1])});
    pass.push_constant("randomScale", effects->reflection_trace_full ? 0.0f : 0.5f);

    pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);

    GPUSamplerState no_filter = GPUSamplerState::default_sampler();

    if (effects->use_split_ssr_pass) {
      /* Prepare passes for split reflections resolve variant. */
      for (int i = 0; i < 2; i++) {
        /* Prepare Reflection Probes resolve pass. */
        /* Prepare SSR resolve pass. */
        blender::draw::PassSimple &pass = (i == 0) ? inst->goo_data->ssr_resolve_probe : inst->goo_data->ssr_resolve_refl;
        pass.init();
        pass.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ADD);

        if (i == 0) {
          pass.shader_set(inst->shaders.static_shader_get(EFFECT_REFLECTION_RESOLVE_PROBE));
        }
        else {
          pass.shader_set(inst->shaders.static_shader_get(EFFECT_REFLECTION_RESOLVE_REFL));
        }
        
        pass.bind_texture("normalBuffer", &effects->ssr_normal_input);
        pass.bind_texture("specroughBuffer", &effects->ssr_specrough_input);
        pass.bind_texture("probeCubes", &lcache->cube_tx.tex);
        pass.bind_texture("probePlanars", &inst->goo_data->planar_pool);
        pass.bind_texture("planarDepth", &inst->goo_data->planar_depth);
        pass.bind_texture("hitBuffer", &effects->ssr_hit_output, no_filter);
        pass.bind_texture("hitDepth", &effects->ssr_hit_depth, no_filter);
        pass.bind_texture("colorBuffer", &inst->goo_data->filtered_radiance);
        pass.bind_texture("maxzBuffer", &inst->goo_data->maxzbuffer);
        pass.bind_texture("shadowCubeTexture", &sldata->shadow_cube_pool);
        pass.bind_texture("shadowCascadeTexture", &sldata->shadow_cascade_pool);
        pass.bind_texture("shadowCubeIDTexture", &sldata->shadow_cube_id_pool);
        pass.bind_texture("shadowCascadeIDTexture", &sldata->shadow_cascade_id_pool);

        pass.bind_texture("irradianceGrid", &lcache->grid_tx.tex);
        pass.bind_texture("utilTex", EEVEE_materials_get_util_tex());
        pass.bind_ubo("light_block", sldata->light_ubo);
        pass.bind_ubo("shadow_block", sldata->shadow_ubo);
        pass.bind_ubo("grid_block", sldata->grid_ubo);
        pass.bind_ubo("probe_block", sldata->probe_ubo);
        pass.bind_ubo("planar_block", sldata->planar_ubo);
        pass.bind_ubo("common_block", sldata->common_ubo);
        pass.bind_ubo("renderpass_block", sldata->renderpass_ubo.combined);
        pass.push_constant("samplePoolOffset", &effects->taa_current_sample);
        
        pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);
      }
    }
    else {
      /* Prepare standard reflections resolve pass. */
      blender::draw::PassSimple &pass = inst->goo_data->ssr_resolve;
      pass.init();
      pass.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ADD);
      pass.shader_set(inst->shaders.static_shader_get(EFFECT_REFLECTION_RESOLVE));

      pass.bind_texture("normalBuffer", &effects->ssr_normal_input);
      pass.bind_texture("specroughBuffer", &effects->ssr_specrough_input);
      pass.bind_texture("probeCubes", &lcache->cube_tx.tex);
      pass.bind_texture("probePlanars", &inst->goo_data->planar_pool);
      pass.bind_texture("planarDepth", &inst->goo_data->planar_depth);
      pass.bind_texture("hitBuffer", &effects->ssr_hit_output, no_filter);
      pass.bind_texture("hitDepth", &effects->ssr_hit_depth, no_filter);
      pass.bind_texture("colorBuffer", &inst->goo_data->filtered_radiance);
      pass.bind_texture("maxzBuffer", &inst->goo_data->maxzbuffer);
      pass.bind_texture("shadowCubeTexture", &sldata->shadow_cube_pool);
      pass.bind_texture("shadowCascadeTexture", &sldata->shadow_cascade_pool);
      pass.bind_texture("shadowCubeIDTexture", &sldata->shadow_cube_id_pool);
      pass.bind_texture("shadowCascadeIDTexture", &sldata->shadow_cascade_id_pool);
      pass.bind_texture("utilTex", EEVEE_materials_get_util_tex());
      pass.bind_ubo("light_block", sldata->light_ubo);
      pass.bind_ubo("shadow_block", sldata->shadow_ubo);
      pass.bind_ubo("grid_block", sldata->grid_ubo);
      pass.bind_ubo("probe_block", sldata->probe_ubo);
      pass.bind_ubo("planar_block", sldata->planar_ubo);
      pass.bind_ubo("common_block", sldata->common_ubo);
      pass.bind_ubo("renderpass_block", sldata->renderpass_ubo.combined);
      pass.push_constant("samplePoolOffset", &effects->taa_current_sample);
      
      pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);
    }
  }
}

void EEVEE_refraction_compute(EEVEE_ViewLayerData * /*sldata*/, Instance *inst)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  if ((effects->enabled_effects & EFFECT_REFRACT) != 0) {
    EEVEE_effects_downsample_radiance_buffer(inst, inst->goo_data->color);

    /* Restore */
    GPU_framebuffer_bind(inst->goo_data->main_fb);
  }
}

void EEVEE_reflection_compute(EEVEE_ViewLayerData * /*sldata*/, Instance *inst)
{
  blender::draw::Manager *manager = DRW_manager_get();
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  if (((effects->enabled_effects & EFFECT_SSR) != 0) && inst->goo_data->g_data->valid_double_buffer) {
    /* Ray-trace. */
    GPU_framebuffer_bind(inst->goo_data->screen_tracing_fb);
    manager->submit(inst->goo_data->ssr_raytrace);

    EEVEE_effects_downsample_radiance_buffer(inst, inst->goo_data->color_double_buffer);

    GPU_framebuffer_bind(inst->goo_data->main_color_fb);

    if (effects->use_split_ssr_pass) {
      /* Trace reflections for probes and SSR independently */
      manager->submit(inst->goo_data->ssr_resolve_probe);
      manager->submit(inst->goo_data->ssr_resolve_refl);
    }
    else {
      manager->submit(inst->goo_data->ssr_resolve);
    }

    /* Restore */
    GPU_framebuffer_bind(inst->goo_data->main_fb);
  }
}

void EEVEE_reflection_output_init(EEVEE_ViewLayerData * /*sldata*/,
                                  Instance *inst,
                                  uint tot_samples)
{

  /* Create FrameBuffer. */
  const eGPUTextureFormat texture_format = (tot_samples > 256) ? GPU_RGBA32F : GPU_RGBA16F;
  DRW_texture_ensure_fullscreen_2d(&inst->goo_data->ssr_accum, texture_format, DRWTextureFlag(0));

  GPU_framebuffer_ensure_config(&inst->goo_data->ssr_accum_fb,
                                {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(inst->goo_data->ssr_accum)});
}

void EEVEE_reflection_output_accumulate(EEVEE_ViewLayerData * /*sldata*/, Instance *inst)
{
  blender::draw::Manager *manager = DRW_manager_get();
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  if (inst->goo_data->g_data->valid_double_buffer) {
    GPU_framebuffer_bind(inst->goo_data->ssr_accum_fb);

    /* Clear texture. */
    if (effects->taa_current_sample == 1) {
      const float4 clear = {0.0f, 0.0f, 0.0f, 0.0f};
      GPU_framebuffer_clear_color(inst->goo_data->ssr_accum_fb, clear);
    }

    if (effects->use_split_ssr_pass) {
      manager->submit(inst->goo_data->ssr_resolve_probe);
      manager->submit(inst->goo_data->ssr_resolve_refl);
    }
    else {
      manager->submit(inst->goo_data->ssr_resolve);
    }
  }
}

}  // namespace blender::gooengine
