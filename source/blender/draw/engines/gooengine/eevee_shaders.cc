/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include "DRW_render.hh"

#include "BKE_lib_id.hh"
#include "BKE_node.hh"

#include "BLI_listbase.h"
#include "BLI_dynstr.h"
#include "BLI_string_utils.hh"
#include "BLI_math_vector.h"

#include "DNA_world_types.h"

#include "MEM_guardedalloc.h"

#include "GPU_capabilities.hh"
#include "GPU_context.hh"
#include "GPU_material.hh"
#include "GPU_shader.hh"

#include "NOD_shader.h"

#include "gooengine_engine.h"
#include "gooengine_private.hh"

#include "BKE_node_legacy_types.hh"

namespace blender::gooengine {

static struct {

  /* LookDev Materials */
  Material *glossy_mat;
  Material *diffuse_mat;

  Material *error_mat;

  World *default_world;

  /* Default Material */
  struct {
    bNodeTree *ntree;
    bNodeSocketValueRGBA *color_socket;
    bNodeSocketValueFloat *metallic_socket;
    bNodeSocketValueFloat *roughness_socket;
    bNodeSocketValueFloat *specular_socket;
  } surface;

  struct {
    bNodeTree *ntree;
    bNodeSocketValueRGBA *color_socket;
  } world;
} e_data = {nullptr}; /* Engine data */

/* *********** FUNCTIONS *********** */

/** \} */

Material *EEVEE_material_default_diffuse_get()
{
  if (!e_data.diffuse_mat) {
    Material *ma = static_cast<Material *>(BKE_id_new_nomain(ID_MA, "EEVEEE default diffuse"));

    bNodeTree *ntree = blender::bke::node_tree_add_tree_embedded(
        nullptr, &ma->id, "Shader Nodetree", ntreeType_Shader->idname);
    ma->use_nodes = true;

    bNode *bsdf = blender::bke::node_add_static_node(nullptr, *ntree, SH_NODE_BSDF_DIFFUSE);
    bNodeSocket *base_color = blender::bke::node_find_socket(*bsdf, SOCK_IN, "Color");
    copy_v3_fl(((bNodeSocketValueRGBA *)base_color->default_value)->value, 0.8f);

    bNode *output = blender::bke::node_add_static_node(nullptr, *ntree, SH_NODE_OUTPUT_MATERIAL);

    blender::bke::node_add_link(*ntree,
                              *bsdf,
                              *blender::bke::node_find_socket(*bsdf, SOCK_OUT, "BSDF"),
                              *output,
                              *blender::bke::node_find_socket(*output, SOCK_IN, "Surface"));

    blender::bke::node_set_active(*ntree, *output);
    e_data.diffuse_mat = ma;
  }
  return e_data.diffuse_mat;
}

Material *EEVEE_material_default_glossy_get()
{
  if (!e_data.glossy_mat) {
    Material *ma = static_cast<Material *>(BKE_id_new_nomain(ID_MA, "EEVEEE default metal"));

    bNodeTree *ntree = blender::bke::node_tree_add_tree_embedded(
        nullptr, &ma->id, "Shader Nodetree", ntreeType_Shader->idname);
    ma->use_nodes = true;

    bNode *bsdf = blender::bke::node_add_static_node(nullptr, *ntree, SH_NODE_BSDF_GLOSSY);
    bNodeSocket *base_color = blender::bke::node_find_socket(*bsdf, SOCK_IN, "Color");
    copy_v3_fl(((bNodeSocketValueRGBA *)base_color->default_value)->value, 1.0f);
    bNodeSocket *roughness = blender::bke::node_find_socket(*bsdf, SOCK_IN, "Roughness");
    ((bNodeSocketValueFloat *)roughness->default_value)->value = 0.0f;

    bNode *output = blender::bke::node_add_static_node(nullptr, *ntree, SH_NODE_OUTPUT_MATERIAL);

    blender::bke::node_add_link(*ntree,
                              *bsdf,
                              *blender::bke::node_find_socket(*bsdf, SOCK_OUT, "BSDF"),
                              *output,
                              *blender::bke::node_find_socket(*output, SOCK_IN, "Surface"));

    blender::bke::node_set_active(*ntree, *output);
    e_data.glossy_mat = ma;
  }
  return e_data.glossy_mat;
}

Material *EEVEE_material_default_error_get()
{
  if (!e_data.error_mat) {
    Material *ma = static_cast<Material *>(BKE_id_new_nomain(ID_MA, "EEVEEE default error"));

    bNodeTree *ntree = blender::bke::node_tree_add_tree_embedded(
        nullptr, &ma->id, "Shader Nodetree", ntreeType_Shader->idname);
    ma->use_nodes = true;

    /* Use emission and output material to be compatible with both World and Material. */
    bNode *bsdf = blender::bke::node_add_static_node(nullptr, *ntree, SH_NODE_EMISSION);
    bNodeSocket *color = blender::bke::node_find_socket(*bsdf, SOCK_IN, "Color");
    copy_v3_fl3(((bNodeSocketValueRGBA *)color->default_value)->value, 1.0f, 0.0f, 1.0f);

    bNode *output = blender::bke::node_add_static_node(nullptr, *ntree, SH_NODE_OUTPUT_MATERIAL);

    blender::bke::node_add_link(*ntree,
                              *bsdf,
                              *blender::bke::node_find_socket(*bsdf, SOCK_OUT, "Emission"),
                              *output,
                              *blender::bke::node_find_socket(*output, SOCK_IN, "Surface"));

    blender::bke::node_set_active(*ntree, *output);
    e_data.error_mat = ma;
  }
  return e_data.error_mat;
}

bNodeTree *EEVEE_shader_default_surface_nodetree(Material *ma)
{
  /* WARNING: This function is not threadsafe. Which is not a problem for the moment. */
  if (!e_data.surface.ntree) {
    bNodeTree *ntree = blender::bke::node_tree_add_tree(
        nullptr, "Shader Nodetree", ntreeType_Shader->idname);
    bNode *bsdf = blender::bke::node_add_static_node(nullptr, *ntree, SH_NODE_BSDF_PRINCIPLED);
    bNode *output = blender::bke::node_add_static_node(nullptr, *ntree, SH_NODE_OUTPUT_MATERIAL);
    bNodeSocket *bsdf_out = blender::bke::node_find_socket(*bsdf, SOCK_OUT, "BSDF");
    bNodeSocket *output_in = blender::bke::node_find_socket(*output, SOCK_IN, "Surface");
    blender::bke::node_add_link(*ntree, *bsdf, *bsdf_out, *output, *output_in);
    blender::bke::node_set_active(*ntree, *output);

    e_data.surface.color_socket = static_cast<bNodeSocketValueRGBA *>(
        blender::bke::node_find_socket(*bsdf, SOCK_IN, "Base Color")->default_value);
    e_data.surface.metallic_socket = static_cast<bNodeSocketValueFloat *>(
        blender::bke::node_find_socket(*bsdf, SOCK_IN, "Metallic")->default_value);
    e_data.surface.roughness_socket = static_cast<bNodeSocketValueFloat *>(
        blender::bke::node_find_socket(*bsdf, SOCK_IN, "Roughness")->default_value);
    e_data.surface.specular_socket = static_cast<bNodeSocketValueFloat *>(
        blender::bke::node_find_socket(*bsdf, SOCK_IN, "Specular IOR Level")->default_value);
    e_data.surface.ntree = ntree;
  }
  /* Update */
  copy_v3_fl3(e_data.surface.color_socket->value, ma->r, ma->g, ma->b);
  e_data.surface.metallic_socket->value = ma->metallic;
  e_data.surface.roughness_socket->value = ma->roughness;
  e_data.surface.specular_socket->value = ma->spec;

  return e_data.surface.ntree;
}

bNodeTree *EEVEE_shader_default_world_nodetree(World *wo)
{
  /* WARNING: This function is not threadsafe. Which is not a problem for the moment. */
  if (!e_data.world.ntree) {
    bNodeTree *ntree = blender::bke::node_tree_add_tree(
        nullptr, "Shader Nodetree", ntreeType_Shader->idname);
    bNode *bg = blender::bke::node_add_static_node(nullptr, *ntree, SH_NODE_BACKGROUND);
    bNode *output = blender::bke::node_add_static_node(nullptr, *ntree, SH_NODE_OUTPUT_WORLD);
    bNodeSocket *bg_out = blender::bke::node_find_socket(*bg, SOCK_OUT, "Background");
    bNodeSocket *output_in = blender::bke::node_find_socket(*output, SOCK_IN, "Surface");
    blender::bke::node_add_link(*ntree, *bg, *bg_out, *output, *output_in);
    blender::bke::node_set_active(*ntree, *output);

    e_data.world.color_socket = static_cast<bNodeSocketValueRGBA *>(
        blender::bke::node_find_socket(*bg, SOCK_IN, "Color")->default_value);
    e_data.world.ntree = ntree;
  }

  copy_v3_fl3(e_data.world.color_socket->value, wo->horr, wo->horg, wo->horb);

  return e_data.world.ntree;
}

World *EEVEE_world_default_get()
{
  if (e_data.default_world == nullptr) {
    e_data.default_world = static_cast<World *>(BKE_id_new_nomain(ID_WO, "EEVEEE default world"));
    copy_v3_fl(&e_data.default_world->horr, 0.0f);
    e_data.default_world->use_nodes = 0;
    e_data.default_world->nodetree = nullptr;
    BLI_listbase_clear(&e_data.default_world->gpumaterial);
  }
  return e_data.default_world;
}

/* Select create info configuration and base source for given material types.
 * Each configuration has an associated base source and create-info.
 * Source is provided separately, rather than via create-info as source is manipulated
 * by `eevee_shader_material_create_info_amend`.
 *
 * We also retain the previous behavior for ensuring library includes occur in the
 * correct order. */
static const char *eevee_get_vert_info(int options)
{
  const char *info_name = nullptr;

  /* Permutations */
  const bool is_hair = (options & (VAR_MAT_HAIR)) != 0;
  const bool is_point_cloud = (options & (VAR_MAT_POINTCLOUD)) != 0;

  if ((options & (VAR_WORLD_PROBE | VAR_WORLD_BACKGROUND)) != 0) {
    info_name = "eevee_legacy_material_world_vert";
  }
  else {
    if (is_hair) {
      info_name = "eevee_legacy_mateiral_surface_vert_hair";
    }
    else if (is_point_cloud) {
      info_name = "eevee_legacy_mateiral_surface_vert_pointcloud";
    }
    else {
      info_name = "eevee_legacy_material_surface_vert";
    }
  }

  return info_name;
}

static const char *eevee_get_geom_info(int options)
{
  const char *info_name = nullptr;
  if (GPU_backend_get_type() == GPU_BACKEND_METAL) {
    /* Geometry shading is unsupported in Metal. Vertex-shader based workarounds will instead
     * be injected where appropriate. For volumetric rendering, volumetric_vert_no_geom replaces
     * the default volumetric_vert + volumetric_geom combination.
     * See: `source/blender/gpu/intern/gpu_shader_create_info.cc` for further details. */
    return nullptr;
  }

  return info_name;
}

static const char *eevee_get_frag_info(int options)
{
  const char *info_name = nullptr;

  const bool is_alpha_hash = ((options & VAR_MAT_HASH) != 0);
  bool is_alpha_blend = ((options & VAR_MAT_BLEND) != 0);
  const bool is_hair = (options & (VAR_MAT_HAIR)) != 0;
  const bool is_point_cloud = (options & (VAR_MAT_POINTCLOUD)) != 0;

  if ((options & VAR_MAT_DEPTH) != 0) {
    /* -- PREPASS FRAG -
     * Select create info permutation for `prepass_frag`. */

    if (is_alpha_hash) {
      /* Alpha hash material variants. */
      if (is_hair) {
        info_name = "eevee_legacy_material_prepass_frag_alpha_hash_hair";
      }
      else if (is_point_cloud) {
        info_name = "eevee_legacy_material_prepass_frag_alpha_hash_pointcloud";
      }
      else {
        info_name = "eevee_legacy_material_prepass_frag_alpha_hash";
      }
    }
    else {
      /* Opaque material variants. */
      if (is_hair) {
        info_name = "eevee_legacy_material_prepass_frag_opaque_hair";
      }
      else if (is_point_cloud) {
        info_name = "eevee_legacy_material_prepass_frag_opaque_pointcloud";
      }
      else {
        info_name = "eevee_legacy_material_prepass_frag_opaque";
      }
    }
  }
  else {
    /* -- SURFACE FRAG --
     * Select create info permutation for `surface_frag`. */
    if (is_alpha_blend) {
      info_name = "eevee_legacy_material_surface_frag_alpha_blend";
    }
    else {
      info_name = "eevee_legacy_material_surface_frag_opaque";
    }
  }

  return info_name;
}

static const char *eevee_get_vert_source(int options)
{
  const char *source_name = nullptr;

  if ((options & (VAR_WORLD_PROBE | VAR_WORLD_BACKGROUND)) != 0) {
    source_name = "world_vert.glsl";
  }
  else {
    source_name = "surface_vert.glsl";
  }

  return source_name;
}

static const char *eevee_get_geom_source(int options)
{
  const char *source_name = nullptr;
  if (GPU_backend_get_type() == GPU_BACKEND_METAL) {
    /* Geometry shading is unsupported in Metal. Vertex-shader based workarounds will instead
     * be injected where appropriate. For volumetric rendering, volumetric_vert_no_geom replaces
     * the default volumetric_vert + volumetric_geom combination.
     * See: `source/blender/gpu/intern/gpu_shader_create_info.cc` for further details. */
    return nullptr;
  }

  return source_name;
}

static const char *eevee_get_frag_source(int options)
{
  const char *source_name = nullptr;

  if ((options & VAR_MAT_DEPTH) != 0) {
    /* -- PREPASS FRAG -
     * Select create info permutation for `prepass_frag`. */
    source_name = "prepass_frag.glsl";
  }
  else {
    /* -- SURFACE FRAG --
     * Select create info permutation for `surface_frag`. */
    source_name = "surface_frag.glsl";
  }

  return source_name;
}

static char *eevee_get_defines(int options)
{
  char *str = nullptr;

  DynStr *ds = BLI_dynstr_new();

  /* Global EEVEE defines included for CreateInfo shaders via `engine_eevee_shared_defines.hh` in
   * eevee_legacy_common_lib CreateInfo. */

  /* Defines which affect bindings are instead injected via CreateInfo permutations. To specify
   * new permutations, references to GPUShaderCreateInfo variants should be fetched in:
   * `eevee_get_vert/geom/frag_info(..)`
   *
   * CreateInfo's for EEVEE materials are declared in:
   * `eevee/shaders/infos/eevee_legacy_material_info.hh`
   *
   * This function should only contain defines which alter behavior, but do not affect shader
   * resources. */

  if ((options & VAR_WORLD_BACKGROUND) != 0) {
    BLI_dynstr_append(ds, "#define WORLD_BACKGROUND\n");
  }
  if ((options & VAR_MAT_VOLUME) != 0) {
    printf("IS VOLUME \n");
    BLI_dynstr_append(ds, "#define VOLUMETRICS\n");
  }
  if ((options & VAR_MAT_MESH) != 0) {
    BLI_dynstr_append(ds, "#define MESH_SHADER\n");
  }
  if ((options & VAR_MAT_DEPTH) != 0) {
    BLI_dynstr_append(ds, "#define DEPTH_SHADER\n");
  }
  if ((options & VAR_WORLD_PROBE) != 0) {
    BLI_dynstr_append(ds, "#define PROBE_CAPTURE\n");
  }
  if ((options & VAR_MAT_REFRACT) != 0) {
    BLI_dynstr_append(ds, "#define USE_REFRACTION\n");
  }
  if ((options & VAR_MAT_HOLDOUT) != 0) {
    BLI_dynstr_append(ds, "#define HOLDOUT\n");
  }
  if ((options & VAR_MAT_SHADOW_ID) != 0) {
    BLI_dynstr_append(ds, "#define USE_SHADOW_ID\n");
  }
  if ((options & VAR_MAT_SHADOW_ID) != 0) {
    BLI_dynstr_append(ds, "#define USE_SHADOW_ID\n");
  }

  str = BLI_dynstr_get_cstring(ds);
  BLI_dynstr_free(ds);

  return str;
}

static void eevee_material_post_eval(void * /*thunk*/, GPUMaterial *mat, GPUCodegenOutput *codegen)
{
  /* Fetch material-specific Create-info's and source. */
  uint64_t options = GPU_material_uuid_get(mat);

  const char *vert_info_name = eevee_get_vert_info(options);
  const char *geom_info_name = eevee_get_geom_info(options);
  const char *frag_info_name = eevee_get_frag_info(options);

  const char *vert_source = eevee_get_vert_source(options);
  const char *geom_source = eevee_get_geom_source(options);
  const char *frag_source = eevee_get_frag_source(options);
  char *defines = eevee_get_defines(options);
  
  printf("%s deffffffffffffff \n", defines);

  eevee_shader_material_create_info_amend(
      mat, codegen, vert_source, geom_source, frag_source, vert_info_name, geom_info_name, frag_info_name, defines);

  MEM_SAFE_FREE(defines);
}

static GPUMaterial *eevee_material_get_ex(
    Scene * /*scene*/, Material *ma, World *wo, int options, bool deferred)
{
  BLI_assert(ma || wo);
  const bool is_volume = (options & VAR_MAT_VOLUME) != 0;
  const bool is_default = (options & VAR_DEFAULT) != 0;

  GPUMaterial *mat = nullptr;
  GPUCodegenCallbackFn cbfn = &eevee_material_post_eval;

  if (ma) {
    printf(ma->id.name);
    printf(" \n");
    bNodeTree *ntree = !is_default ? ma->nodetree : EEVEE_shader_default_surface_nodetree(ma);
    mat = DRW_shader_from_material(
        ma, ntree, GPU_MAT_EEVEE_LEGACY, options, is_volume, deferred, cbfn, nullptr);
  }
  else {
    printf("world \n");
    bNodeTree *ntree = !is_default ? wo->nodetree : EEVEE_shader_default_world_nodetree(wo);
    mat = DRW_shader_from_world(
        wo, ntree, GPU_MAT_EEVEE_LEGACY, options, is_volume, deferred, cbfn, nullptr);
  }
  return mat;
}

GPUMaterial *EEVEE_material_default_get(Scene *scene, Material *ma, int options)
{
  Material *def_ma = (ma && (options & VAR_MAT_VOLUME)) ? BKE_material_default_volume() :
                                                          BKE_material_default_surface();
  BLI_assert(def_ma->use_nodes && def_ma->nodetree);
  printf("GET DEFAULT 1 \n");
  return eevee_material_get_ex(scene, def_ma, nullptr, options, false);
}

GPUMaterial *EEVEE_material_get(
    Instance *inst, Scene *scene, Material *ma, World *wo, int options)
{
  if ((ma && (!ma->use_nodes || !ma->nodetree)) || (wo && (!wo->use_nodes || !wo->nodetree))) {
    options |= VAR_DEFAULT;
  }

  if ((ma && ma->check_shadow_id)) {
    options |= VAR_MAT_SHADOW_ID;
  }

  /* Meh, implicit option. World probe cannot be deferred because they need
   * to be rendered immediately. */
  const bool deferred = (options & VAR_WORLD_PROBE) == 0;

  printf(" GET  \n");
  GPUMaterial *mat = eevee_material_get_ex(scene, ma, wo, options, deferred);

  int status = GPU_material_status(mat);
  /* Return null material and bypass drawing for volume shaders. */
  if ((options & VAR_MAT_VOLUME) && status != GPU_MAT_SUCCESS) {
    return nullptr;
  }
  switch (status) {
    case GPU_MAT_SUCCESS: {
      /* Determine optimization status for remaining compilations counter. */
      int optimization_status = GPU_material_optimization_status(mat);
      if (optimization_status == GPU_MAT_OPTIMIZATION_QUEUED) {
        inst->goo_data->g_data->queued_optimise_shaders_count++;
      }
      break;
    }
    case GPU_MAT_QUEUED: {
      inst->goo_data->g_data->queued_shaders_count++;
      GPUMaterial *default_mat = EEVEE_material_default_get(scene, ma, options);
      /* Mark pending material with its default material for future cache warming. */
      GPU_material_set_default(mat, default_mat);
      /* Return default material. */
      mat = default_mat;
      break;
    }
    case GPU_MAT_FAILED:
    default:
      printf(" GET DEFAULT IT FAILED \n");
      ma = EEVEE_material_default_error_get();
      mat = eevee_material_get_ex(scene, ma, nullptr, options, false);
      break;
  }
  /* Returned material should be ready to be drawn. */
  BLI_assert(GPU_material_status(mat) == GPU_MAT_SUCCESS);
  return mat;
}

void EEVEE_shaders_free()
{

  if (e_data.default_world) {
    BKE_id_free(nullptr, e_data.default_world);
    e_data.default_world = nullptr;
  }
  if (e_data.glossy_mat) {
    BKE_id_free(nullptr, e_data.glossy_mat);
    e_data.glossy_mat = nullptr;
  }
  if (e_data.diffuse_mat) {
    BKE_id_free(nullptr, e_data.diffuse_mat);
    e_data.diffuse_mat = nullptr;
  }
  if (e_data.error_mat) {
    BKE_id_free(nullptr, e_data.error_mat);
    e_data.error_mat = nullptr;
  }
  if (e_data.surface.ntree) {
    blender::bke::node_tree_free_embedded_tree(e_data.surface.ntree);
    MEM_freeN(e_data.surface.ntree);
    e_data.surface.ntree = nullptr;
  }
  if (e_data.world.ntree) {
    blender::bke::node_tree_free_embedded_tree(e_data.world.ntree);
    MEM_freeN(e_data.world.ntree);
    e_data.world.ntree = nullptr;
  }
}

}  // namespace blender::gooengine
