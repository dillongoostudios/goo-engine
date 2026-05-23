/* SPDX-FileCopyrightText: 2019-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

void world_normals_get(out float3 N)
{
  N = g_data.N;
}

void world_position_get(out float3 P)
{
#ifndef EEVEE_ENGINE
    return;
#else
    P = worldPosition;
#endif
}

void view_position_get(out float3 P)
{
#ifndef EEVEE_ENGINE
    return;
#else
    P = viewPosition * float3(1.0f, 1.0f, -1.0f);
#endif
}
