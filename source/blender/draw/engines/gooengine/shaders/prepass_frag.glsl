/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Required by some nodes. */
#include "gooengine_nodetree_lib.glsl"
#include "draw_curves_lib.glsl"
#include "common_utiltex_lib.glsl"

#include "common_uniforms_lib.glsl"
#include "closure_eval_surface_lib.glsl"
#include "surface_lib.glsl"

#ifdef USE_ALPHA_HASH

/* From the paper "Hashed Alpha Testing" by Chris Wyman and Morgan McGuire */
float hash(float2 a)
{
  return fract(1e4f * sin(17.0f * a.x + 0.1f * a.y) * (0.1f + abs(sin(13.0f * a.y + a.x))));
}

float hash3d(float3 a)
{
  return hash(float2(hash(a.xy), a.z));
}

float hashed_alpha_threshold(float3 co)
{
  /* Find the discretized derivatives of our coordinates. */
  float max_deriv = max(length(gpu_dfdx(co)), length(gpu_dfdy(co)));
  float pix_scale = 1.0f / (alphaHashScale * max_deriv);

  /* Find two nearest log-discretized noise scales. */
  float pix_scale_log = log2(pix_scale);
  float2 pix_scales;
  pix_scales.x = exp2(floor(pix_scale_log));
  pix_scales.y = exp2(ceil(pix_scale_log));

  /* Compute alpha thresholds at our two noise scales. */
  float2 alpha;
  alpha.x = hash3d(floor(pix_scales.x * co));
  alpha.y = hash3d(floor(pix_scales.y * co));

  /* Factor to interpolate lerp with. */
  float fac = fract(log2(pix_scale));

  /* Interpolate alpha threshold from noise at two scales. */
  float x = mix(alpha.x, alpha.y, fac);

  /* Pass into CDF to compute uniformly distributed threshold. */
  float a = min(fac, 1.0f - fac);
  float one_a = 1.0f - a;
  float denom = 1.0f / (2 * a * one_a);
  float one_x = (1 - x);
  float3 cases = float3((x * x) * denom, (x - 0.5f * a) / one_a, 1.0f - (one_x * one_x * denom));

  /* Find our final, uniformly distributed alpha threshold. */
  float threshold = (x < one_a) ? ((x < a) ? cases.x : cases.y) : cases.z;

  /* Jitter the threshold for TAA accumulation. */
  threshold = fract(threshold + alphaHashOffset);

  /* Avoids threshold == 0. */
  threshold = clamp(threshold, 1.0e-6f, 1.0f);

  return threshold;
}

#endif

void main()
{
#if defined(USE_ALPHA_HASH)
  g_data = init_globals();

  Closure cl = nodetree_exec();

  float opacity = saturate(1.0f - avg(cl.transmittance));

  if (alphaClipThreshold == -1.0f) {
    /* NOTE: uniform control flow required for gpu_dfdx(). */
    if (opacity < hashed_alpha_threshold(worldPosition)) {
      gpu_discard_fragment();
    }
  }
  else {
    if (opacity <= alphaClipThreshold) {
      gpu_discard_fragment();
    }
  }
#endif

  resource_id_out = resource_id;
}

/* Passthrough. */
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
