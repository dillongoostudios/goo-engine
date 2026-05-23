/* SPDX-FileCopyrightText: 2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_glsl_cpp_stubs.hh"

/* Prototype of functions to implement to load attributes data.
 * Implementation changes based on object data type. */

#ifndef GPU_METAL /* MSL does not require prototypes. */
float3 attr_load_orco(float4 orco);
float4 attr_load_tangent(float4 tangent);
float4 attr_load_vec4(float4 attr);
float3 attr_load_vec3(float3 attr);
float2 attr_load_vec2(float2 attr);
float attr_load_float(float attr);

float3 attr_load_orco(samplerBuffer orco);
float4 attr_load_tangent(samplerBuffer tangent);
float4 attr_load_vec4(samplerBuffer attr);
float3 attr_load_vec3(samplerBuffer attr);
float2 attr_load_vec2(samplerBuffer attr);
float attr_load_float(samplerBuffer attr);

float3 attr_load_orco(sampler3D orco);
float4 attr_load_tangent(sampler3D tangent);
float4 attr_load_vec4(sampler3D tex);
float3 attr_load_vec3(sampler3D tex);
float2 attr_load_vec2(sampler3D tex);
float attr_load_float(sampler3D tex);

float attr_load_temperature_post(float attr);
float4 attr_load_color_post(float4 attr);
float4 attr_load_uniform(float4 attr, const uint attr_hash);
#endif
