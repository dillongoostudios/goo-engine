/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Gather all screen space effects technique such as Bloom, Motion Blur, DoF, SSAO, SSR, ...
 */

#include "DRW_render.hh"
#include "draw_handle.hh"

#include "BKE_global.hh" /* for G.debug_value */

#include "GPU_capabilities.hh"
#include "GPU_platform.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"
#include "gooengine_private.hh"

namespace blender::gooengine {

static struct {
  /* These are just references, not actually allocated */
  GPUTexture *depth_src;
  GPUTexture *color_src;

  int depth_src_layer;
  /* Size can be vec3. But we only use 2 components in the shader. */
  float2 texel_size;
} e_data = {nullptr}; /* Engine data */

#define SETUP_BUFFER(tex, fb, fb_color) \
  { \
    eGPUTextureFormat format = draw_ctx->is_scene_render() ? GPU_RGBA32F : GPU_RGBA16F; \
    DRW_texture_ensure_fullscreen_2d(&tex, format, DRW_TEX_FILTER); \
    GPU_framebuffer_ensure_config(&fb, \
                                  { \
                                      GPU_ATTACHMENT_TEXTURE(dtxl->depth), \
                                      GPU_ATTACHMENT_TEXTURE(tex), \
                                  }); \
    GPU_framebuffer_ensure_config(&fb_color, \
                                  { \
                                      GPU_ATTACHMENT_NONE, \
                                      GPU_ATTACHMENT_TEXTURE(tex), \
                                  }); \
  } \
  ((void)0)

#define CLEANUP_BUFFER(tex, fb, fb_color) \
  { \
    /* Cleanup to release memory */ \
    DRW_TEXTURE_FREE_SAFE(tex); \
    GPU_FRAMEBUFFER_FREE_SAFE(fb); \
    GPU_FRAMEBUFFER_FREE_SAFE(fb_color); \
  } \
  ((void)0)

void EEVEE_effects_init(EEVEE_ViewLayerData *sldata,
                        Instance *inst,
                        Object *camera,
                        const bool minimal)
{
  EEVEE_CommonUniformBuffer *common_data = &sldata->common_data;
  EEVEE_EffectsInfo *effects;
  const DRWContext *draw_ctx = DRW_context_get();
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();

  const float2 viewport_size = draw_ctx->viewport_size_get();
  const int size_fs[2] = {int(viewport_size[0]), int(viewport_size[1])};
  if (!inst->goo_data->effects) {
    inst->goo_data->effects = static_cast<EEVEE_EffectsInfo *>(
        MEM_callocN(sizeof(EEVEE_EffectsInfo), "EEVEE_EffectsInfo"));
    inst->goo_data->effects->taa_render_sample = 1;
  }

  /* WORKAROUND: EEVEE_lookdev_init can reset TAA and needs a inst->goo_data->effect.
   * So putting this before EEVEE_temporal_sampling_init for now. */
  EEVEE_lookdev_init(inst);
  
  effects = inst->goo_data->effects;
  
  int div = 1 << MAX_SCREEN_BUFFERS_LOD_LEVEL;
  effects->hiz_size[0] = divide_ceil_u(size_fs[0], div) * div;
  effects->hiz_size[1] = divide_ceil_u(size_fs[1], div) * div;
  
  effects->enabled_effects = EEVEE_EffectsFlag(0);
  effects->enabled_effects |= (G.debug_value == 9) ? EFFECT_VELOCITY_BUFFER : EEVEE_EffectsFlag(0);
  effects->enabled_effects |= EEVEE_EffectsFlag(EEVEE_motion_blur_init(sldata, inst));
  effects->enabled_effects |= EEVEE_EffectsFlag(EEVEE_bloom_init(sldata, inst));
  effects->enabled_effects |= EEVEE_EffectsFlag(EEVEE_depth_of_field_init(sldata, inst, camera));
  effects->enabled_effects |= EEVEE_EffectsFlag(EEVEE_temporal_sampling_init(sldata, inst));
  effects->enabled_effects |= EEVEE_EffectsFlag(EEVEE_occlusion_init(sldata, inst));
  effects->enabled_effects |= EEVEE_EffectsFlag(EEVEE_screen_raytrace_init(sldata, inst));

  /* Update matrices here because EEVEE_screen_raytrace_init can have reset the
   * taa_current_sample. (See #66811) */
  EEVEE_temporal_sampling_update_matrices(inst);

  EEVEE_volumes_init(sldata, inst);
  EEVEE_subsurface_init(sldata, inst);

  /* Force normal buffer creation. */
  if (!minimal && (inst->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_NORMAL) != 0) {
    effects->enabled_effects |= EFFECT_NORMAL_BUFFER;
  }

  /**
   * MinMax Pyramid
   */

  if (GPU_type_matches_ex(GPU_DEVICE_INTEL, GPU_OS_ANY, GPU_DRIVER_ANY, GPU_BACKEND_OPENGL)) {
    /* Intel gpu seems to have problem rendering to only depth hiz_format */
    DRW_texture_ensure_2d(&inst->goo_data->maxzbuffer, UNPACK2(effects->hiz_size), GPU_R32F, DRW_TEX_MIPMAP);
    GPU_framebuffer_ensure_config(&inst->goo_data->maxzbuffer_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(inst->goo_data->maxzbuffer),
                                  });
  }
  else {
    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_ATTACHMENT | GPU_TEXTURE_USAGE_SHADER_READ;
    DRW_texture_ensure_2d_ex(&inst->goo_data->maxzbuffer,
                             UNPACK2(effects->hiz_size),
                             GPU_DEPTH_COMPONENT24,
                             usage,
                             DRW_TEX_MIPMAP);
    GPU_framebuffer_ensure_config(&inst->goo_data->maxzbuffer_fb,
                                  {
                                      GPU_ATTACHMENT_TEXTURE(inst->goo_data->maxzbuffer),
                                      GPU_ATTACHMENT_NONE,
                                  });
  }

