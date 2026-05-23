/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "draw_math_geom_lib.glsl"
#include "bsdf_common_lib.glsl"
#include "bsdf_sampling_lib.glsl"
#include "raytrace_lib.glsl"
#include "surface_lib.glsl"

/* ------------ Refraction ------------ */

#define BTDF_BIAS 0.85f

float4 screen_space_refraction(float3 vP, float3 N, float3 V, float ior, float roughnessSquared, float4 rand)
{
  float alpha = max(0.002f, roughnessSquared);

  /* Importance sampling bias */
  rand.x = mix(rand.x, 0.0f, BTDF_BIAS);

  float3 T, B;
  make_orthonormal_basis(N, T, B);
  float pdf;
  /* Microfacet normal */
  float3 H = sample_ggx(rand.xzw, alpha, V, N, T, B, pdf);

  /* If ray is bad (i.e. going below the plane) regenerate. */
  if (F_eta(ior, dot(H, V)) < 1.0f) {
    H = sample_ggx(rand.xzw * float3(1.0f, -1.0f, -1.0f), alpha, V, N, T, B, pdf);
  }

  float3 vV = viewCameraVec(vP);
  float eta = 1.0f / ior;
  if (dot(H, V) < 0.0f) {
    H = -H;
    eta = ior;
  }

  float3 R = refract(-V, H, 1.0f / ior);

  R = transform_direction(ViewMatrixGoo, R);

  Ray ray;
  ray.origin = vP;
  ray.direction = R * 1e16f;

  RayTraceParameters params;
  params.thickness = ssrThickness;
  params.jitter = rand.y;
  params.trace_quality = ssrQuality;
  params.roughness = roughnessSquared;

  float3 hit_pos;
  bool hit = raytrace(ray, params, false, true, hit_pos);

  if (hit && (F_eta(ior, dot(H, V)) < 1.0f)) {
    hit_pos = get_view_space_from_depth(hit_pos.xy, hit_pos.z);
    float hit_dist = distance(hit_pos, vP);

    float cone_cos = cone_cosine(roughnessSquared);
    float cone_tan = sqrt(1 - cone_cos * cone_cos) / cone_cos;

    /* Empirical fit for refraction. */
    /* TODO: find a better fit or precompute inside the LUT. */
    cone_tan *= 0.5f * fast_sqrt(F0_from_ior((ior < 1.0f) ? 1.0f / ior : ior));

    float cone_footprint = hit_dist * cone_tan;

    /* find the offset in screen space by multiplying a point
     * in camera space at the depth of the point by the projection matrix. */
    float homcoord = ProjectionMatrixGoo[2][3] * hit_pos.z + ProjectionMatrixGoo[3][3];
    /* UV space footprint */
    cone_footprint = BTDF_BIAS * 0.5f * max(ProjectionMatrixGoo[0][0], ProjectionMatrixGoo[1][1]) *
                     cone_footprint / homcoord;

    float2 hit_uvs = project_point(ProjectionMatrixGoo, hit_pos).xy * 0.5f + 0.5f;

    /* Texel footprint */
    float2 texture_size = float2(textureSize(refractColorBuffer, 0).xy) / hizUvScale.xy;
    float mip = log2(cone_footprint * max(texture_size.x, texture_size.y));

    float3 spec = textureLod(refractColorBuffer, hit_uvs * hizUvScale.xy, mip).xyz;
    float mask = screen_border_mask(hit_uvs);

    return float4(spec, mask);
  }

  return float4(0.0f);
}
