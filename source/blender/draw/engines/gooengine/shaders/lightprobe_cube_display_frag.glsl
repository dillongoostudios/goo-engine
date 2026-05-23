/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "goo_common_view_lib.glsl"
#include "lightprobe_lib.glsl"

void main()
{
  float dist_sqr = dot(quadCoord, quadCoord);

  /* Discard outside the circle. */
  if (dist_sqr > 1.0f) {
    gpu_discard_fragment();
    return;
  }

  float3 view_nor = float3(quadCoord, sqrt(max(0.0f, 1.0f - dist_sqr)));
  float3 world_ref = to_float3x3(ViewMatrixInverseGoo) * reflect(float3(0.0f, 0.0f, -1.0f), view_nor);
  FragColor = float4(textureLod(probeCubes, float4(world_ref, pid), 0.0f).rgb, 1.0f);
}
