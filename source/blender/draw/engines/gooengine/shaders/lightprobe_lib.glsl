/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "engine_eevee_legacy_shared.hh"
#include "draw_math_geom_lib.glsl"
#include "goo_common_view_lib.glsl"
#include "common_utiltex_lib.glsl"
#include "common_uniforms_lib.glsl"
#include "ambient_occlusion_lib.glsl"
#include "irradiance_lib.glsl"

/* ----------- Uniforms --------- */

#if !defined(USE_GPU_SHADER_CREATE_INFO)

uniform sampler2DArray probePlanars;
uniform samplerCubeArray probeCubes;

#endif

/* ----------- Structures --------- */

#define PROBE_PARALLAX_BOX 1.0f
#define PROBE_ATTENUATION_BOX 1.0f

#define p_position position_type.xyz
#define p_parallax_type position_type.w
#define p_atten_fac attenuation_fac_type.x
#define p_atten_type attenuation_fac_type.y

#define pl_plane_eq plane_equation
#define pl_normal plane_equation.xyz
#define pl_facing_scale facing_scale_bias.x
#define pl_facing_bias facing_scale_bias.y
#define pl_fade_scale clip_vec_x_fade_scale.w
#define pl_fade_bias clip_vec_y_fade_bias.w
#define pl_clip_pos_x clip_vec_x_fade_scale.xyz
#define pl_clip_pos_y clip_vec_y_fade_bias.xyz
#define pl_clip_edges clip_edges

#define g_corner ws_corner_atten_scale.xyz
#define g_atten_scale ws_corner_atten_scale.w
#define g_atten_bias ws_increment_x_atten_bias.w
#define g_level_bias ws_increment_y_lvl_bias.w
#define g_increment_x ws_increment_x_atten_bias.xyz
#define g_increment_y ws_increment_y_lvl_bias.xyz
#define g_increment_z ws_increment_z.xyz
#define g_resolution resolution_offset.xyz
#define g_offset resolution_offset.w
#define g_vis_bias vis_bias_bleed_range.x
#define g_vis_bleed vis_bias_bleed_range.y
#define g_vis_range vis_bias_bleed_range.z

#ifndef MAX_PROBE
#  define MAX_PROBE 1
#endif
#ifndef MAX_GRID
#  define MAX_GRID 1
#endif
#ifndef MAX_PLANAR
#  define MAX_PLANAR 1
#endif

#if !defined(USE_GPU_SHADER_CREATE_INFO)

layout(std140) uniform probe_block
{
  ProbeBlock _probe_block;
};

layout(std140) uniform grid_block
{
  GridBlock _grid_block;
};

layout(std140) uniform planar_block
{
  PlanarBlock _planar_block;
};

#  define probes_data _probe_block.probes_data
#  define grids_data _grid_block.grids_data
#  define planars_data _planar_block.planars_data

#endif
/* ----------- Functions --------- */

float probe_attenuation_cube(int pd_id, float3 P)
{
  float3 localpos = transform_point(probes_data[pd_id].influencemat, P);

  float probe_atten_fac = probes_data[pd_id].p_atten_fac;
  float fac;
  if (probes_data[pd_id].p_atten_type == PROBE_ATTENUATION_BOX) {
    float3 axes_fac = saturate(probe_atten_fac - probe_atten_fac * abs(localpos));
    fac = min_v3(axes_fac);
  }
  else {
    fac = saturate(probe_atten_fac - probe_atten_fac * length(localpos));
  }

  return fac;
}

float probe_attenuation_planar(PlanarData pd, float3 P)
{
  /* Distance from plane */
  float fac = saturate(abs(dot(pd.pl_plane_eq, float4(P, 1.0f))) * pd.pl_fade_scale +
                       pd.pl_fade_bias);
  /* Fancy fast clipping calculation */
  float2 dist_to_clip;
  dist_to_clip.x = dot(pd.pl_clip_pos_x, P);
  dist_to_clip.y = dot(pd.pl_clip_pos_y, P);
  /* compare and add all tests */
  fac *= step(2.0f, dot(step(pd.pl_clip_edges, dist_to_clip.xxyy), float2(-1.0f, 1.0f).xyxy));
  return fac;
}

