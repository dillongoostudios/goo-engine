/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "draw_math_geom_lib.glsl"
#include "common_uniforms_lib.glsl"
#include "octahedron_lib.glsl"

#define IRRADIANCE_LIB

/* ---------------------------------------------------------------------- */
/** \name Structure
 * \{ */

#if defined(IRRADIANCE_SH_L2)
struct IrradianceData {
  float3 shcoefs[9];
};

#else /* defined(IRRADIANCE_HL2) */
struct IrradianceData {
  float3 cubesides[3];
};

#endif

/** \} */

/* ---------------------------------------------------------------------- */
/** \name Functions
 * \{ */

float4 irradiance_encode(float3 rgb)
{
  float maxRGB = max_v3(rgb);
  float fexp = ceil(log2(maxRGB));
  return float4(rgb / exp2(fexp), (fexp + 128.0f) / 255.0f);
}

float3 irradiance_decode(float4 data)
{
  float fexp = data.a * 255.0f - 128.0f;
  return data.rgb * exp2(fexp);
}

float4 visibility_encode(float2 accum, float range)
{
  accum /= range;

  float4 data;
  data.x = fract(accum.x);
  data.y = floor(accum.x) / 255.0f;
  data.z = fract(accum.y);
  data.w = floor(accum.y) / 255.0f;

  return data;
}

float2 visibility_decode(float4 data, float range)
{
  return (data.xz + data.yw * 255.0f) * range;
}

IrradianceData load_irradiance_cell(int cell, float3 N)
{
  /* Keep in sync with diffuse_filter_probe() */

#if defined(IRRADIANCE_SH_L2)

  int2 cell_co = int2(3, 3);
  int cell_per_row = textureSize(irradianceGrid, 0).x / cell_co.x;
  cell_co.x *= cell % cell_per_row;
  cell_co.y *= cell / cell_per_row;

  int3 ofs = int3(0, 1, 2);

  IrradianceData ir;
  ir.shcoefs[0] = texelFetch(irradianceGrid, int3(cell_co + ofs.xx, 0), 0).rgb;
  ir.shcoefs[1] = texelFetch(irradianceGrid, int3(cell_co + ofs.yx, 0), 0).rgb;
  ir.shcoefs[2] = texelFetch(irradianceGrid, int3(cell_co + ofs.zx, 0), 0).rgb;
  ir.shcoefs[3] = texelFetch(irradianceGrid, int3(cell_co + ofs.xy, 0), 0).rgb;
  ir.shcoefs[4] = texelFetch(irradianceGrid, int3(cell_co + ofs.yy, 0), 0).rgb;
  ir.shcoefs[5] = texelFetch(irradianceGrid, int3(cell_co + ofs.zy, 0), 0).rgb;
  ir.shcoefs[6] = texelFetch(irradianceGrid, int3(cell_co + ofs.xz, 0), 0).rgb;
  ir.shcoefs[7] = texelFetch(irradianceGrid, int3(cell_co + ofs.yz, 0), 0).rgb;
  ir.shcoefs[8] = texelFetch(irradianceGrid, int3(cell_co + ofs.zz, 0), 0).rgb;

#else /* defined(IRRADIANCE_HL2) */

  int2 cell_co = int2(3, 2);
  int cell_per_row = textureSize(irradianceGrid, 0).x / cell_co.x;
  cell_co.x *= cell % cell_per_row;
  cell_co.y *= cell / cell_per_row;

  int3 is_negative = int3(step(0.0f, -N));

  IrradianceData ir;
  ir.cubesides[0] = irradiance_decode(
      texelFetch(irradianceGrid, int3(cell_co + int2(0, is_negative.x), 0), 0));
  ir.cubesides[1] = irradiance_decode(
      texelFetch(irradianceGrid, int3(cell_co + int2(1, is_negative.y), 0), 0));
  ir.cubesides[2] = irradiance_decode(
      texelFetch(irradianceGrid, int3(cell_co + int2(2, is_negative.z), 0), 0));

#endif

  return ir;
}

