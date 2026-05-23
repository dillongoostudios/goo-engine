/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_create_info.hh"

/* EEVEE_shaders_shadow_sh_get */
GPU_SHADER_CREATE_INFO(eevee_legacy_shader_shadow)
ADDITIONAL_INFO(draw_view_goo)
ADDITIONAL_INFO(draw_modelmat_goo)
ADDITIONAL_INFO(draw_curves_infos_goo)
ADDITIONAL_INFO(eevee_legacy_hair_lib)
ADDITIONAL_INFO(eevee_legacy_surface_lib_common)
ADDITIONAL_INFO(eevee_legacy_surface_lib_hair)
VERTEX_IN(0, float3, pos)
VERTEX_IN(1, float3, nor)
VERTEX_SOURCE("shadow_vert.glsl")
FRAGMENT_SOURCE("shadow_frag.glsl")
AUTO_RESOURCE_LOCATION()
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

/* EEVEE_shaders_shadow_accum_sh_get */
GPU_SHADER_CREATE_INFO(eevee_legacy_shader_shadow_accum)
ADDITIONAL_INFO(gpu_fullscreen)
ADDITIONAL_INFO(draw_view_goo)
ADDITIONAL_INFO(eevee_legacy_common_lib)
ADDITIONAL_INFO(eevee_legacy_common_utiltex_lib)
ADDITIONAL_INFO(eevee_legacy_lights_lib)
FRAGMENT_SOURCE("shadow_accum_frag.glsl")
SAMPLER(0, Depth2D, depthBuffer)
FRAGMENT_OUT(0, float4, FragColor)
AUTO_RESOURCE_LOCATION()
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