float probe_attenuation_planar_normal_roughness(PlanarData pd, float3 N, float roughness)
{
  /* Normal Facing */
  float fac = saturate(dot(pd.pl_normal, N) * pd.pl_facing_scale + pd.pl_facing_bias);
  /* Decrease influence for high roughness */
  return fac * saturate(1.0f - roughness * 10.0f);
}

float probe_attenuation_grid(GridData gd, float3 P, out float3 localpos)
{
  localpos = transform_point(gd.localmat, P);
  float3 pos_to_edge = max(float3(0.0f), abs(localpos) - 1.0f);
  float fade = length(pos_to_edge);
  return saturate(-fade * gd.g_atten_scale + gd.g_atten_bias);
}

float3 probe_evaluate_cube(int pd_id, float3 P, float3 R, float roughness)
{
  /* Correct reflection ray using parallax volume intersection. */
  float3 localpos = transform_point(probes_data[pd_id].parallaxmat, P);
  float3 localray = transform_direction(probes_data[pd_id].parallaxmat, R);

  float dist;
  if (probes_data[pd_id].p_parallax_type == PROBE_PARALLAX_BOX) {
    dist = line_unit_box_intersect_dist(localpos, localray);
  }
  else {
    dist = line_unit_sphere_intersect_dist(localpos, localray);
  }

  /* Use Distance in WS directly to recover intersection */
  float3 intersection = P + R * dist - probes_data[pd_id].p_position;

  /* From Frostbite PBR Course
   * Distance based roughness
   * http://www.frostbite.com/wp-content/uploads/2014/11/course_notes_moving_frostbite_to_pbr.pdf
   */
  float original_roughness = roughness;
  float linear_roughness = fast_sqrt(roughness);
  float distance_roughness = saturate(dist * linear_roughness / length(intersection));
  linear_roughness = mix(distance_roughness, linear_roughness, linear_roughness);
  roughness = linear_roughness * linear_roughness;

  float fac = saturate(original_roughness * 2.0f - 1.0f);
  R = mix(intersection, R, fac * fac);

  float lod = linear_roughness * prbLodCubeMax;
  return textureLod(probeCubes, float4(R, float(pd_id)), lod).rgb;
}

float3 probe_evaluate_world_spec(float3 R, float roughness)
{
  float lod = fast_sqrt(roughness) * prbLodCubeMax;
  return textureLod(probeCubes, float4(R, 0.0f), lod).rgb;
}

float3 probe_evaluate_planar(int id, PlanarData pd, float3 P, float3 N, float3 V, float roughness)
{
  /* Find view vector / reflection plane intersection. */
  float3 point_on_plane = line_plane_intersect(P, V, pd.pl_plane_eq);

  /* How far the pixel is from the plane. */
  float ref_depth = 1.0f; /* TODO: parameter. */

  /* Compute distorted reflection vector based on the distance to the reflected object.
   * In other words find intersection between reflection vector and the sphere center
   * around point_on_plane. */
  float3 proj_ref = reflect(reflect(-V, N) * ref_depth, pd.pl_normal);

  /* Final point in world space. */
  float3 ref_pos = point_on_plane + proj_ref;

  /* Reproject to find texture coords. */
  float4 refco = ProjectionMatrixGoo * (ViewMatrixGoo * float4(ref_pos, 1.0f));
  refco.xy /= refco.w;

  /* TODO: If we support non-SSR planar reflection, we should blur them with gaussian
   * and chose the right mip depending on the cone footprint after projection */
  /* NOTE: X is inverted here to compensate inverted drawing. */
  float3 radiance = textureLod(probePlanars, float3(refco.xy * float2(-0.5f, 0.5f) + 0.5f, id), 0.0f).rgb;

  return radiance;
}