  if (inst->goo_data->downsample_fb == nullptr) {
    inst->goo_data->downsample_fb = GPU_framebuffer_create("downsample_fb");
  }

  /**
   * Compute hiZ texel alignment.
   */
  common_data->hiz_uv_scale[0] = viewport_size[0] / effects->hiz_size[0];
  common_data->hiz_uv_scale[1] = viewport_size[1] / effects->hiz_size[1];

  /* Compute pixel size. Size is multiplied by 2 because it is applied in NDC [-1..1] range. */
  sldata->common_data.ssr_pixelsize[0] = 2.0f / size_fs[0];
  sldata->common_data.ssr_pixelsize[1] = 2.0f / size_fs[1];

  /**
   * Color buffer with correct down-sampling alignment.
   * Used for SSReflections & SSRefractions.
   */
  if ((effects->enabled_effects & EFFECT_RADIANCE_BUFFER) != 0) {
    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_ATTACHMENT | GPU_TEXTURE_USAGE_SHADER_READ;
    DRW_texture_ensure_2d_ex(&inst->goo_data->filtered_radiance,
                             UNPACK2(effects->hiz_size),
                             GPU_R11F_G11F_B10F,
                             usage,
                             DRWTextureFlag(DRW_TEX_FILTER | DRW_TEX_MIPMAP));

    GPU_framebuffer_ensure_config(&inst->goo_data->radiance_filtered_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(inst->goo_data->filtered_radiance),
                                  });
  }
  else {
    DRW_TEXTURE_FREE_SAFE(inst->goo_data->filtered_radiance);
    GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->radiance_filtered_fb);
  }

  /**
   * Normal buffer for deferred passes.
   */
  if ((effects->enabled_effects & EFFECT_NORMAL_BUFFER) != 0) {
    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
    effects->ssr_normal_input = DRW_texture_pool_query_2d_ex(
        size_fs[0], size_fs[1], GPU_RG16, usage, 0);

    GPU_framebuffer_texture_attach(inst->goo_data->main_fb, effects->ssr_normal_input, 1, 0);
  }
  else {
    effects->ssr_normal_input = nullptr;
  }

  /**
   * Motion vector buffer for correct TAA / motion blur.
   */
  if ((effects->enabled_effects & EFFECT_VELOCITY_BUFFER) != 0) {
    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
    effects->velocity_tx = DRW_texture_pool_query_2d_ex(
        size_fs[0], size_fs[1], GPU_RGBA16, usage, 0);

    GPU_framebuffer_ensure_config(&inst->goo_data->velocity_fb,
                                  {
                                      GPU_ATTACHMENT_TEXTURE(dtxl->depth),
                                      GPU_ATTACHMENT_TEXTURE(effects->velocity_tx),
                                  });

    GPU_framebuffer_ensure_config(
        &inst->goo_data->velocity_resolve_fb,
        {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(effects->velocity_tx)});
  }
  else {
    effects->velocity_tx = nullptr;
  }

  /**
   * Setup depth double buffer.
   */
  if ((effects->enabled_effects & EFFECT_DEPTH_DOUBLE_BUFFER) != 0) {
    DRW_texture_ensure_fullscreen_2d(
        &inst->goo_data->depth_double_buffer, GPU_DEPTH24_STENCIL8, DRWTextureFlag(0));

    GPU_framebuffer_ensure_config(&inst->goo_data->double_buffer_depth_fb,
                                  {GPU_ATTACHMENT_TEXTURE(inst->goo_data->depth_double_buffer)});
  }
  else {
    /* Cleanup to release memory */
    DRW_TEXTURE_FREE_SAFE(inst->goo_data->depth_double_buffer);
    GPU_FRAMEBUFFER_FREE_SAFE(inst->goo_data->double_buffer_depth_fb);
  }

  if ((effects->enabled_effects & (EFFECT_TAA | EFFECT_TAA_REPROJECT)) != 0) {
    SETUP_BUFFER(inst->goo_data->taa_history, inst->goo_data->taa_history_fb, inst->goo_data->taa_history_color_fb);
  }
  else {
    CLEANUP_BUFFER(inst->goo_data->taa_history, inst->goo_data->taa_history_fb, inst->goo_data->taa_history_color_fb);
  }
}

