/* SPDX-FileCopyrightText: 2017 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 */

#include <cstdio>

#include "BLI_hash.h"
#include "BLI_listbase.h"
#include "BLI_math_vector.h"
#include "BLI_rand.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_defaults.h"
#include "DNA_gpencil_legacy_types.h"
#include "DNA_gpencil_modifier_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_context.h"
#include "BKE_deform.h"
#include "BKE_gpencil_geom_legacy.h"
#include "BKE_gpencil_modifier_legacy.h"
#include "BKE_lib_query.h"
#include "BKE_modifier.h"
#include "BKE_screen.hh"

#include "DEG_depsgraph.hh"

#include "UI_interface.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"

#include "MOD_gpencil_legacy_modifiertypes.h"
#include "MOD_gpencil_legacy_ui_common.h"
#include "MOD_gpencil_legacy_util.h"

static void init_data(GpencilModifierData *md)
{
  TextureGpencilModifierData *gpmd = (TextureGpencilModifierData *)md;

  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(gpmd, modifier));

  MEMCPY_STRUCT_AFTER(gpmd, DNA_struct_default_get(TextureGpencilModifierData), modifier);
}

static void copy_data(const GpencilModifierData *md, GpencilModifierData *target)
{
  BKE_gpencil_modifier_copydata_generic(md, target);
}