float load_visibility_cell(int cell, float3 L, float dist, float bias, float bleed_bias, float range)
{
  /* Keep in sync with diffuse_filter_probe() */
  int2 cell_co = int2(prbIrradianceVisSize);
  int2 cell_per_row_col = textureSize(irradianceGrid, 0).xy / prbIrradianceVisSize;
  cell_co.x *= (cell % cell_per_row_col.x);
  cell_co.y *= (cell / cell_per_row_col.x) % cell_per_row_col.y;
  float layer = 1.0f + float((cell / cell_per_row_col.x) / cell_per_row_col.y);

  float2 texel_size = 1.0f / float2(textureSize(irradianceGrid, 0).xy);
  float2 co = float2(cell_co) * texel_size;

  float2 uv = mapping_octahedron(-L, float2(1.0f / float(prbIrradianceVisSize)));
  uv *= float2(prbIrradianceVisSize) * texel_size;

  float4 data = texture(irradianceGrid, float3(co + uv, layer));

  /* Decoding compressed data */
  float2 moments = visibility_decode(data, range);

  /* Doing chebishev test */
  float variance = abs(moments.x * moments.x - moments.y);
  variance = max(variance, bias / 10.0f);

  float d = dist - moments.x;
  float p_max = variance / (variance + d * d);

  /* Increase contrast in the weight by squaring it */
  p_max *= p_max;

  /* Now reduce light-bleeding by removing the [0, x] tail and linearly re-scaling [x, 1]. */
  p_max = clamp((p_max - bleed_bias) / (1.0f - bleed_bias), 0.0f, 1.0f);

  return (dist <= moments.x) ? 1.0f : p_max;
}

/* http://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/ */
float3 spherical_harmonics_L1(float3 N, float3 shcoefs[4])
{
  float3 sh = float3(0.0f);

  sh += 0.282095f * shcoefs[0];

  sh += -0.488603f * N.z * shcoefs[1];
  sh += 0.488603f * N.y * shcoefs[2];
  sh += -0.488603f * N.x * shcoefs[3];

  return sh;
}

float3 spherical_harmonics_L2(float3 N, float3 shcoefs[9])
{
  float3 sh = float3(0.0f);

  sh += 0.282095f * shcoefs[0];

  sh += -0.488603f * N.z * shcoefs[1];
  sh += 0.488603f * N.y * shcoefs[2];
  sh += -0.488603f * N.x * shcoefs[3];

  sh += 1.092548f * N.x * N.z * shcoefs[4];
  sh += -1.092548f * N.z * N.y * shcoefs[5];
  sh += 0.315392f * (3.0f * N.y * N.y - 1.0f) * shcoefs[6];
  sh += -1.092548f * N.x * N.y * shcoefs[7];
  sh += 0.546274f * (N.x * N.x - N.z * N.z) * shcoefs[8];

  return sh;
}

float3 hl2_basis(float3 N, float3 cubesides[3])
{
  float3 irradiance = float3(0.0f);

  float3 n_squared = N * N;

  irradiance += n_squared.x * cubesides[0];
  irradiance += n_squared.y * cubesides[1];
  irradiance += n_squared.z * cubesides[2];

  return irradiance;
}

float3 compute_irradiance(float3 N, IrradianceData ird)
{
#if defined(IRRADIANCE_SH_L2)
  return spherical_harmonics_L2(N, ird.shcoefs);
#else /* defined(IRRADIANCE_HL2) */
  return hl2_basis(N, ird.cubesides);
#endif
}

float3 irradiance_from_cell_get(int cell, float3 ir_dir)
{
  IrradianceData ir_data = load_irradiance_cell(cell, ir_dir);
  return compute_irradiance(ir_dir, ir_data);
}

/** \} */
