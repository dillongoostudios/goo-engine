/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "goo_common_view_lib.glsl"

void main()
{
  /* Generate Triangle : less memory fetches from a VBO */
  int v_id = gl_VertexID % 3; /* Vertex Id */
  int t_id = gl_VertexID / 3; /* Triangle Id */

  /* Crappy diagram
   * ex 1
   *    | \
   *    |   \
   *  1 |     \
   *    |       \
   *    |         \
   *  0 |           \
   *    |             \
   *    |               \
   * -1 0 --------------- 2
   *   -1     0     1     ex
   */
  volumetric_vert_iface.vPos.x = float(v_id / 2) * 4.0f - 1.0f; /* int divisor round down */
  volumetric_vert_iface.vPos.y = float(v_id % 2) * 4.0f - 1.0f;
  volumetric_vert_iface.vPos.z = float(t_id);
  volumetric_vert_iface.vPos.w = 1.0f;

  PASS_RESOURCE_ID

#ifdef GPU_METAL
  volumetric_geom_iface.slice = int(volumetric_vert_iface.vPos.z);
  gpu_Layer = int(volumetric_vert_iface.vPos.z);
  gl_Position = volumetric_vert_iface.vPos.xyww;
#endif
}

/* Stubs */
float2 bsdf_lut(float a, float b, float c, bool d)
{
  return float2(0.0f);
}

void bsdf_lut(float3 F0,
              float3 F90,
              float3 transmission_tint,
              float cos_theta,
              float roughness,
              float ior,
              bool do_multiscatter,
              out float3 reflectance,
              out float3 transmittance)
{
  reflectance = float3(0.0f);
  transmittance = float3(0.0f);
  return;
}

float2 brdf_lut(float a, float b)
{
  return float2(0.0f);
}

void brdf_f82_tint_lut(float3 F0,
                       float3 F82,
                       float cos_theta,
                       float roughness,
                       bool do_multiscatter,
                       out float3 reflectance)
{
}

float3 F_brdf_multi_scatter(float3 a, float3 b, float2 c)
{
  return float3(0.0f);
}

float3 F_brdf_single_scatter(float3 a, float3 b, float2 c)
{
  return float3(0.0f);
}

float F_eta(float a, float b)
{
  return 0.0f;
}

float3 coordinate_camera(float3 P)
{
  return float3(0.0f);
}

float3 coordinate_screen(float3 P)
{
  return float3(0.0f);
}

float3 coordinate_reflect(float3 P, float3 N)
{
  return float3(0.0f);
}

float3 coordinate_incoming(float3 P)
{
  return float3(0.0f);
}

float texture_lod_bias_get()
{
  return 1.0f;
}

float attr_load_temperature_post(float attr)
{
  return attr;
}

float4 attr_load_color_post(float4 attr)
{
  return attr;
}

float4 attr_load_uniform(float4 attr, const uint attr_hash)
{
  return attr;
}
