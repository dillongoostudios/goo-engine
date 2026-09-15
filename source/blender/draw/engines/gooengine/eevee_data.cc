/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * All specific data handler for Objects, Lights, ViewLayers, ...
 */

#include "DRW_render.hh"
#include "DRW_engine.hh"
#include "draw_handle.hh"

#include "BLI_listbase.h"
#include "BLI_ghash.h"
#include "BLI_memblock.h"

#include "BKE_duplilist.hh"
#include "BKE_modifier.hh"
#include "BKE_object.hh"

#include "DEG_depsgraph_query.hh"

#include "GPU_vertex_buffer.hh"

#include "eevee_lightcache.h"
#include "gooengine_private.hh"

namespace blender::gooengine {

/* View Layer data. */

void EEVEE_view_layer_data_free(void *storage)
{
  EEVEE_ViewLayerData *sldata = (EEVEE_ViewLayerData *)storage;

  /* Lights */
  MEM_SAFE_FREE(sldata->lights);
  GPU_UBO_FREE_SAFE(sldata->light_ubo);
  GPU_UBO_FREE_SAFE(sldata->shadow_ubo);
  GPU_FRAMEBUFFER_FREE_SAFE(sldata->shadow_fb);
  DRW_TEXTURE_FREE_SAFE(sldata->shadow_cube_pool);
  DRW_TEXTURE_FREE_SAFE(sldata->shadow_cascade_pool);
  DRW_TEXTURE_FREE_SAFE(sldata->shadow_cube_id_pool);
  DRW_TEXTURE_FREE_SAFE(sldata->shadow_cascade_id_pool);
  for (int i = 0; i < 2; i++) {
    MEM_SAFE_FREE(sldata->shcasters_buffers[i].bbox);
    MEM_SAFE_FREE(sldata->shcasters_buffers[i].update);
  }

  if (sldata->fallback_lightcache) {
    EEVEE_lightcache_free(sldata->fallback_lightcache);
    sldata->fallback_lightcache = nullptr;
  }

  /* Probes */
  MEM_SAFE_FREE(sldata->probes);
  GPU_UBO_FREE_SAFE(sldata->probe_ubo);
  GPU_UBO_FREE_SAFE(sldata->grid_ubo);
  GPU_UBO_FREE_SAFE(sldata->planar_ubo);
  GPU_UBO_FREE_SAFE(sldata->common_ubo);

  GPU_UBO_FREE_SAFE(sldata->renderpass_ubo.combined);
  GPU_UBO_FREE_SAFE(sldata->renderpass_ubo.diff_color);
  GPU_UBO_FREE_SAFE(sldata->renderpass_ubo.diff_light);
  GPU_UBO_FREE_SAFE(sldata->renderpass_ubo.spec_color);
  GPU_UBO_FREE_SAFE(sldata->renderpass_ubo.spec_light);
  GPU_UBO_FREE_SAFE(sldata->renderpass_ubo.emit);
  GPU_UBO_FREE_SAFE(sldata->renderpass_ubo.environment);
  for (int aov_index = 0; aov_index < MAX_AOVS; aov_index++) {
    GPU_UBO_FREE_SAFE(sldata->renderpass_ubo.aovs[aov_index]);
  }

  if (sldata->material_cache) {
    BLI_memblock_destroy(sldata->material_cache, nullptr);
    sldata->material_cache = nullptr;
  }
}

static void eevee_view_layer_init(EEVEE_ViewLayerData *sldata)
{
  sldata->common_ubo = GPU_uniformbuf_create(sizeof(sldata->common_data));
}

EEVEE_ViewLayerData *EEVEE_view_layer_data_ensure()
{
  EEVEE_ViewLayerData **sldata = (EEVEE_ViewLayerData **)DRW_view_layer_engine_data_ensure(
      &EEVEE_view_layer_data_free);

  if (*sldata == nullptr) {
    *sldata = static_cast<EEVEE_ViewLayerData *>(
        MEM_callocN(sizeof(**sldata), "EEVEE_ViewLayerData"));
    eevee_view_layer_init(*sldata);
  }

  return *sldata;
}

/* Object data. */

static void eevee_object_data_init(DrawData *dd)
{
  EEVEE_ObjectEngineData *eevee_data = (EEVEE_ObjectEngineData *)dd;
  eevee_data->shadow_caster_id = -1;
  eevee_data->need_update = false;
  eevee_data->geom_update = false;
}

EEVEE_ObjectEngineData *EEVEE_object_data_get(Object *ob)
{
  if (ELEM(ob->type, OB_LIGHTPROBE, OB_LAMP)) {
    return nullptr;
  }
  return (EEVEE_ObjectEngineData *)DRW_drawdata_get(&ob->id);
}

EEVEE_ObjectEngineData *EEVEE_object_data_ensure(Object *ob)
{
  BLI_assert(!ELEM(ob->type, OB_LIGHTPROBE, OB_LAMP));
  return (EEVEE_ObjectEngineData *)DRW_drawdata_ensure(&ob->id,
                                                       sizeof(EEVEE_ObjectEngineData),
                                                       eevee_object_data_init,
                                                       nullptr);
}

/* Light probe data. */

static void eevee_lightprobe_data_init(DrawData *dd)
{
  EEVEE_LightProbeEngineData *ped = (EEVEE_LightProbeEngineData *)dd;
  ped->need_update = false;
}

EEVEE_LightProbeEngineData *EEVEE_lightprobe_data_get(Object *ob)
{
  if (ob->type != OB_LIGHTPROBE) {
    return nullptr;
  }
  return (EEVEE_LightProbeEngineData *)DRW_drawdata_get(&ob->id);
}

EEVEE_LightProbeEngineData *EEVEE_lightprobe_data_ensure(Object *ob)
{
  BLI_assert(ob->type == OB_LIGHTPROBE);
  return (EEVEE_LightProbeEngineData *)DRW_drawdata_ensure(&ob->id,
                                                           sizeof(EEVEE_LightProbeEngineData),
                                                           eevee_lightprobe_data_init,
                                                           nullptr);
}

/* Light data. */

static void eevee_light_data_init(DrawData *dd)
{
  EEVEE_LightEngineData *led = (EEVEE_LightEngineData *)dd;
  led->need_update = true;
}

EEVEE_LightEngineData *EEVEE_light_data_get(Object *ob)
{
  if (ob->type != OB_LAMP) {
    return nullptr;
  }
  return (EEVEE_LightEngineData *)DRW_drawdata_get(&ob->id);
}

EEVEE_LightEngineData *EEVEE_light_data_ensure(Object *ob)
{
  BLI_assert(ob->type == OB_LAMP);
  return (EEVEE_LightEngineData *)DRW_drawdata_ensure(&ob->id,
                                                      sizeof(EEVEE_LightEngineData),
                                                      eevee_light_data_init,
                                                      nullptr);
}

/* World data. */

static void eevee_world_data_init(DrawData *dd)
{
  EEVEE_WorldEngineData *wed = (EEVEE_WorldEngineData *)dd;
  wed->dd.recalc |= 1;
}

EEVEE_WorldEngineData *EEVEE_world_data_get(World *wo)
{
  return (EEVEE_WorldEngineData *)DRW_drawdata_get(&wo->id);
}

EEVEE_WorldEngineData *EEVEE_world_data_ensure(World *wo)
{
  return (EEVEE_WorldEngineData *)DRW_drawdata_ensure(&wo->id,
                                                      sizeof(EEVEE_WorldEngineData),
                                                      eevee_world_data_init,
                                                      nullptr);
}

}  // namespace blender::gooengine
