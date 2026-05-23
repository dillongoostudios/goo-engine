/* SPDX-FileCopyrightText: 2021-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "common_utiltex_lib.glsl"
#include "lights_lib.glsl"
#include "lightprobe_lib.glsl"
#include "ambient_occlusion_lib.glsl"
#include "closure_eval_lib.glsl"
#include "renderpass_lib.glsl"

struct ClosureInputTranslucent {
  float3 N; /** Shading normal. */
};
#ifdef GPU_METAL
/* C++ struct initialization. */
#  define CLOSURE_INPUT_Translucent_DEFAULT \
    { \
      float3(0.0f) \
    }
#else
#  define CLOSURE_INPUT_Translucent_DEFAULT ClosureInputTranslucent(float3(0.0f))
#endif
/* Stubs. */
#define ClosureEvalTranslucent ClosureEvalDummy
#define ClosureOutputTranslucent ClosureOutput
#define closure_Translucent_planar_eval(cl_in, cl_eval, cl_common, data, cl_out)
#define closure_Translucent_cubemap_eval(cl_in, cl_eval, cl_common, data, cl_out)

ClosureEvalTranslucent closure_Translucent_eval_init(inout ClosureInputTranslucent cl_in,
                                                     ClosureEvalCommon cl_common,
                                                     out ClosureOutputTranslucent cl_out)
{
  cl_in.N = safe_normalize(cl_in.N);
  cl_out.radiance = float3(0.0f);
  return CLOSURE_EVAL_DUMMY;
}

void closure_Translucent_light_eval(ClosureInputTranslucent cl_in,
                                    ClosureEvalTranslucent cl_eval,
                                    ClosureEvalCommon cl_common,
                                    ClosureLightData light,
                                    inout ClosureOutputTranslucent cl_out)
{
  float radiance = light_diffuse(light.data, cl_in.N, cl_common.V, light.L);
  cl_out.radiance += light.data.l_color * (light.data.l_diff * light.vis * radiance);
}

void closure_Translucent_grid_eval(ClosureInputTranslucent cl_in,
                                   ClosureEvalTranslucent cl_eval,
                                   ClosureEvalCommon cl_common,
                                   ClosureGridData grid,
                                   inout ClosureOutputTranslucent cl_out)
{
  float3 probe_radiance = probe_evaluate_grid(grid.data, cl_common.P, cl_in.N, grid.local_pos);
  cl_out.radiance += grid.attenuation * probe_radiance;
}

void closure_Translucent_indirect_end(ClosureInputTranslucent cl_in,
                                      ClosureEvalTranslucent cl_eval,
                                      ClosureEvalCommon cl_common,
                                      inout ClosureOutputTranslucent cl_out)
{
  /* If not enough light has been accumulated from probes, use the world specular cube-map
   * to fill the remaining energy needed. */
  if (cl_common.diffuse_accum > 0.0f) {
    float3 probe_radiance = probe_evaluate_world_diff(cl_in.N);
    cl_out.radiance += cl_common.diffuse_accum * probe_radiance;
  }
}

void closure_Translucent_eval_end(ClosureInputTranslucent cl_in,
                                  ClosureEvalTranslucent cl_eval,
                                  ClosureEvalCommon cl_common,
                                  inout ClosureOutputTranslucent cl_out)
{
  cl_out.radiance = render_pass_diffuse_mask(cl_out.radiance);
#if defined(DEPTH_SHADER) || defined(WORLD_BACKGROUND)
  /* This makes shader resources become unused and avoid issues with samplers. (see #59747) */
  cl_out.radiance = float3(0.0f);
  return;
#endif
}
