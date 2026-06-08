/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

/**
 * Render functions for final render outputs.
 */

#include "DRW_engine.hh"
#include "DRW_render.hh"

#include "DNA_node_types.h"
#include "DNA_object_types.h"

#include "BKE_global.hh"
#include "BKE_object.hh"

#include "BLI_rand.h"
#include "BLI_rect.h"
#include "BLI_math_vector.h"

#include "DEG_depsgraph_query.hh"

#include "GPU_capabilities.hh"
#include "GPU_context.hh"
#include "GPU_framebuffer.hh"
#include "GPU_state.hh"

#include "RE_pipeline.h"

#include "IMB_imbuf_types.hh"

#include "draw_common.hh"
#include "gooengine_private.hh"

namespace blender::gooengine {

bool EEVEE_render_init(Instance *inst, RenderEngine *engine, Depsgraph *depsgraph)
{
  Scene *scene = DEG_get_evaluated_scene(depsgraph);
  const DRWContext *draw_ctx = DRW_context_get();
  const float2 size_orig = draw_ctx->viewport_size_get();
  float size_final[2];

  /* Init default FB and render targets:
   * In render mode the default framebuffer is not generated
   * because there is no viewport. So we need to manually create it or
   * not use it. For code clarity we just allocate it make use of it. */
  DefaultFramebufferList *dfbl = draw_ctx->viewport_framebuffer_list_get();
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();

  /* Alloc transient data. */
  if (!inst->goo_data->g_data) {
    inst->goo_data->g_data = static_cast<EEVEE_PrivateData *>(MEM_callocN(sizeof(*inst->goo_data->g_data), __func__));
  }
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;
  g_data->background_alpha = DRW_state_draw_background() ? 1.0f : 0.0f;
  g_data->valid_double_buffer = false;
  copy_v2_v2(g_data->size_orig, size_orig);

  float *camtexcofac = g_data->camtexcofac;
  if (scene->eevee.flag & SCE_EEVEE_OVERSCAN) {
    g_data->overscan = scene->eevee.overscan / 100.0f;
    g_data->overscan_pixels = roundf(max_ff(size_orig[0], size_orig[1]) * g_data->overscan);

    madd_v2_v2v2fl(size_final, size_orig, blender::float2{2.0f, 2.0f}, g_data->overscan_pixels);

    camtexcofac[0] = size_final[0] / size_orig[0];
    camtexcofac[1] = size_final[1] / size_orig[1];

    camtexcofac[2] = -camtexcofac[0] * g_data->overscan_pixels / size_final[0];
    camtexcofac[3] = -camtexcofac[1] * g_data->overscan_pixels / size_final[1];
  }
  else {
    copy_v2_v2(size_final, size_orig);
    g_data->overscan = 0.0f;
    g_data->overscan_pixels = 0.0f;
    copy_v4_fl4(camtexcofac, 1.0f, 1.0f, 0.0f, 0.0f);
  }

  const int final_res[2] = {
      int(size_orig[0] + g_data->overscan_pixels * 2.0f),
      int(size_orig[1] + g_data->overscan_pixels * 2.0f),
  };

  int max_dim = max_ii(final_res[0], final_res[1]);
  if (max_dim > GPU_max_texture_size()) {
    char error_msg[128];
    SNPRINTF(error_msg,
             "Error: Reported texture size limit (%dpx) is lower than output size (%dpx).",
             GPU_max_texture_size(),
             max_dim);
    RE_engine_set_error_message(engine, error_msg);
    G.is_break = true;
    return false;
  }

  /* XXX overriding viewport size. Simplify things but is not really 100% safe. */
  DRW_render_viewport_size_set(final_res);

  /* TODO: 32 bit depth. */
  DRW_texture_ensure_fullscreen_2d(&dtxl->depth, GPU_DEPTH24_STENCIL8, DRWTextureFlag(0));
  DRW_texture_ensure_fullscreen_2d(&inst->goo_data->color, GPU_RGBA32F, DRW_TEX_FILTER);

  GPU_framebuffer_ensure_config(
      &dfbl->default_fb,
      {GPU_ATTACHMENT_TEXTURE(dtxl->depth), GPU_ATTACHMENT_TEXTURE(inst->goo_data->color)});
  GPU_framebuffer_ensure_config(
      &inst->goo_data->main_fb, {GPU_ATTACHMENT_TEXTURE(dtxl->depth), GPU_ATTACHMENT_TEXTURE(inst->goo_data->color)});
  GPU_framebuffer_ensure_config(&inst->goo_data->main_color_fb,
                                {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(inst->goo_data->color)});

  /* Camera could change because of Motion blur. */
  g_data->cam_original_ob = RE_GetCamera(engine->re);

  return true;
}

void EEVEE_render_modules_init(Instance *inst, RenderEngine *engine, Depsgraph *depsgraph)
{
  EEVEE_ViewLayerData *sldata = EEVEE_view_layer_data_ensure();
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;
  /* TODO(sergey): Shall render hold pointer to an evaluated camera instead? */
  Object *ob_camera_eval = DEG_get_evaluated(depsgraph, g_data->cam_original_ob);
  EEVEE_render_view_sync(inst, engine, depsgraph);

  /* `EEVEE_renderpasses_init` will set the active render passes used by `EEVEE_effects_init`.
   * `EEVEE_effects_init` needs to go second for TAA. */
  EEVEE_renderpasses_init(inst);
  EEVEE_effects_init(sldata, inst, ob_camera_eval, false);
  EEVEE_materials_init(sldata, inst);
  EEVEE_shadows_init(sldata);
  EEVEE_lightprobes_init(sldata, inst);
}

void EEVEE_render_view_sync(Instance *inst, RenderEngine *engine, Depsgraph *depsgraph)
{
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;

  /* Set the perspective & view matrix. */
  float winmat[4][4], viewmat[4][4], viewinv[4][4];
  /* TODO(sergey): Shall render hold pointer to an evaluated camera instead? */
  Object *ob_camera_eval = DEG_get_evaluated(depsgraph, g_data->cam_original_ob);

  RE_GetCameraWindow(engine->re, ob_camera_eval, winmat);
  RE_GetCameraWindowWithOverscan(engine->re, g_data->overscan, winmat);
  RE_GetCameraModelMatrix(engine->re, ob_camera_eval, viewinv);

  invert_m4_m4(viewmat, viewinv);

  DRWView *view = DRW_view_create(viewmat, winmat, nullptr, nullptr, nullptr);
  DRW_view_reset();
  DRW_view_default_set(view);
  DRW_view_set_active(view);
}

void EEVEE_render_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst)
{
  EEVEE_view_layer_data_ensure();
  EEVEE_bloom_cache_init(sldata, inst);
  EEVEE_depth_of_field_cache_init(sldata, inst);
  EEVEE_effects_cache_init(sldata, inst);
  EEVEE_lightprobes_cache_init(sldata, inst);
  EEVEE_lights_cache_init(sldata, inst);
  EEVEE_materials_cache_init(sldata, inst);
  EEVEE_motion_blur_cache_init(sldata, inst);
  EEVEE_occlusion_cache_init(sldata, inst);
  EEVEE_screen_raytrace_cache_init(sldata, inst);
  EEVEE_subsurface_cache_init(sldata, inst);
  EEVEE_temporal_sampling_cache_init(sldata, inst);
  EEVEE_volumes_cache_init(sldata, inst);
  EEVEE_cryptomatte_cache_init(sldata, inst);
}

