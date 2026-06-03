/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_create_info.hh"

#pragma once

/* Volumetric iface. */
GPU_SHADER_NAMED_INTERFACE_INFO(legacy_volume_vert_geom_iface, volumetric_vert_iface)
SMOOTH(float4, vPos)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_NAMED_INTERFACE_INFO(legacy_volume_geom_frag_iface, volumetric_geom_iface)
FLAT(int, slice)
GPU_SHADER_INTERFACE_END()

/* EEVEE_shaders_volumes_clear_sh_get */
GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_clear)
DEFINE("STANDALONE")
DEFINE("VOLUMETRICS")
DEFINE("CLEAR")
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_view_goo)
ADDITIONAL_INFO(draw_resource_id_varying)
ADDITIONAL_INFO(eevee_legacy_volumetric_lib)
VERTEX_SOURCE("volumetric_vert.glsl")
GEOMETRY_SOURCE("volumetric_geom.glsl")
FRAGMENT_SOURCE("volumetric_frag.glsl")
VERTEX_OUT(legacy_volume_vert_geom_iface)
GEOMETRY_OUT(legacy_volume_geom_frag_iface)
GEOMETRY_LAYOUT(PrimitiveIn::TRIANGLES, PrimitiveOut::TRIANGLE_STRIP, 3)
FRAGMENT_OUT(0, float4, volumeScattering)
FRAGMENT_OUT(1, float4, volumeExtinction)
FRAGMENT_OUT(2, float4, volumeEmissive)
FRAGMENT_OUT(3, float4, volumePhase)
DO_STATIC_COMPILATION()
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
/* Non-geometry shader equivalent for multilayered rendering.
 * NOTE: Layer selection can be done in vertex shader, and thus
 * vertex shader emits both vertex and geometry shader output
 * interfaces. */
GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_clear_no_geom)
DEFINE("STANDALONE")
DEFINE("VOLUMETRICS")
DEFINE("CLEAR")
BUILTINS(BuiltinBits::LAYER)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_view_goo)
ADDITIONAL_INFO(draw_resource_id_varying)
ADDITIONAL_INFO(eevee_legacy_volumetric_lib)
VERTEX_SOURCE("volumetric_vert.glsl")
FRAGMENT_SOURCE("volumetric_frag.glsl")
VERTEX_OUT(legacy_volume_vert_geom_iface)
VERTEX_OUT(legacy_volume_geom_frag_iface)
FRAGMENT_OUT(0, float4, volumeScattering)
FRAGMENT_OUT(1, float4, volumeExtinction)
FRAGMENT_OUT(2, float4, volumeEmissive)
FRAGMENT_OUT(3, float4, volumePhase)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()
#endif

/* EEVEE_shaders_volumes_scatter_sh_get */
GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_scatter_common)
DEFINE("STANDALONE")
DEFINE("VOLUMETRICS")
DEFINE("VOLUME_SHADOW")
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_view_goo)
ADDITIONAL_INFO(draw_resource_id_varying)
ADDITIONAL_INFO(eevee_legacy_volumetric_lib)
/* NOTE: Unique sampler IDs assigned for consistency between library includes,
* and to avoid unique assignment collision validation error.
* However, resources will be auto assigned locations within shader usage. */
SAMPLER(15, Float3D, volumeScattering)
SAMPLER(16, Float3D, volumeExtinction)
SAMPLER(17, Float3D, volumeEmission)
SAMPLER(18, Float3D, volumePhase)
SAMPLER(19, Float3D, historyScattering)
SAMPLER(20, Float3D, historyTransmittance)

FRAGMENT_OUT(0, float4, outScattering)
FRAGMENT_OUT(1, float4, outTransmittance)
VERTEX_SOURCE("volumetric_vert.glsl")
FRAGMENT_SOURCE("volumetric_scatter_frag.glsl")
VERTEX_OUT(legacy_volume_vert_geom_iface)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_scatter)
ADDITIONAL_INFO(eevee_legacy_volumes_scatter_common)
GEOMETRY_SOURCE("volumetric_geom.glsl")
GEOMETRY_OUT(legacy_volume_geom_frag_iface)
GEOMETRY_LAYOUT(PrimitiveIn::TRIANGLES, PrimitiveOut::TRIANGLE_STRIP, 3)
DO_STATIC_COMPILATION()
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_scatter_no_geom)
ADDITIONAL_INFO(eevee_legacy_volumes_scatter_common)
BUILTINS(BuiltinBits::LAYER)
VERTEX_OUT(legacy_volume_geom_frag_iface)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()
#endif

