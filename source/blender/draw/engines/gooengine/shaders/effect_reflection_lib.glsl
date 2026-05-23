/* SPDX-FileCopyrightText: 2021-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Based on:
 * "Stochastic Screen Space Reflections"
 * by Tomasz Stachowiak.
 * https://www.ea.com/frostbite/news/stochastic-screen-space-reflections
 * and
 * "Stochastic all the things: raytracing in hybrid real-time rendering"
 * by Tomasz Stachowiak.
 * https://media.contentapi.ea.com/content/dam/ea/seed/presentations/dd18-seed-raytracing-in-hybrid-real-time-rendering.pdf
 */

#pragma once

#include "goo_common_view_lib.glsl"

struct HitData {
  /** Hit direction scaled by intersection time. */
  float3 hit_dir;
  /** Screen space [0..1f] depth of the reflection hit position, or -1.0f for planar reflections. */
  float hit_depth;
  /** Inverse probability of ray spawning in this direction. */
  float ray_pdf_inv;
  /** True if ray has hit valid geometry. */
  bool is_hit;
  /** True if ray was generated from a planar reflection probe. */
  bool is_planar;
};

void encode_hit_data(HitData data, float3 hit_sP, float3 vP, out float4 hit_data, out float hit_depth)
{
  float3 hit_vP = get_view_space_from_depth(hit_sP.xy, hit_sP.z);
  hit_data.xyz = hit_vP - vP;
  hit_depth = data.is_planar ? -1.0f : hit_sP.z;
  /* Record 1.0f / pdf to reduce the computation in the resolve phase. */
  /* Encode hit validity in sign. */
  hit_data.w = data.ray_pdf_inv * ((data.is_hit) ? 1.0f : -1.0f);
}

HitData decode_hit_data(float4 hit_data, float hit_depth)
{
  HitData data;
  data.hit_dir.xyz = hit_data.xyz;
  data.hit_depth = hit_depth;
  data.is_planar = (hit_depth == -1.0f);
  data.ray_pdf_inv = abs(hit_data.w);
  data.is_hit = (hit_data.w > 0.0f);
  return data;
}

/* Blue noise categorized into 4 sets of samples.
 * See "Stochastic all the things" presentation slide 32-37. */
#define resolve_samples_count 9
