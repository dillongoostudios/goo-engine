/* SPDX-FileCopyrightText: 2021-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Gather Filter pass: Filter the gather pass result to reduce noise.
 *
 * This is a simple 3x3 median filter to avoid dilating highlights with a 3x3 max filter even if
 * cheaper.
 */

#include "effect_dof_lib.glsl"

/* From:
 * Implementing Median Filters in XC4000E FPGAs
 * JOHN L. SMITH, Univision Technologies Inc., Billerica, MA
 * http://users.utcluj.ro/~baruch/resources/Image/xl23_16.pdf
 * Figure 1 */

/* Outputs low median and high value of a triple. */
void lmh(float4 s1, float4 s2, float4 s3, out float4 l, out float4 m, out float4 h)
{
  /* From diagram, with nodes numbered from top to bottom. */
  float4 h1 = max(s2, s3);
  float4 l1 = min(s2, s3);

  float4 h2 = max(s1, l1);
  float4 l2 = min(s1, l1);

  float4 h3 = max(h2, h1);
  float4 l3 = min(h2, h1);

  l = l2;
  m = l3;
  h = h3;
}

float4 median_filter(sampler2D tex, float2 uv)
{
  float2 texel_size = 1.0f / float2(textureSize(tex, 0).xy);
  float4 samples[9];
  int s = 0;

  constexpr float2 ofs[9] = float2_array(float2(-1, -1),
                              float2(0, -1),
                              float2(1, -1),
                              float2(-1, 0),
                              float2(0, 0),
                              float2(1, 0),
                              float2(-1, 1),
                              float2(0, 1),
                              float2(1, 1));

  for (int s = 0; s < 9; s++) {
    samples[s] = textureLod(tex, uv + ofs[s] * texel_size, 0.0f);
  }

  if (no_gather_filtering) {
    return samples[4];
  }

  for (int s = 0; s < 9; s += 3) {
    lmh(samples[s], samples[s + 1], samples[s + 2], samples[s], samples[s + 1], samples[s + 2]);
  }
  /* Some aliases to better understand what's happening. */
  float4 L123 = samples[0 + 0], L456 = samples[3 + 0], L789 = samples[6 + 0];
  float4 M123 = samples[0 + 1], M456 = samples[3 + 1], M789 = samples[6 + 1];
  float4 H123 = samples[0 + 2], H456 = samples[3 + 2], H789 = samples[6 + 2];
  float4 dummy, l, m, h;
  /* Left nodes. */
  h = max(max(L123, L456), L789);
  /* Right nodes. */
  l = min(min(H123, H456), H789);
  /* Center nodes. */
  lmh(M123, M456, M789, dummy, m, dummy);
  /* Last bottom nodes. */
  lmh(l, m, h, dummy, m, dummy);

  return m;
}

void main()
{
  /* OPTI(fclem) Could early return on some tiles. */

  outColor = median_filter(colorBuffer, screen_uv);
  outWeight = median_filter(weightBuffer, screen_uv).r;
}
