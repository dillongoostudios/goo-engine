/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_create_info.hh"

/* For EEVEE Materials prepared in `eevee_shader_material_create_info_amend`,
 * differing permutations are generated based on material options.
 *
 * Sources, e.g.
 * -> datatoc_world_vert_glsl
 * -> datatoc_surface_vert_glsl
 *
 * Are not included in the create-infos, but should have a corresponding
 * Create info block, which defines bindings and other library requirements.
 */

/*** EMPTY EEVEE STUB COMMON INCLUDES following 'eevee_empty.glsl' and
 * 'eevee_empty_volume.glsl'****/
GPU_SHADER_CREATE_INFO(eevee_legacy_material_empty_base)
ADDITIONAL_INFO(eevee_legacy_closure_type_lib)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_modelmat_goo)
ADDITIONAL_INFO(draw_view_goo)
GPU_SHADER_CREATE_END()

/* Volumetrics skips uniform bindings in `closure_type_lib`. */
GPU_SHADER_CREATE_INFO(eevee_legacy_material_empty_base_volume)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_modelmat_goo)
ADDITIONAL_INFO(draw_view_goo)
GPU_SHADER_CREATE_END()

/**** MATERIAL VERTEX SHADER PERMUTATIONS ****/

/* -------------------------------------------------------------------- */
/** \name World Shader
 * \{ */

GPU_SHADER_CREATE_INFO(eevee_legacy_material_world_vert)
ADDITIONAL_INFO(eevee_legacy_material_empty_base)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_surface_lib)
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
ADDITIONAL_INFO(draw_resource_id_varying)
VERTEX_IN(0, float2, pos)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name Surface Shader
 * \{ */

GPU_SHADER_CREATE_INFO(eevee_legacy_material_surface_vert_common)
ADDITIONAL_INFO(eevee_legacy_material_empty_base)
ADDITIONAL_INFO(draw_resource_id_varying)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_surface_lib)
/* Planar reflections assigns to gl_ClipDistance via surface_vert.glsl. */
DEFINE("USE_CLIP_PLANES")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_surface_vert)
ADDITIONAL_INFO(eevee_legacy_material_surface_vert_common)
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
VERTEX_IN(0, float3, pos)
VERTEX_IN(1, float3, nor)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_mateiral_surface_vert_hair)
ADDITIONAL_INFO(eevee_legacy_material_surface_vert_common)
ADDITIONAL_INFO(eevee_legacy_surface_lib_hair)
ADDITIONAL_INFO(eevee_legacy_hair_lib)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_mateiral_surface_vert_pointcloud)
ADDITIONAL_INFO(draw_pointcloud_goo)
ADDITIONAL_INFO(eevee_legacy_material_surface_vert_common)
ADDITIONAL_INFO(eevee_legacy_surface_lib_pointcloud)
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()

/**** MATERIAL FRAGMENT SHADER PERMUTATIONS ****/

/* -------------------------------------------------------------------- */
/** \name Pre-pass Shader
 * \{ */

/* Common info for all `prepass_frag` variants. */
GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_common)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
FRAGMENT_OUT(1, uint, resource_id_out)
ADDITIONAL_INFO(draw_view_goo)
ADDITIONAL_INFO(eevee_legacy_closure_eval_surface_lib)
GPU_SHADER_CREATE_END()

/* Common info for all `prepass_frag_opaque` variants. */
GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_opaque_common)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_common)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_opaque)
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_opaque_common)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_opaque_hair)
ADDITIONAL_INFO(eevee_legacy_surface_lib_hair)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_opaque_common)
ADDITIONAL_INFO(draw_hair_goo)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_opaque_pointcloud)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_opaque_common)
ADDITIONAL_INFO(draw_pointcloud_goo)
GPU_SHADER_CREATE_END()

/* Common info for all `prepass_frag_alpha_hash` variants. */
GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_alpha_hash_common)
DEFINE("USE_ALPHA_HASH")
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_common)
PUSH_CONSTANT(float, alphaClipThreshold)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_alpha_hash)
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_alpha_hash_common)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_alpha_hash_hair)
ADDITIONAL_INFO(eevee_legacy_surface_lib_hair)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_alpha_hash_common)
ADDITIONAL_INFO(draw_hair_goo)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_alpha_hash_pointcloud)
ADDITIONAL_INFO(eevee_legacy_surface_lib_pointcloud)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_alpha_hash_common)
ADDITIONAL_INFO(draw_pointcloud_goo)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name Surface Shader
 * \{ */

GPU_SHADER_CREATE_INFO(eevee_legacy_material_surface_frag_common)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_surface_lib)
ADDITIONAL_INFO(eevee_legacy_renderpass_lib)
PUSH_CONSTANT(float, backgroundAlpha)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_surface_frag_opaque)
ADDITIONAL_INFO(eevee_legacy_material_surface_frag_common)
FRAGMENT_OUT(0, float4, outRadiance)
FRAGMENT_OUT(1, float2, ssrNormals)
FRAGMENT_OUT(2, float4, ssrData)
FRAGMENT_OUT(3, float3, sssIrradiance)
FRAGMENT_OUT(4, float, sssRadius)
FRAGMENT_OUT(5, float3, sssAlbedo)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_surface_frag_alpha_blend)
DEFINE("USE_ALPHA_BLEND")
ADDITIONAL_INFO(eevee_legacy_material_surface_frag_common)
FRAGMENT_OUT_DUAL(0, float4, outRadiance, SRC_0)
FRAGMENT_OUT_DUAL(0, float4, outTransmittance, SRC_1)
GPU_SHADER_CREATE_END()

/** \} */