void fallback_cubemap(float3 N,
                      float3 V,
                      float3 P,
                      float3 vP,
                      float roughness,
                      float roughnessSquared,
                      inout float4 spec_accum)
{
  /* Specular probes */
  float3 spec_dir = specular_dominant_dir(N, V, roughnessSquared);

  OcclusionData occlusion_data = occlusion_load(vP, 1.0f);
  float final_ao = specular_occlusion(occlusion_data, V, N, roughness, spec_dir);

  /* Starts at 1 because 0 is world probe */
  for (int i = 1; i < MAX_PROBE && i < prbNumRenderCube && spec_accum.a < 0.999f; i++) {
    float fade = probe_attenuation_cube(i, P);

    if (fade > 0.0f) {
      float3 spec = final_ao * probe_evaluate_cube(i, P, spec_dir, roughness);
      accumulate_light(spec, fade, spec_accum);
    }
  }

  /* World Specular */
  if (spec_accum.a < 0.999f) {
    float3 spec = final_ao * probe_evaluate_world_spec(spec_dir, roughness);
    accumulate_light(spec, 1.0f, spec_accum);
  }
}

float3 probe_evaluate_grid(GridData gd, float3 P, float3 N, float3 localpos)
{
  localpos = localpos * 0.5f + 0.5f;
  localpos = localpos * float3(gd.g_resolution) - 0.5f;

  float3 localpos_floored = floor(localpos);
  float3 trilinear_weight = fract(localpos);

  float weight_accum = 0.0f;
  float3 irradiance_accum = float3(0.0f);

  /* For each neighbor cells */
  for (int i = 0; i < 8; i++) {
    int3 offset = int3(i, i >> 1, i >> 2) & int3(1);
    float3 cell_cos = clamp(localpos_floored + float3(offset), float3(0.0f), float3(gd.g_resolution) - 1.0f);

    /* Keep in sync with update_irradiance_probe */
    int3 icell_cos = int3(gd.g_level_bias * floor(cell_cos / gd.g_level_bias));
    int cell = gd.g_offset + icell_cos.z + icell_cos.y * gd.g_resolution.z +
               icell_cos.x * gd.g_resolution.z * gd.g_resolution.y;

    float3 color = irradiance_from_cell_get(cell, N);

    /* We need this because we render probes in world space (so we need light vector in WS).
     * And rendering them in local probe space is too much problem. */
    float3 ws_cell_location = gd.g_corner +
                            (gd.g_increment_x * cell_cos.x + gd.g_increment_y * cell_cos.y +
                             gd.g_increment_z * cell_cos.z);

    float3 ws_point_to_cell = ws_cell_location - P;
    float ws_dist_point_to_cell = length(ws_point_to_cell);
    float3 ws_light = ws_point_to_cell / ws_dist_point_to_cell;

    /* Smooth back-face test. */
    float weight = saturate(dot(ws_light, N));

    /* Precomputed visibility */
    weight *= load_visibility_cell(
        cell, ws_light, ws_dist_point_to_cell, gd.g_vis_bias, gd.g_vis_bleed, gd.g_vis_range);

    /* Smoother transition */
    weight += prbIrradianceSmooth;

    /* Trilinear weights */
    float3 trilinear = mix(1.0f - trilinear_weight, trilinear_weight, float3(offset));
    weight *= trilinear.x * trilinear.y * trilinear.z;

    /* Avoid zero weight */
    weight = max(0.00001f, weight);

    weight_accum += weight;
    irradiance_accum += color * weight;
  }

  return irradiance_accum / weight_accum;
}

float3 probe_evaluate_world_diff(float3 N)
{
  if (prbNumRenderGrid == 0) {
    return float3(0);
  }
  return irradiance_from_cell_get(0, N);
}