void EEVEE_effects_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst)
{
  blender::draw::Manager *manager = DRW_manager_get();
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  DRWState downsample_write = DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_ALWAYS;
  DRWShadingGroup *grp;

  /* Intel gpu seems to have problem rendering to only depth format.
   * Use color texture instead. */
  if (GPU_type_matches_ex(GPU_DEVICE_INTEL, GPU_OS_ANY, GPU_DRIVER_ANY, GPU_BACKEND_OPENGL)) {
    downsample_write = DRW_STATE_WRITE_COLOR;
  }

  blender::gpu::Batch *quad = DRW_cache_fullscreen_quad_get();

  if (effects->enabled_effects & EFFECT_RADIANCE_BUFFER) {
    {
      blender::draw::PassSimple &pass = inst->goo_data->color_copy_ps;
      pass.init();
      pass.state_set(DRW_STATE_WRITE_COLOR);
      pass.shader_set(inst->shaders.static_shader_get(EFFECT_COLOR_COPY));
      pass.bind_texture("source", &e_data.color_src, GPUSamplerState::default_sampler());
      pass.push_constant("fireflyFactor", &sldata->common_data.ssr_firefly_fac);
      pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);
    }

    {
      const GPUSamplerState sampler_state = {GPU_SAMPLER_FILTERING_LINEAR};
      blender::draw::PassSimple &pass = inst->goo_data->color_downsample_ps;
      pass.init();
      pass.state_set(DRW_STATE_WRITE_COLOR);
      pass.shader_set(inst->shaders.static_shader_get(EFFECT_DOWNSAMPLE));
      pass.bind_texture("source", inst->goo_data->filtered_radiance, sampler_state);
      pass.push_constant("texelSize", &e_data.texel_size);
      pass.draw_procedural(GPU_PRIM_TRIS, 1, 3);
    }
  }

  {
    blender::draw::PassSimple &pass = inst->goo_data->color_downsample_cube_ps;
    pass.init();
    pass.state_set(DRW_STATE_WRITE_COLOR);
    pass.shader_set(inst->shaders.static_shader_get(EFFECT_DOWNSAMPLE_CUBE));
    pass.bind_texture("source", &e_data.color_src);
    pass.push_constant("texelSize", &e_data.texel_size);
    pass.push_constant("Layer", 0);
    pass.draw(quad, 6);
  }

  /* Perform min/max down-sample. */
  {
    blender::draw::PassSimple &pass = inst->goo_data->maxz_downlevel_ps;
    pass.init();
    pass.state_set(downsample_write);
    pass.shader_set(inst->shaders.static_shader_get(EFFECT_MAXZ_DOWNLEVEL));
    pass.bind_texture("depthBuffer", &inst->goo_data->maxzbuffer, GPUSamplerState::default_sampler());
    pass.push_constant("texelSize", &e_data.texel_size);
    pass.draw(quad);
  }

  {
    /* Copy depth buffer to top level of HiZ */
    blender::draw::PassSimple &pass = inst->goo_data->maxz_copydepth_ps;
    pass.init();
    pass.state_set(downsample_write);
    pass.shader_set(inst->shaders.static_shader_get(EFFECT_MAXZ_COPYDEPTH));
    pass.bind_texture("depthBuffer", &e_data.depth_src, GPUSamplerState::default_sampler());
    pass.draw(quad);
  }

  {
    blender::draw::PassSimple &pass = inst->goo_data->maxz_copydepth_layer_ps;
    pass.init();
    pass.state_set(downsample_write);
    pass.shader_set(inst->shaders.static_shader_get(EFFECT_MAXZ_COPYDEPTH_LAYER));
    pass.bind_texture("depthBuffer", &e_data.depth_src, GPUSamplerState::default_sampler());
    pass.push_constant("depthLayer", &e_data.depth_src_layer);
    pass.draw(quad);
  }

  if ((effects->enabled_effects & EFFECT_VELOCITY_BUFFER) != 0) {
    EEVEE_MotionBlurData *mb_data = &effects->motion_blur;

    /* This pass compute camera motions to the non moving objects. */
    DRW_PASS_CREATE(inst->goo_data->velocity_resolve, DRW_STATE_WRITE_COLOR);
    grp = DRW_shgroup_create(inst->shaders.static_shader_get(VELOCITY_RESOLVE), inst->goo_data->velocity_resolve);
    DRW_shgroup_uniform_texture_ref(grp, "depthBuffer", &e_data.depth_src);
    DRW_shgroup_uniform_block(grp, "common_block", sldata->common_ubo);
    DRW_shgroup_uniform_block(grp, "renderpass_block", sldata->renderpass_ubo.combined);

    DRW_shgroup_uniform_mat4(grp, "prevViewProjMatrix", mb_data->camera[MB_PREV].persmat);
    DRW_shgroup_uniform_mat4(grp, "currViewProjMatrixInv", mb_data->camera[MB_CURR].persinv);
    DRW_shgroup_uniform_mat4(grp, "nextViewProjMatrix", mb_data->camera[MB_NEXT].persmat);
    DRW_shgroup_call(grp, quad, nullptr);
  }
}

