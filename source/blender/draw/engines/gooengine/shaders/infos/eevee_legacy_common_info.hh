/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_create_info.hh"

#pragma once

/* EEVEE defines. */
GPU_SHADER_CREATE_INFO(eevee_legacy_defines_info)
TYPEDEF_SOURCE("engine_eevee_shared_defines.hh")
GPU_SHADER_CREATE_END()

/* Only specifies bindings for common_uniform_lib.glsl. */
GPU_SHADER_CREATE_INFO(eevee_legacy_common_lib)
TYPEDEF_SOURCE("engine_eevee_shared_defines.hh")
TYPEDEF_SOURCE("engine_eevee_legacy_shared.hh")
UNIFORM_BUF(1, CommonUniformBlock, common_block)
GPU_SHADER_CREATE_END()

/* Only specifies bindings for irradiance_lib.glsl. */
GPU_SHADER_CREATE_INFO(eevee_legacy_irradiance_lib)
ADDITIONAL_INFO(eevee_legacy_common_lib)
SAMPLER(1, sampler2DArray, irradianceGrid)
GPU_SHADER_CREATE_END()

/* Utiltex Lib. */
GPU_SHADER_CREATE_INFO(eevee_legacy_common_utiltex_lib)
SAMPLER(2, sampler2DArray, utilTex)
GPU_SHADER_CREATE_END()

/* Ray-trace lib. */
GPU_SHADER_CREATE_INFO(eevee_legacy_raytrace_lib)
ADDITIONAL_INFO(draw_view_goo)
ADDITIONAL_INFO(eevee_legacy_common_lib)
SAMPLER(3, sampler2D, maxzBuffer)
SAMPLER(4, Depth2DArray, planarDepth)
GPU_SHADER_CREATE_END()

/* Ambient occlusion lib. */
GPU_SHADER_CREATE_INFO(eevee_legacy_ambient_occlusion_lib)
ADDITIONAL_INFO(eevee_legacy_raytrace_lib)
SAMPLER(5, sampler2D, horizonBuffer)
GPU_SHADER_CREATE_END()

/* Light-probe lib. */
GPU_SHADER_CREATE_INFO(eevee_legacy_lightprobe_lib)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
ADDITIONAL_INFO(eevee_legacy_ambient_occlusion_lib)
ADDITIONAL_INFO(eevee_legacy_irradiance_lib)
SAMPLER(6, sampler2DArray, probePlanars)
SAMPLER(7, FloatCubeArray, probeCubes)
UNIFORM_BUF(2, ProbeBlock, probe_block)
UNIFORM_BUF(3, GridBlock, grid_block)
UNIFORM_BUF(4, PlanarBlock, planar_block)
GPU_SHADER_CREATE_END()

/* LTC Lib. */
GPU_SHADER_CREATE_INFO(eevee_legacy_ltc_lib)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
GPU_SHADER_CREATE_END()

/* Lights lib. */
GPU_SHADER_CREATE_INFO(eevee_legacy_lights_lib)
ADDITIONAL_INFO(eevee_legacy_ltc_lib)
ADDITIONAL_INFO(eevee_legacy_raytrace_lib)
UNIFORM_BUF(5, ShadowBlock, shadow_block)
UNIFORM_BUF(6, LightBlock, light_block)
PUSH_CONSTANT(int4, light_groups_in)
PUSH_CONSTANT(int4, light_group_shadows_in)
SAMPLER(8, Shadow2DArray, shadowCubeTexture)
SAMPLER(9, Shadow2DArray, shadowCascadeTexture)
/* GooEngine: Use slots 23, 24 as others are already taken. */
SAMPLER(23, isampler2DArray, shadowCubeIDTexture)
SAMPLER(24, isampler2DArray, shadowCascadeIDTexture)
GPU_SHADER_CREATE_END()

/* Hair lib. */
GPU_SHADER_CREATE_INFO(eevee_legacy_hair_lib)
ADDITIONAL_INFO(draw_hair_goo)
SAMPLER(10, UintBuffer, hairStrandBuffer)
SAMPLER(11, UintBuffer, hairStrandSegBuffer)
GPU_SHADER_CREATE_END()

/* SSR Lib */
GPU_SHADER_CREATE_INFO(eevee_legacy_ssr_lib)
ADDITIONAL_INFO(eevee_legacy_raytrace_lib)
PUSH_CONSTANT(float, refractionDepth)
SAMPLER(12, sampler2D, refractColorBuffer)
GPU_SHADER_CREATE_END()

/* renderpass_lib */
GPU_SHADER_CREATE_INFO(eevee_legacy_renderpass_lib)
ADDITIONAL_INFO(eevee_legacy_common_lib)
UNIFORM_BUF(12, RenderpassBlock, renderpass_block)
GPU_SHADER_CREATE_END()

/* Reflection lib */
GPU_SHADER_CREATE_INFO(eevee_legacy_reflection_lib)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_view_goo)
PUSH_CONSTANT(int2, halfresOffset)
GPU_SHADER_CREATE_END()

/* eevee_legacy_cryptomatte_lib. */
GPU_SHADER_CREATE_INFO(eevee_legacy_cryptomatte_lib)
ADDITIONAL_INFO(draw_curves_infos_goo)
GPU_SHADER_CREATE_END()

/* ----- SURFACE LIB ----- */
/* Surface lib has several different components depending on how it is used.
* Differing root permutations need to be generated and included depending
* on use-case. */