void EEVEE_render_cache(Instance *inst, blender::draw::ObjectRef &ob_ref)
{
  EEVEE_ViewLayerData *sldata = EEVEE_view_layer_data_ensure();
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;
  EEVEE_LightProbesInfo *pinfo = sldata->probes;
  Object *ob = ob_ref.object;
  bool cast_shadow = false;

  const bool do_cryptomatte = (inst->render != nullptr) &&
                              ((g_data->render_passes & EEVEE_RENDER_PASS_CRYPTOMATTE) != 0);

  eevee_id_update(inst, &ob->id);

  if (pinfo->vis_data.collection) {
    /* Used for rendering probe with visibility groups. */
    bool ob_vis = BKE_collection_has_object_recursive(pinfo->vis_data.collection, ob);
    ob_vis = (pinfo->vis_data.invert) ? !ob_vis : ob_vis;

    if (!ob_vis) {
      return;
    }
  }

  /* Don't print dupli objects as this can be very verbose and
   * increase the render time on Windows because of slow windows term.
   * (see #59649) */
  if (inst->render && (ob->base_flag & BASE_FROM_DUPLI) == 0) {
    char info[42];
    SNPRINTF(info, "Syncing %s", ob->id.name + 2);
    RE_engine_update_stats(inst->render, nullptr, info);
  }

  const int ob_visibility = DRW_object_visibility_in_active_context(ob);
  if (ob_visibility & OB_VISIBLE_PARTICLES) {
    GOOENGINE_sync_hair(
        inst, sldata, ob_ref, &cast_shadow);
    if (do_cryptomatte) {
      EEVEE_cryptomatte_particle_hair_cache_populate(inst, sldata, ob_ref);
    }
  }

  if (ob_visibility & OB_VISIBLE_SELF) {
    if (ob->type == OB_MESH) {
      GOOENGINE_sync_mesh(inst, sldata, ob_ref, &cast_shadow);
      if (do_cryptomatte) {
        EEVEE_cryptomatte_cache_populate(inst, sldata, ob_ref);
      }
    }
    else if (ob->type == OB_CURVES) {
      GOOENGINE_sync_curves(
          inst, sldata, ob_ref, &cast_shadow);
      if (do_cryptomatte) {
        EEVEE_cryptomatte_object_curves_cache_populate(inst, sldata, ob_ref);
      }
    }
    else if (ob->type == OB_VOLUME) {
      Scene *scene = DEG_get_evaluated_scene(inst->depsgraph);
      GOOENGINE_sync_volume(sldata, inst, scene, ob_ref);
    }
    else if (ob->type == OB_LIGHTPROBE) {
      GOOENGINE_sync_probe(sldata, inst, ob);
    }
    else if (ob->type == OB_LAMP) {
      GOOENGINE_sync_light(sldata, ob);
    }
  }

  if (cast_shadow) {
    EEVEE_shadows_caster_register(sldata, ob);
  }
}

