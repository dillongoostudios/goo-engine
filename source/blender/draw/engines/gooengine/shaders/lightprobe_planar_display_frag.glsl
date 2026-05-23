/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "goo_common_view_lib.glsl"

void main()
{
  float4 refco = ProjectionMatrixGoo * (ViewMatrixGoo * float4(worldPosition, 1.0f));
  refco.xy /= refco.w;
  FragColor = float4(
      textureLod(probePlanars, float3(refco.xy * float2(-0.5f, 0.5f) + 0.5f, float(probeIdx)), 0.0f).rgb,
      1.0f);
}
