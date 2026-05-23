/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "draw_math_geom_lib.glsl"

float3 diffuse_dominant_dir(float3 bent_normal)
{
  return bent_normal;
}

float3 specular_dominant_dir(float3 N, float3 V, float roughness)
{
  float3 R = -reflect(V, N);
  float smoothness = 1.0f - roughness;
  float fac = smoothness * (sqrt(smoothness) + roughness);
  return normalize(mix(N, R, fac));
}

/* Simplified form of F_eta(eta, 1.0f). */
float F0_from_ior(float eta)
{
  float A = (eta - 1.0f) / (eta + 1.0f);
  return A * A;
}

float3 refraction_dominant_dir(float3 N, float3 V, float roughness, float ior)
{
  /* TODO: This a bad approximation. Better approximation should fit
   * the refracted vector and roughness into the best prefiltered reflection
   * lobe. */
  /* Correct the IOR for ior < 1.0f to not see the abrupt delimitation or the TIR */
  ior = (ior < 1.0f) ? mix(ior, 1.0f, roughness) : ior;
  float eta = 1.0f / ior;

  float NV = dot(N, -V);

  /* Custom Refraction. */
  float k = 1.0f - eta * eta * (1.0f - NV * NV);
  k = max(0.0f, k); /* Only this changes. */
  float3 R = eta * -V - (eta * NV + sqrt(k)) * N;

  return R;
}

/* Fresnel monochromatic, perfect mirror */
float F_eta(float eta, float cos_theta)
{
  /* compute fresnel reflectance without explicitly computing
   * the refracted direction */
  float c = abs(cos_theta);
  float g = eta * eta - 1.0f + c * c;
  if (g > 0.0f) {
    g = sqrt(g);
    float A = (g - c) / (g + c);
    float B = (c * (g + c) - 1.0f) / (c * (g - c) + 1.0f);
    return 0.5f * A * A * (1.0f + B * B);
  }
  /* Total internal reflections. */
  return 1.0f;
}

/* Fresnel color blend base on fresnel factor */
float3 F_color_blend(float eta, float fresnel, float3 F0_color)
{
  float F0 = F0_from_ior(eta);
  float fac = saturate((fresnel - F0) / (1.0f - F0));
  return mix(F0_color, float3(1.0f), fac);
}

/* Fresnel split-sum approximation. */
float3 F_brdf_single_scatter(float3 F0, float3 F90, float2 lut)
{
  return F0 * lut.x + F90 * lut.y;
}

/* Multi-scattering brdf approximation from
 * "A Multiple-Scattering Microfacet Model for Real-Time Image-based Lighting"
 * https://jcgt.org/published/0008/01/03/paper.pdf by Carmelo J. Fdez-Agüera. */
float3 F_brdf_multi_scatter(float3 F0, float3 F90, float2 lut)
{
  float3 FssEss = F_brdf_single_scatter(F0, F90, lut);

  float Ess = lut.x + lut.y;
  float Ems = 1.0f - Ess;
  float3 Favg = F0 + (F90 - F0) / 21.0f;

  /* The original paper uses `FssEss * radiance + Fms*Ems * irradiance`, but
   * "A Journey Through Implementing Multi-scattering BRDFs and Area Lights" by Steve McAuley
   * suggests to use `FssEss * radiance + Fms*Ems * radiance` which results in comparable quality.
   * We handle `radiance` outside of this function, so the result simplifies to:
   * `FssEss + Fms*Ems = FssEss * (1 + Ems*Favg / (1 - Ems*Favg)) = FssEss / (1 - Ems*Favg)`.
   * This is a simple albedo scaling very similar to the approach used by Cycles:
   * "Practical multiple scattering compensation for microfacet model". */
  return FssEss / (1.0f - Ems * Favg);
}

/* GGX */
float bxdf_ggx_D(float NH, float a2)
{
  return a2 / (M_PI * sqr((a2 - 1.0f) * (NH * NH) + 1.0f));
}

float D_ggx_opti(float NH, float a2)
{
  float tmp = (NH * a2 - NH) * NH + 1.0f;
  return M_PI * tmp * tmp; /* Doing RCP and multiply a2 at the end. */
}

float bxdf_ggx_smith_G1(float NX, float a2)
{
  return 2.0f / (1.0f + sqrt(1.0f + a2 * (1.0f / (NX * NX) - 1.0f)));
}

float G1_Smith_GGX_opti(float NX, float a2)
{
  /* Using Brian Karis approach and refactoring by NX/NX
   * this way the (2*NL)*(2*NV) in G = G1(V) * G1(L) gets canceled by the brdf denominator 4*NL*NV
   * RCP is done on the whole G later
   * Note that this is not convenient for the transmission formula */
  return NX + sqrt(NX * (NX - NX * a2) + a2);
  // return 2 / (1 + sqrt(1 + a2 * (1 - NX*NX) / (NX*NX) ) ); /* Reference function. */
}

/* Compute the GGX BRDF without the Fresnel term, multiplied by the cosine foreshortening term. */
float bsdf_ggx(float3 N, float3 L, float3 V, float roughness)
{
  float a = roughness;
  float a2 = a * a;

  float3 H = normalize(L + V);
  float NH = max(dot(N, H), 1e-8f);
  float NL = max(dot(N, L), 1e-8f);
  float NV = max(dot(N, V), 1e-8f);

  float G = bxdf_ggx_smith_G1(NV, a2) * bxdf_ggx_smith_G1(NL, a2);
  float D = bxdf_ggx_D(NH, a2);

  /* brdf * NL =  `((D * G) / (4 * NV * NL)) * NL`. */
  return (0.25f * D * G) / NV;
}

void accumulate_light(float3 light, float fac, inout float4 accum)
{
  accum += float4(light, 1.0f) * min(fac, (1.0f - accum.a));
}

/* Same thing as Cycles without the comments to make it shorter. */
float3 ensure_valid_specular_reflection(float3 Ng, float3 I, float3 N)
{
  float3 R = -reflect(I, N);

  float Iz = dot(I, Ng);

  /* Reflection rays may always be at least as shallow as the incoming ray. */
  float threshold = min(0.9f * Iz, 0.025f);
  if (dot(Ng, R) >= threshold) {
    return N;
  }

  float3 X = normalize(N - dot(N, Ng) * Ng);
  if (any(isnan(X))) {
    X = N;
  }
  float Ix = dot(I, X);

  float a = sqr(Ix) + sqr(Iz);
  float b = 2.0f * (a + Iz * threshold);
  float c = sqr(threshold + Iz);

  float Nz2 = (Ix < 0.0f) ? 0.25f * (b + safe_sqrt(sqr(b) - 4.0f * a * c)) / a :
                           0.25f * (b - safe_sqrt(sqr(b) - 4.0f * a * c)) / a;

  float Nx = safe_sqrt(1.0f - Nz2);
  float Nz = safe_sqrt(Nz2);

  return Nx * X + Nz * Ng;
}

/* ----------- Cone angle Approximation --------- */

/* Return a fitted cone angle given the input roughness */
float cone_cosine(float r)
{
  /* Using phong gloss
   * roughness = sqrt(2/(gloss+2)) */
  float gloss = -2 + 2 / (r * r);
  /* Drobot 2014 in GPUPro5 */
  // return cos(2.0f * sqrt(2.0f / (gloss + 2)));
  /* Uludag 2014 in GPUPro5 */
  // return pow(0.244f, 1 / (gloss + 1));
  /* Jimenez 2016 in Practical Realtime Strategies for Accurate Indirect Occlusion. */
  return exp2(-3.32193f * r * r);
}
