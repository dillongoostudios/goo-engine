/* SPDX-FileCopyrightText: 2019 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include "DRW_engine.hh"
#include "DRW_render.hh"
#include "draw_handle.hh"

#include "draw_color_management.hh" /* TODO: remove dependency. */

#include "BKE_global.hh" /* for G.debug_value */

#include "BLI_listbase.h"
#include "BLI_hash.h"
#include "BLI_string_utils.hh"

#include "DEG_depsgraph_query.hh"

#include "eevee_private.hh"

namespace blender::gooengine {

enum eRenderPassPostProcessType {
  PASS_POST_UNDEFINED = 0,
  PASS_POST_ACCUMULATED_COLOR = 1,
  PASS_POST_ACCUMULATED_COLOR_ALPHA = 2,
  PASS_POST_ACCUMULATED_LIGHT = 3,
  PASS_POST_ACCUMULATED_VALUE = 4,
  PASS_POST_DEPTH = 5,
  PASS_POST_AO = 6,
  PASS_POST_NORMAL = 7,
  PASS_POST_TWO_LIGHT_BUFFERS = 8,
  PASS_POST_ACCUMULATED_TRANSMITTANCE_COLOR = 9,
};

/* bitmask containing all renderpasses that need post-processing */
#define EEVEE_RENDERPASSES_WITH_POST_PROCESSING \
  (EEVEE_RENDER_PASS_Z | EEVEE_RENDER_PASS_MIST | EEVEE_RENDER_PASS_NORMAL | \
   EEVEE_RENDER_PASS_AO | EEVEE_RENDER_PASS_BLOOM | EEVEE_RENDER_PASS_VOLUME_LIGHT | \
   EEVEE_RENDER_PASS_TRANSPARENT | EEVEE_RENDER_PASS_SHADOW | EEVEE_RENDERPASSES_MATERIAL)

#define EEVEE_RENDERPASSES_ALL \
  (EEVEE_RENDERPASSES_WITH_POST_PROCESSING | EEVEE_RENDER_PASS_COMBINED)

#define EEVEE_RENDERPASSES_POST_PROCESS_ON_FIRST_SAMPLE \
  (EEVEE_RENDER_PASS_Z | EEVEE_RENDER_PASS_NORMAL)

#define EEVEE_RENDERPASSES_COLOR_PASS \
  (EEVEE_RENDER_PASS_DIFFUSE_COLOR | EEVEE_RENDER_PASS_SPECULAR_COLOR | EEVEE_RENDER_PASS_EMIT | \
   EEVEE_RENDER_PASS_BLOOM)
#define EEVEE_RENDERPASSES_LIGHT_PASS \
  (EEVEE_RENDER_PASS_DIFFUSE_LIGHT | EEVEE_RENDER_PASS_SPECULAR_LIGHT)
/* Render passes that uses volume transmittance when available */
#define EEVEE_RENDERPASSES_USES_TRANSMITTANCE \
  (EEVEE_RENDER_PASS_DIFFUSE_COLOR | EEVEE_RENDER_PASS_SPECULAR_COLOR | EEVEE_RENDER_PASS_EMIT | \
   EEVEE_RENDER_PASS_ENVIRONMENT)
bool EEVEE_renderpasses_only_first_sample_pass_active(Instance *inst)
{
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;
  return (g_data->render_passes & ~EEVEE_RENDERPASSES_POST_PROCESS_ON_FIRST_SAMPLE) == 0;
}

uint EEVEE_renderpasses_aov_hash(const ViewLayerAOV *aov)
{
  uint hash = BLI_hash_string(aov->name) << 1u;
  SET_FLAG_FROM_TEST(hash, aov->type == AOV_TYPE_COLOR, EEVEE_AOV_HASH_COLOR_TYPE_MASK);
  return hash;
}

void EEVEE_renderpasses_init(Instance *inst)
{
  const DRWContext *draw_ctx = DRW_context_get();
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;
  ViewLayer *view_layer = draw_ctx->view_layer;
  View3D *v3d = draw_ctx->v3d;

  if (v3d) {
    const Scene *scene = draw_ctx->scene;
    eViewLayerEEVEEPassType render_pass = eViewLayerEEVEEPassType(v3d->shading.render_pass);
    g_data->aov_hash = 0;

    if (render_pass == EEVEE_RENDER_PASS_BLOOM &&
        ((scene->eevee.flag & SCE_EEVEE_BLOOM_ENABLED) == 0))
    {
      render_pass = EEVEE_RENDER_PASS_COMBINED;
    }
    if (render_pass == EEVEE_RENDER_PASS_AOV) {
      ViewLayerAOV *aov = static_cast<ViewLayerAOV *>(
          BLI_findstring(&view_layer->aovs, v3d->shading.aov_name, offsetof(ViewLayerAOV, name)));
      if (aov != nullptr) {
        g_data->aov_hash = EEVEE_renderpasses_aov_hash(aov);
      }
      else {
        /* AOV not found in view layer. */
        render_pass = EEVEE_RENDER_PASS_COMBINED;
      }
    }

    g_data->render_passes = render_pass;
  }
  else {
    eViewLayerEEVEEPassType enabled_render_passes = eViewLayerEEVEEPassType(
        view_layer->eevee.render_passes);

#define ENABLE_FROM_LEGACY(name_legacy, name_eevee) \
  SET_FLAG_FROM_TEST(enabled_render_passes, \
                     (view_layer->passflag & SCE_PASS_##name_legacy) != 0, \
                     EEVEE_RENDER_PASS_##name_eevee);

    ENABLE_FROM_LEGACY(Z, Z)
    ENABLE_FROM_LEGACY(MIST, MIST)
    ENABLE_FROM_LEGACY(NORMAL, NORMAL)
    ENABLE_FROM_LEGACY(SHADOW, SHADOW)
    ENABLE_FROM_LEGACY(AO, AO)
    ENABLE_FROM_LEGACY(EMIT, EMIT)
    ENABLE_FROM_LEGACY(ENVIRONMENT, ENVIRONMENT)
    ENABLE_FROM_LEGACY(DIFFUSE_COLOR, DIFFUSE_COLOR)
    ENABLE_FROM_LEGACY(GLOSSY_COLOR, SPECULAR_COLOR)
    ENABLE_FROM_LEGACY(DIFFUSE_DIRECT, DIFFUSE_LIGHT)
    ENABLE_FROM_LEGACY(GLOSSY_DIRECT, SPECULAR_LIGHT)

    ENABLE_FROM_LEGACY(ENVIRONMENT, ENVIRONMENT)

#undef ENABLE_FROM_LEGACY
    if (draw_ctx->is_image_render() && !BLI_listbase_is_empty(&view_layer->aovs)) {
      enabled_render_passes |= EEVEE_RENDER_PASS_AOV;
      g_data->aov_hash = EEVEE_AOV_HASH_ALL;
    }

    g_data->render_passes = (enabled_render_passes & EEVEE_RENDERPASSES_ALL) |
                            EEVEE_RENDER_PASS_COMBINED;
  }
  EEVEE_material_renderpasses_init(inst);
  EEVEE_material_transparent_output_init(inst);
  EEVEE_cryptomatte_renderpasses_init(inst);
}

BLI_INLINE bool eevee_renderpasses_volumetric_active(const EEVEE_EffectsInfo *effects,
                                                     const EEVEE_PrivateData *g_data)
{
  if (effects->enabled_effects & EFFECT_VOLUMETRIC) {
    if (g_data->render_passes &
        (EEVEE_RENDER_PASS_VOLUME_LIGHT | EEVEE_RENDERPASSES_USES_TRANSMITTANCE))
    {
      return true;
    }
  }
  return false;
}

void EEVEE_renderpasses_output_init(EEVEE_ViewLayerData *sldata,
                                    Instance *inst,
                                    uint tot_samples)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;

  const bool needs_post_processing = (g_data->render_passes &
                                      EEVEE_RENDERPASSES_WITH_POST_PROCESSING) > 0;
  if (needs_post_processing) {
    /* Create FrameBuffer. */

    /* Should be enough to store the data needs for a single pass.
     * Some passes will use less, but it is only relevant for final renderings and
     * when renderpasses other than `EEVEE_RENDER_PASS_COMBINED` are requested */
    DRW_texture_ensure_fullscreen_2d(&inst->goo_data->renderpass, GPU_RGBA16F, DRWTextureFlag(0));
    GPU_framebuffer_ensure_config(&inst->goo_data->renderpass_fb,
                                  {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(inst->goo_data->renderpass)});

    if ((g_data->render_passes & EEVEE_RENDERPASSES_MATERIAL) != 0) {
      EEVEE_material_output_init(sldata, inst, tot_samples);
    }

    if ((g_data->render_passes & EEVEE_RENDER_PASS_MIST) != 0) {
      EEVEE_mist_output_init(sldata, inst);
    }
    if ((g_data->render_passes & EEVEE_RENDER_PASS_SHADOW) != 0) {
      EEVEE_shadow_output_init(sldata, inst, tot_samples);
    }

    if ((g_data->render_passes & EEVEE_RENDER_PASS_AO) != 0) {
      EEVEE_occlusion_output_init(sldata, inst, tot_samples);
    }

    if ((g_data->render_passes & EEVEE_RENDER_PASS_BLOOM) != 0 &&
        (effects->enabled_effects & EFFECT_BLOOM) != 0)
    {
      EEVEE_bloom_output_init(sldata, inst, tot_samples);
    }

    if (eevee_renderpasses_volumetric_active(effects, g_data)) {
      EEVEE_volumes_output_init(sldata, inst, tot_samples);
    }

    /* We set a default texture as not all post processes uses the inputBuffer. */
    g_data->renderpass_input = inst->goo_data->color;
    g_data->renderpass_col_input = inst->goo_data->color;
    g_data->renderpass_light_input = inst->goo_data->color;
    g_data->renderpass_transmittance_input = inst->goo_data->color;
  }
  else {
    /* Free unneeded memory */
    DRW_TEXTURE_FREE_SAFE(inst->goo_data->renderpass);
    GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->renderpass_fb);
  }

  /* Cryptomatte doesn't use the GPU shader for post processing */
  if ((g_data->render_passes & (EEVEE_RENDER_PASS_CRYPTOMATTE)) != 0) {
    EEVEE_cryptomatte_output_init(sldata, inst, tot_samples);
  }
}

void EEVEE_renderpasses_cache_finish(EEVEE_ViewLayerData *sldata, Instance *inst)
{
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;
  const DRWContext *draw_ctx = DRW_context_get();
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();

  const bool needs_post_processing = (g_data->render_passes &
                                      EEVEE_RENDERPASSES_WITH_POST_PROCESSING) > 0;
  if (needs_post_processing) {
    DRW_PASS_CREATE(inst->goo_data->renderpass_pass, DRW_STATE_WRITE_COLOR);
    DRWShadingGroup *grp = DRW_shgroup_create(EEVEE_shaders_renderpasses_post_process_sh_get(),
                                              inst->goo_data->renderpass_pass);
    DRW_shgroup_uniform_texture_ref(grp, "inputBuffer", &g_data->renderpass_input);
    DRW_shgroup_uniform_texture_ref(grp, "inputColorBuffer", &g_data->renderpass_col_input);
    DRW_shgroup_uniform_texture_ref(
        grp, "inputSecondLightBuffer", &g_data->renderpass_light_input);
    DRW_shgroup_uniform_texture_ref(
        grp, "inputTransmittanceBuffer", &g_data->renderpass_transmittance_input);
    DRW_shgroup_uniform_texture_ref(grp, "depthBuffer", &dtxl->depth);
    DRW_shgroup_uniform_block_ref(grp, "common_block", &sldata->common_ubo);
    DRW_shgroup_uniform_block_ref(grp, "renderpass_block", &sldata->renderpass_ubo.combined);
    DRW_shgroup_uniform_int(grp, "currentSample", &g_data->renderpass_current_sample, 1);
    DRW_shgroup_uniform_int(grp, "renderpassType", &g_data->renderpass_type, 1);
    DRW_shgroup_uniform_int(grp, "postProcessType", &g_data->renderpass_postprocess, 1);
    DRW_shgroup_call(grp, DRW_cache_fullscreen_quad_get(), nullptr);
  }
  else {
    inst->goo_data->renderpass_pass = nullptr;
  }
}

void EEVEE_renderpasses_postprocess(EEVEE_ViewLayerData * /*sldata*/,
                                    Instance *inst,
                                    eViewLayerEEVEEPassType renderpass_type,
                                    int aov_index)
{
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  const DRWContext *draw_ctx = DRW_context_get();

  /* Compensate for taa_current_sample being incremented after last drawing in
   * EEVEE_temporal_sampling_draw when DRW_state_is_image_render(). */
  const int current_sample = draw_ctx->is_image_render() ? effects->taa_current_sample - 1 :
                                                           effects->taa_current_sample;
  g_data->renderpass_current_sample = current_sample;
  g_data->renderpass_type = renderpass_type;
  g_data->renderpass_postprocess = PASS_POST_UNDEFINED;
  const bool volumetric_active = eevee_renderpasses_volumetric_active(effects, g_data);
  eRenderPassPostProcessType default_color_pass_type =
      volumetric_active ? PASS_POST_ACCUMULATED_TRANSMITTANCE_COLOR : PASS_POST_ACCUMULATED_COLOR;
  g_data->renderpass_transmittance_input = volumetric_active ? inst->goo_data->volume_transmittance_accum :
                                                               inst->goo_data->color;

  if (!volumetric_active && renderpass_type == EEVEE_RENDER_PASS_VOLUME_LIGHT) {
    /* Early exit: Volumetric effect is off, but the volume light pass was requested. */
    static float clear_col[4] = {0.0f};
    GPU_framebuffer_bind(inst->goo_data->renderpass_fb);
    GPU_framebuffer_clear_color(inst->goo_data->renderpass_fb, clear_col);
    return;
  }

  switch (renderpass_type) {
    case EEVEE_RENDER_PASS_Z: {
      g_data->renderpass_postprocess = PASS_POST_DEPTH;
      break;
    }
    case EEVEE_RENDER_PASS_AO: {
      g_data->renderpass_postprocess = PASS_POST_AO;
      g_data->renderpass_input = inst->goo_data->ao_accum;
      break;
    }
    case EEVEE_RENDER_PASS_NORMAL: {
      g_data->renderpass_postprocess = PASS_POST_NORMAL;
      g_data->renderpass_input = effects->ssr_normal_input;
      break;
    }
    case EEVEE_RENDER_PASS_MIST: {
      g_data->renderpass_postprocess = PASS_POST_ACCUMULATED_VALUE;
      g_data->renderpass_input = inst->goo_data->mist_accum;
      break;
    }
    case EEVEE_RENDER_PASS_VOLUME_LIGHT: {
      g_data->renderpass_postprocess = PASS_POST_ACCUMULATED_COLOR;
      g_data->renderpass_input = inst->goo_data->volume_scatter_accum;
      break;
    }
    case EEVEE_RENDER_PASS_SHADOW: {
      g_data->renderpass_postprocess = PASS_POST_ACCUMULATED_VALUE;
      g_data->renderpass_input = inst->goo_data->shadow_accum;
      break;
    }
    case EEVEE_RENDER_PASS_DIFFUSE_COLOR: {
      g_data->renderpass_postprocess = default_color_pass_type;
      g_data->renderpass_input = inst->goo_data->diff_color_accum;
      break;
    }
    case EEVEE_RENDER_PASS_SPECULAR_COLOR: {
      g_data->renderpass_postprocess = default_color_pass_type;
      g_data->renderpass_input = inst->goo_data->spec_color_accum;
      break;
    }
    case EEVEE_RENDER_PASS_ENVIRONMENT: {
      g_data->renderpass_postprocess = default_color_pass_type;
      g_data->renderpass_input = inst->goo_data->env_accum;
      break;
    }
    case EEVEE_RENDER_PASS_EMIT: {
      g_data->renderpass_postprocess = default_color_pass_type;
      g_data->renderpass_input = inst->goo_data->emit_accum;
      break;
    }
    case EEVEE_RENDER_PASS_SPECULAR_LIGHT: {
      g_data->renderpass_postprocess = PASS_POST_ACCUMULATED_LIGHT;
      g_data->renderpass_input = inst->goo_data->spec_light_accum;
      g_data->renderpass_col_input = inst->goo_data->spec_color_accum;
      if ((inst->goo_data->effects->enabled_effects & EFFECT_SSR) != 0) {
        g_data->renderpass_postprocess = PASS_POST_TWO_LIGHT_BUFFERS;
        g_data->renderpass_light_input = inst->goo_data->ssr_accum;
      }
      else {
        g_data->renderpass_postprocess = PASS_POST_ACCUMULATED_LIGHT;
      }
      break;
    }
    case EEVEE_RENDER_PASS_DIFFUSE_LIGHT: {
      g_data->renderpass_postprocess = PASS_POST_ACCUMULATED_LIGHT;
      g_data->renderpass_input = inst->goo_data->diff_light_accum;
      g_data->renderpass_col_input = inst->goo_data->diff_color_accum;
      if ((inst->goo_data->effects->enabled_effects & EFFECT_SSS) != 0) {
        g_data->renderpass_postprocess = PASS_POST_TWO_LIGHT_BUFFERS;
        g_data->renderpass_light_input = inst->goo_data->sss_accum;
      }
      else {
        g_data->renderpass_postprocess = PASS_POST_ACCUMULATED_LIGHT;
      }
      break;
    }
    case EEVEE_RENDER_PASS_AOV: {
      g_data->renderpass_postprocess = PASS_POST_ACCUMULATED_COLOR_ALPHA;
      g_data->renderpass_input = inst->goo_data->aov_surface_accum[aov_index];
      break;
    }
    case EEVEE_RENDER_PASS_TRANSPARENT: {
      g_data->renderpass_postprocess = PASS_POST_ACCUMULATED_COLOR_ALPHA;
      g_data->renderpass_input = inst->goo_data->transparent_accum;
      break;
    }
    case EEVEE_RENDER_PASS_BLOOM: {
      g_data->renderpass_postprocess = PASS_POST_ACCUMULATED_COLOR;
      g_data->renderpass_input = inst->goo_data->bloom_accum;
      g_data->renderpass_current_sample = 1;
      break;
    }
    default: {
      break;
    }
  }
  GPU_framebuffer_bind(inst->goo_data->renderpass_fb);
  DRW_draw_pass(inst->goo_data->renderpass_pass);
}

void EEVEE_renderpasses_output_accumulate(EEVEE_ViewLayerData *sldata,
                                          Instance *inst,
                                          bool post_effect)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;
  eViewLayerEEVEEPassType render_pass = g_data->render_passes;

