/* SPDX-FileCopyrightText: 2016-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "goo_common_view_lib.glsl"

void main()
{
  worldPosition = (probe_mat * float4(-pos.x, pos.y, 0.0f, 1.0f)).xyz;
  gl_Position = ProjectionMatrixGoo * (ViewMatrixGoo * float4(worldPosition, 1.0f));
  probeIdx = probe_id;
}
