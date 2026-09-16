/* SPDX-FileCopyrightText: 2021-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "lights_lib.glsl"
#include "lightprobe_lib.glsl"
#include "draw_math_geom_lib.glsl"
#include "raytrace_lib.glsl"
#include "surface_lib.glsl"
#include "closure_eval_lib.glsl"
#include "renderpass_lib.glsl"

struct ClosureInputDiffuse {
  float3 N;      /** Shading normal. */
  float3 albedo; /** Used for multi-bounce GTAO approximation. Not applied to final radiance. */
};

#ifdef GPU_METAL
/* C++ struct initialization. */
#  define CLOSURE_INPUT_Diffuse_DEFAULT \
    { \
      float3(0.0f), float3(0.0f) \
    }
#else
#  define CLOSURE_INPUT_Diffuse_DEFAULT ClosureInputDiffuse(float3(0.0f), float3(0.0f))
#endif

struct ClosureEvalDiffuse {
  float3 probe_sampling_dir; /** Direction to sample probes from. */
  float ambient_occlusion; /** Final occlusion for distant lighting. */
};

/* Stubs. */
#define ClosureOutputDiffuse ClosureOutput
#define closure_Diffuse_planar_eval(cl_in, cl_eval, cl_common, data, cl_out)
#define closure_Diffuse_cubemap_eval(cl_in, cl_eval, cl_common, data, cl_out)

ClosureEvalDiffuse closure_Diffuse_eval_init(inout ClosureInputDiffuse cl_in,
                                             ClosureEvalCommon cl_common,
                                             out ClosureOutputDiffuse cl_out)
{
  cl_in.N = safe_normalize(cl_in.N);
  cl_out.radiance = float3(0.0f);

  ClosureEvalDiffuse cl_eval;
  cl_eval.ambient_occlusion = 1.0f;
  return cl_eval;
}

void closure_Diffuse_light_eval(ClosureInputDiffuse cl_in,
                                ClosureEvalDiffuse cl_eval,
                                ClosureEvalCommon cl_common,
                                ClosureLightData light,
                                inout ClosureOutputDiffuse cl_out)
{
  float radiance = light_diffuse(light.data, cl_in.N, cl_common.V, light.L);
  /* TODO(@fclem): We could try to shadow lights that are shadowless with the ambient_occlusion
   * factor here. */
  cl_out.radiance += light.data.l_color *
                     (light.data.l_diff * light.vis * light.contact_shadow * radiance);
}

void closure_Diffuse_grid_eval(ClosureInputDiffuse cl_in,
                               ClosureEvalDiffuse cl_eval,
                               ClosureEvalCommon cl_common,
                               ClosureGridData grid,
                               inout ClosureOutputDiffuse cl_out)
{
  float3 probe_radiance = probe_evaluate_grid(
      grid.data, cl_common.P, cl_eval.probe_sampling_dir, grid.local_pos);
  cl_out.radiance += grid.attenuation * probe_radiance;
}

void closure_Diffuse_indirect_end(ClosureInputDiffuse cl_in,
                                  ClosureEvalDiffuse cl_eval,
                                  ClosureEvalCommon cl_common,
                                  inout ClosureOutputDiffuse cl_out)
{
  /* If not enough light has been accumulated from probes, use the world specular cube-map
   * to fill the remaining energy needed. */
  if (cl_common.diffuse_accum > 0.0f) {
    float3 probe_radiance = probe_evaluate_world_diff(cl_eval.probe_sampling_dir);
    cl_out.radiance += cl_common.diffuse_accum * probe_radiance;
  }
  /* Apply occlusion on radiance before the light loop. */
  cl_out.radiance *= cl_eval.ambient_occlusion;
}

void closure_Diffuse_eval_end(ClosureInputDiffuse cl_in,
                              ClosureEvalDiffuse cl_eval,
                              ClosureEvalCommon cl_common,
                              inout ClosureOutputDiffuse cl_out)
{
  cl_out.radiance = render_pass_diffuse_mask(cl_out.radiance);
#if defined(DEPTH_SHADER) || defined(WORLD_BACKGROUND)
  /* This makes shader resources become unused and avoid issues with samplers. (see #59747) */
  cl_out.radiance = float3(0.0f);
  return;
#endif
}
