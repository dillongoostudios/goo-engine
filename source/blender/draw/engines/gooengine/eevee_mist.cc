/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Implementation of Blender Mist pass.
 * IMPORTANT: This is a "post process" of the Z depth so it will lack any transparent objects.
 */

#include "DRW_engine.hh"
#include "DRW_render.hh"
#include "draw_handle.hh"

#include "DNA_world_types.h"

#include "BLI_string_utils.hh"

#include "gooengine_private.hh"

namespace blender::gooengine {

void EEVEE_mist_output_init(EEVEE_ViewLayerData *sldata, Instance *inst)
{
  const DRWContext *draw_ctx = DRW_context_get();
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
  EEVEE_PrivateData *g_data = inst->goo_data->g_data;
  Scene *scene = draw_ctx->scene;

  /* Create FrameBuffer. */
  /* Should be enough precision for many samples. */
  DRW_texture_ensure_fullscreen_2d(&inst->goo_data->mist_accum, GPU_R32F, DRWTextureFlag(0));

  GPU_framebuffer_ensure_config(&inst->goo_data->mist_accum_fb,
                                {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(inst->goo_data->mist_accum)});

  /* Mist settings. */
  if (scene && scene->world) {
    g_data->mist_data[0] = scene->world->miststa;
    g_data->mist_data[1] = (scene->world->mistdist > 0.0f) ? 1.0f / scene->world->mistdist : 0.0f;

    switch (scene->world->mistype) {
      case WO_MIST_QUADRATIC:
        g_data->mist_data[2] = 2.0f;
        break;
      case WO_MIST_LINEAR:
        g_data->mist_data[2] = 1.0f;
        break;
      case WO_MIST_INVERSE_QUADRATIC:
        g_data->mist_data[2] = 0.5f;
        break;
    }
  }
  else {
    float near = DRW_view_near_distance_get(nullptr);
    float far = DRW_view_far_distance_get(nullptr);
    /* Fallback */
    g_data->mist_data[0] = near;
    g_data->mist_data[1] = 1.0f / fabsf(far - near);
    g_data->mist_data[2] = 1.0f;
  }

  /* XXX ??!! WHY? If not it does not match cycles. */
  g_data->mist_data[2] *= 0.5f;

  /* Create Pass and shgroup. */
  blender::draw::PassSimple &mist_accum_ps_ = inst->goo_data->mist_accum_ps;
  mist_accum_ps_.init();
  mist_accum_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ADD);
  mist_accum_ps_.shader_set(inst->shaders.static_shader_get(EFFECT_MIST));
  mist_accum_ps_.bind_texture("depthBuffer", &dtxl->depth);
  mist_accum_ps_.bind_ubo("common_block", sldata->common_ubo);
  mist_accum_ps_.bind_ubo("renderpass_block", sldata->renderpass_ubo.combined);
  mist_accum_ps_.push_constant("mistSettings", &g_data->mist_data);
  mist_accum_ps_.draw(DRW_cache_fullscreen_quad_get());
}

void EEVEE_mist_output_accumulate(EEVEE_ViewLayerData * /*sldata*/, Instance *inst)
{
  blender::draw::Manager *manager = DRW_manager_get();
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  if (inst->goo_data->mist_accum_fb != nullptr) {
    GPU_framebuffer_bind(inst->goo_data->mist_accum_fb);

    /* Clear texture. */
    if (effects->taa_current_sample == 1) {
      const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      GPU_framebuffer_clear_color(inst->goo_data->mist_accum_fb, clear);
    }

    manager->submit(inst->goo_data->mist_accum_ps);

    /* Restore */
    GPU_framebuffer_bind(inst->goo_data->main_fb);
  }
}

}  // namespace blender::gooengine
