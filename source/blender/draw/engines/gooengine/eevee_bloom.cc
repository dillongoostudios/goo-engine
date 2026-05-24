/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Eevee's bloom shader.
 */

#include "DRW_render.hh"
#include "draw_handle.hh"

#include "BLI_math_vector.h"

#include "GPU_texture.hh"

#include "DEG_depsgraph_query.hh"

#include "gooengine_private.hh"

namespace blender::gooengine {

int EEVEE_bloom_init(EEVEE_ViewLayerData * /*sldata*/, Instance *inst)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  const DRWContext *draw_ctx = DRW_context_get();
  const Scene *scene_eval = DEG_get_evaluated_scene(draw_ctx->depsgraph);

  if (scene_eval->eevee.flag & SCE_EEVEE_BLOOM_ENABLED) {
    const float2 viewport_size = draw_ctx->viewport_size_get();

    /* Bloom */
    int blitsize[2], texsize[2];

    /* Blit Buffer */
    effects->source_texel_size[0] = 1.0f / viewport_size[0];
    effects->source_texel_size[1] = 1.0f / viewport_size[1];

    blitsize[0] = int(viewport_size[0]);
    blitsize[1] = int(viewport_size[1]);

    effects->blit_texel_size[0] = 1.0f / float(blitsize[0]);
    effects->blit_texel_size[1] = 1.0f / float(blitsize[1]);

    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
    effects->bloom_blit = DRW_texture_pool_query_2d_ex(
        blitsize[0], blitsize[1], GPU_R11F_G11F_B10F, usage, 0);

    GPU_framebuffer_ensure_config(
        &inst->goo_data->bloom_blit_fb, {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(effects->bloom_blit)});

    /* Parameters */
    const float threshold = scene_eval->eevee.bloom_threshold;
    const float knee = scene_eval->eevee.bloom_knee;
    const float intensity = scene_eval->eevee.bloom_intensity;
    const float *color = scene_eval->eevee.bloom_color;
    const float radius = scene_eval->eevee.bloom_radius;
    effects->bloom_clamp = scene_eval->eevee.bloom_clamp;

    /* determine the iteration count */
    const float minDim = float(std::min(blitsize[0], blitsize[1]));
    const float maxIter = (radius - 8.0f) + log(minDim) / log(2);
    const int maxIterInt = effects->bloom_iteration_len = int(maxIter);

    CLAMP(effects->bloom_iteration_len, 1, MAX_BLOOM_STEP);

    effects->bloom_sample_scale = 0.5f + maxIter - float(maxIterInt);
    effects->bloom_curve_threshold[0] = threshold - knee;
    effects->bloom_curve_threshold[1] = knee * 2.0f;
    effects->bloom_curve_threshold[2] = 0.25f / max_ff(1e-5f, knee);
    effects->bloom_curve_threshold[3] = threshold;

    mul_v3_v3fl(effects->bloom_color, color, intensity);

    /* Downsample buffers */
    copy_v2_v2_int(texsize, blitsize);
    for (int i = 0; i < effects->bloom_iteration_len; i++) {
      texsize[0] /= 2;
      texsize[1] /= 2;

      texsize[0] = std::max(texsize[0], 2);
      texsize[1] = std::max(texsize[1], 2);

      effects->downsamp_texel_size[i][0] = 1.0f / float(texsize[0]);
      effects->downsamp_texel_size[i][1] = 1.0f / float(texsize[1]);

      eGPUTextureUsage downsample_usage = GPU_TEXTURE_USAGE_SHADER_READ |
                                          GPU_TEXTURE_USAGE_ATTACHMENT;
      effects->bloom_downsample[i] = DRW_texture_pool_query_2d_ex(
          texsize[0], texsize[1], GPU_R11F_G11F_B10F, downsample_usage, 0);
      GPU_framebuffer_ensure_config(
          &inst->goo_data->bloom_down_fb[i],
          {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(effects->bloom_downsample[i])});
    }

    /* Upsample buffers */
    copy_v2_v2_int(texsize, blitsize);
    for (int i = 0; i < effects->bloom_iteration_len - 1; i++) {
      texsize[0] /= 2;
      texsize[1] /= 2;

      texsize[0] = std::max(texsize[0], 2);
      texsize[1] = std::max(texsize[1], 2);

      eGPUTextureUsage upsample_usage = GPU_TEXTURE_USAGE_SHADER_READ |
                                        GPU_TEXTURE_USAGE_ATTACHMENT;

      effects->bloom_upsample[i] = DRW_texture_pool_query_2d_ex(
          texsize[0], texsize[1], GPU_R11F_G11F_B10F, upsample_usage, 0);
      GPU_framebuffer_ensure_config(
          &inst->goo_data->bloom_accum_fb[i],
          {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(effects->bloom_upsample[i])});
    }

    return EFFECT_BLOOM | EFFECT_POST_BUFFER;
  }