void EEVEE_effects_draw_init(EEVEE_ViewLayerData * /*sldata*/, Instance *inst)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  const DRWContext *draw_ctx = DRW_context_get();
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
  /**
   * Setup double buffer so we can access last frame as it was before post processes.
   */
  if ((effects->enabled_effects & EFFECT_DOUBLE_BUFFER) != 0) {
    SETUP_BUFFER(inst->goo_data->color_double_buffer, inst->goo_data->double_buffer_fb, inst->goo_data->double_buffer_color_fb);
  }
  else {
    CLEANUP_BUFFER(inst->goo_data->color_double_buffer, inst->goo_data->double_buffer_fb, inst->goo_data->double_buffer_color_fb);
  }

  /**
   * Ping Pong buffer
   */
  if ((effects->enabled_effects & EFFECT_POST_BUFFER) != 0) {
    SETUP_BUFFER(inst->goo_data->color_post, inst->goo_data->effect_fb, inst->goo_data->effect_color_fb);
  }
  else {
    CLEANUP_BUFFER(inst->goo_data->color_post, inst->goo_data->effect_fb, inst->goo_data->effect_color_fb);
  }
}

#if 0 /* Not required for now */
static void min_downsample_cb(Instance *inst, int /*level*/)
{
  DRW_draw_pass(inst->minz_downlevel_ps);
}
#endif

