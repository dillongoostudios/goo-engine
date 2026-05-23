/* SPDX-FileCopyrightText: 2021-2022 Blender Authors
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

#include "draw_math_geom_lib.glsl"
#include "common_utiltex_lib.glsl"
#include "raytrace_lib.glsl"
#include "lightprobe_lib.glsl"
#include "bsdf_sampling_lib.glsl"
#include "effect_reflection_lib.glsl"

void main()
{
  float4 rand = texelfetch_noise_tex(gl_FragCoord.xy);
  /* Decorrelate from AA. */
  /* TODO(@fclem): we should use a more general approach for more random number dimensions. */
  float2 random_px = floor(fract(rand.xy * 2.2074408460575947536f) * 1.99999f) - 0.5f;
  rand.xy = fract(rand.xy * 3.2471795724474602596f);

  /* Randomly choose the pixel to start the ray from when tracing at lower resolution.
   * This method also make sure we always start from the center of a full-resolution texel. */
  float2 uvs = (gl_FragCoord.xy + random_px * randomScale) / (targetSize * ssrUvScale);

  float depth = textureLod(maxzBuffer, uvs * hizUvScale.xy, 0.0f).r;

  HitData data;
  data.is_planar = false;
  data.ray_pdf_inv = 0.0f;
  data.is_hit = false;
  data.hit_dir = float3(0.0f, 0.0f, 0.0f);
  /* Default: not hits. */
  encode_hit_data(data, data.hit_dir, data.hit_dir, hitData, hitDepth);

  /* Early out */
  /* We can't do discard because we don't clear the render target. */
  if (depth == 1.0f) {
    return;
  }

  /* Using view space */
  float3 vP = get_view_space_from_depth(uvs, depth);
  float3 P = transform_point(ViewMatrixInverseGoo, vP);
  float3 vV = viewCameraVec(vP);
  float3 V = cameraVec(P);
  float3 vN = normal_decode(textureLod(normalBuffer, uvs, 0.0f).rg, vV);
  float3 N = transform_direction(ViewMatrixInverseGoo, vN);

  /* Retrieve pixel data */
  float4 speccol_roughness = textureLod(specroughBuffer, uvs, 0.0f).rgba;

  /* Early out */
  if (dot(speccol_roughness.rgb, float3(1.0f)) == 0.0f) {
    return;
  }

  float roughness = speccol_roughness.a;
  float alpha = max(1e-3f, roughness * roughness);

  /* Early out */
  if (roughness > ssrMaxRoughness + 0.2f) {
    return;
  }

  /* Planar Reflections */
  int planar_id = -1;
  for (int i = 0; i < MAX_PLANAR && i < prbNumPlanar; i++) {
    PlanarData pd = planars_data[i];

    float fade = probe_attenuation_planar(pd, P);
    fade *= probe_attenuation_planar_normal_roughness(pd, N, 0.0f);

    if (fade > 0.5f) {
      /* Find view vector / reflection plane intersection. */
      /* TODO: optimize, use view space for all. */
      float3 P_plane = line_plane_intersect(P, V, pd.pl_plane_eq);
      vP = transform_point(ViewMatrixGoo, P_plane);

      planar_id = i;
      data.is_planar = true;
      break;
    }
  }

  /* Gives *perfect* reflection for very small roughness */
  if (roughness < 0.04f) {
    rand.xzw *= 0.0f;
  }
  /* Importance sampling bias */
  rand.x = mix(rand.x, 0.0f, ssrBrdfBias);

  float3 vT, vB;
  make_orthonormal_basis(vN, vT, vB); /* Generate tangent space */

  float pdf;
  float3 vH = sample_ggx(rand.xzw, alpha, vV, vN, vT, vB, pdf);
  float3 vR = reflect(-vV, vH);

  if (isnan(pdf)) {
    /* Seems that somethings went wrong.
     * This only happens on extreme cases where the normal deformed too much to have any valid
     * reflections. */
    return;
  }

  if (data.is_planar) {
    float3 view_plane_normal = transform_direction(ViewMatrixGoo, planars_data[planar_id].pl_normal);
    /* For planar reflections, we trace inside the reflected view. */
    vR = reflect(vR, view_plane_normal);
  }

  Ray ray;
  ray.origin = vP;
  ray.direction = vR * 1e16f;

  RayTraceParameters params;
  params.thickness = ssrThickness;
  params.jitter = rand.y;
  params.trace_quality = ssrQuality;
  params.roughness = alpha * alpha;

  float3 hit_sP;
  if (data.is_planar) {
    data.is_hit = raytrace_planar(ray, params, planar_id, hit_sP);
  }
  else {
    data.is_hit = raytrace(ray, params, true, false, hit_sP);
  }
  data.ray_pdf_inv = safe_rcp(pdf);

  encode_hit_data(data, hit_sP, ray.origin, hitData, hitDepth);
}