  /* Cleanup to release memory */
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->bloom_blit_fb);

  for (int i = 0; i < MAX_BLOOM_STEP - 1; i++) {
    GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->bloom_down_fb[i]);
    GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->bloom_accum_fb[i]);
  }

  return 0;
}

void EEVEE_bloom_cache_init(EEVEE_ViewLayerData * /*sldata*/, Instance *inst)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  if ((effects->enabled_effects & EFFECT_BLOOM) != 0) {
    /**
     * Bloom Algorithm
     *
     * Overview:
     * - Down-sample the color buffer doing a small blur during each step.
     * - Accumulate bloom color using previously down-sampled color buffers
     *   and do an up-sample blur for each new accumulated layer.
     * - Finally add accumulation buffer onto the source color buffer.
     *
     *  [1/1] is original copy resolution (can be half or quarter res for performance)
     * <pre>
     *                            [DOWNSAMPLE CHAIN]                      [UPSAMPLE CHAIN]
     *
     * Source Color ─ [Blit] ─> Bright Color Extract [1/1]                  Final Color
     *                                    |                                      Λ
     *                            [Downsample First]       Source Color ─> + [Resolve]
     *                                    v                                      |
     *                          Color Downsampled [1/2] ────────────> + Accumulation Buffer [1/2]
     *                                    |                                      Λ
     *                                   ───                                    ───
     *                                  Repeat                                 Repeat
     *                                   ───                                    ───
     *                                    v                                      |
     *                          Color Downsampled [1/N-1] ──────────> + Accumulation Buffer [1/N-1]
     *                                    |                                      Λ
     *                               [Downsample]                            [Upsample]
     *                                    v                                      |
     *                          Color Downsampled [1/N] ─────────────────────────┘
     * </pre>
     */

    {
      blender::draw::PassSimple &pass = inst->goo_data->bloom_downsample_first;
      pass.init();
      pass.state_set(DRW_STATE_WRITE_COLOR);
      pass.shader_set(inst->shaders.static_shader_get(BLOOM_DOWNSAMPLE_HQ));
      pass.bind_texture("sourceBuffer", &effects->unf_source_buffer);
      pass.push_constant("sourceBufferTexelSize", &effects->unf_source_texel_size);
      pass.draw(DRW_cache_fullscreen_quad_get());
    }
    
    {
      blender::draw::PassSimple &pass = inst->goo_data->bloom_downsample;
      pass.init();
      pass.state_set(DRW_STATE_WRITE_COLOR);
      pass.shader_set(inst->shaders.static_shader_get(BLOOM_DOWNSAMPLE));
      pass.bind_texture("sourceBuffer", &effects->unf_source_buffer);
      pass.push_constant("sourceBufferTexelSize", &effects->unf_source_texel_size);
      pass.draw(DRW_cache_fullscreen_quad_get());
    }

    {
      blender::draw::PassSimple &pass = inst->goo_data->bloom_upsample;
      pass.init();
      pass.state_set(DRW_STATE_WRITE_COLOR);
      pass.shader_set(inst->shaders.static_shader_get(BLOOM_UPSAMPLE_HQ));
      pass.bind_texture("sourceBuffer", &effects->unf_source_buffer);
      pass.push_constant("sourceBufferTexelSize", &effects->unf_source_texel_size);
      pass.bind_texture("baseBuffer", &effects->unf_base_buffer);
      pass.push_constant("sampleScale", &effects->bloom_sample_scale);
      pass.draw(DRW_cache_fullscreen_quad_get());
    }    

    {
      blender::draw::PassSimple &pass = inst->goo_data->bloom_blit;
      pass.init();
      pass.state_set(DRW_STATE_WRITE_COLOR);
      pass.shader_set(inst->shaders.static_shader_get(BLOOM_BLIT_HQ));
      pass.bind_texture("sourceBuffer", &effects->unf_source_buffer);
      pass.push_constant("sourceBufferTexelSize", &effects->unf_source_texel_size);
      pass.push_constant("curveThreshold", effects->bloom_curve_threshold);
      pass.push_constant("clampIntensity", &effects->bloom_clamp);
      pass.draw(DRW_cache_fullscreen_quad_get());
    }

    {
      blender::draw::PassSimple &pass = inst->goo_data->bloom_resolve;
      pass.init();
      pass.state_set(DRW_STATE_WRITE_COLOR);
      pass.shader_set(inst->shaders.static_shader_get(BLOOM_RESOLVE_HQ));
      pass.bind_texture("sourceBuffer", &effects->unf_source_buffer);
      pass.push_constant("sourceBufferTexelSize", &effects->unf_source_texel_size);
      pass.bind_texture("baseBuffer", &effects->unf_base_buffer);
      pass.push_constant("sampleScale", &effects->bloom_sample_scale);
      pass.push_constant("bloomColor", effects->bloom_color);
      pass.push_constant("bloomAddBase", true);
      pass.draw(DRW_cache_fullscreen_quad_get());
    }
  }
}

