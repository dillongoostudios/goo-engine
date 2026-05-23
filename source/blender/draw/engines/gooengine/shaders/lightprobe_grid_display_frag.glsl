/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "goo_common_view_lib.glsl"
#include "irradiance_lib.glsl"

void main()
{
  float dist_sqr = dot(quadCoord, quadCoord);

  /* Discard outside the circle. */
  if (dist_sqr > 1.0f) {
    gpu_discard_fragment();
    return;
  }

  float3 view_nor = float3(quadCoord, sqrt(max(0.0f, 1.0f - dist_sqr)));
  float3 world_nor = to_float3x3(ViewMatrixInverseGoo) * view_nor;
  IrradianceData ir_data = load_irradiance_cell(cellOffset, world_nor);
  FragColor = float4(compute_irradiance(world_nor, ir_data), 1.0f);
}
