/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(eevee_legacy_bloom_common)
PUSH_CONSTANT(float2, sourceBufferTexelSize)
PUSH_CONSTANT(float4, curveThreshold)
PUSH_CONSTANT(float, clampIntensity)
PUSH_CONSTANT(float2, baseBufferTexelSize)
PUSH_CONSTANT(float, sampleScale)
PUSH_CONSTANT(float3, bloomColor)
PUSH_CONSTANT(bool, bloomAddBase)
SAMPLER(0, sampler2D, sourceBuffer)
SAMPLER(1, sampler2D, baseBuffer)
FRAGMENT_OUT(0, float4, FragColor)
ADDITIONAL_INFO(gpu_fullscreen)
FRAGMENT_SOURCE("effect_bloom_frag.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_bloom_blit)
DEFINE("STEP_BLIT")
ADDITIONAL_INFO(eevee_legacy_bloom_common)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_bloom_blit_hq)
DEFINE("HIGH_QUALITY")
ADDITIONAL_INFO(eevee_legacy_bloom_blit)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_bloom_downsample)
DEFINE("STEP_DOWNSAMPLE")
ADDITIONAL_INFO(eevee_legacy_bloom_common)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_bloom_downsample_hq)
DEFINE("HIGH_QUALITY")
ADDITIONAL_INFO(eevee_legacy_bloom_downsample)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_bloom_upsample)
DEFINE("STEP_UPSAMPLE")
ADDITIONAL_INFO(eevee_legacy_bloom_common)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_bloom_upsample_hq)
DEFINE("HIGH_QUALITY")
ADDITIONAL_INFO(eevee_legacy_bloom_upsample)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_bloom_resolve)
DEFINE("STEP_RESOLVE")
ADDITIONAL_INFO(eevee_legacy_bloom_common)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_legacy_bloom_resolve_hq)
DEFINE("HIGH_QUALITY")
ADDITIONAL_INFO(eevee_legacy_bloom_resolve)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