static void eevee_render_color_result(RenderLayer *rl,
                                      const char *viewname,
                                      const rcti *rect,
                                      const char *render_pass_name,
                                      int num_channels,
                                      GPUFrameBuffer *framebuffer,
                                      Instance *inst)
{
  RenderPass *rp = RE_pass_find_by_name(rl, render_pass_name, viewname);
  if (rp == nullptr) {
    return;
  }
  GPU_framebuffer_bind(framebuffer);
  GPU_framebuffer_read_color(framebuffer,
                             inst->goo_data->g_data->overscan_pixels + rect->xmin,
                             inst->goo_data->g_data->overscan_pixels + rect->ymin,
                             BLI_rcti_size_x(rect),
                             BLI_rcti_size_y(rect),
                             num_channels,
                             0,
                             GPU_DATA_FLOAT,
                             rp->ibuf->float_buffer.data);
}

static void eevee_render_result_combined(RenderLayer *rl,
                                         const char *viewname,
                                         const rcti *rect,
                                         Instance *inst,
                                         EEVEE_ViewLayerData * /*sldata*/)
{
  eevee_render_color_result(
      rl, viewname, rect, RE_PASSNAME_COMBINED, 4, inst->goo_data->effects->final_fb, inst);
}

static void eevee_render_result_normal(RenderLayer *rl,
                                       const char *viewname,
                                       const rcti *rect,
                                       Instance *inst,
                                       EEVEE_ViewLayerData *sldata)
{
  const int current_sample = inst->goo_data->effects->taa_current_sample;

  /* Only read the center texel. */
  if (current_sample > 1) {
    return;
  }

  if ((inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_NORMAL) != 0) {
    EEVEE_renderpasses_postprocess(sldata, inst, EEVEE_RENDER_PASS_NORMAL, 0);
    eevee_render_color_result(
        rl, viewname, rect, RE_PASSNAME_NORMAL, 3, inst->goo_data->renderpass_fb, inst);
  }
}

static void eevee_render_result_z(RenderLayer *rl,
                                  const char *viewname,
                                  const rcti *rect,
                                  Instance *inst,
                                  EEVEE_ViewLayerData *sldata)
{
  const int current_sample = inst->goo_data->effects->taa_current_sample;

  /* Only read the center texel. */
  if (current_sample > 1) {
    return;
  }

  if ((inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_Z) != 0) {
    EEVEE_renderpasses_postprocess(sldata, inst, EEVEE_RENDER_PASS_Z, 0);
    eevee_render_color_result(
        rl, viewname, rect, RE_PASSNAME_Z, 1, inst->goo_data->renderpass_fb, inst);
  }
}

