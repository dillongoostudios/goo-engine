/* SPDX-FileCopyrightText: 2019-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

void world_normals_get(out vec3 N)
{
  N = g_data.N;
}

void world_position_get(out vec3 P)
{
  P = worldPosition;
}

void view_position_get(out vec3 P)
{
  P = viewPosition * vec3(1.0, 1.0, -1.0);
}

