/* SPDX-FileCopyrightText: 2017-2020 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

float2 mapping_octahedron(float3 cubefloat, float2 texel_size)
{
  /* projection onto octahedron */
  cubefloat /= dot(float3(1.0f), abs(cubefloat));

  /* out-folding of the downward faces */
  if (cubefloat.z < 0.0f) {
    float2 cubefloat_sign = step(0.0f, cubefloat.xy) * 2.0f - 1.0f;
    cubefloat.xy = (1.0f - abs(cubefloat.yx)) * cubefloat_sign;
  }

  /* mapping to [0;1]ˆ2 texture space */
  float2 uvs = cubefloat.xy * (0.5f) + 0.5f;

  /* edge filtering fix */
  uvs = (1.0f - 2.0f * texel_size) * uvs + texel_size;

  return uvs;
}
