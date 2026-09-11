/* SPDX-FileCopyrightText: 2025 Goo Engine Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Goo Screen Space Info reads engine-owned current-sample snapshots when Raytraced
 * Transmission is enabled. It does not imply a transparent or refraction closure. */

#include "gpu_shader_material_transform_utils.glsl"

/* Default View Position when the socket is unlinked: the fragment's own view position in Goo's
 * convention (+Z forward). node_screenspace_info flips it back before projecting, so an unlinked
 * node samples its own pixel -- matching Goo's view_position_get. */
[[node]] void view_position_get(out float3 P)
{
  float3 vP;
  point_transform_world_to_view(g_data.P, vP);
  P = vP * float3(1.0f, 1.0f, -1.0f);
}

[[node]] void node_screenspace_info(float3 view_position,
                                    float4 &scene_color,
                                    float &scene_depth)
{
  scene_color = float4(0.0f, 0.0f, 0.0f, 1.0f);
  scene_depth = 0.0f;
  screenspace_info_eval(view_position, scene_color, scene_depth);
}
