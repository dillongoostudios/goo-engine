/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Required by some nodes. */
#include "common_hair_lib.glsl"
#include "common_utiltex_lib.glsl"
#include "common_math_lib.glsl"
#include "common_math_geom_lib.glsl"

#include "goo_common_view_lib.glsl"
#include "common_uniforms_lib.glsl"
#include "closure_eval_surface_lib.glsl"
#include "surface_lib.glsl"

#ifdef USE_ALPHA_HASH

/* From the paper "Hashed Alpha Testing" by Chris Wyman and Morgan McGuire */
float hash(vec2 a)
{
  return fract(1e4 * sin(17.0 * a.x + 0.1 * a.y) * (0.1 + abs(sin(13.0 * a.y + a.x))));
}

float hash3d(vec3 a)
{
  return hash(vec2(hash(a.xy), a.z));
}

float hashed_alpha_threshold(vec3 co)
{
  /* Find the discretized derivatives of our coordinates. */
  float max_deriv = max(length(dFdx(co)), length(dFdy(co)));
  float pix_scale = 1.0 / (alphaHashScale * max_deriv);

  /* Find two nearest log-discretized noise scales. */
  float pix_scale_log = log2(pix_scale);
  vec2 pix_scales;
  pix_scales.x = exp2(floor(pix_scale_log));
  pix_scales.y = exp2(ceil(pix_scale_log));

  /* Compute alpha thresholds at our two noise scales. */
  vec2 alpha;
  alpha.x = hash3d(floor(pix_scales.x * co));
  alpha.y = hash3d(floor(pix_scales.y * co));

  /* Factor to interpolate lerp with. */
  float fac = fract(log2(pix_scale));

  /* Interpolate alpha threshold from noise at two scales. */
  float x = mix(alpha.x, alpha.y, fac);

  /* Pass into CDF to compute uniformly distributed threshold. */
  float a = min(fac, 1.0 - fac);
  float one_a = 1.0 - a;
  float denom = 1.0 / (2 * a * one_a);
  float one_x = (1 - x);
  vec3 cases = vec3((x * x) * denom, (x - 0.5 * a) / one_a, 1.0 - (one_x * one_x * denom));

  /* Find our final, uniformly distributed alpha threshold. */
  float threshold = (x < one_a) ? ((x < a) ? cases.x : cases.y) : cases.z;

  /* Jitter the threshold for TAA accumulation. */
  threshold = fract(threshold + alphaHashOffset);

  /* Avoids threshold == 0. */
  threshold = clamp(threshold, 1.0e-6, 1.0);

  return threshold;
}

#endif

void main()
{
#if defined(USE_ALPHA_HASH)
  g_data = init_globals();

  Closure cl = nodetree_exec();

  float opacity = saturate(1.0 - avg(cl.transmittance));

  if (alphaClipThreshold == -1.0) {
    /* NOTE: uniform control flow required for dFdx(). */
    if (opacity < hashed_alpha_threshold(worldPosition)) {
      discard;
    }
  }
  else {
    if (opacity <= alphaClipThreshold) {
      discard;
    }
  }
#endif

  /* ISS013_FIXL: out_normal now declared at location 1 (RG16 ssr_normal buffer) to match the
   * gtao/SSR normalBuffer readers (.rg). resource_id_out removed (vestigial, never read). */
  /* GooEngine Fix: Output Normal to RG16 buffer (Loc 1).
   * Use viewNormal from vertex shader interface, which is always initialized.
   * g_data.N is only initialized when USE_ALPHA_HASH is defined. */
#ifndef SHADOW_PASS
  /* viewNormal comes from surface_lib interface, already in view space. */
  vec3 N = normalize(viewNormal);
  out_normal = normal_encode(N, vec3(0.0));
#else
  /* Fix S (ISS-017/018): restore the upstream shadow-ID write (Fix L removed it as "vestigial" —
   * wrong: sample_ID_texture reads this pool to suppress same-object self-shadow; live on GL).
   * The shadow FB binds the R16UI id pool at attachment 1. Explicit uint() cast for MSL (C04). */
  resource_id_out = uint(resource_id);
#endif
}

/* Passthrough. */
float attr_load_temperature_post(float attr)
{
  return attr;
}
vec4 attr_load_color_post(vec4 attr)
{
  return attr;
}
vec4 attr_load_uniform(vec4 attr, const uint attr_hash)
{
  return attr;
}
