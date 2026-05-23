/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "goo_common_view_lib.glsl"

void main()
{
  /* Constant array moved inside function scope.
   * Minimizes local register allocation in MSL. */
  constexpr float2 pos[6] = float2_array(float2(-1.0f, -1.0f),
                              float2(1.0f, -1.0f),
                              float2(-1.0f, 1.0f),

                              float2(1.0f, -1.0f),
                              float2(1.0f, 1.0f),
                              float2(-1.0f, 1.0f));

  pid = 1 + (gl_VertexID / 6); /* +1 for the world */
  int vert_id = gl_VertexID % 6;

  quadCoord = pos[vert_id];

  float3 ws_location = probes_data[pid].position_type.xyz;
  float3 screen_pos = ViewMatrixInverseGoo[0].xyz * quadCoord.x +
                    ViewMatrixInverseGoo[1].xyz * quadCoord.y;
  ws_location += screen_pos * sphere_size;

  gl_Position = ProjectionMatrixGoo * (ViewMatrixGoo * float4(ws_location, 1.0f));
  gl_Position.z += 0.0001f; /* Small bias to let the icon draw without Z-fighting. */
}
