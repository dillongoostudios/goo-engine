/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Based on Frosbite Unified Volumetric.
 * https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite */

#include "volumetric_lib.glsl"

/* Step 4 : Apply final integration on top of the scene color. */

void main()
{
  float2 uvs = gl_FragCoord.xy / float2(textureSize(inSceneDepth, 0));
  float scene_depth = texture(inSceneDepth, uvs).r;

  float3 transmittance, scattering;
  volumetric_resolve(uvs, scene_depth, transmittance, scattering);

  /* Approximate volume alpha by using a monochromatic transmittance
   * and adding it to the scene alpha. */
  float alpha = dot(transmittance, float3(1.0f / 3.0f));

  FragColor0 = float4(scattering, 1.0f - alpha);
  FragColor1 = float4(transmittance, alpha);
}
