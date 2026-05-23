/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "random_lib.glsl"
#include "bsdf_sampling_lib.glsl"
#include "draw_math_geom_lib.glsl"

float3 octahedral_to_cubemap_proj(float2 co)
{
  co = co * 2.0f - 1.0f;

  float2 abs_co = abs(co);
  float3 v = float3(co, 1.0f - (abs_co.x + abs_co.y));

  if (abs_co.x + abs_co.y > 1.0f) {
    v.xy = (abs(co.yx) - 1.0f) * -sign(co.xy);
  }

  return v;
}

void main()
{
  float3 N, T, B, V;

  float3 R = normalize(geom_iface.worldPosition);

  /* Isotropic assumption */
  N = V = R;

  make_orthonormal_basis(N, T, B); /* Generate tangent space */

  /* Integrating environment-map. */
  float weight = 0.0f;
  float3 out_radiance = float3(0.0f);
  for (float i = 0; i < sampleCount; i++) {
    float3 Xi = rand2d_to_cylinder(hammersley_2d(i, sampleCount));

    float pdf;
    /* Microfacet normal */
    float3 H = sample_ggx(Xi, probe_roughness, V, N, T, B, pdf);
    float3 L = -reflect(V, H);
    float NL = dot(N, L);

    if (NL > 0.0f) {
      float NH = max(1e-8f, dot(N, H)); /* cosTheta */

      /* Coarse Approximation of the mapping distortion
       * Unit Sphere -> Cube-map Face. */
      constexpr float dist = 4.0f * M_PI / 6.0f;
      /* http://http.developer.nvidia.com/GPUGems3/gpugems3_ch20.html : Equation 13 */
      float lod = clamp(lodFactor - 0.5f * log2(pdf * dist), 0.0f, lodMax);

      float3 l_col = textureLod(probeHdr, L, lod).rgb;

      /* Clamped brightness. */
      float luma = max(1e-8f, max_v3(l_col));
      l_col *= 1.0f - max(0.0f, luma - fireflyFactor) / luma;

      out_radiance += l_col * NL;
      weight += NL;
    }
  }

  FragColor = float4(intensityFac * out_radiance / weight, 1.0f);
}