  if (!post_effect) {
    if ((render_pass & EEVEE_RENDER_PASS_MIST) != 0) {
      EEVEE_mist_output_accumulate(sldata, inst);
    }
    if ((render_pass & EEVEE_RENDER_PASS_AO) != 0) {
      EEVEE_occlusion_output_accumulate(sldata, inst);
    }
    if ((render_pass & EEVEE_RENDER_PASS_SHADOW) != 0) {
      EEVEE_shadow_output_accumulate(sldata, inst);
    }
    if ((render_pass & EEVEE_RENDERPASSES_MATERIAL) != 0) {
      EEVEE_material_output_accumulate(sldata, inst);
    }
    if (eevee_renderpasses_volumetric_active(effects, g_data)) {
      EEVEE_volumes_output_accumulate(sldata, inst);
    }
    if ((render_pass & EEVEE_RENDER_PASS_CRYPTOMATTE) != 0) {
      EEVEE_cryptomatte_output_accumulate(sldata, inst);
    }
  }
  else {
    if ((render_pass & EEVEE_RENDER_PASS_BLOOM) != 0 &&
        (effects->enabled_effects & EFFECT_BLOOM) != 0)
    {
      EEVEE_bloom_output_accumulate(sldata, inst);
    }
  }
}

void EEVEE_renderpasses_draw(EEVEE_ViewLayerData *sldata, Instance *inst)
{
  const DRWContext *draw_ctx = DRW_context_get();
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  DefaultFramebufferList *dfbl = draw_ctx->viewport_framebuffer_list_get();

  /* We can only draw a single render-pass. Light-passes also select their color pass
   * (a second pass). We mask the light pass when a light pass is selected. */
  const eViewLayerEEVEEPassType render_pass =
      ((inst->goo_data->g_data->render_passes & EEVEE_RENDERPASSES_LIGHT_PASS) != 0) ?
          (inst->goo_data->g_data->render_passes & EEVEE_RENDERPASSES_LIGHT_PASS) :
          inst->goo_data->g_data->render_passes;

  bool is_valid = (render_pass & EEVEE_RENDERPASSES_ALL) != 0;
  bool needs_color_transfer = (render_pass & EEVEE_RENDERPASSES_COLOR_PASS) != 0 &&
                              draw_ctx->is_viewport_image_render();
  UNUSED_VARS(needs_color_transfer);

  if ((render_pass & EEVEE_RENDER_PASS_BLOOM) != 0 &&
      (effects->enabled_effects & EFFECT_BLOOM) == 0)
  {
    is_valid = false;
  }

  const int current_sample = inst->goo_data->effects->taa_current_sample;
  const int total_samples = inst->goo_data->effects->taa_total_sample;
  if ((render_pass & EEVEE_RENDERPASSES_POST_PROCESS_ON_FIRST_SAMPLE) &&
      (current_sample > 1 && total_samples != 1))
  {
    return;
  }

  if (is_valid) {
    EEVEE_renderpasses_postprocess(sldata, inst, render_pass, 0);
    GPU_framebuffer_bind(dfbl->default_fb);
    DRW_transform_none(inst->goo_data->renderpass);
  }
  else {
    /* Draw state is not valid for this pass, clear the buffer */
    static float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    GPU_framebuffer_bind(dfbl->default_fb);
    GPU_framebuffer_clear_color(dfbl->default_fb, clear_color);
  }
  GPU_framebuffer_bind(inst->goo_data->main_fb);
}

