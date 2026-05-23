/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Simple down-sample shader.
 * Do a gaussian filter using 4 bilinear texture samples.
 */

#include "draw_math_geom_lib.glsl"

void main()
{
#ifdef COPY_SRC
  float2 uvs = gl_FragCoord.xy / float2(textureSize(source, 0));
  FragColor = textureLod(source, uvs, 0.0f);
  FragColor = safe_color(FragColor);

  /* Clamped brightness. */
  float luma = max(1e-8f, max_v3(FragColor.rgb));
  FragColor *= 1.0f - max(0.0f, luma - fireflyFactor) / luma;

#else
  /* NOTE(@fclem): textureSize() does not work the same on all implementations
   * when changing the min and max texture levels. Use uniform instead (see #87801). */
  float2 uvs = gl_FragCoord.xy * texelSize;
  float4 ofs = texelSize.xyxy * float4(0.75f, 0.75f, -0.75f, -0.75f);
  uvs *= 2.0f;

  FragColor = textureLod(source, uvs + ofs.xy, 0.0f);
  FragColor += textureLod(source, uvs + ofs.xw, 0.0f);
  FragColor += textureLod(source, uvs + ofs.zy, 0.0f);
  FragColor += textureLod(source, uvs + ofs.zw, 0.0f);
  FragColor *= 0.25f;
#endif
}
