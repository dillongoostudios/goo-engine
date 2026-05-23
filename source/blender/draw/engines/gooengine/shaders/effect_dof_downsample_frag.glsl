/* SPDX-FileCopyrightText: 2021-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Downsample pass: CoC aware downsample to quarter resolution.
 *
 * Pretty much identical to the setup pass but get CoC from buffer. Also does not
 * weight luma for the bilateral weights.
 */

#include "effect_dof_lib.glsl"

void main()
{
  DEFINE_DOF_QUAD_OFFSETS
  float2 halfres_texel_size = 1.0f / float2(textureSize(colorBuffer, 0).xy);
  /* Center uv around the 4 half-resolution pixels. */
  float2 quad_center = (floor(gl_FragCoord.xy) * 2.0f + 1.0f) * halfres_texel_size;

  float4 colors[4];
  float4 cocs;
  for (int i = 0; i < 4; i++) {
    float2 sample_uv = quad_center + quad_offsets[i] * halfres_texel_size;
    colors[i] = textureLod(colorBuffer, sample_uv, 0.0f);
    cocs[i] = textureLod(cocBuffer, sample_uv, 0.0f).r;
  }

  float4 weights = dof_downsample_bilateral_coc_weights(cocs);
  /* Normalize so that the sum is 1. */
  weights *= safe_rcp(sum(weights));

  outColor = weighted_sum_array(colors, weights);
}
