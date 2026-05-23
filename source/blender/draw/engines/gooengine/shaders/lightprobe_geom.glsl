/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

constexpr float3 maj_axes[6] = float3_array(float3(1.0f, 0.0f, 0.0f),
                                 float3(-1.0f, 0.0f, 0.0f),
                                 float3(0.0f, 1.0f, 0.0f),
                                 float3(0.0f, -1.0f, 0.0f),
                                 float3(0.0f, 0.0f, 1.0f),
                                 float3(0.0f, 0.0f, -1.0f));
constexpr float3 x_axis[6] = float3_array(float3(0.0f, 0.0f, -1.0f),
                               float3(0.0f, 0.0f, 1.0f),
                               float3(1.0f, 0.0f, 0.0f),
                               float3(1.0f, 0.0f, 0.0f),
                               float3(1.0f, 0.0f, 0.0f),
                               float3(-1.0f, 0.0f, 0.0f));
constexpr float3 y_axis[6] = float3_array(float3(0.0f, -1.0f, 0.0f),
                               float3(0.0f, -1.0f, 0.0f),
                               float3(0.0f, 0.0f, 1.0f),
                               float3(0.0f, 0.0f, -1.0f),
                               float3(0.0f, -1.0f, 0.0f),
                               float3(0.0f, -1.0f, 0.0f));

void main()
{
  geom_iface_flat.fFace = vert_iface_flat[0].face;
  gl_Layer = Layer + geom_iface_flat.fFace;

  for (int v = 0; v < 3; v++) {
    gl_Position = vert_iface[v].vPos;
    geom_iface.worldPosition = x_axis[geom_iface_flat.fFace] * vert_iface[v].vPos.x +
                               y_axis[geom_iface_flat.fFace] * vert_iface[v].vPos.y +
                               maj_axes[geom_iface_flat.fFace];
    gpu_EmitVertex();
  }

  EndPrimitive();
}