static void eevee_render_result_mist(RenderLayer *rl,
                                     const char *viewname,
                                     const rcti *rect,
                                     Instance *inst,
                                     EEVEE_ViewLayerData *sldata)
{
  if ((inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_MIST) != 0) {
    EEVEE_renderpasses_postprocess(sldata, inst, EEVEE_RENDER_PASS_MIST, 0);
    eevee_render_color_result(
        rl, viewname, rect, RE_PASSNAME_MIST, 1, inst->goo_data->renderpass_fb, inst);
  }
}

static void eevee_render_result_shadow(RenderLayer *rl,
                                       const char *viewname,
                                       const rcti *rect,
                                       Instance *inst,
                                       EEVEE_ViewLayerData *sldata)
{
  if ((inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_SHADOW) != 0) {
    EEVEE_renderpasses_postprocess(sldata, inst, EEVEE_RENDER_PASS_SHADOW, 0);
    eevee_render_color_result(
        rl, viewname, rect, RE_PASSNAME_SHADOW, 3, inst->goo_data->renderpass_fb, inst);
  }
}

static void eevee_render_result_occlusion(RenderLayer *rl,
                                          const char *viewname,
                                          const rcti *rect,
                                          Instance *inst,
                                          EEVEE_ViewLayerData *sldata)
{
  if ((inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_AO) != 0) {
    EEVEE_renderpasses_postprocess(sldata, inst, EEVEE_RENDER_PASS_AO, 0);
    eevee_render_color_result(
        rl, viewname, rect, RE_PASSNAME_AO, 3, inst->goo_data->renderpass_fb, inst);
  }
}

static void eevee_render_result_bloom(RenderLayer *rl,
                                      const char *viewname,
                                      const rcti *rect,
                                      Instance *inst,
                                      EEVEE_ViewLayerData *sldata)
{
  if ((inst->goo_data->effects->enabled_effects & EFFECT_BLOOM) == 0) {
    /* Bloom is not enabled. */
    return;
  }

  if ((inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_BLOOM) != 0) {
    EEVEE_renderpasses_postprocess(sldata, inst, EEVEE_RENDER_PASS_BLOOM, 0);
    eevee_render_color_result(
        rl, viewname, rect, RE_PASSNAME_BLOOM, 3, inst->goo_data->renderpass_fb, inst);
  }
}

static void eevee_render_result_transparent(RenderLayer *rl,
                                            const char *viewname,
                                            const rcti *rect,
                                            Instance *inst,
                                            EEVEE_ViewLayerData *sldata)
{
  if ((inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_TRANSPARENT) != 0) {
    EEVEE_renderpasses_postprocess(sldata, inst, EEVEE_RENDER_PASS_TRANSPARENT, 0);
    eevee_render_color_result(
        rl, viewname, rect, RE_PASSNAME_TRANSPARENT, 4, inst->goo_data->renderpass_fb, inst);
  }
}

