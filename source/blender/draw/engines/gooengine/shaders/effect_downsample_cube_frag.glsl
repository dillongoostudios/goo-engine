/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Simple down-sample shader. Takes the average of the 4 texels of lower mip.
 */

#include "draw_math_geom_lib.glsl"

void main()
{

  /* Global scope arrays get allocated using local memory in Metal. Moving inside function scope to
   * reduce register pressure. */
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

  float2 uvs = gl_FragCoord.xy * texelSize;

  uvs = 2.0f * uvs - 1.0f;

  float3 cubefloat = x_axis[geom_iface_flat.fFace] * uvs.x + y_axis[geom_iface_flat.fFace] * uvs.y +
                 maj_axes[geom_iface_flat.fFace];

  FragColor = textureLod(source, cubefloat, 0.0f);
}
