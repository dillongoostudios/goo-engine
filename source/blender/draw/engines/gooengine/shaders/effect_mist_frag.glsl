/* SPDX-FileCopyrightText: 2018-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "draw_math_geom_lib.glsl"
#include "goo_common_view_lib.glsl"

#define mistStart mistSettings.x
#define mistInvDistance mistSettings.y
#define mistFalloff mistSettings.z

void main()
{
  float2 texel_size = 1.0f / float2(textureSize(depthBuffer, 0)).xy;
  float2 uvs = gl_FragCoord.xy * texel_size;

  float depth = textureLod(depthBuffer, uvs, 0.0f).r;
  float3 co = get_view_space_from_depth(uvs, depth);

  float zcor = (ProjectionMatrixGoo[3][3] == 0.0f) ? length(co) : -co.z;

  /* bring depth into 0..1f range */
  float mist = saturate((zcor - mistStart) * mistInvDistance);

  /* falloff */
  mist = pow(mist, mistFalloff);

  FragColor = float4(mist);

  // if (mist > 0.999f) FragColor = float4(1.0f);
  // else if (mist > 0.0001f) FragColor = float4(0.5f);
  // else FragColor = float4(0.0f);
}
