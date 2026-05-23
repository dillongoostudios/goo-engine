/* SPDX-FileCopyrightText: 2021-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "common_utiltex_lib.glsl"
#include "lights_lib.glsl"
#include "lightprobe_lib.glsl"
#include "ambient_occlusion_lib.glsl"
#include "bsdf_common_lib.glsl"
#include "closure_eval_lib.glsl"
#include "renderpass_lib.glsl"

struct ClosureInputGlossy {
  float3 N;          /** Shading normal. */
  float roughness; /** Input roughness, not squared. */
};

#ifdef GPU_METAL
#  define CLOSURE_INPUT_Glossy_DEFAULT \
    { \
      float3(0.0f), 0.0f \
    }
#else
#  define CLOSURE_INPUT_Glossy_DEFAULT ClosureInputGlossy(float3(0.0f), 0.0f)
#endif

struct ClosureEvalGlossy {
  float4 ltc_mat;            /** LTC matrix values. */
  float ltc_brdf_scale;    /** LTC BRDF scaling. */
  float3 probe_sampling_dir; /** Direction to sample probes from. */
  float spec_occlusion;    /** Specular Occlusion. */
  float3 raytrace_radiance;  /** Ray-trace reflection to be accumulated after occlusion. */
};

/* Stubs. */
#define ClosureOutputGlossy ClosureOutput
#define closure_Glossy_grid_eval(cl_in, cl_eval, cl_common, data, cl_out)

#ifdef STEP_RESOLVE /* SSR */
/* Prototype. */
#  ifndef GPU_METAL
/* MSL does not require prototypes. */
void raytrace_resolve(ClosureInputGlossy cl_in,
                      inout ClosureEvalGlossy cl_eval,
                      inout ClosureEvalCommon cl_common,
                      inout ClosureOutputGlossy cl_out);
#  endif
#endif

ClosureEvalGlossy closure_Glossy_eval_init(inout ClosureInputGlossy cl_in,
                                           inout ClosureEvalCommon cl_common,
                                           out ClosureOutputGlossy cl_out)
{
  cl_in.N = safe_normalize(cl_in.N);
  cl_in.roughness = clamp(cl_in.roughness, 1e-8f, 0.9999f);
  cl_out.radiance = float3(0.0f);

#ifndef STEP_RESOLVE /* SSR */
  cl_in.N = ensure_valid_specular_reflection(cl_common.Ng, cl_common.V, cl_in.N);
#endif

  float NV = dot(cl_in.N, cl_common.V);
  float2 lut_uv = lut_coords(NV, cl_in.roughness);

  ClosureEvalGlossy cl_eval;
  cl_eval.ltc_mat = texture(utilTex, float3(lut_uv, LTC_MAT_LAYER));
  cl_eval.probe_sampling_dir = specular_dominant_dir(cl_in.N, cl_common.V, sqr(cl_in.roughness));
  cl_eval.spec_occlusion = specular_occlusion(cl_common.occlusion_data,
                                              cl_common.V,
                                              cl_common.N,
                                              cl_in.roughness,
                                              cl_eval.probe_sampling_dir);
  cl_eval.raytrace_radiance = float3(0.0f);

#ifdef STEP_RESOLVE /* SSR */
  raytrace_resolve(cl_in, cl_eval, cl_common, cl_out);
#endif

  /* The BRDF split sum LUT is applied after the radiance accumulation.
   * Correct the LTC so that its energy is constant. */
  cl_eval.ltc_brdf_scale = texture(utilTex, float3(lut_uv, LTC_BRDF_LAYER)).g;
  return cl_eval;
}

void closure_Glossy_light_eval(ClosureInputGlossy cl_in,
                               ClosureEvalGlossy cl_eval,
                               ClosureEvalCommon cl_common,
                               ClosureLightData light,
                               inout ClosureOutputGlossy cl_out)
{
/* Ensure specular light contribution only gets applied once when running split pass */
#ifndef RESOLVE_SSR
  float radiance = light_specular(light.data, cl_eval.ltc_mat, cl_in.N, cl_common.V, light.L);
  radiance *= cl_eval.ltc_brdf_scale;
  cl_out.radiance += light.data.l_color *
                     (light.data.l_spec * light.vis * light.contact_shadow * radiance);
#endif
}

void closure_Glossy_planar_eval(ClosureInputGlossy cl_in,
                                ClosureEvalGlossy cl_eval,
                                inout ClosureEvalCommon cl_common,
                                ClosurePlanarData planar,
                                inout ClosureOutputGlossy cl_out)
{
#ifndef STEP_RESOLVE /* SSR already evaluates planar reflections. */
  float attenuation = planar.attenuation * probe_attenuation_planar_normal_roughness(
                                               planar.data, cl_in.N, cl_in.roughness);

  float3 probe_radiance = probe_evaluate_planar(
      planar.id, planar.data, cl_common.P, cl_in.N, cl_common.V, cl_in.roughness);

  cl_out.radiance = mix(cl_out.radiance, probe_radiance, attenuation);
#endif
}

void closure_Glossy_cubemap_eval(ClosureInputGlossy cl_in,
                                 ClosureEvalGlossy cl_eval,
                                 ClosureEvalCommon cl_common,
                                 ClosureCubemapData cube,
                                 inout ClosureOutputGlossy cl_out)
{
/* Ensure cube-map probes contribution only gets applied once when running split pass */
#ifndef RESOLVE_SSR
  float3 probe_radiance = probe_evaluate_cube(
      cube.id, cl_common.P, cl_eval.probe_sampling_dir, cl_in.roughness);
  cl_out.radiance += cube.attenuation * probe_radiance;
#endif
}

void closure_Glossy_indirect_end(ClosureInputGlossy cl_in,
                                 ClosureEvalGlossy cl_eval,
                                 ClosureEvalCommon cl_common,
                                 inout ClosureOutputGlossy cl_out)
{
/* Ensure specular contribution only gets applied once when running split pass */
#ifndef RESOLVE_SSR
  /* If not enough light has been accumulated from probes, use the world specular cube-map
   * to fill the remaining energy needed. */
  if (specToggle && cl_common.specular_accum > 0.0f) {
    float3 probe_radiance = probe_evaluate_world_spec(cl_eval.probe_sampling_dir, cl_in.roughness);
    cl_out.radiance += cl_common.specular_accum * probe_radiance;
  }

  /* Apply occlusion on distant lighting. */
  cl_out.radiance *= cl_eval.spec_occlusion;
#endif
  /* Apply Ray-trace reflections after occlusion since they are direct, local reflections. */
#if defined(RESOLVE_PROBE)
  /* NO OP - output base radiance. */
#elif defined(RESOLVE_SSR)
  /* Output only ray-trace radiance. */
  cl_out.radiance = cl_eval.raytrace_radiance;
#else
  /* Standard resolve */
  cl_out.radiance += cl_eval.raytrace_radiance;
#endif
}

void closure_Glossy_eval_end(ClosureInputGlossy cl_in,
                             ClosureEvalGlossy cl_eval,
                             ClosureEvalCommon cl_common,
                             inout ClosureOutputGlossy cl_out)
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
