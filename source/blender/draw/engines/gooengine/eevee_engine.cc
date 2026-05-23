/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include "BLI_rect.h"

#include "DRW_render.hh"

#include "draw_color_management.hh" /* TODO: remove dependency. */

#include "BLI_rand.h"
#include "BLI_listbase.h"
#include "BLI_math_vector.h"

#include "BLT_translation.hh"

#include "BKE_object.hh"

#include "DEG_depsgraph_query.hh"

#include "DNA_world_types.h"

#include "ED_screen.hh"
#include "ED_view3d.hh"
#include "GPU_context.hh"

#include "IMB_imbuf.hh"

#include "eevee_private.hh"

#include "draw_handle.hh"

#include "draw_view_data.hh"

#include "eevee_engine.h" /* own include */

namespace blender::gooengine {

static void eevee_id_object_update(Instance * /*inst*/, Object *object)
{
  EEVEE_LightProbeEngineData *ped = EEVEE_lightprobe_data_get(object);
  if (ped != nullptr && ped->dd.recalc != 0) {
    ped->need_update = (ped->dd.recalc & ID_RECALC_TRANSFORM) != 0;
    ped->dd.recalc = 0;
  }
  EEVEE_LightEngineData *led = EEVEE_light_data_get(object);
  if (led != nullptr && led->dd.recalc != 0) {
    led->need_update = true;
    led->dd.recalc = 0;
  }
  EEVEE_ObjectEngineData *oedata = EEVEE_object_data_get(object);
  if (oedata != nullptr && oedata->dd.recalc != 0) {
    oedata->need_update = true;
    oedata->geom_update = (oedata->dd.recalc & (ID_RECALC_GEOMETRY)) != 0;
    oedata->dd.recalc = 0;
  }
}

static void eevee_id_world_update(Instance *inst, World *wo)
{
  LightCache *lcache = inst->goo_data->g_data->light_cache;

  if (ELEM(lcache, nullptr, inst->goo_data->lookdev_lightcache)) {
    /* Avoid Lookdev viewport clearing the update flag (see #67741). */
    return;
  }

  EEVEE_WorldEngineData *wedata = EEVEE_world_data_ensure(wo);

  if (wedata != nullptr && wedata->dd.recalc != 0) {
    if ((lcache->flag & LIGHTCACHE_BAKING) == 0) {
      lcache->flag |= LIGHTCACHE_UPDATE_WORLD;
    }
    wedata->dd.recalc = 0;
  }
}

void eevee_id_update(Instance *inst, ID *id)
{
  /* Handle updates based on ID type. */
  switch (GS(id->name)) {
    case ID_WO:
      eevee_id_world_update(inst, (World *)id);
      break;
    case ID_OB:
      eevee_id_object_update(inst, (Object *)id);
      break;
    default:
      /* pass */
      break;
  }
}

static void eevee_render_reset_passes(Instance */*inst*/)
{
  /* Need to modify this to rests each pass separately or is it not needed? */

  // /* Reset pass-list. This is safe as they are stored into managed memory chunks. */
  // memset(vedata->psl, 0, sizeof(*vedata->psl));
}

void Instance::init()
{
  printf("\n\n\n INIT \n\n\n");

  const DRWContext *ctx_state = DRW_context_get();
  Depsgraph *depsgraph = ctx_state->depsgraph;
  Scene *scene = ctx_state->scene;
  View3D *v3d = ctx_state->v3d;
  ARegion *region = ctx_state->region;
  RegionView3D *rv3d = ctx_state->rv3d;

  DefaultTextureList *dtxl = ctx_state->viewport_texture_list_get();
  int2 size = int2(GPU_texture_width(dtxl->color), GPU_texture_height(dtxl->color));

  draw::View &default_view = draw::View::default_get();

  Object *camera = nullptr;
  if (v3d) {
    if (rv3d && (rv3d->persp == RV3D_CAMOB)) {
      camera = v3d->camera;
    }
  }

  init(size, nullptr, depsgraph, camera, nullptr, &default_view, v3d, rv3d);
}

void Instance::init(const int2 &output_res,
                    RenderEngine *render_,
                    Depsgraph *depsgraph_,
                    Object *camera_object_,
                    const RenderLayer *render_layer_,
                    blender::draw::View *drw_view_,
                    const View3D *v3d_,
                    const RegionView3D *rv3d_)
{
  render = render_;
  depsgraph = depsgraph_;
  camera_orig_object = camera_object_;
  render_layer = render_layer_;
  drw_view = drw_view_;
  v3d = v3d_;
  rv3d = rv3d_;
  manager = DRW_manager_get();

  printf("\n INIT 2 \n");

  EEVEE_ViewLayerData *sldata = EEVEE_view_layer_data_ensure();
  const DRWContext *draw_ctx = DRW_context_get();
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();

  View3D *v3d = draw_ctx->v3d;
  RegionView3D *rv3d = draw_ctx->rv3d;

  if (!this->goo_data->g_data) {
    /* Alloc transient pointers */
    this->goo_data->g_data = static_cast<EEVEE_PrivateData *>(MEM_callocN(sizeof(*this->goo_data->g_data), __func__));
  }

  printf(" GOT DATA \n");

  if (this->goo_data->g_data->depsgraph_last_update_ != DEG_get_update_count(draw_ctx->depsgraph)) {
    this->goo_data->g_data->view_updated = true;
  }

  const float *size_cur = draw_ctx->viewport_size_get();
  int viewport_size[2] = {int(this->goo_data->g_data->size_orig[0]), int(this->goo_data->g_data->size_orig[1])};
  if (viewport_size[0] != int(size_cur[0]) || viewport_size[1] != int(size_cur[1])) {
    printf("NEW VIEWPORT SIZE \n");
    EEVEE_reset_viewport_data(this);
  }

  copy_v2_v2(this->goo_data->g_data->size_orig, size_cur);
  
  this->goo_data->g_data->use_color_render_settings = USE_SCENE_LIGHT(v3d) ||
                                           !LOOK_DEV_STUDIO_LIGHT_ENABLED(v3d);
  this->goo_data->g_data->background_alpha = DRW_state_draw_background() ? 1.0f : 0.0f;
  this->goo_data->g_data->valid_double_buffer = (this->goo_data->color_double_buffer != nullptr);
  this->goo_data->g_data->valid_taa_history = (this->goo_data->taa_history != nullptr);
  this->goo_data->g_data->queued_shaders_count = 0;
  this->goo_data->g_data->queued_optimise_shaders_count = 0;
  this->goo_data->g_data->render_timesteps = 1;
  this->goo_data->g_data->disable_ligthprobes = v3d &&
                                     (v3d->object_type_exclude_viewport & (1 << OB_LIGHTPROBE));

  /* Main Buffer */
  DRW_texture_ensure_fullscreen_2d(&this->goo_data->color, GPU_RGBA16F, DRW_TEX_FILTER);

  GPU_framebuffer_ensure_config(&this->goo_data->main_fb,
                                {GPU_ATTACHMENT_TEXTURE(dtxl->depth),
                                 GPU_ATTACHMENT_TEXTURE(this->goo_data->color),
                                 GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_LEAVE});

  GPU_framebuffer_ensure_config(&this->goo_data->main_color_fb,
                                {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(this->goo_data->color)});

  /* `EEVEE_renderpasses_init` will set the active render passes used by `EEVEE_effects_init`.
  * `EEVEE_effects_init` needs to go second for TAA. */
  printf(" inititgi stuff \n");
  EEVEE_renderpasses_init(this);
  EEVEE_effects_init(sldata, this, camera_orig_object, false);
  EEVEE_materials_init(sldata, this);
  EEVEE_shadows_init(sldata);
  EEVEE_lightprobes_init(sldata, this);
}
















void Instance::begin_sync()
{
  printf("\n       BEGIN SYNC \n");
  EEVEE_ViewLayerData *sldata = EEVEE_view_layer_data_ensure();

  EEVEE_bloom_cache_init(sldata, this);
  EEVEE_depth_of_field_cache_init(sldata, this);
  EEVEE_effects_cache_init(sldata, this);
  EEVEE_lightprobes_cache_init(sldata, this);
  EEVEE_lights_cache_init(sldata, this);
  EEVEE_materials_cache_init(sldata, this);
  EEVEE_motion_blur_cache_init(sldata, this);
  EEVEE_occlusion_cache_init(sldata, this);
  EEVEE_screen_raytrace_cache_init(sldata, this);
  EEVEE_subsurface_cache_init(sldata, this);
  EEVEE_temporal_sampling_cache_init(sldata, this);
  EEVEE_volumes_cache_init(sldata, this);
}

void Instance::object_sync(blender::draw::ObjectRef &ob_ref, blender::draw::Manager &manager)
{
  printf("\n       OBJECT SYNC \n");
  EEVEE_ViewLayerData *sldata = EEVEE_view_layer_data_ensure();

  Object *ob = ob_ref.object;

  const DRWContext *draw_ctx = DRW_context_get();
  const int ob_visibility = DRW_object_visibility_in_active_context(ob);
  bool cast_shadow = false;

  if (ob_visibility & OB_VISIBLE_PARTICLES) {
    EEVEE_particle_hair_cache_populate(
        this, sldata, ob, &cast_shadow);
  }

  if (DRW_object_is_renderable(ob) && (ob_visibility & OB_VISIBLE_SELF)) {

    eevee_id_update(this, &ob->id);

    if (ob->type == OB_MESH) {
      EEVEE_materials_cache_populate(this, sldata, ob, &cast_shadow);
    }
    else if (ob->type == OB_CURVES) {
      EEVEE_object_curves_cache_populate(
          this, sldata, ob, &cast_shadow);
    }
    else if (ob->type == OB_VOLUME) {
      EEVEE_volumes_cache_object_add(
          sldata, this, draw_ctx->scene, ob);
    }
    else if (!USE_SCENE_LIGHT(draw_ctx->v3d)) {
      /* do not add any scene light sources to the cache */
    }
    else if (ob->type == OB_LIGHTPROBE) {
      if ((ob->base_flag & BASE_FROM_DUPLI) != 0) {
        /* TODO: Special case for dupli objects because we cannot save the object pointer. */
      }
      else {
        EEVEE_lightprobes_cache_add(sldata, this, ob);
      }
    }
    else if (ob->type == OB_LAMP) {
      EEVEE_lights_cache_add(sldata, ob);
    }
  }

  if (cast_shadow) {
    EEVEE_shadows_caster_register(sldata, ob);
  }
}

void Instance::end_sync()
{
  printf("\n       END SYNC \n");
  EEVEE_ViewLayerData *sldata = EEVEE_view_layer_data_ensure();
  EEVEE_PrivateData *g_data = this->goo_data->g_data;
  const DRWContext *draw_ctx = DRW_context_get();
  const Scene *scene_eval = DEG_get_evaluated_scene(draw_ctx->depsgraph);

  EEVEE_volumes_cache_finish(sldata, this);
  EEVEE_materials_cache_finish(sldata, this);
  EEVEE_lights_cache_finish(sldata, this);
  EEVEE_lightprobes_cache_finish(sldata, this);
  EEVEE_renderpasses_cache_finish(sldata, this);

  EEVEE_subsurface_draw_init(sldata, this);
  EEVEE_effects_draw_init(sldata, this);
  EEVEE_volumes_draw_init(sldata, this);

  uint tot_samples = scene_eval->eevee.taa_render_samples;
  uint vl_samples = draw_ctx->view_layer->samples;

  if (vl_samples > 0){
    tot_samples = vl_samples;
  }

  if (tot_samples == 0) {
    /* Use a high number of samples so the outputs accumulation buffers
     * will have the highest possible precision. */
    tot_samples = 1024;
  }
  EEVEE_renderpasses_output_init(sldata, this, tot_samples);

  /* Restart TAA if a shader has finish compiling. */
  /* HACK: We should use notification of some sort from the compilation job instead. */
  if (g_data->queued_shaders_count != g_data->queued_shaders_count_prev) {
    g_data->queued_shaders_count_prev = g_data->queued_shaders_count;
    printf(" reset 4 \n");
    EEVEE_temporal_sampling_reset(this);
  }

  if (g_data->queued_shaders_count > 0) {
    SNPRINTF(this->info, RPT_("Compiling Shaders (%d remaining)"), g_data->queued_shaders_count);
    DRW_viewport_request_redraw();
  }
  else if (g_data->queued_optimise_shaders_count > 0) {
    SNPRINTF(this->info,
             RPT_("Optimizing Shaders (%d remaining)"),
             g_data->queued_optimise_shaders_count);
  }

  g_data->depsgraph_last_update_ = DEG_get_update_count(draw_ctx->depsgraph);
}

// void Instance::acquire_resources()
// {
// }

// void Instance::release_resources()
// {
// }

void Instance::render_frame(RenderEngine *engine, RenderLayer *render_layer, const char *view_name, const rcti *rect)
{
  printf("\n       RENDER FRAME \n");
  const DRWContext *draw_ctx = DRW_context_get();
  Depsgraph *depsgraph = draw_ctx->depsgraph;
  Scene *scene = DEG_get_evaluated_scene(depsgraph);
  EEVEE_ViewLayerData *sldata = EEVEE_view_layer_data_ensure();
  const bool do_motion_blur = (scene->r.mode & R_MBLUR) != 0;
  const bool do_motion_blur_fx = do_motion_blur && (scene->eevee.motion_blur_max > 0);

  if (!EEVEE_render_init(this, engine, depsgraph)) {
    return;
  }
  EEVEE_PrivateData *g_data = this->goo_data->g_data;

  int initial_frame = scene->r.cfra;
  float initial_subframe = scene->r.subframe;
  float shuttertime = (do_motion_blur) ? scene->r.motion_blur_shutter : 0.0f;
  int time_steps_tot = (do_motion_blur) ? max_ii(1, scene->eevee.motion_blur_steps) : 1;
  g_data->render_timesteps = time_steps_tot;

  EEVEE_render_modules_init(this, engine, depsgraph);

  g_data->render_sample_count_per_timestep = EEVEE_temporal_sampling_sample_count_get(scene,
                                                                                      this);

  /* Reset in case the same engine is used on multiple views. */
  printf(" reset 3 \n");
  EEVEE_temporal_sampling_reset(this);

  /* Compute start time. The motion blur will cover `[time ...time + shuttertime]`. */
  float time = initial_frame + initial_subframe;
  switch (scene->r.motion_blur_position) {
    case SCE_MB_START:
      /* No offset. */
      break;
    case SCE_MB_CENTER:
      time -= shuttertime * 0.5f;
      break;
    case SCE_MB_END:
      time -= shuttertime;
      break;
    default:
      BLI_assert_msg(0, "Invalid motion blur position enum!");
      break;
  }

  float time_step = shuttertime / time_steps_tot;
  for (int i = 0; i < time_steps_tot && !RE_engine_test_break(engine); i++) {
    float time_prev = time;
    float time_curr = time + time_step * 0.5f;
    float time_next = time + time_step;
    time += time_step;

    /* Previous motion step. */
    if (do_motion_blur_fx) {
      if (i == 0) {
        EEVEE_motion_blur_step_set(this, MB_PREV);
        DRW_render_set_time(engine, depsgraph, floorf(time_prev), fractf(time_prev));
        EEVEE_render_modules_init(this, engine, depsgraph);
        sldata = EEVEE_view_layer_data_ensure();

        EEVEE_render_cache_init(sldata, this);

        DRW_render_object_iter(this->render,
                              this->depsgraph,
                              [&](blender::draw::ObjectRef &ob_ref, RenderEngine *, Depsgraph *) {
                                EEVEE_render_cache(this, ob_ref);
                              });

        EEVEE_motion_blur_cache_finish(this);
        EEVEE_materials_cache_finish(sldata, this);
        eevee_render_reset_passes(this);
      }
    }

    /* Next motion step. */
    if (do_motion_blur_fx) {
      EEVEE_motion_blur_step_set(this, MB_NEXT);
      DRW_render_set_time(engine, depsgraph, floorf(time_next), fractf(time_next));
      EEVEE_render_modules_init(this, engine, depsgraph);
      sldata = EEVEE_view_layer_data_ensure();

      EEVEE_render_cache_init(sldata, this);

      DRW_render_object_iter(this->render,
                            this->depsgraph,
                            [&](blender::draw::ObjectRef &ob_ref, RenderEngine *, Depsgraph *) {
                              EEVEE_render_cache(this, ob_ref);
                            });

      EEVEE_motion_blur_cache_finish(this);
      EEVEE_materials_cache_finish(sldata, this);
      eevee_render_reset_passes(this);
    }

    /* Current motion step. */
    {
      if (do_motion_blur) {
        EEVEE_motion_blur_step_set(this, MB_CURR);
        DRW_render_set_time(engine, depsgraph, floorf(time_curr), fractf(time_curr));
        EEVEE_render_modules_init(this, engine, depsgraph);
        sldata = EEVEE_view_layer_data_ensure();
      }

      EEVEE_render_cache_init(sldata, this);

      DRW_render_object_iter(this->render,
                            this->depsgraph,
                            [&](blender::draw::ObjectRef &ob_ref, RenderEngine *, Depsgraph *) {
                              EEVEE_render_cache(this, ob_ref);
                            });

      EEVEE_motion_blur_cache_finish(this);
      EEVEE_volumes_cache_finish(sldata, this);
      EEVEE_materials_cache_finish(sldata, this);
      EEVEE_lights_cache_finish(sldata, this);
      EEVEE_lightprobes_cache_finish(sldata, this);
      EEVEE_renderpasses_cache_finish(sldata, this);

      EEVEE_subsurface_draw_init(sldata, this);
      EEVEE_effects_draw_init(sldata, this);
      EEVEE_volumes_draw_init(sldata, this);
    }

    /* Actual drawing. */
    {
      EEVEE_renderpasses_output_init(sldata,
                                     this,
                                     g_data->render_sample_count_per_timestep * time_steps_tot);

      if (scene->world) {
        /* Update world in case of animated world material. */
        eevee_id_world_update(this, scene->world);
      }

      EEVEE_temporal_sampling_create_view(this);
      EEVEE_render_draw(this, engine, render_layer, rect);

      if (i < time_steps_tot - 1) {
        /* Don't reset after the last loop. Since EEVEE_render_read_result
         * might need some DRWPasses. */
        DRW_cache_restart();
      }
    }

    if (do_motion_blur_fx) {
      /* The previous step of next iteration N is exactly the next step of this iteration N - 1.
       * So we just swap the resources to avoid too much re-evaluation.
       * Note that this also clears the VBO references from the GPUBatches of deformed
       * geometries. */
      EEVEE_motion_blur_swap_data(this);
    }
  }

  EEVEE_motion_blur_data_free(&this->goo_data->effects->motion_blur);

  if (RE_engine_test_break(engine)) {
    return;
  }

  EEVEE_render_read_result(this, engine, render_layer, rect);

  /* Restore original viewport size. */
  int viewport_size[2] = {int(g_data->size_orig[0]), int(g_data->size_orig[1])};
  DRW_render_viewport_size_set(viewport_size);

  if (scene->r.cfra != initial_frame || scene->r.subframe != initial_subframe) {
    /* Restore original frame number. This is because the render pipeline expects it. */
    RE_engine_frame_set(engine, initial_frame, initial_subframe);
  }
}

void Instance::draw(blender::draw::Manager &manager)
{
  printf("\n       DRAW \n");
  EEVEE_ViewLayerData *sldata = EEVEE_view_layer_data_ensure();

  /* Default framebuffer and texture */
  const DRWContext *draw_ctx = DRW_context_get();
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
  DefaultFramebufferList *dfbl = draw_ctx->viewport_framebuffer_list_get();

  /* Sort transparents before the loop. */
  DRW_pass_sort_shgroup_z(this->goo_data->transparent_pass);

  /* Number of iteration: Use viewport taa_samples when using viewport rendering */
  int loop_len = 1;
  if (draw_ctx->is_image_render()) {
    const Scene *scene = draw_ctx->scene;
    loop_len = std::max(1, scene->eevee.taa_samples);
    if (scene->world) {
      /* Update world in case of animated world material. */
      eevee_id_world_update(this, scene->world);
    }
  }

  if (this->goo_data->effects->bypass_drawing) {
    loop_len = 0;
  }

  while (loop_len--) {
    const float clear_col[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float clear_depth = 1.0f;
    uint clear_stencil = 0x0;
    const uint primes[3] = {2, 3, 7};
    double offset[3] = {0.0, 0.0, 0.0};
    double r[3];

    bool taa_use_reprojection = (this->goo_data->effects->enabled_effects & EFFECT_TAA_REPROJECT) != 0;

    if (draw_ctx->is_image_render() || taa_use_reprojection ||
        ((this->goo_data->effects->enabled_effects & EFFECT_TAA) != 0))
    {
      int samp = taa_use_reprojection ? this->goo_data->effects->taa_reproject_sample + 1 :
                                        this->goo_data->effects->taa_current_sample;
      BLI_halton_3d(primes, offset, samp, r);
      EEVEE_update_noise(this, r);
      EEVEE_volumes_set_jitter(sldata, samp - 1);
      EEVEE_materials_init(sldata, this);
    }
    /* Copy previous persmat to UBO data */
    copy_m4_m4(sldata->common_data.prev_persmat, this->goo_data->effects->prev_persmat);

    /* Refresh Probes
     * Shadows needs to be updated for correct probes */
    EEVEE_shadows_update(sldata, this);
    EEVEE_lightprobes_refresh(sldata, this);
    EEVEE_lightprobes_refresh_planar(sldata, this);
  
    /* Refresh shadows */
    EEVEE_shadows_draw(sldata, this, this->goo_data->effects->taa_view);

    printf("\n SAMPLE CURRENT %i \n", this->goo_data->effects->taa_current_sample);

    if (((this->goo_data->effects->enabled_effects & EFFECT_TAA) != 0) &&
        (this->goo_data->effects->taa_current_sample > 1) && !draw_ctx->is_image_render() &&
        !taa_use_reprojection)
    {
      printf(" set active \n");
      DRW_view_set_active(this->goo_data->effects->taa_view);
    }
    /* when doing viewport rendering the overrides needs to be recalculated for
     * every loop as this normally happens once inside
     * `EEVEE_temporal_sampling_init` */
    else if (((this->goo_data->effects->enabled_effects & EFFECT_TAA) != 0) &&
             (this->goo_data->effects->taa_current_sample > 1) && draw_ctx->is_image_render())
    {
      printf(" update matrices \n");
      EEVEE_temporal_sampling_update_matrices(this);
    }

    /* Set ray type. */
    sldata->common_data.ray_type = EEVEE_RAY_CAMERA;
    sldata->common_data.ray_depth = 0.0f;
    if (this->goo_data->g_data->disable_ligthprobes) {
      sldata->common_data.prb_num_render_cube = 1;
      sldata->common_data.prb_num_render_grid = 1;
    }
    GPU_uniformbuf_update(sldata->common_ubo, &sldata->common_data);

    GPU_framebuffer_bind(this->goo_data->main_fb);
    eGPUFrameBufferBits clear_bits = GPU_DEPTH_BIT;
    SET_FLAG_FROM_TEST(clear_bits, !DRW_state_draw_background(), GPU_COLOR_BIT);
    SET_FLAG_FROM_TEST(clear_bits, (this->goo_data->effects->enabled_effects & EFFECT_SSS), GPU_STENCIL_BIT);
    GPU_framebuffer_clear(this->goo_data->main_fb, clear_bits, clear_col, clear_depth, clear_stencil);

    /* Depth pre-pass. */
    DRW_draw_pass(this->goo_data->depth_ps);

    /* Create minmax texture */
    EEVEE_create_minmax_buffer(this, dtxl->depth, -1);

    EEVEE_occlusion_compute(sldata, this);
    EEVEE_volumes_compute(sldata, this);

    /* Shading pass */
    if (DRW_state_draw_background()) {
      DRW_draw_pass(this->goo_data->background_ps);
    }
    DRW_draw_pass(this->goo_data->material_ps);
    EEVEE_subsurface_data_render(sldata, this);

    /* Effects pre-transparency */
    EEVEE_subsurface_compute(sldata, this);
    EEVEE_reflection_compute(sldata, this);
    EEVEE_occlusion_draw_debug(sldata, this);
    if (this->goo_data->probe_display) {
      DRW_draw_pass(this->goo_data->probe_display);
    }
    EEVEE_refraction_compute(sldata, this);

    /* Opaque refraction */
    DRW_draw_pass(this->goo_data->depth_refract_ps);
    DRW_draw_pass(this->goo_data->material_refract_ps);

    /* Streamlined version of EEVEE_refraction_compute to just copy the colour buffer */
    EEVEE_effects_radiance_copy(sldata, this);

    /* Volumetrics Resolve Opaque */
    EEVEE_volumes_resolve(sldata, this);

    /* Render-passes. */
    EEVEE_renderpasses_output_accumulate(sldata, this, false);

    /* Transparent */
    EEVEE_material_transparent_output_accumulate(this);
    /* TODO(@fclem): should be its own Frame-buffer.
     * This is needed because dual-source blending only works with 1 color buffer. */
    GPU_framebuffer_texture_attach(this->goo_data->main_color_fb, dtxl->depth, 0, 0);
    GPU_framebuffer_bind(this->goo_data->main_color_fb);
    DRW_draw_pass(this->goo_data->transparent_pass);
    GPU_framebuffer_bind(this->goo_data->main_fb);
    GPU_framebuffer_texture_detach(this->goo_data->main_color_fb, dtxl->depth);

    /* Post Process */
    EEVEE_draw_effects(sldata, this);

    DRW_view_set_active(nullptr);

    if (draw_ctx->is_image_render() && (this->goo_data->effects->enabled_effects & EFFECT_SSR) &&
        !this->goo_data->effects->ssr_was_valid_double_buffer)
    {
      /* SSR needs one iteration to start properly. */
      loop_len++;
      /* Reset sampling (and accumulation) after the first sample to avoid
       * washed out first bounce for SSR. */
      printf(" reset 2 \n");
      EEVEE_temporal_sampling_reset(this);
      this->goo_data->effects->ssr_was_valid_double_buffer = this->goo_data->g_data->valid_double_buffer;
    }

    /* Perform render step between samples to allow flushing of freed temporary GPUBackend
     * resources. This prevents the GPU backend accumulating a high amount of in-flight memory when
     * performing renders using eevee_draw_scene. e.g. During file thumbnail generation. */
    if (loop_len > 2) {
      if (GPU_backend_get_type() == GPU_BACKEND_METAL) {
        GPU_flush();
        GPU_render_step();
      }
    }
  }

  if ((this->goo_data->g_data->render_passes & EEVEE_RENDER_PASS_COMBINED) != 0) {
    /* Transfer result to default framebuffer. */
    GPU_framebuffer_bind(dfbl->default_fb);
    DRW_transform_none(this->goo_data->effects->final_tx);
  }
  else {
    EEVEE_renderpasses_draw(sldata, this);
  }

  if (this->goo_data->effects->bypass_drawing) {
    /* Restore the depth from sample 1. */
    GPU_framebuffer_blit(this->goo_data->double_buffer_depth_fb, 0, dfbl->default_fb, 0, GPU_DEPTH_BIT);
  }

  EEVEE_renderpasses_draw_debug(this);

  this->goo_data->g_data->view_updated = false;

  DRW_view_set_active(nullptr);
}

void Instance::store_metadata(RenderResult *render_result)
{
  printf("\n       STORE METADATA \n");
  EEVEE_PrivateData *g_data = this->goo_data->g_data;
  if (g_data->render_passes & EEVEE_RENDER_PASS_CRYPTOMATTE) {
    EEVEE_cryptomatte_store_metadata(this, render_result);
    EEVEE_cryptomatte_free(this);
  }
}

void Instance::update_passes(RenderEngine *engine, Scene *scene, ViewLayer *view_layer)
{
  printf("\n       UPDATE PASSES \n");
  RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_COMBINED, 4, "RGBA", SOCK_RGBA);

#define CHECK_PASS_LEGACY(name, type, channels, chanid) \
  if (view_layer->passflag & (SCE_PASS_##name)) { \
    RE_engine_register_pass( \
        engine, scene, view_layer, RE_PASSNAME_##name, channels, chanid, type); \
  } \
  ((void)0)
#define CHECK_PASS_EEVEE(name, type, channels, chanid) \
  if (view_layer->eevee.render_passes & (EEVEE_RENDER_PASS_##name)) { \
    RE_engine_register_pass( \
        engine, scene, view_layer, RE_PASSNAME_##name, channels, chanid, type); \
  } \
  ((void)0)

  CHECK_PASS_LEGACY(Z, SOCK_FLOAT, 1, "Z");
  CHECK_PASS_LEGACY(MIST, SOCK_FLOAT, 1, "Z");
  CHECK_PASS_LEGACY(NORMAL, SOCK_VECTOR, 3, "XYZ");
  CHECK_PASS_LEGACY(DIFFUSE_DIRECT, SOCK_RGBA, 3, "RGB");
  CHECK_PASS_LEGACY(DIFFUSE_COLOR, SOCK_RGBA, 3, "RGB");
  CHECK_PASS_LEGACY(GLOSSY_DIRECT, SOCK_RGBA, 3, "RGB");
  CHECK_PASS_LEGACY(GLOSSY_COLOR, SOCK_RGBA, 3, "RGB");
  CHECK_PASS_EEVEE(VOLUME_LIGHT, SOCK_RGBA, 3, "RGB");
  CHECK_PASS_LEGACY(EMIT, SOCK_RGBA, 3, "RGB");
  CHECK_PASS_LEGACY(ENVIRONMENT, SOCK_RGBA, 3, "RGB");
  CHECK_PASS_LEGACY(SHADOW, SOCK_RGBA, 3, "RGB");
  CHECK_PASS_LEGACY(AO, SOCK_RGBA, 3, "RGB");
  CHECK_PASS_EEVEE(BLOOM, SOCK_RGBA, 3, "RGB");
  CHECK_PASS_EEVEE(TRANSPARENT, SOCK_RGBA, 4, "RGBA");

  LISTBASE_FOREACH (ViewLayerAOV *, aov, &view_layer->aovs) {
    if ((aov->flag & AOV_CONFLICT) != 0) {
      continue;
    }
    switch (aov->type) {
      case AOV_TYPE_COLOR:
        RE_engine_register_pass(engine, scene, view_layer, aov->name, 4, "RGBA", SOCK_RGBA);
        break;
      case AOV_TYPE_VALUE:
        RE_engine_register_pass(engine, scene, view_layer, aov->name, 1, "X", SOCK_FLOAT);
        break;
      default:
        break;
    }
  }
  EEVEE_cryptomatte_update_passes(engine, scene, view_layer);

#undef CHECK_PASS_LEGACY
#undef CHECK_PASS_EEVEE
}



DrawEngine *Engine::create_instance()
{
  printf("\n       CREATE INSTANCE \n");
  return new Instance();
}

void Engine::free_static()
{
}

}  // namespace blender::gooengine











using namespace blender::gooengine;

void gooengine_render(RenderEngine *engine, Depsgraph *depsgraph)
{
  Instance *instance = nullptr;
  
  printf("\n       GOOENGINE_RENDER \n");

  auto gooengine_render_to_image = [&](RenderEngine *engine, RenderLayer *layer, const rcti rect) {
    Render *render = engine->re;
    Depsgraph *depsgraph = DRW_context_get()->depsgraph;
    Object *camera_original_ob = RE_GetCamera(engine->re);
    const char *viewname = RE_GetActiveRenderView(engine->re);
    int size[2] = {engine->resolution_x, engine->resolution_y};

    /* WORKAROUND: Fails if created in the parent scope. Must be because of lack of active
     * `DRWContext`. To be revisited. */
      
    printf("\n       RENDER TO IMAGE \n");
    instance = new Instance();

    instance->init(size, engine, depsgraph, camera_original_ob, layer);
      
    printf("\n       INITED \n");
    instance->render_frame(engine, layer, viewname, &rect);
  };

  auto gooengine_store_metadata = [&](RenderResult *render_result) {  
    printf("\n       STORE METADATA 1 \n");
    instance->store_metadata(render_result);
  };
  printf("\n       DRW RENDEDR TO IMAG \n");

  DRW_render_to_image(engine, depsgraph, gooengine_render_to_image, gooengine_store_metadata);

  delete instance;
}

void gooengine_render_update_passes(RenderEngine *engine, Scene *scene, ViewLayer *view_layer)
{  
  printf("\n       RENDER UPDATE PASSES \n");
  Instance::update_passes(engine, scene, view_layer);
}

RenderEngineType DRW_engine_viewport_gooengine_type = {
    /*next*/ nullptr,
    /*prev*/ nullptr,
    /*idname*/ "BLENDER_EEVEE",
    /*name*/ N_("Goo Engine"),
    /*flag*/ RE_INTERNAL | RE_USE_PREVIEW | RE_USE_STEREO_VIEWPORT | RE_USE_GPU_CONTEXT,
    /*update*/ nullptr,
    /*render*/ &gooengine_render,
    /*render_frame_finish*/ nullptr,
    /*draw*/ nullptr,
    /*bake*/ nullptr,
    /*view_update*/ nullptr,
    /*view_draw*/ nullptr,
    /*update_script_node*/ nullptr,
    /*update_render_passes*/ &gooengine_render_update_passes,
    /*update_custom_camera*/ nullptr,
    /*draw_engine*/ nullptr,
    /*rna_ext*/
    {
        /*data*/ nullptr,
        /*srna*/ nullptr,
        /*call*/ nullptr,
    },
};
