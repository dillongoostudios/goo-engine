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

  int cell_id = gl_VertexID / 6;
  int vert_id = gl_VertexID % 6;

  float3 ls_cell_location;
  /* Keep in sync with update_irradiance_probe */
  ls_cell_location.z = float(cell_id % grid_resolution.z);
  ls_cell_location.y = float((cell_id / grid_resolution.z) % grid_resolution.y);
  ls_cell_location.x = float(cell_id / (grid_resolution.z * grid_resolution.y));

  cellOffset = offset + cell_id;

  float3 ws_cell_location = corner +
                          (increment_x * ls_cell_location.x + increment_y * ls_cell_location.y +
                           increment_z * ls_cell_location.z);

  quadCoord = pos[vert_id];
  float3 screen_pos = ViewMatrixInverseGoo[0].xyz * quadCoord.x +
                    ViewMatrixInverseGoo[1].xyz * quadCoord.y;
  ws_cell_location += screen_pos * sphere_size;

  gl_Position = ProjectionMatrixGoo * (ViewMatrixGoo * float4(ws_cell_location, 1.0f));
  gl_Position.z += 0.0001f; /* Small bias to let the icon draw without Z-fighting. */
}
