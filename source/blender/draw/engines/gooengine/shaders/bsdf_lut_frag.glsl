/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "common_utiltex_lib.glsl"
#include "bsdf_sampling_lib.glsl"

/* Generate BRDF LUT following "Real shading in unreal engine 4" by Brian Karis
 * https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
 * Parametrizing with `x = roughness` and `y = sqrt(1.0f - cos(theta))`.
 * The result is interpreted as: `integral = f0 * scale + f90 * bias`. */
void main()
{
  /* Make sure coordinates are covering the whole [0..1f] range at texel center. */
  float x = floor(gl_FragCoord.x) / (LUT_SIZE - 1);
  float y = floor(gl_FragCoord.y) / (LUT_SIZE - 1);

  /* Squaring for perceptually linear roughness, see [Physically Based Shading at Disney]
   * (https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf)
   * Section 5.4f. */
  float roughness = x * x;
  float roughness_sq = roughness * roughness;

  float NV = clamp(1.0f - y * y, 1e-4f, 0.9999f);
  float3 V = float3(sqrt(1.0f - NV * NV), 0.0f, NV);

  /* Integrating BRDF */
  float scale = 0.0f;
  float bias = 0.0f;
  for (float j = 0.0f; j < sampleCount; j++) {
    for (float i = 0.0f; i < sampleCount; i++) {
      float3 Xi = (float3(i, j, 0.0f) + 0.5f) / sampleCount;
      Xi.yz = float2(cos(Xi.y * M_2PI), sin(Xi.y * M_2PI));

      /* Microfacet normal */
      float3 H = sample_ggx(Xi, roughness, V);
      float3 L = -reflect(V, H);
      float NL = L.z;

      if (NL > 0.0f) {
        /* Assuming sample visible normals, `weight = brdf * NV / (pdf * fresnel).` */
        float weight = bxdf_ggx_smith_G1(NL, roughness_sq);

        /* Schlick's Fresnel. */
        float s = pow(1.0f - saturate(dot(V, H)), 5.0f);

        scale += (1.0f - s) * weight;
        bias += s * weight;
      }
    }
  }
  scale /= sampleCount * sampleCount;
  bias /= sampleCount * sampleCount;

  FragColor = float2(scale, bias);
}