/* SURFACE LIB INTERFACES */
GPU_SHADER_INTERFACE_INFO(eevee_legacy_surface_common_iface)
SMOOTH(float3, worldPosition)
SMOOTH(float3, viewPosition)
SMOOTH(float3, worldNormal)
SMOOTH(float3, viewNormal)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_INTERFACE_INFO(eevee_legacy_surface_point_cloud_iface)
SMOOTH(float, pointRadius)
SMOOTH(float, pointPosition)
FLAT(int, pointID)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_INTERFACE_INFO(eevee_legacy_surface_hair_iface)
SMOOTH(float3, hairTangent)
SMOOTH(float, hairThickTime)
SMOOTH(float, hairThickness)
SMOOTH(float, hairTime)
FLAT(int, hairStrandID)
SMOOTH(float2, hairBary)
GPU_SHADER_INTERFACE_END()

/* Surface lib components */
GPU_SHADER_CREATE_INFO(eevee_legacy_surface_lib_common)
VERTEX_OUT(eevee_legacy_surface_common_iface)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_surface_lib_hair)
DEFINE("USE_SURFACE_LIB_HAIR")
/* Hair still uses the common interface as well. */
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
VERTEX_OUT(eevee_legacy_surface_hair_iface)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_surface_lib_pointcloud)
DEFINE("USE_SURFACE_LIB_POINTCLOUD")
/* Point-cloud still uses the common interface as well. */
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
VERTEX_OUT(eevee_legacy_surface_point_cloud_iface)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_surface_lib_step_resolve)
DEFINE("STEP_RESOLVE")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_surface_lib_step_raytrace)
DEFINE("STEP_RAYTRACE")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_surface_lib_world_background)
DEFINE("WORLD_BACKGROUND")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_surface_lib_step_probe_capture)
DEFINE("PROBE_CAPTURE")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_surface_lib_use_barycentrics)
DEFINE("USE_BARYCENTRICS")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_surface_lib_codegen_lib)
DEFINE("CODEGEN_LIB")
GPU_SHADER_CREATE_END()

/* Surface lib permutations. */

/* Basic - lookdev world frag */
GPU_SHADER_CREATE_INFO(eevee_legacy_surface_lib_lookdev)
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
GPU_SHADER_CREATE_END()

/** Closure evaluation libraries **/

/* eevee_legacy_closure_type_lib */
GPU_SHADER_CREATE_INFO(eevee_legacy_closure_type_lib)
PUSH_CONSTANT(int, outputSsrId)
PUSH_CONSTANT(int, outputSssId)
GPU_SHADER_CREATE_END()

/* eevee_legacy_closure_eval_lib */
GPU_SHADER_CREATE_INFO(eevee_legacy_closure_eval_lib)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
ADDITIONAL_INFO(eevee_legacy_lights_lib)
ADDITIONAL_INFO(eevee_legacy_lightprobe_lib)
GPU_SHADER_CREATE_END()

/* eevee_legacy_closure_eval_diffuse_lib */
GPU_SHADER_CREATE_INFO(eevee_legacy_closure_eval_diffuse_lib)
ADDITIONAL_INFO(eevee_legacy_lights_lib)
ADDITIONAL_INFO(eevee_legacy_lightprobe_lib)
ADDITIONAL_INFO(eevee_legacy_ambient_occlusion_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_lib)
ADDITIONAL_INFO(eevee_legacy_renderpass_lib)
GPU_SHADER_CREATE_END()

/* eevee_legacy_closure_eval_glossy_lib */
GPU_SHADER_CREATE_INFO(eevee_legacy_closure_eval_glossy_lib)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
ADDITIONAL_INFO(eevee_legacy_lights_lib)
ADDITIONAL_INFO(eevee_legacy_lightprobe_lib)
ADDITIONAL_INFO(eevee_legacy_ambient_occlusion_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_lib)
ADDITIONAL_INFO(eevee_legacy_renderpass_lib)
GPU_SHADER_CREATE_END()

/* eevee_legacy_closure_eval_refraction_lib */
GPU_SHADER_CREATE_INFO(eevee_legacy_closure_eval_refraction_lib)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
ADDITIONAL_INFO(eevee_legacy_lights_lib)
ADDITIONAL_INFO(eevee_legacy_lightprobe_lib)
ADDITIONAL_INFO(eevee_legacy_ambient_occlusion_lib)
ADDITIONAL_INFO(eevee_legacy_ssr_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_lib)
ADDITIONAL_INFO(eevee_legacy_renderpass_lib)
GPU_SHADER_CREATE_END()

/* eevee_legacy_closure_eval_translucent_lib */
GPU_SHADER_CREATE_INFO(eevee_legacy_closure_eval_translucent_lib)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
ADDITIONAL_INFO(eevee_legacy_lights_lib)
ADDITIONAL_INFO(eevee_legacy_lightprobe_lib)
ADDITIONAL_INFO(eevee_legacy_ambient_occlusion_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_lib)
ADDITIONAL_INFO(eevee_legacy_renderpass_lib)
GPU_SHADER_CREATE_END()

/* eevee_legacy_closure_eval_surface_lib */
GPU_SHADER_CREATE_INFO(eevee_legacy_closure_eval_surface_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_diffuse_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_glossy_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_refraction_lib)
ADDITIONAL_INFO(eevee_legacy_closure_eval_translucent_lib)
ADDITIONAL_INFO(eevee_legacy_renderpass_lib)
GPU_SHADER_CREATE_END()