auto max_downsample_cb(void *instance, int level)
{
  blender::draw::Manager *manager = DRW_manager_get();
  Instance *inst = static_cast<Instance *>(instance);
  
  int texture_size[3];
  GPU_texture_get_mipmap_size(inst->goo_data->maxzbuffer, level - 1, texture_size);
  e_data.texel_size[0] = 1.0f / texture_size[0];
  e_data.texel_size[1] = 1.0f / texture_size[1];
  manager->submit(inst->goo_data->maxz_downlevel_ps);
}

auto simple_downsample_cube_cb(void *instance, int level)
{
  Instance *inst = static_cast<Instance *>(instance);
  blender::draw::Manager *manager = DRW_manager_get();

  e_data.texel_size[0] = float(1 << level) / float(GPU_texture_width(e_data.color_src));
  e_data.texel_size[1] = e_data.texel_size[0];
  manager->submit(inst->goo_data->color_downsample_cube_ps);
}

void EEVEE_create_minmax_buffer(Instance *inst, GPUTexture *depth_src, int layer)
{
  blender::draw::Manager *manager = DRW_manager_get();

  e_data.depth_src = depth_src;
  e_data.depth_src_layer = layer;

  /* Copy depth buffer to max texture top level */
  GPU_framebuffer_bind(inst->goo_data->maxzbuffer_fb);
  if (layer >= 0) {
    manager->submit(inst->goo_data->maxz_copydepth_layer_ps);
  }
  else {
    manager->submit(inst->goo_data->maxz_copydepth_ps);
  }
  /* Create lower levels */
  GPU_framebuffer_recursive_downsample(
      inst->goo_data->maxzbuffer_fb, MAX_SCREEN_BUFFERS_LOD_LEVEL, max_downsample_cb, inst);

  /* Restore */
  GPU_framebuffer_bind(inst->goo_data->main_fb);

  if (GPU_mip_render_workaround() ||
      GPU_type_matches(GPU_DEVICE_INTEL_UHD, GPU_OS_WIN, GPU_DRIVER_ANY))
  {
    /* Fix dot corruption on intel HD5XX/HD6XX series. */
    GPU_flush();
  }
}

auto downsample_radiance_cb(void *instance, int level)
{
  Instance *inst = static_cast<Instance *>(instance);
  blender::draw::Manager *manager = DRW_manager_get();

  int texture_size[3];
  GPU_texture_get_mipmap_size(inst->goo_data->filtered_radiance, level - 1, texture_size);
  
  e_data.texel_size[0] = 1.0f / texture_size[0];
  e_data.texel_size[1] = 1.0f / texture_size[1];
  manager->submit(inst->goo_data->color_downsample_ps);
}

void EEVEE_effects_downsample_radiance_buffer(Instance *inst, GPUTexture *texture_src)
{
  blender::draw::Manager *manager = DRW_manager_get();

  e_data.color_src = texture_src;

  GPU_framebuffer_bind(inst->goo_data->radiance_filtered_fb);
  manager->submit(inst->goo_data->color_copy_ps);

  GPU_framebuffer_recursive_downsample(
      inst->goo_data->radiance_filtered_fb, MAX_SCREEN_BUFFERS_LOD_LEVEL, downsample_radiance_cb, inst);
}

void EEVEE_effects_radiance_copy(EEVEE_ViewLayerData */*sldata*/, Instance *inst)
{
  blender::draw::Manager *manager = DRW_manager_get();
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  /* Copy color buffer to texture */
  if ((effects->enabled_effects & EFFECT_REFRACT) != 0) {
    GPU_framebuffer_bind(inst->goo_data->radiance_filtered_fb);
    manager->submit(inst->goo_data->color_copy_ps);

    /* Restore */
    GPU_framebuffer_bind(inst->goo_data->main_fb);
  }
}

