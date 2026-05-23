/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Simple down-sample shader. Takes the average of the 4 texels of lower mip.
 */

#include "draw_math_geom_lib.glsl"

void main()
{
#if 0
  /* Reconstructing Target uvs like this avoid missing pixels if NPO2 */
  float2 uvs = gl_FragCoord.xy * 2.0f / float2(textureSize(source, 0).xy);

  FragColor = textureLod(source, float3(uvs, lightprobe_geom_iface.layer), 0.0f);
#else
  float2 texel_size = 1.0f / float2(textureSize(source, 0).xy);
  float2 uvs = gl_FragCoord.xy * 2.0f * texel_size;
  float4 ofs = texel_size.xyxy * float4(0.75f, 0.75f, -0.75f, -0.75f);

  FragColor = textureLod(source, float3(uvs + ofs.xy, lightprobe_geom_iface.layer), 0.0f);
  FragColor += textureLod(source, float3(uvs + ofs.xw, lightprobe_geom_iface.layer), 0.0f);
  FragColor += textureLod(source, float3(uvs + ofs.zy, lightprobe_geom_iface.layer), 0.0f);
  FragColor += textureLod(source, float3(uvs + ofs.zw, lightprobe_geom_iface.layer), 0.0f);
  FragColor *= 0.25f;

  /* Clamped brightness. */
  float luma = max(1e-8f, max_v3(FragColor.rgb));
  FragColor *= 1.0f - max(0.0f, luma - fireflyFactor) / luma;
#endif
}