void EEVEE_renderpasses_draw_debug(Instance *inst)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  GPUTexture *tx = nullptr;
  /* Debug : Output buffer to view. */
  switch (G.debug_value) {
    case 1:
      tx = inst->goo_data->maxzbuffer;
      break;
    case 2:
      /* UNUSED */
      break;
    case 3:
      tx = effects->ssr_normal_input;
      break;
    case 4:
      tx = effects->ssr_specrough_input;
      break;
    case 5:
      tx = inst->goo_data->color_double_buffer;
      break;
    case 6:
      tx = effects->gtao_horizons_renderpass;
      break;
    case 7:
      tx = effects->gtao_horizons_renderpass;
      break;
    case 8:
      tx = effects->sss_irradiance;
      break;
    case 9:
      tx = effects->sss_radius;
      break;
    case 10:
      tx = effects->sss_albedo;
      break;
    case 11:
      tx = effects->velocity_tx;
      break;
    default:
      break;
  }

  if (tx) {
    DRW_transform_none(tx);
  }
}

void EEVEE_reset_viewport_data(Instance *inst)
{
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->gtao_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->gtao_debug_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->downsample_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->maxzbuffer_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->bloom_blit_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(*inst->goo_data->bloom_down_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(*inst->goo_data->bloom_accum_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->bloom_pass_accum_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->cryptomatte_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->shadow_accum_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->ssr_accum_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->sss_blur_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->sss_blit_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->sss_resolve_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->sss_clear_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->sss_translucency_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->sss_accum_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_setup_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_flatten_tiles_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_dilate_tiles_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_downsample_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_reduce_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_reduce_copy_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_bokeh_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_gather_fg_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_filter_fg_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_gather_fg_holefill_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_gather_bg_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_filter_bg_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_scatter_fg_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->dof_scatter_bg_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->volumetric_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->volumetric_scat_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->volumetric_integ_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->volumetric_accum_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->screen_tracing_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->mist_accum_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->material_accum_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->renderpass_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->ao_accum_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->velocity_resolve_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->velocity_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(*inst->goo_data->velocity_tiles_fb);

  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->update_noise_fb);

  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->planarref_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->planar_downsample_fb);

  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->main_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->main_color_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->effect_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->effect_color_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->radiance_filtered_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->double_buffer_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->double_buffer_color_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->double_buffer_depth_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->transparent_rpass_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->transparent_rpass_accum_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->taa_history_fb);
  GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->taa_history_color_fb);

  GPU_TEXTURE_FREE_SAFE(inst->goo_data->color_post);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->mist_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->ao_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->sss_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->env_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->diff_color_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->diff_light_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->spec_color_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->spec_light_accum);
  GPU_TEXTURE_FREE_SAFE(*inst->goo_data->aov_surface_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->emit_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->bloom_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->ssr_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->shadow_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->transparent_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->transparent_depth_tmp);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->transparent_color_tmp);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->cryptomatte);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->taa_history);

  GPU_TEXTURE_FREE_SAFE(inst->goo_data->dof_reduced_color);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->dof_reduced_coc);

  GPU_TEXTURE_FREE_SAFE(inst->goo_data->volume_prop_scattering);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->volume_prop_extinction);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->volume_prop_emission);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->volume_prop_phase);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->volume_scatter);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->volume_transmit);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->volume_scatter_history);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->volume_transmit_history);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->volume_scatter_accum);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->volume_transmittance_accum);

  GPU_TEXTURE_FREE_SAFE(inst->goo_data->lookdev_grid_tx);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->lookdev_cube_tx);

  GPU_TEXTURE_FREE_SAFE(inst->goo_data->planar_pool);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->planar_depth);

  GPU_TEXTURE_FREE_SAFE(inst->goo_data->maxzbuffer);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->filtered_radiance);

  GPU_TEXTURE_FREE_SAFE(inst->goo_data->renderpass);

  GPU_TEXTURE_FREE_SAFE(inst->goo_data->color);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->color_double_buffer);
  GPU_TEXTURE_FREE_SAFE(inst->goo_data->depth_double_buffer);
}

}  // namespace blender::gooengine