void EEVEE_downsample_cube_buffer(Instance *inst, GPUTexture *texture_src, int level)
{
  e_data.color_src = texture_src;

  /* Create lower levels */
  GPU_framebuffer_texture_attach(inst->goo_data->downsample_fb, texture_src, 0, 0);
  GPU_framebuffer_recursive_downsample(
      inst->goo_data->downsample_fb, level, simple_downsample_cube_cb, inst);
  GPU_framebuffer_texture_detach(inst->goo_data->downsample_fb, texture_src);
}

static void EEVEE_velocity_resolve(Instance *inst)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;

  if ((effects->enabled_effects & EFFECT_VELOCITY_BUFFER) != 0) {
    const DRWContext *draw_ctx = DRW_context_get();
    DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
    e_data.depth_src = dtxl->depth;

    GPU_framebuffer_bind(inst->goo_data->velocity_resolve_fb);
    DRW_draw_pass(inst->goo_data->velocity_resolve);

    if (inst->goo_data->velocity_object) {
      GPU_framebuffer_bind(inst->goo_data->velocity_fb);
      DRW_draw_pass(inst->goo_data->velocity_object);
    }
  }
}

void EEVEE_draw_effects(EEVEE_ViewLayerData *sldata, Instance *inst)
{
  EEVEE_EffectsInfo *effects = inst->goo_data->effects;
  const DRWContext *draw_ctx = DRW_context_get();

  /* only once per frame after the first post process */
  effects->swap_double_buffer = ((effects->enabled_effects & EFFECT_DOUBLE_BUFFER) != 0);

  /* Init pointers */
  effects->source_buffer = inst->goo_data->color;           /* latest updated texture */
  effects->target_buffer = inst->goo_data->effect_color_fb; /* next target to render to */

  /* Post process stack (order matters) */
  EEVEE_velocity_resolve(inst);
  EEVEE_motion_blur_draw(inst);
  EEVEE_depth_of_field_draw(inst);

  /* NOTE: Lookdev drawing happens before TAA but after
   * motion blur and DOF to avoid distortions.
   * Velocity resolve use a hack to exclude lookdev
   * spheres from creating shimmering re-projection vectors. */
  EEVEE_lookdev_draw(inst);

  EEVEE_temporal_sampling_draw(inst);
  EEVEE_bloom_draw(inst);

  /* Post effect render passes are done here just after the drawing of the effects and just before
   * the swapping of the buffers. */
  EEVEE_renderpasses_output_accumulate(sldata, inst, true);

  /* Save the final texture and frame-buffer for final transformation or read. */
  effects->final_tx = effects->source_buffer;
  effects->final_fb = (effects->target_buffer != inst->goo_data->main_color_fb) ? inst->goo_data->main_fb :
                                                                       inst->goo_data->effect_fb;
  if ((effects->enabled_effects & EFFECT_TAA) && (effects->source_buffer == inst->goo_data->taa_history)) {
    effects->final_fb = inst->goo_data->taa_history_fb;
  }

  /* If no post processes is enabled, buffers are still not swapped, do it now. */
  SWAP_DOUBLE_BUFFERS();

  if (!inst->goo_data->g_data->valid_double_buffer &&
      ((effects->enabled_effects & EFFECT_DOUBLE_BUFFER) != 0) &&
      (draw_ctx->is_image_render() == false))
  {
    /* If history buffer is not valid request another frame.
     * This fix black reflections on area resize. */
    DRW_viewport_request_redraw();
  }

  /* Record perspective matrix for the next frame. */
  DRW_view_persmat_get(effects->taa_view, effects->prev_persmat, false);

  /* Update double buffer status if render mode. */
  if (draw_ctx->is_image_render()) {
    inst->goo_data->g_data->valid_double_buffer = (inst->goo_data->color_double_buffer != nullptr);
    inst->goo_data->g_data->valid_taa_history = (inst->goo_data->taa_history != nullptr);
  }
}

}  // namespace blender::gooengine