/* EEVEE_shaders_volumes_scatter_with_lights_sh_get */
GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_scatter_with_lights_common)
DEFINE("VOLUME_LIGHTING")
DEFINE("IRRADIANCE_HL2")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_scatter_with_lights)
ADDITIONAL_INFO(eevee_legacy_volumes_scatter_with_lights_common)
ADDITIONAL_INFO(eevee_legacy_volumes_scatter)
DO_STATIC_COMPILATION()
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_scatter_with_lights_no_geom)
ADDITIONAL_INFO(eevee_legacy_volumes_scatter_with_lights_common)
ADDITIONAL_INFO(eevee_legacy_volumes_scatter_no_geom)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()
#endif

/* EEVEE_shaders_volumes_integration_sh_get */
GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_integration_common)
DEFINE("STANDALONE")
DEFINE("VOLUMETRICS")
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_view_goo)
ADDITIONAL_INFO(eevee_legacy_volumetric_lib)
ADDITIONAL_INFO(draw_resource_id_varying)
/* NOTE: Unique sampler IDs assigned for consistency between library includes,
* and to avoid unique assignment collision validation error.
* However, resources will be auto assigned locations within shader usage. */
SAMPLER(20, Float3D, volumeScattering)
SAMPLER(21, Float3D, volumeExtinction)
VERTEX_OUT(legacy_volume_vert_geom_iface)
VERTEX_SOURCE("volumetric_vert.glsl")
FRAGMENT_SOURCE("volumetric_integration_frag.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_integration_common_opti)
DEFINE("USE_VOLUME_OPTI")
IMAGE(0, GPU_R11F_G11F_B10F, write, Float3D, finalScattering_img)
IMAGE(1, GPU_R11F_G11F_B10F, write, Float3D, finalTransmittance_img)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_integration_common_no_opti)
FRAGMENT_OUT(0, float3, finalScattering)
FRAGMENT_OUT(1, float3, finalTransmittance)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_integration_common_geom)
ADDITIONAL_INFO(eevee_legacy_volumes_integration_common)
GEOMETRY_SOURCE("volumetric_geom.glsl")
GEOMETRY_OUT(legacy_volume_geom_frag_iface)
GEOMETRY_LAYOUT(PrimitiveIn::TRIANGLES, PrimitiveOut::TRIANGLE_STRIP, 3)
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_integration_common_no_geom)
ADDITIONAL_INFO(eevee_legacy_volumes_integration_common)
BUILTINS(BuiltinBits::LAYER)
VERTEX_OUT(legacy_volume_geom_frag_iface)
GPU_SHADER_CREATE_END()
#endif

GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_integration)
ADDITIONAL_INFO(eevee_legacy_volumes_integration_common_geom)
ADDITIONAL_INFO(eevee_legacy_volumes_integration_common_no_opti)
DO_STATIC_COMPILATION()
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_integration_OPTI)
ADDITIONAL_INFO(eevee_legacy_volumes_integration_common_geom)
ADDITIONAL_INFO(eevee_legacy_volumes_integration_common_opti)
DO_STATIC_COMPILATION()
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_integration_no_geom)
ADDITIONAL_INFO(eevee_legacy_volumes_integration_common_no_geom)
ADDITIONAL_INFO(eevee_legacy_volumes_integration_common_no_opti)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_integration_OPTI_no_geom)
ADDITIONAL_INFO(eevee_legacy_volumes_integration_common_no_geom)
ADDITIONAL_INFO(eevee_legacy_volumes_integration_common_opti)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()
#endif

/* EEVEE_shaders_volumes_resolve_sh_get */
GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_resolve_common)
ADDITIONAL_INFO(gpu_fullscreen)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_view_goo)
ADDITIONAL_INFO(eevee_legacy_volumetric_lib)
SAMPLER(0, Depth2D, inSceneDepth)
FRAGMENT_SOURCE("volumetric_resolve_frag.glsl")
AUTO_RESOURCE_LOCATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_resolve)
ADDITIONAL_INFO(eevee_legacy_volumes_resolve_common)
FRAGMENT_OUT_DUAL(0, float4, FragColor0, SRC_0)
FRAGMENT_OUT_DUAL(0, float4, FragColor1, SRC_1)
AUTO_RESOURCE_LOCATION()
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_resolve_accum)
DEFINE("VOLUMETRICS_ACCUM")
ADDITIONAL_INFO(eevee_legacy_volumes_resolve_common)
FRAGMENT_OUT(0, float4, FragColor0)
FRAGMENT_OUT(1, float4, FragColor1)
AUTO_RESOURCE_LOCATION()
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

/* EEVEE_shaders_volumes_accum_sh_get */
GPU_SHADER_CREATE_INFO(eevee_legacy_volumes_accum)
ADDITIONAL_INFO(gpu_fullscreen)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(draw_view_goo)
ADDITIONAL_INFO(eevee_legacy_volumetric_lib)
FRAGMENT_OUT(0, float4, FragColor0)
FRAGMENT_OUT(1, float4, FragColor1)
FRAGMENT_SOURCE("volumetric_accum_frag.glsl")
AUTO_RESOURCE_LOCATION()
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