void EEVEE_bloom_draw(Instance *inst)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  /* Bloom */
  if ((effects->enabled_effects & EFFECT_BLOOM) != 0) {
    blender::draw::Manager *manager = DRW_manager_get();
    GPUTexture *last;

    /* Extract bright pixels */
    copy_v2_v2(effects->unf_source_texel_size, effects->source_texel_size);
    effects->unf_source_buffer = effects->source_buffer;

    GPU_framebuffer_bind(inst->goo_data->bloom_blit_fb);
    manager->submit(inst->goo_data->bloom_blit);

    /* Downsample */
    copy_v2_v2(effects->unf_source_texel_size, effects->blit_texel_size);
    effects->unf_source_buffer = effects->bloom_blit;

    GPU_framebuffer_bind(inst->goo_data->bloom_down_fb[0]);
    manager->submit(inst->goo_data->bloom_downsample_first);

    last = effects->bloom_downsample[0];

    for (int i = 1; i < effects->bloom_iteration_len; i++) {
      copy_v2_v2(effects->unf_source_texel_size, effects->downsamp_texel_size[i - 1]);
      effects->unf_source_buffer = last;

      GPU_framebuffer_bind(inst->goo_data->bloom_down_fb[i]);
      manager->submit(inst->goo_data->bloom_downsample);

      /* Used in next loop */
      last = effects->bloom_downsample[i];
    }

    /* Upsample and accumulate */
    for (int i = effects->bloom_iteration_len - 2; i >= 0; i--) {
      copy_v2_v2(effects->unf_source_texel_size, effects->downsamp_texel_size[i]);
      effects->unf_source_buffer = last;
      effects->unf_base_buffer = effects->bloom_downsample[i];

      GPU_framebuffer_bind(inst->goo_data->bloom_accum_fb[i]);
      manager->submit(inst->goo_data->bloom_upsample);

      last = effects->bloom_upsample[i];
    }

    /* Resolve */
    copy_v2_v2(effects->unf_source_texel_size, effects->downsamp_texel_size[0]);
    effects->unf_source_buffer = last;
    effects->unf_base_buffer = effects->source_buffer;

    GPU_framebuffer_bind(effects->target_buffer);
    manager->submit(inst->goo_data->bloom_resolve);
    SWAP_BUFFERS();
  }
}

void EEVEE_bloom_output_init(EEVEE_ViewLayerData * /*sldata*/,
                             Instance *inst,
                             uint /*tot_samples*/)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  /* Create FrameBuffer. */
  DRW_texture_ensure_fullscreen_2d(&inst->goo_data->bloom_accum, GPU_R11F_G11F_B10F, DRWTextureFlag(0));

  GPU_framebuffer_ensure_config(&inst->goo_data->bloom_pass_accum_fb,
                                {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(inst->goo_data->bloom_accum)});

  /* Create Pass. */
  blender::draw::PassSimple &pass = inst->goo_data->bloom_accum_ps;
  pass.init();
  pass.state_set(DRW_STATE_WRITE_COLOR);
  pass.shader_set(inst->shaders.static_shader_get(BLOOM_RESOLVE_HQ));
  pass.bind_texture("sourceBuffer", &effects->unf_source_buffer);
  pass.push_constant("sourceBufferTexelSize", &effects->unf_source_texel_size);
  pass.bind_texture("baseBuffer", &effects->unf_base_buffer);
  pass.push_constant("sampleScale", &effects->bloom_sample_scale);
  pass.push_constant("bloomColor", effects->bloom_color);
  pass.push_constant("bloomAddBase", false);
  pass.draw(DRW_cache_fullscreen_quad_get());
}

void EEVEE_bloom_output_accumulate(EEVEE_ViewLayerData * /*sldata*/, Instance *inst)
{

  if (inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_BLOOM) {
    blender::draw::Manager *manager = DRW_manager_get();
    
    GPU_framebuffer_bind(inst->goo_data->bloom_pass_accum_fb);
    manager->submit(inst->goo_data->bloom_accum_ps);

    /* Restore */
    GPU_framebuffer_bind(inst->goo_data->main_fb);
  }
}

}  // namespace blender::gooengine