/* change stroke uv texture values */
static void deform_stroke(GpencilModifierData *md,
                          Depsgraph * /*depsgraph*/,
                          Object *ob,
                          bGPDlayer *gpl,
                          bGPDframe * gpf,
                          bGPDstroke *gps)
{
  TextureGpencilModifierData *mmd = (TextureGpencilModifierData *)md;
  const int def_nr = BKE_object_defgroup_name_index(ob, mmd->vgname);
  bGPdata *gpd = static_cast<bGPdata *>(ob->data);

  if (!is_stroke_affected_by_modifier(ob,
                                      mmd->layername,
                                      mmd->material,
                                      mmd->pass_index,
                                      mmd->layer_pass,
                                      1,
                                      gpl,
                                      gps,
                                      mmd->flag & GP_TEX_INVERT_LAYER,
                                      mmd->flag & GP_TEX_INVERT_PASS,
                                      mmd->flag & GP_TEX_INVERT_LAYERPASS,
                                      mmd->flag & GP_TEX_INVERT_MATERIAL))
  {
    return;
  }

  const bool is_fill_randomized = !(mmd->rnd_fill_rot == 0.0f &&
                                    is_zero_v2(mmd->rnd_fill_offset) &&
                                    mmd->rnd_fill_scale == 0.0f);
  const bool is_uv_randomized = !(mmd->rnd_uv_offset == 0.0f && mmd->rnd_uv_scale == 0.0f);

  int seed = mmd->seed;
  /* Make sure different modifiers get different seeds. */
  seed += BLI_hash_string(ob->id.name + 2);
  seed += BLI_hash_string(md->name);

  float fill_rot = mmd->fill_rotation;
  float fill_offset[2];
  fill_offset[0] = mmd->fill_offset[0];
  fill_offset[1] = mmd->fill_offset[1];
  float fill_scale = mmd->fill_scale;
  float uv_offset = mmd->uv_offset;
  float uv_scale = mmd->uv_scale;
  float rand_offset = 0.0;
  int rnd_index = 0;

  if (is_fill_randomized || is_uv_randomized) {
    rand_offset = BLI_hash_int_01(seed);

    /* Get stroke index for random offset. */
    rnd_index = BLI_findindex(&gpf->strokes, gps);
  }

  if (ELEM(mmd->mode, FILL, STROKE_AND_FILL)) {

    if (!(mmd->rnd_fill_rot == 0.0f)) {
      float rnd_fill_rot = 0.0f;

      double r;
      /* To ensure a nice distribution, we use halton sequence and offset using the seed. */
      BLI_halton_1d(2, 0.0, rnd_index, &r);

      rnd_fill_rot = fmodf(r * 2.0f - 1.0f + rand_offset, 1.0f);
      rnd_fill_rot = fmodf(sin(rnd_fill_rot * 12.9898f) * 43758.5453f, 1.0f);

      fill_rot += mmd->rnd_fill_rot * rnd_fill_rot;
    }

    if (!is_zero_v2(mmd->rnd_fill_offset)) {
      float rnd_fill_offset[2] = {0.0f, 0.0f};

      const uint primes[2] = {3, 7};
      double offset[2] = {0.0f, 0.0f};
      double r[2];
      /* To ensure a nice distribution, we use halton sequence and offset using the seed. */
      BLI_halton_2d(primes, offset, rnd_index, r);

      rnd_fill_offset[0] = fmodf(r[0] * 2.0f - 1.0f + rand_offset, 1.0f);
      rnd_fill_offset[0] = fmodf(sin(rnd_fill_offset[0] * 12.9898f) * 43758.5453f, 1.0f);
      rnd_fill_offset[1] = fmodf(r[1] * 2.0f - 1.0f + rand_offset, 1.0f);
      rnd_fill_offset[1] = fmodf(sin(rnd_fill_offset[1] * 12.9898f + 78.233f) * 43758.5453f, 1.0f);

      fill_offset[0] += mmd->rnd_fill_offset[0] * rnd_fill_offset[0];
      fill_offset[1] += mmd->rnd_fill_offset[1] * rnd_fill_offset[1];
    }

    if (!(mmd->rnd_fill_scale == 0.0f)) {
      float rnd_fill_scale = 0.0f;

      double r;
      /* To ensure a nice distribution, we use halton sequence and offset using the seed. */
      BLI_halton_1d(11, 0.0, rnd_index, &r);

      rnd_fill_scale = fmodf(r * 2.0f - 1.0f + rand_offset, 1.0f);
      rnd_fill_scale = fmodf(sin(rnd_fill_scale * 12.9898f) * 43758.5453f, 1.0f);

      fill_scale += mmd->rnd_fill_scale * rnd_fill_scale;
    }

    gps->uv_rotation += fill_rot;
    gps->uv_translation[0] += fill_offset[0];
    gps->uv_translation[1] += fill_offset[1];
    gps->uv_scale *= fill_scale;
    BKE_gpencil_stroke_geometry_update(gpd, gps);
  }

  if (ELEM(mmd->mode, STROKE, STROKE_AND_FILL)) {

    if (!(mmd->rnd_uv_offset == 0.0f)) {
      float rnd_uv_offset = 0.0f;

      double r;
      /* To ensure a nice distribution, we use halton sequence and offset using the seed. */
      BLI_halton_1d(2, 0.0, rnd_index, &r);

      rnd_uv_offset = fmodf(r * 2.0f - 1.0f + rand_offset, 1.0f);
      rnd_uv_offset = fmodf(sin(rnd_uv_offset * 12.9898f) * 43758.5453f, 1.0f);

      uv_offset += mmd->rnd_uv_offset * rnd_uv_offset;
    }

    if (!(mmd->rnd_uv_scale == 0.0f)) {
      float rnd_uv_scale = 0.0f;

      double r;
      /* To ensure a nice distribution, we use halton sequence and offset using the seed. */
      BLI_halton_1d(3, 0.0, rnd_index, &r);

      rnd_uv_scale = fmodf(r * 2.0f - 1.0f + rand_offset, 1.0f);
      rnd_uv_scale = fmodf(sin(rnd_uv_scale * 12.9898f) * 43758.5453f, 1.0f);

      uv_scale += mmd->rnd_uv_scale * rnd_uv_scale;
    }

    float totlen = 1.0f;
    if (mmd->fit_method == GP_TEX_FIT_STROKE) {
      totlen = 0.0f;
      for (int i = 1; i < gps->totpoints; i++) {
        totlen += len_v3v3(&gps->points[i - 1].x, &gps->points[i].x);
      }
    }

    for (int i = 0; i < gps->totpoints; i++) {
      bGPDspoint *pt = &gps->points[i];
      MDeformVert *dvert = gps->dvert != nullptr ? &gps->dvert[i] : nullptr;
      /* Verify point is part of vertex group. */
      float weight = get_modifier_point_weight(
          dvert, (mmd->flag & GP_TEX_INVERT_VGROUP) != 0, def_nr);
      if (weight < 0.0f) {
        continue;
      }

      pt->uv_fac /= totlen;
      pt->uv_fac *= uv_scale;
      pt->uv_fac += uv_offset;
      pt->uv_rot += mmd->alignment_rotation;
    }
  }
}

