/* SPDX-FileCopyrightText: 2021-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "effect_dof_lib.glsl"

/* Load 4 Circle of confusion values. texel_co is centered around the 4 taps. */
float4 fetch_cocs(float2 texel_co)
{
  /* TODO(@fclem): The `textureGather(sampler, co, comp)` variant isn't here on some
   * implementations.
   */
#if 0  // GPU_ARB_texture_gather
  float2 uvs = texel_co / float2(textureSize(cocBuffer, 0));
  /* Reminder: Samples order is CW starting from top left. */
  cocs = textureGather(cocBuffer, uvs, isForegroundPass ? 0 : 1);
#else
  int2 texel = int2(texel_co - 0.5f);
  float4 cocs;
  cocs.x = texelFetchOffset(cocBuffer, texel, 0, int2(0, 1)).r;
  cocs.y = texelFetchOffset(cocBuffer, texel, 0, int2(1, 1)).r;
  cocs.z = texelFetchOffset(cocBuffer, texel, 0, int2(1, 0)).r;
  cocs.w = texelFetchOffset(cocBuffer, texel, 0, int2(0, 0)).r;
#endif

#ifdef DOF_FOREGROUND_PASS
  cocs *= -1.0f;
#endif

  cocs = max(float4(0.0f), cocs);
  /* We are scattering at half resolution, so divide CoC by 2. */
  return cocs * 0.5f;
}

void vertex_discard()
{
  /* Don't produce any fragments */
  gl_Position = float4(0.0f, 0.0f, 0.0f, 1.0f);
}

void main()
{
  DEFINE_DOF_QUAD_OFFSETS
  int2 tex_size = textureSize(cocBuffer, 0);

  int t_id = gl_VertexID / 3; /* Triangle Id */

  /* Some math to get the target pixel. */
  int2 texelco = int2(t_id % spritePerRow, t_id / spritePerRow) * 2;

  /* Center sprite around the 4 texture taps. */
  spritepos = float2(texelco) + 1.0f;

  cocs = fetch_cocs(spritepos);

  /* Early out from local CoC radius. */
  if (all(lessThan(cocs, float4(0.5f)))) {
    vertex_discard();
    return;
  }

  float2 input_texel_size = 1.0f / float2(tex_size);
  float2 quad_center = spritepos * input_texel_size;
  float4 colors[4];
  bool no_color = true;
  for (int i = 0; i < 4; i++) {
    float2 sample_uv = quad_center + quad_offsets[i] * input_texel_size;

    colors[i] = dof_load_scatter_color(colorBuffer, sample_uv, 0.0f);
    no_color = no_color && all(equal(colors[i].rgb, float3(0.0f)));
  }

  /* Early out from no color to scatter. */
  if (no_color) {
    vertex_discard();
    return;
  }

  weights = dof_layer_weight(cocs) * dof_sample_weight(cocs);
  /* Filter NaNs. */
  for (int i = 0; i < 4; i++) {
    if (isnan(weights[i]) || isinf(weights[i])) {
      weights[i] = 0.0f;
    }
  }

  color1 = colors[0] * weights[0];
  color2 = colors[1] * weights[1];
  color3 = colors[2] * weights[2];
  color4 = colors[3] * weights[3];

  /* Extend to cover at least the unit circle */
  constexpr float extend = (cos(M_PI_4) + 1.0f) * 2.0f;
  /* Crappy diagram
   * ex 1
   *    | \
   *    |   \
   *  1 |     \
   *    |       \
   *    |         \
   *  0 |     x     \
   *    |   Circle    \
   *    |   Origin      \
   * -1 0 --------------- 2
   *   -1     0     1     ex
   */

  /* Generate Triangle : less memory fetches from a VBO */
  int v_id = gl_VertexID % 3;                     /* Vertex Id */
  gl_Position.x = float(v_id / 2) * extend - 1.0f; /* int divisor round down */
  gl_Position.y = float(v_id % 2) * extend - 1.0f;
  gl_Position.z = 0.0f;
  gl_Position.w = 1.0f;

  spritesize = max_v4(cocs);

  /* Add 2.5f to max_coc because the max_coc may not be centered on the sprite origin
   * and because we smooth the bokeh shape a bit in the pixel shader. */
  gl_Position.xy *= spritesize * bokehAnisotropy + 2.5f;
  /* Position the sprite. */
  gl_Position.xy += spritepos;
  /* NDC range [-1..1f]. */
  gl_Position.xy = gl_Position.xy * targetTexelSize * 2.0f - 1.0f;

  /* Add 2.5f for the same reason but without the ratio. */
  spritesize += 2.5f;
}
