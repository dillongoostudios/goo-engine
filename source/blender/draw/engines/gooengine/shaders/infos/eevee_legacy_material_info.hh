/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "eevee_legacy_volume_info.hh"
#include "gpu_shader_create_info.hh"

/* For EEVEE Materials prepared in `eevee_shader_material_create_info_amend`,
 * differing permutations are generated based on material options.
 *
 * Sources, e.g.
 * -> datatoc_volumetric_vert_glsl
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
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_view)
GPU_SHADER_CREATE_END()

/* Volumetrics skips uniform bindings in `closure_type_lib`. */
GPU_SHADER_CREATE_INFO(eevee_legacy_material_empty_base_volume)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_view)
GPU_SHADER_CREATE_END()

/**** MATERIAL VERTEX SHADER PERMUTATIONS ****/

/* -------------------------------------------------------------------- */
/** \name Volumetric
 * \{ */

GPU_SHADER_CREATE_INFO(eevee_legacy_material_volumetric_vert)
ADDITIONAL_INFO(eevee_legacy_material_empty_base_volume)
VERTEX_OUT(legacy_volume_vert_geom_iface)
ADDITIONAL_INFO(draw_resource_id_varying)
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(eevee_legacy_material_volumetric_vert_no_geom)
ADDITIONAL_INFO(eevee_legacy_material_empty_base_volume)
BUILTINS(BuiltinBits::LAYER)
VERTEX_OUT(legacy_volume_vert_geom_iface)
VERTEX_OUT(legacy_volume_geom_frag_iface)
ADDITIONAL_INFO(draw_resource_id_varying)
GPU_SHADER_CREATE_END()
#endif

/** \} */

/* -------------------------------------------------------------------- */
/** \name World Shader
 * \{ */

GPU_SHADER_CREATE_INFO(eevee_legacy_material_world_vert)
ADDITIONAL_INFO(eevee_legacy_material_empty_base)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_surface_lib)
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
ADDITIONAL_INFO(draw_resource_id_varying)
VERTEX_IN(0, VEC2, pos)
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
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, VEC3, nor)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_mateiral_surface_vert_hair)
ADDITIONAL_INFO(eevee_legacy_material_surface_vert_common)
ADDITIONAL_INFO(eevee_legacy_surface_lib_hair)
ADDITIONAL_INFO(eevee_legacy_hair_lib)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_mateiral_surface_vert_pointcloud)
ADDITIONAL_INFO(draw_pointcloud)
ADDITIONAL_INFO(eevee_legacy_material_surface_vert_common)
ADDITIONAL_INFO(eevee_legacy_surface_lib_pointcloud)
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()

/**** MATERIAL GEOMETRY SHADER PERMUTATIONS ****/

/** \} */

/* -------------------------------------------------------------------- */
/** \name Volumetric
 * \{ */

GPU_SHADER_CREATE_INFO(eevee_legacy_material_volumetric_geom)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_view)
GEOMETRY_OUT(legacy_volume_geom_frag_iface)
GEOMETRY_LAYOUT(PrimitiveIn::TRIANGLES, PrimitiveOut::TRIANGLE_STRIP, 3)
ADDITIONAL_INFO(draw_resource_id_varying)
GPU_SHADER_CREATE_END()

/** \} */

/**** MATERIAL FRAGMENT SHADER PERMUTATIONS ****/

/* -------------------------------------------------------------------- */
/** \name Volumetric Shader
 * \{ */

GPU_SHADER_CREATE_INFO(eevee_legacy_material_volumetric_frag)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_resource_id_varying)
ADDITIONAL_INFO(eevee_legacy_volumetric_lib)
FRAGMENT_OUT(0, VEC4, volumeScattering)
FRAGMENT_OUT(1, VEC4, volumeExtinction)
FRAGMENT_OUT(2, VEC4, volumeEmissive)
FRAGMENT_OUT(3, VEC4, volumePhase)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name Pre-pass Shader
 * \{ */

/* Common info for all `prepass_frag` variants.
 * NOTE: `resource_id_out` is only declared on the shadow variants below (their FB has the R16UI
 * shadow-ID pool at attachment 1). The depth prepass draws into the main FB, whose attachment 1 is
 * the RG16 ssr_normal buffer: Metal refuses a UINT output against it at pipeline creation, so the
 * non-shadow variants declare a VEC2 `out_normal` at location 1 instead. The depth prepass has
 * colour writes disabled, so this output never reaches the buffer on any backend. */
GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_common)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(eevee_legacy_closure_eval_surface_lib)
GPU_SHADER_CREATE_END()

