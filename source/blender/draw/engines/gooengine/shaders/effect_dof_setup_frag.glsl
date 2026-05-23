/* SPDX-FileCopyrightText: 2021-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Setup pass: CoC and luma aware downsample to half resolution of the input scene color buffer.
 *
 * An addition to the downsample CoC, we output the maximum slight out of focus CoC to be
 * sure we don't miss a pixel.
 */

#include "effect_dof_lib.glsl"

void main()
{
  DEFINE_DOF_QUAD_OFFSETS
  float2 fullres_texel_size = 1.0f / float2(textureSize(colorBuffer, 0).xy);
  /* Center uv around the 4 fullres pixels. */
  float2 quad_center = (floor(gl_FragCoord.xy) * 2.0f + 1.0f) * fullres_texel_size;

  float4 colors[4];
  float4 depths;
  for (int i = 0; i < 4; i++) {
    float2 sample_uv = quad_center + quad_offsets[i] * fullres_texel_size;
    colors[i] = safe_color(textureLod(colorBuffer, sample_uv, 0.0f));
    depths[i] = textureLod(depthBuffer, sample_uv, 0.0f).r;
  }

  float4 cocs = dof_coc_from_zdepth(depths);

  cocs = clamp(cocs, -bokehMaxSize, bokehMaxSize);

  float4 weights = dof_downsample_bilateral_coc_weights(cocs);
  weights *= dof_downsample_bilateral_color_weights(colors);
  /* Normalize so that the sum is 1. */
  weights *= safe_rcp(sum(weights));

  outColor = weighted_sum_array(colors, weights);
  outCoc.x = dot(cocs, weights);

  /* Max slight focus abs CoC. */

  /* Clamp to 0.5f if full in defocus to differentiate full focus tiles with coc == 0.0f.
   * This enables an optimization in the resolve pass. */
  constexpr float4 threshold = float4(layer_threshold + layer_offset);
  cocs = abs(cocs);
  bfloat4 defocus = greaterThan(cocs, threshold);
  bfloat4 focus = lessThanEqual(cocs, float4(0.5f));
  if (any(defocus) && any(focus)) {
    /* For the same reason as in the flatten pass. This is a case we cannot optimize for. */
    cocs = select(cocs, float4(DOF_TILE_MIXED), focus);
    cocs = select(cocs, float4(DOF_TILE_MIXED), defocus);
  }
  else {
    cocs = select(cocs, float4(DOF_TILE_FOCUS), focus);
    cocs = select(cocs, float4(DOF_TILE_DEFOCUS), defocus);
  }
  outCoc.y = max_v4(cocs);
}
