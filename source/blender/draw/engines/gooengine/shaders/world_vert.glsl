/* SPDX-FileCopyrightText: 2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "goo_common_view_lib.glsl"
#include "draw_math_geom_lib.glsl"
#include "common_utiltex_lib.glsl"

#include "closure_eval_surface_lib.glsl"

RESOURCE_ID_VARYING

void main()
{
  GPU_INTEL_VERTEX_SHADER_WORKAROUND

  PASS_RESOURCE_ID

  gl_Position = float4(pos, 1.0f, 1.0f);
  viewPosition = project_point(ProjectionMatrixInverseGoo, float3(pos, 0.0f));
  worldPosition = transform_point(ViewMatrixInverseGoo, viewPosition);
  /* Not usable. */
  viewNormal = float3(0.0f);
  worldNormal = float3(0.0f);
}
