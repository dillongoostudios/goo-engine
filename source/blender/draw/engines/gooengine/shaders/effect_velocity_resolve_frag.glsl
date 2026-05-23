/* SPDX-FileCopyrightText: 2018-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "draw_math_geom_lib.glsl"

void main()
{
  /* Extract pixel motion vector from camera movement. */
  int2 texel = int2(gl_FragCoord.xy);
  float2 uv_curr = gl_FragCoord.xy / float2(textureSize(depthBuffer, 0).xy);

  float depth = texelFetch(depthBuffer, texel, 0).r;

  uv_curr = uv_curr * 2.0f - 1.0f;
  depth = depth * 2.0f - 1.0f;

  float3 world_position = project_point(currViewProjMatrixInv, float3(uv_curr, depth));
  float2 uv_prev = project_point(prevViewProjMatrix, world_position).xy;
  float2 uv_next = project_point(nextViewProjMatrix, world_position).xy;

  outData.xy = uv_prev - uv_curr;
  outData.zw = uv_next - uv_curr;

  /* Encode to unsigned normalized 16bit texture. */
  outData = outData * 0.5f + 0.5f;
}