#define EEVEE_RENDER_RESULT_MATERIAL_PASS(pass_name, eevee_pass_type) \
  if ((inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_##eevee_pass_type) != 0) { \
    EEVEE_renderpasses_postprocess(sldata, inst, EEVEE_RENDER_PASS_##eevee_pass_type, 0); \
    eevee_render_color_result( \
        rl, viewname, rect, RE_PASSNAME_##pass_name, 3, inst->goo_data->renderpass_fb, inst); \
  }

static void eevee_render_result_diffuse_color(RenderLayer *rl,
                                              const char *viewname,
                                              const rcti *rect,
                                              Instance *inst,
                                              EEVEE_ViewLayerData *sldata)
{
  EEVEE_RENDER_RESULT_MATERIAL_PASS(DIFFUSE_COLOR, DIFFUSE_COLOR)
}

static void eevee_render_result_diffuse_direct(RenderLayer *rl,
                                               const char *viewname,
                                               const rcti *rect,
                                               Instance *inst,
                                               EEVEE_ViewLayerData *sldata)
{
  EEVEE_RENDER_RESULT_MATERIAL_PASS(DIFFUSE_DIRECT, DIFFUSE_LIGHT)
}

static void eevee_render_result_specular_color(RenderLayer *rl,
                                               const char *viewname,
                                               const rcti *rect,
                                               Instance *inst,
                                               EEVEE_ViewLayerData *sldata)
{
  EEVEE_RENDER_RESULT_MATERIAL_PASS(GLOSSY_COLOR, SPECULAR_COLOR)
}

static void eevee_render_result_specular_direct(RenderLayer *rl,
                                                const char *viewname,
                                                const rcti *rect,
                                                Instance *inst,
                                                EEVEE_ViewLayerData *sldata)
{
  EEVEE_RENDER_RESULT_MATERIAL_PASS(GLOSSY_DIRECT, SPECULAR_LIGHT)
}

static void eevee_render_result_emission(RenderLayer *rl,
                                         const char *viewname,
                                         const rcti *rect,
                                         Instance *inst,
                                         EEVEE_ViewLayerData *sldata)
{
  EEVEE_RENDER_RESULT_MATERIAL_PASS(EMIT, EMIT)
}

static void eevee_render_result_environment(RenderLayer *rl,
                                            const char *viewname,
                                            const rcti *rect,
                                            Instance *inst,
                                            EEVEE_ViewLayerData *sldata)
{
  EEVEE_RENDER_RESULT_MATERIAL_PASS(ENVIRONMENT, ENVIRONMENT)
}

static void eevee_render_result_volume_light(RenderLayer *rl,
                                             const char *viewname,
                                             const rcti *rect,
                                             Instance *inst,
                                             EEVEE_ViewLayerData *sldata)
{
  EEVEE_RENDER_RESULT_MATERIAL_PASS(VOLUME_LIGHT, VOLUME_LIGHT)
}

static void eevee_render_result_aovs(RenderLayer *rl,
                                     const char *viewname,
                                     const rcti *rect,
                                     Instance *inst,
                                     EEVEE_ViewLayerData *sldata)
{
  if ((inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_AOV) != 0) {
    const DRWContext *draw_ctx = DRW_context_get();
    ViewLayer *view_layer = draw_ctx->view_layer;
    int aov_index = 0;
    LISTBASE_FOREACH (ViewLayerAOV *, aov, &view_layer->aovs) {
      if ((aov->flag & AOV_CONFLICT) != 0) {
        continue;
      }
      EEVEE_renderpasses_postprocess(sldata, inst, EEVEE_RENDER_PASS_AOV, aov_index);
      switch (aov->type) {
        case AOV_TYPE_COLOR:
          eevee_render_color_result(
              rl, viewname, rect, aov->name, 4, inst->goo_data->renderpass_fb, inst);
          break;
        case AOV_TYPE_VALUE:
          eevee_render_color_result(
              rl, viewname, rect, aov->name, 1, inst->goo_data->renderpass_fb, inst);
      }
      aov_index++;
    }
  }
}

#undef EEVEE_RENDER_RESULT_MATERIAL_PASS

static void eevee_render_result_cryptomatte(RenderLayer *rl,
                                            const char *viewname,
                                            const rcti *rect,
                                            Instance *inst,
                                            EEVEE_ViewLayerData *sldata)
{
  if ((inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_CRYPTOMATTE) != 0) {
    EEVEE_cryptomatte_render_result(rl, viewname, rect, inst, sldata);
  }
}

static void eevee_render_draw_background(Instance *inst)
{

  /* Prevent background to write to data buffers.
   * NOTE: This also make sure the textures are bound to the right double buffer. */
  GPU_framebuffer_ensure_config(&inst->goo_data->main_fb,
                                {GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_NONE,
                                 GPU_ATTACHMENT_NONE,
                                 GPU_ATTACHMENT_NONE,
                                 GPU_ATTACHMENT_NONE});
  GPU_framebuffer_bind(inst->goo_data->main_fb);

  DRW_draw_pass(inst->goo_data->background_ps);

  GPU_framebuffer_ensure_config(&inst->goo_data->main_fb,
                                {GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_TEXTURE(inst->goo_data->effects->ssr_normal_input),
                                 GPU_ATTACHMENT_TEXTURE(inst->goo_data->effects->ssr_specrough_input),
                                 GPU_ATTACHMENT_TEXTURE(inst->goo_data->effects->sss_irradiance),
                                 GPU_ATTACHMENT_TEXTURE(inst->goo_data->effects->sss_radius),
                                 GPU_ATTACHMENT_TEXTURE(inst->goo_data->effects->sss_albedo)});
  GPU_framebuffer_bind(inst->goo_data->main_fb);
}

void EEVEE_render_draw(Instance *inst, RenderEngine *engine, RenderLayer *rl, const rcti *rect)
{
  using namespace blender::draw;
  const char *viewname = RE_GetActiveRenderView(engine->re);
  const DRWContext *draw_ctx = DRW_context_get();
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
  EEVEE_ViewLayerData *sldata = EEVEE_view_layer_data_ensure();

  /* Push instances attributes to the GPU. */
  DRW_render_instance_buffer_finish();

  /* Need to be called after DRW_render_instance_buffer_finish() */
  /* Also we weed to have a correct FBO bound for DRW_curves_update */
  GPU_framebuffer_bind(inst->goo_data->main_fb);
  DRW_curves_update(*DRW_manager_get());

  /* Sort transparents before the loop. */
  DRW_pass_sort_shgroup_z(inst->goo_data->transparent_pass);

  uint tot_sample = inst->goo_data->g_data->render_sample_count_per_timestep;
  uint render_samples = 0;

  /* SSR needs one iteration to start properly. */
  if ((inst->goo_data->effects->enabled_effects & EFFECT_SSR) && !inst->goo_data->effects->ssr_was_valid_double_buffer) {
    tot_sample += 1;
  }

  while (render_samples < tot_sample && !RE_engine_test_break(engine)) {
    const float clear_col[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float clear_depth = 1.0f;
    uint clear_stencil = 0x00;
    const uint primes[3] = {2, 3, 7};
    double offset[3] = {0.0, 0.0, 0.0};
    double r[3];

    if ((inst->goo_data->effects->enabled_effects & EFFECT_SSR) && (render_samples == 1) &&
        !inst->goo_data->effects->ssr_was_valid_double_buffer)
    {
      /* SSR needs one iteration to start properly.
       * This iteration was done, reset to the original target sample count. */
      render_samples--;
      tot_sample--;
      /* Reset sampling (and accumulation) after the first sample to avoid
       * washed out first bounce for SSR. */
      printf(" reset 1 \n");
      EEVEE_temporal_sampling_reset(inst);
      inst->goo_data->effects->ssr_was_valid_double_buffer = inst->goo_data->g_data->valid_double_buffer;
    }
    /* Don't print every samples as it can lead to bad performance. (see #59649) */
    else if ((render_samples % 25) == 0 || (render_samples + 1) == tot_sample) {
      char info[42];
      SNPRINTF(info, "Rendering %u / %u samples", render_samples + 1, tot_sample);
      RE_engine_update_stats(engine, nullptr, info);
    }

    /* Copy previous persmat to UBO data */
    copy_m4_m4(sldata->common_data.prev_persmat, inst->goo_data->effects->prev_persmat);

    BLI_halton_3d(primes, offset, inst->goo_data->effects->taa_current_sample, r);
    EEVEE_update_noise(inst, r);
    EEVEE_temporal_sampling_matrices_calc(inst->goo_data->effects, r);
    EEVEE_volumes_set_jitter(sldata, inst->goo_data->effects->taa_current_sample - 1);
    EEVEE_materials_init(sldata, inst);

    /* Refresh Probes
     * Shadows needs to be updated for correct probes */
    EEVEE_shadows_update(sldata, inst);
    EEVEE_lightprobes_refresh(sldata, inst);
    EEVEE_lightprobes_refresh_planar(sldata, inst);

    /* Refresh Shadows */
    EEVEE_shadows_draw(sldata, inst, inst->goo_data->effects->taa_view);

    /* Set matrices. */
    DRW_view_set_active(inst->goo_data->effects->taa_view);

    /* Set ray type. */
    sldata->common_data.ray_type = EEVEE_RAY_CAMERA;
    sldata->common_data.ray_depth = 0.0f;
    GPU_uniformbuf_update(sldata->common_ubo, &sldata->common_data);

    GPU_framebuffer_bind(inst->goo_data->main_fb);
    GPU_framebuffer_clear_color_depth_stencil(inst->goo_data->main_fb, clear_col, clear_depth, clear_stencil);
    /* Depth pre-pass. */
    DRW_draw_pass(inst->goo_data->depth_ps);
    /* Create minmax texture */
    EEVEE_create_minmax_buffer(inst, dtxl->depth, -1);
    EEVEE_occlusion_compute(sldata, inst);
    EEVEE_volumes_compute(sldata, inst);
    /* Shading pass */
    eevee_render_draw_background(inst);
    GPU_framebuffer_bind(inst->goo_data->main_fb);
    DRW_draw_pass(inst->goo_data->material_ps);
    EEVEE_subsurface_data_render(sldata, inst);
    /* Effects pre-transparency */
    EEVEE_subsurface_compute(sldata, inst);
    EEVEE_reflection_compute(sldata, inst);
    EEVEE_refraction_compute(sldata, inst);
    /* Opaque refraction */
    DRW_draw_pass(inst->goo_data->depth_refract_ps);
    DRW_draw_pass(inst->goo_data->material_refract_ps);
    /* Result NORMAL */
    eevee_render_result_normal(rl, viewname, rect, inst, sldata);
    /* Volumetrics Resolve Opaque */
    EEVEE_volumes_resolve(sldata, inst);
    /* Subsurface output, Occlusion output, Mist output */
    EEVEE_renderpasses_output_accumulate(sldata, inst, false);
    /* Transparent */
    EEVEE_material_transparent_output_accumulate(inst);
    GPU_framebuffer_texture_attach(inst->goo_data->main_color_fb, dtxl->depth, 0, 0);
    GPU_framebuffer_bind(inst->goo_data->main_color_fb);
    DRW_draw_pass(inst->goo_data->transparent_pass);
    GPU_framebuffer_bind(inst->goo_data->main_fb);
    GPU_framebuffer_texture_detach(inst->goo_data->main_color_fb, dtxl->depth);
    /* Result Z */
    eevee_render_result_z(rl, viewname, rect, inst, sldata);
    /* Post Process */
    EEVEE_draw_effects(sldata, inst);

    /* NOTE(@fclem): Seems to fix TDR issue with NVidia drivers. */
    if (GPU_type_matches_ex(GPU_DEVICE_NVIDIA, GPU_OS_ANY, GPU_DRIVER_ANY, GPU_BACKEND_OPENGL)) {
      GPU_finish();
    }

    /* Perform render step between samples to allow
     * flushing of freed GPUBackend resources. */
    GPU_render_step();
    if (GPU_type_matches_ex(GPU_DEVICE_ANY, GPU_OS_ANY, GPU_DRIVER_ANY, GPU_BACKEND_METAL)) {
      if (render_samples > 0 && ((render_samples % 64) == 0)) {
        /* Allow GPU to sync with CPU to prevent overly large command submissions being in-flight
         * simultaneously. Reduces total in-flight memory required for rendering. */
        GPU_finish();
      }
      else {
        GPU_flush();
      }
    }

    RE_engine_update_progress(engine, float(render_samples++) / float(tot_sample));
  }
}

void EEVEE_render_read_result(Instance *inst,
                              RenderEngine *engine,
                              RenderLayer *rl,
                              const rcti *rect)
{
  const char *viewname = RE_GetActiveRenderView(engine->re);
  EEVEE_ViewLayerData *sldata = EEVEE_view_layer_data_ensure();

  eevee_render_result_combined(rl, viewname, rect, inst, sldata);
  eevee_render_result_mist(rl, viewname, rect, inst, sldata);
  eevee_render_result_occlusion(rl, viewname, rect, inst, sldata);
  eevee_render_result_shadow(rl, viewname, rect, inst, sldata);
  eevee_render_result_diffuse_color(rl, viewname, rect, inst, sldata);
  eevee_render_result_diffuse_direct(rl, viewname, rect, inst, sldata);
  eevee_render_result_specular_color(rl, viewname, rect, inst, sldata);
  eevee_render_result_specular_direct(rl, viewname, rect, inst, sldata);
  eevee_render_result_emission(rl, viewname, rect, inst, sldata);
  eevee_render_result_environment(rl, viewname, rect, inst, sldata);
  eevee_render_result_bloom(rl, viewname, rect, inst, sldata);
  eevee_render_result_volume_light(rl, viewname, rect, inst, sldata);
  eevee_render_result_transparent(rl, viewname, rect, inst, sldata);
  eevee_render_result_aovs(rl, viewname, rect, inst, sldata);
  eevee_render_result_cryptomatte(rl, viewname, rect, inst, sldata);
}

}  // namespace blender::gooengine
