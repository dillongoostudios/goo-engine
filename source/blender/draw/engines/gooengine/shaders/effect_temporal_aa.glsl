/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "draw_math_geom_lib.glsl"
#include "goo_common_view_lib.glsl"

#if 0
uniform sampler2D colorBuffer;
uniform depth2D depthBuffer;
uniform sampler2D colorHistoryBuffer;

uniform float4x4 prevViewProjectionMatrix;

out float4 FragColor;
#endif

#ifdef USE_REPROJECTION

/**
 * Adapted from https://casual-effects.com/g3d/G3D10/data-files/shader/Film/Film_temporalAA.pix
 * which is adapted from
 * https://github.com/gokselgoktas/temporal-anti-aliasing/blob/master/Assets/Resources/Shaders/TemporalAntiAliasing.cginc
 * which is adapted from https://github.com/playdeadgames/temporal
 * Optimization by Stubbesaurus and epsilon adjustment to avoid division by zero.
 *
 * This can cause 3x3 blocks of color when there is a thin edge of a similar color that
 * is varying in intensity.
 */
float3 clip_to_aabb(float3 color, float3 minimum, float3 maximum, float3 average)
{
  /* NOTE: only clips towards aabb center (but fast!) */
  float3 center = 0.5f * (maximum + minimum);
  float3 extents = 0.5f * (maximum - minimum);
  float3 dist = color - center;
  float3 ts = abs(extents) / max(abs(dist), float3(0.0001f));
  float t = saturate(min_v3(ts));
  return center + dist * t;
}

/**
 * Vastly based on https://github.com/playdeadgames/temporal
 */
void main()
{
  float2 screen_res = float2(textureSize(colorBuffer, 0).xy);
  float2 uv = gl_FragCoord.xy / screen_res;
  int2 texel = int2(gl_FragCoord.xy);

  /* Compute pixel position in previous frame. */
  float depth = textureLod(depthBuffer, uv, 0.0f).r;
  float3 pos = get_world_space_from_depth(uv, depth);
  float2 uv_history = project_point(prevViewProjectionMatrix, pos).xy * 0.5f + 0.5f;

  /* HACK: Reject lookdev spheres from TAA reprojection. */
  if (depth == 0.0f) {
    uv_history = uv;
  }

  int2 texel_history = int2(uv_history * screen_res);
  float4 color_history = textureLod(colorHistoryBuffer, uv_history, 0.0f);

  /* Color bounding box clamping. 3x3 neighborhood. */
  float4 c02 = texelFetchOffset(colorBuffer, texel, 0, int2(-1, 1));
  float4 c12 = texelFetchOffset(colorBuffer, texel, 0, int2(0, 1));
  float4 c22 = texelFetchOffset(colorBuffer, texel, 0, int2(1, 1));
  float4 c01 = texelFetchOffset(colorBuffer, texel, 0, int2(-1, 0));
  float4 c11 = texelFetchOffset(colorBuffer, texel, 0, int2(0, 0));
  float4 c21 = texelFetchOffset(colorBuffer, texel, 0, int2(1, 0));
  float4 c00 = texelFetchOffset(colorBuffer, texel, 0, int2(-1, -1));
  float4 c10 = texelFetchOffset(colorBuffer, texel, 0, int2(0, -1));
  float4 c20 = texelFetchOffset(colorBuffer, texel, 0, int2(1, -1));

  float4 color = c11;

  /* AABB minmax */
  float4 min_col = min9(c02, c12, c22, c01, c11, c21, c00, c10, c20);
  float4 max_col = max9(c02, c12, c22, c01, c11, c21, c00, c10, c20);
  float4 avg_col = avg9(c02, c12, c22, c01, c11, c21, c00, c10, c20);

  /* bias the color aabb toward the center (rounding the shape) */
  float4 min_center = min5(c12, c01, c11, c21, c10);
  float4 max_center = max5(c12, c01, c11, c21, c10);
  float4 avg_center = avg5(c12, c01, c11, c21, c10);
  min_col = (min_col + min_center) * 0.5f;
  max_col = (max_col + max_center) * 0.5f;
  avg_col = (avg_col + avg_center) * 0.5f;

  /* Clip color toward the center of the neighborhood colors AABB box. */
  color_history.rgb = clip_to_aabb(color_history.rgb, min_col.rgb, max_col.rgb, avg_col.rgb);

  /* Luminance weighting. */
  /* TODO: correct luminance. */
  float lum0 = dot(color.rgb, float3(0.333f));
  float lum1 = dot(color_history.rgb, float3(0.333f));
  float diff = abs(lum0 - lum1) / max(lum0, max(lum1, 0.2f));
  float weight = 1.0f - diff;
  float alpha = mix(0.04f, 0.12f, weight * weight);

  color_history = mix(color_history, color, alpha);

  bool out_of_view = any(greaterThanEqual(abs(uv_history - 0.5f), float2(0.5f)));
  color_history = (out_of_view) ? color : color_history;

  FragColor = safe_color(color_history);
  /* There is some ghost issue if we use the alpha
   * in the viewport. Overwriting alpha fixes it. */
  FragColor.a = color.a;
}

#else

void main()
{
  int2 texel = int2(gl_FragCoord.xy);
  float4 color = texelFetch(colorBuffer, texel, 0);
  float4 color_history = texelFetch(colorHistoryBuffer, texel, 0);
  FragColor = safe_color(mix(color_history, color, alpha));
}
#endif
