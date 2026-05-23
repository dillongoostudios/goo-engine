/* SPDX-FileCopyrightText: 2021-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Reduce pass: Downsample the color buffer to generate mipmaps.
 * Also decide if a pixel is to be convolved by scattering or gathering during the first pass.
 */

#include "effect_dof_lib.glsl"

#ifdef COPY_PASS

/* NOTE: Do not compare alpha as it is not scattered by the scatter pass. */
float dof_scatter_neighborhood_rejection(float3 color)
{
  DEFINE_DOF_QUAD_OFFSETS;
  color = min(float3(scatterColorNeighborMax), color);

  float validity = 0.0f;

  /* Centered in the middle of 4 quarter res texel. */
  float2 texel_size = 1.0f / float2(textureSize(downsampledBuffer, 0).xy);
  float2 uv = (gl_FragCoord.xy * 0.5f) * texel_size;

  float3 max_diff = float3(0.0f);
  for (int i = 0; i < 4; i++) {
    float2 sample_uv = uv + quad_offsets[i] * texel_size;
    float3 ref = textureLod(downsampledBuffer, sample_uv, 0.0f).rgb;

    ref = min(float3(scatterColorNeighborMax), ref);
    float diff = max_v3(max(float3(0.0f), abs(ref - color)));

    constexpr float rejection_threshold = 0.7f;
    diff = saturate(diff / rejection_threshold - 1.0f);
    validity = max(validity, diff);
  }

  return validity;
}

/* This avoids sprite popping in and out at the screen border and
 * drawing sprites larger than the screen. */
float dof_scatter_screen_border_rejection(float coc, float2 uv, float2 screen_size)
{
  float2 screen_pos = uv * screen_size;
  float min_screen_border_distance = min_v2(min(screen_pos, screen_size - screen_pos));
  /* Full-resolution to half-resolution CoC. */
  coc *= 0.5f;
  /* Allow 10px transition. */
  constexpr float rejection_hardness = 1.0f / 10.0f;
  return saturate((min_screen_border_distance - abs(coc)) * rejection_hardness + 1.0f);
}

float dof_scatter_luminosity_rejection(float3 color)
{
  constexpr float rejection_hardness = 1.0f;
  return saturate(max_v3(color - scatterColorThreshold) * rejection_hardness);
}

float dof_scatter_coc_radius_rejection(float coc)
{
  constexpr float rejection_hardness = 0.3f;
  return saturate((abs(coc) - scatterCocThreshold) * rejection_hardness);
}

float fast_luma(float3 color)
{
  return (2.0f * color.g) + color.r + color.b;
}

/* Lightweight version of neighborhood clamping found in TAA. */
float3 dof_neighborhood_clamping(float3 color)
{
  float2 texel_size = 1.0f / float2(textureSize(colorBuffer, 0));
  float2 uv = gl_FragCoord.xy * texel_size;
  float4 ofs = float4(-1, 1, -1, 1) * texel_size.xxyy;

  /* Luma clamping. 3x3 square neighborhood. */
  float c00 = fast_luma(textureLod(colorBuffer, uv + ofs.xz, 0.0f).rgb);
  float c01 = fast_luma(textureLod(colorBuffer, uv + ofs.xz * float2(1.0f, 0.0f), 0.0f).rgb);
  float c02 = fast_luma(textureLod(colorBuffer, uv + ofs.xw, 0.0f).rgb);

  float c10 = fast_luma(textureLod(colorBuffer, uv + ofs.xz * float2(0.0f, 1.0f), 0.0f).rgb);
  float c11 = fast_luma(color);
  float c12 = fast_luma(textureLod(colorBuffer, uv + ofs.xw * float2(0.0f, 1.0f), 0.0f).rgb);

  float c20 = fast_luma(textureLod(colorBuffer, uv + ofs.yz, 0.0f).rgb);
  float c21 = fast_luma(textureLod(colorBuffer, uv + ofs.yz * float2(1.0f, 0.0f), 0.0f).rgb);
  float c22 = fast_luma(textureLod(colorBuffer, uv + ofs.yw, 0.0f).rgb);

  float avg_luma = avg8(c00, c01, c02, c10, c12, c20, c21, c22);
  float max_luma = max8(c00, c01, c02, c10, c12, c20, c21, c22);

  float upper_bound = mix(max_luma, avg_luma, colorNeighborClamping);
  upper_bound = mix(c11, upper_bound, colorNeighborClamping);

  float clamped_luma = min(upper_bound, c11);

  return color * clamped_luma * safe_rcp(c11);
}

/* Simple copy pass where we select what pixels to scatter. Also the resolution might change.
 * NOTE: The texture can end up being too big because of the mipmap padding. We correct for
 * that during the convolution phase. */
void main()
{
  float2 halfres = float2(textureSize(colorBuffer, 0).xy);
  float2 uv = gl_FragCoord.xy / halfres;

  outColor = textureLod(colorBuffer, uv, 0.0f);
  outCoc = textureLod(cocBuffer, uv, 0.0f).r;

  outColor.rgb = dof_neighborhood_clamping(outColor.rgb);

  /* Only scatter if luminous enough. */
  float do_scatter = dof_scatter_luminosity_rejection(outColor.rgb);
  /* Only scatter if CoC is big enough. */
  do_scatter *= dof_scatter_coc_radius_rejection(outCoc);
  /* Only scatter if CoC is not too big to avoid performance issues. */
  do_scatter *= dof_scatter_screen_border_rejection(outCoc, uv, halfres);
  /* Only scatter if neighborhood is different enough. */
  do_scatter *= dof_scatter_neighborhood_rejection(outColor.rgb);
  /* For debugging. */
  do_scatter *= float(!no_scatter_pass);

  outScatterColor = mix(float3(0.0f), outColor.rgb, do_scatter);
  outColor.rgb = mix(outColor.rgb, float3(0.0f), do_scatter);

  /* Apply energy conservation to anamorphic scattered bokeh. */
  outScatterColor /= min_v2(bokehAnisotropy);
}

#else /* REDUCE_PASS */

/* Downsample pass done for each mip starting from mip1. */
void main()
{
  DEFINE_DOF_QUAD_OFFSETS
  float2 input_texel_size = 1.0f / float2(textureSize(colorBuffer, 0).xy);
  /* Center uv around the 4 pixels of the previous mip. */
  float2 quad_center = (floor(gl_FragCoord.xy) * 2.0f + 1.0f) * input_texel_size;

  float4 colors[4];
  float4 cocs;
  for (int i = 0; i < 4; i++) {
    float2 sample_uv = quad_center + quad_offsets[i] * input_texel_size;
    colors[i] = dof_load_gather_color(colorBuffer, sample_uv, 0.0f);
    cocs[i] = textureLod(cocBuffer, sample_uv, 0.0f).r;
  }

  float4 weights = dof_downsample_bilateral_coc_weights(cocs);
  weights *= dof_downsample_bilateral_color_weights(colors);
  /* Normalize so that the sum is 1. */
  weights *= safe_rcp(sum(weights));

  outColor = weighted_sum_array(colors, weights);
  outCoc = dot(cocs, weights);
}

#endif