static void bake_modifier(Main * /*bmain*/,
                          Depsgraph *depsgraph,
                          GpencilModifierData *md,
                          Object *ob)
{
  generic_bake_deform_stroke(depsgraph, md, ob, false, deform_stroke);
}

static void foreach_ID_link(GpencilModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  TextureGpencilModifierData *mmd = (TextureGpencilModifierData *)md;

  walk(user_data, ob, (ID **)&mmd->material, IDWALK_CB_USER);
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  uiLayout *col;
  uiLayout *layout = panel->layout;

  PointerRNA *ptr = gpencil_modifier_panel_get_property_pointers(panel, nullptr);

  int mode = RNA_enum_get(ptr, "mode");

  uiLayoutSetPropSep(layout, true);

  uiItemR(layout, ptr, "mode", UI_ITEM_NONE, nullptr, ICON_NONE);

  if (ELEM(mode, STROKE, STROKE_AND_FILL)) {
    col = uiLayoutColumn(layout, false);
    uiItemR(col, ptr, "fit_method", UI_ITEM_NONE, IFACE_("Stroke Fit Method"), ICON_NONE);
    uiItemR(col, ptr, "uv_offset", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(col, ptr, "alignment_rotation", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(col, ptr, "uv_scale", UI_ITEM_NONE, IFACE_("Scale"), ICON_NONE);
  }

  if (mode == STROKE_AND_FILL) {
    uiItemS(layout);
  }

  if (ELEM(mode, FILL, STROKE_AND_FILL)) {
    col = uiLayoutColumn(layout, false);
    uiItemR(col, ptr, "fill_rotation", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(col, ptr, "fill_offset", UI_ITEM_NONE, IFACE_("Offset"), ICON_NONE);
    uiItemR(col, ptr, "fill_scale", UI_ITEM_NONE, IFACE_("Scale"), ICON_NONE);
  }

  gpencil_modifier_panel_end(layout, ptr);
}

static void mask_panel_draw(const bContext * /*C*/, Panel *panel)
{
  gpencil_modifier_masking_panel_draw(panel, true, true);
}

static void random_panel_draw(const bContext * /*C*/, Panel *panel)
{
  uiLayout *layout = panel->layout;

  PointerRNA *ptr = gpencil_modifier_panel_get_property_pointers(panel, NULL);
  uiLayoutSetPropSep(layout, true);

  uiItemR(layout, ptr, "rnd_uv_offset", UI_ITEM_NONE, IFACE_("Stroke Offset"), ICON_NONE);
  uiItemR(layout, ptr, "rnd_uv_scale", UI_ITEM_NONE, IFACE_("Stroke Scale"), ICON_NONE);
  uiItemR(layout, ptr, "rnd_fill_rot", UI_ITEM_NONE, IFACE_("Fill Rot"), ICON_NONE);
  uiItemR(layout, ptr, "rnd_fill_offset", UI_ITEM_NONE, IFACE_("Fil Offset"), ICON_NONE);
  uiItemR(layout, ptr, "rnd_fill_scale", UI_ITEM_NONE, IFACE_("Fill Scale"), ICON_NONE);
  uiItemR(layout, ptr, "seed", UI_ITEM_NONE, nullptr, ICON_NONE);

  gpencil_modifier_panel_end(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  PanelType *panel_type = gpencil_modifier_panel_register(
      region_type, eGpencilModifierType_Texture, panel_draw);
  gpencil_modifier_subpanel_register(
      region_type, "randomize", "Randomize", nullptr, random_panel_draw, panel_type);
  gpencil_modifier_subpanel_register(
      region_type, "mask", "Influence", nullptr, mask_panel_draw, panel_type);
}

GpencilModifierTypeInfo modifierType_Gpencil_Texture = {
    /*name*/ N_("TextureMapping"),
    /*struct_name*/ "TextureGpencilModifierData",
    /*struct_size*/ sizeof(TextureGpencilModifierData),
    /*type*/ eGpencilModifierTypeType_Gpencil,
    /*flags*/ eGpencilModifierTypeFlag_SupportsEditmode,

    /*copy_data*/ copy_data,

    /*deform_stroke*/ deform_stroke,
    /*generate_strokes*/ nullptr,
    /*bake_modifier*/ bake_modifier,
    /*remap_time*/ nullptr,

    /*init_data*/ init_data,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*panel_register*/ panel_register,
};