/* Common info for all `prepass_frag_opaque` variants. */
GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_opaque_common)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_common)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_opaque)
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_opaque_common)
FRAGMENT_OUT(1, VEC2, out_normal);
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_opaque_hair)
ADDITIONAL_INFO(eevee_legacy_surface_lib_hair)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_opaque_common)
ADDITIONAL_INFO(draw_hair)
FRAGMENT_OUT(1, VEC2, out_normal);
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_opaque_pointcloud)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_opaque_common)
ADDITIONAL_INFO(draw_pointcloud)
FRAGMENT_OUT(1, VEC2, out_normal);
GPU_SHADER_CREATE_END()

/* Common info for all `prepass_frag_alpha_hash` variants. */
GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_alpha_hash_common)
DEFINE("USE_ALPHA_HASH")
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_common)
PUSH_CONSTANT(FLOAT, alphaClipThreshold)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_alpha_hash)
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_alpha_hash_common)
FRAGMENT_OUT(1, VEC2, out_normal);
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_alpha_hash_hair)
ADDITIONAL_INFO(eevee_legacy_surface_lib_hair)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_alpha_hash_common)
ADDITIONAL_INFO(draw_hair)
FRAGMENT_OUT(1, VEC2, out_normal);
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_prepass_frag_alpha_hash_pointcloud)
ADDITIONAL_INFO(eevee_legacy_surface_lib_pointcloud)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_alpha_hash_common)
ADDITIONAL_INFO(draw_pointcloud)
FRAGMENT_OUT(1, VEC2, out_normal);
GPU_SHADER_CREATE_END()

/* Shadow Variants (Same as prepass but NO Normal Output) */

/* The shadow FB binds the R16UI shadow-ID pool at attachment 1 (eevee_shadows_cascade.cc /
 * eevee_shadows_cube.cc); sample_ID_texture (lights_lib.glsl, USE_SHADOW_ID) reads it to suppress
 * same-object self-shadowing, so every shadow variant must write `resource_id_out`. */
GPU_SHADER_CREATE_INFO(eevee_legacy_material_shadow_frag_opaque)
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_opaque_common)
FRAGMENT_OUT(1, UINT, resource_id_out)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_shadow_frag_opaque_hair)
ADDITIONAL_INFO(eevee_legacy_surface_lib_hair)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_opaque_common)
ADDITIONAL_INFO(draw_hair)
FRAGMENT_OUT(1, UINT, resource_id_out)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_shadow_frag_opaque_pointcloud)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_opaque_common)
ADDITIONAL_INFO(draw_pointcloud)
FRAGMENT_OUT(1, UINT, resource_id_out)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_shadow_frag_alpha_hash)
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_alpha_hash_common)
FRAGMENT_OUT(1, UINT, resource_id_out)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_shadow_frag_alpha_hash_hair)
ADDITIONAL_INFO(eevee_legacy_surface_lib_hair)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_alpha_hash_common)
ADDITIONAL_INFO(draw_hair)
FRAGMENT_OUT(1, UINT, resource_id_out)
GPU_SHADER_CREATE_END()


GPU_SHADER_CREATE_INFO(eevee_legacy_material_shadow_frag_alpha_hash_pointcloud)
ADDITIONAL_INFO(eevee_legacy_surface_lib_pointcloud)
ADDITIONAL_INFO(eevee_legacy_material_prepass_frag_alpha_hash_common)
ADDITIONAL_INFO(draw_pointcloud)
FRAGMENT_OUT(1, UINT, resource_id_out)
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
ADDITIONAL_INFO(eevee_legacy_volumetric_lib)
PUSH_CONSTANT(FLOAT, backgroundAlpha)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_surface_frag_opaque)
ADDITIONAL_INFO(eevee_legacy_material_surface_frag_common)
FRAGMENT_OUT(0, VEC4, outRadiance)
FRAGMENT_OUT(1, VEC2, ssrNormals)
FRAGMENT_OUT(2, VEC4, ssrData)
FRAGMENT_OUT(3, VEC3, sssIrradiance)
FRAGMENT_OUT(4, FLOAT, sssRadius)
FRAGMENT_OUT(5, VEC3, sssAlbedo)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_material_surface_frag_alpha_blend)
DEFINE("USE_ALPHA_BLEND")
ADDITIONAL_INFO(eevee_legacy_material_surface_frag_common)
FRAGMENT_OUT_DUAL(0, VEC4, outRadiance, SRC_0)
FRAGMENT_OUT_DUAL(0, VEC4, outTransmittance, SRC_1)
GPU_SHADER_CREATE_END()

/** \} */
