/* SPDX-FileCopyrightText: 2021-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "common_utiltex_lib.glsl"
#include "lights_lib.glsl"
#include "lightprobe_lib.glsl"
#include "ambient_occlusion_lib.glsl"
#include "ssr_lib.glsl"
#include "closure_eval_lib.glsl"
#include "renderpass_lib.glsl"

struct ClosureInputRefraction {
  float3 N;          /** Shading normal. */
  float roughness; /** Input roughness, not squared. */
  float ior;       /** Index of refraction ratio. */
};
#ifdef GPU_METAL
/* C++ struct initialization. */
#  define CLOSURE_INPUT_Refraction_DEFAULT \
    { \
      float3(0.0f), 0.0f, 0.0f \
    }
#else
#  define CLOSURE_INPUT_Refraction_DEFAULT ClosureInputRefraction(float3(0.0f), 0.0f, 0.0f)
#endif

struct ClosureEvalRefraction {
  float3 P;                  /** LTC matrix values. */
  float3 ltc_brdf;           /** LTC BRDF values. */
  float3 probe_sampling_dir; /** Direction to sample probes from. */
  float probes_weight;     /** Factor to apply to probe radiance. */
};

/* Stubs. */
#define ClosureOutputRefraction ClosureOutput
#define closure_Refraction_grid_eval(cl_in, cl_eval, cl_common, data, cl_out)

ClosureEvalRefraction closure_Refraction_eval_init(inout ClosureInputRefraction cl_in,
                                                   ClosureEvalCommon cl_common,
                                                   out ClosureOutputRefraction cl_out)
{
  cl_in.N = safe_normalize(cl_in.N);
  cl_in.roughness = clamp(cl_in.roughness, 1e-8f, 0.9999f);
  cl_in.ior = max(cl_in.ior, 1e-5f);
  cl_out.radiance = float3(0.0f);

  ClosureEvalRefraction cl_eval;
  float3 cl_V;
  float eval_ior;
  /* Refract the view vector using the depth heuristic.
   * Then later Refract a second time the already refracted
   * ray using the inverse ior. */
  if (refractionDepth > 0.0f) {
    eval_ior = 1.0f / cl_in.ior;
    cl_V = -refract(-cl_common.V, cl_in.N, eval_ior);
    float3 plane_pos = cl_common.P - cl_in.N * refractionDepth;
    cl_eval.P = line_plane_intersect(cl_common.P, cl_V, plane_pos, cl_in.N);
  }
  else {
    eval_ior = cl_in.ior;
    cl_V = cl_common.V;
    cl_eval.P = cl_common.P;
  }

  cl_eval.probe_sampling_dir = refraction_dominant_dir(cl_in.N, cl_V, cl_in.roughness, eval_ior);
  cl_eval.probes_weight = 1.0f;

#ifdef USE_REFRACTION
  if (ssrefractToggle && cl_in.roughness < ssrMaxRoughness + 0.2f) {
    /* Find approximated position of the 2nd refraction event. */
    float3 vP = (refractionDepth > 0.0f) ? transform_point(ViewMatrixGoo, cl_eval.P) : cl_common.vP;
    float4 ssr_output = screen_space_refraction(
        vP, cl_in.N, cl_V, eval_ior, sqr(cl_in.roughness), cl_common.rand);
    ssr_output.a *= smoothstep(ssrMaxRoughness + 0.2f, ssrMaxRoughness, cl_in.roughness);
    cl_out.radiance += ssr_output.rgb * ssr_output.a;
    cl_eval.probes_weight -= ssr_output.a;
  }
#endif
  return cl_eval;
}

void closure_Refraction_light_eval(ClosureInputRefraction cl_in,
                                   ClosureEvalRefraction cl_eval,
                                   ClosureEvalCommon cl_common,
                                   ClosureLightData light,
                                   inout ClosureOutputRefraction cl_out)
{
  /* Not implemented yet. */
}

void closure_Refraction_planar_eval(ClosureInputRefraction cl_in,
                                    ClosureEvalRefraction cl_eval,
                                    ClosureEvalCommon cl_common,
                                    ClosurePlanarData planar,
                                    inout ClosureOutputRefraction cl_out)
{
  /* Not implemented yet. */
}

void closure_Refraction_cubemap_eval(ClosureInputRefraction cl_in,
                                     ClosureEvalRefraction cl_eval,
                                     ClosureEvalCommon cl_common,
                                     ClosureCubemapData cube,
                                     inout ClosureOutputRefraction cl_out)
{
  float3 probe_radiance = probe_evaluate_cube(
      cube.id, cl_eval.P, cl_eval.probe_sampling_dir, sqr(cl_in.roughness));
  cl_out.radiance += (cube.attenuation * cl_eval.probes_weight) * probe_radiance;
}

void closure_Refraction_indirect_end(ClosureInputRefraction cl_in,
                                     ClosureEvalRefraction cl_eval,
                                     ClosureEvalCommon cl_common,
                                     inout ClosureOutputRefraction cl_out)
{
  /* If not enough light has been accumulated from probes, use the world specular cube-map
   * to fill the remaining energy needed. */
  if (specToggle && cl_common.specular_accum > 0.0f) {
    float3 probe_radiance = probe_evaluate_world_spec(cl_eval.probe_sampling_dir,
                                                    sqr(cl_in.roughness));
    cl_out.radiance += (cl_common.specular_accum * cl_eval.probes_weight) * probe_radiance;
  }
}

void closure_Refraction_eval_end(ClosureInputRefraction cl_in,
                                 ClosureEvalRefraction cl_eval,
                                 ClosureEvalCommon cl_common,
                                 inout ClosureOutputRefraction cl_out)
{
  cl_out.radiance = render_pass_glossy_mask(cl_out.radiance);
#if defined(DEPTH_SHADER) || defined(WORLD_BACKGROUND)
  /* This makes shader resources become unused and avoid issues with samplers. (see #59747) */
  cl_out.radiance = float3(0.0f);
  return;
#endif

  if (!specToggle) {
    cl_out.radiance = float3(0.0f);
  }
}
