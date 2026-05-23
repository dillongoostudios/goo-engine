/* SPDX-FileCopyrightText: 2022-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Non-geometry shader equivalent for eevee_legacy_lightprobe_vert + eevee_legacy_lightprobe_geom.
 * generates geometry instance per cube-face for multi-layered rendering. */
void main()
{

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
  geom_iface_flat.fFace = gl_InstanceID;
  gl_Position = float4(pos.xyz, 1.0f);
  geom_iface.worldPosition = x_axis[geom_iface_flat.fFace] * pos.x +
                             y_axis[geom_iface_flat.fFace] * pos.y +
                             maj_axes[geom_iface_flat.fFace];

#ifdef GPU_METAL
  /* In the Metal API, gl_Layer equivalent is specified in the vertex shader for multilayered
   * rendering support. */
  gpu_Layer = Layer + geom_iface_flat.fFace;
#endif
}
