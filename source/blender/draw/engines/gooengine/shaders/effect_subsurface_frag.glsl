/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "goo_common_view_lib.glsl"
#include "draw_math_geom_lib.glsl"
#include "common_utiltex_lib.glsl"
#include "common_uniforms_lib.glsl"

/* Based on Separable SSS. by Jorge Jimenez and Diego Gutierrez */

void main(void)
{
  float2 pixel_size = 1.0f / float2(textureSize(depthBuffer, 0).xy); /* TODO: precompute. */
  float2 uvs = gl_FragCoord.xy * pixel_size;
  float3 sss_irradiance = texture(sssIrradiance, uvs).rgb;
  float sss_radius = texture(sssRadius, uvs).r * radii_max_radius.w * avg_inv_radius;
  float depth = texture(depthBuffer, uvs).r;
  float depth_view = get_view_z_from_depth(depth);

  float rand = texelfetch_noise_tex(gl_FragCoord.xy).r;
#ifdef FIRST_PASS
  float angle = M_2PI * rand + M_PI_2;
  float2 dir = float2(1.0f, 0.0f);
#else /* SECOND_PASS */
  float angle = M_2PI * rand;
  float2 dir = float2(0.0f, 1.0f);
#endif
  float2 dir_rand = float2(cos(angle), sin(angle));

  /* Compute kernel bounds in 2D. */
  float homcoord = ProjectionMatrixGoo[2][3] * depth_view + ProjectionMatrixGoo[3][3];
  float2 scale = float2(ProjectionMatrixGoo[0][0], ProjectionMatrixGoo[1][1]) * sss_radius / homcoord;
  float2 finalStep = scale * 0.5f; /* samples range -1..1f */

  float sss_radius_inv = 1.0f / max(1e-8f, sss_radius);

  /* Center sample */
  float3 accum = sss_irradiance * sss_kernel[0].rgb;

  for (int i = 1; i < sss_samples && i < MAX_SSS_SAMPLES; i++) {
    float2 sample_uv = uvs + sss_kernel[i].a * finalStep *
                               ((abs(sss_kernel[i].a) > sssJitterThreshold) ? dir : dir_rand);
    float3 color = texture(sssIrradiance, sample_uv).rgb;
    float sample_depth = texture(depthBuffer, sample_uv).r;
    sample_depth = get_view_z_from_depth(sample_depth);
    /* Depth correction factor. See Real Time Realistic Skin Translucency 2010
     * by Jimenez, eqs. 2 and 9, and D9740.
     * Coefficient -2 follows from gaussian_profile() from gpu_material.c and
     * from the definition of finalStep. */
    float depth_delta = (depth_view - sample_depth) * sss_radius_inv;
    float s = exp(-2.0f * sqr(depth_delta));
    /* Out of view samples. */
    if (any(lessThan(sample_uv, float2(0.0f))) || any(greaterThan(sample_uv, float2(1.0f)))) {
      s = 0.0f;
    }
    /* Mix with first sample in failure case and apply sss_kernel color. */
    accum += sss_kernel[i].rgb * mix(sss_irradiance, color, s);
  }

#if defined(FIRST_PASS)
  sssRadiance = float4(accum, 1.0f);
#else /* SECOND_PASS */
  sssRadiance = float4(accum * texture(sssAlbedo, uvs).rgb, 1.0f);
#endif
}
