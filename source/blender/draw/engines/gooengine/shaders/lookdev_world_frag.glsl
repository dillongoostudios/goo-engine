/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "draw_math_geom_lib.glsl"
#include "goo_common_view_lib.glsl"
#include "lightprobe_lib.glsl"
#include "surface_lib.glsl"

float3 background_transform_to_world(float3 viewfloat)
{
  float4 v = (ProjectionMatrixGoo[3][3] == 0.0f) ? float4(viewfloat, 1.0f) : float4(0.0f, 0.0f, 1.0f, 1.0f);
  float4 co_homogeneous = (ProjectionMatrixInverseGoo * v);

  float4 co = float4(co_homogeneous.xyz / co_homogeneous.w, 0.0f);
  return (ViewMatrixInverseGoo * co).xyz;
}

float goo_hypot(float x, float y)
{
  return sqrt(x * x + y * y);
}

float4 node_tex_environment_equirectangular(float3 co, sampler2D ima)
{
  float3 nco = normalize(co);
  float u = -atan(nco.y, nco.x) / (2.0f * M_PI) + 0.5f;
  float v = atan(nco.z, goo_hypot(nco.x, nco.y)) / M_PI + 0.5f;
  return textureLod(ima, float2(u, v), 0.0f);
}

void main()
{
  float3 worldfloat = background_transform_to_world(viewPosition);

  float3 background_color;
#if defined(LOOKDEV_BG)
  background_color = probe_evaluate_world_spec(worldfloat, studioLightBlur).rgb;
#else
  worldfloat = StudioLightMatrix * worldfloat;
  background_color = node_tex_environment_equirectangular(worldfloat, studioLight).rgb;
  background_color *= studioLightIntensity;
#endif

  FragColor = float4(clamp(background_color, float3(0.0f), float3(1e10f)), 1.0f) * backgroundAlpha;
}
