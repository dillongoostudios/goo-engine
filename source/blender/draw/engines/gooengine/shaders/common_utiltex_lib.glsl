/* SPDX-FileCopyrightText: 2020-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "bsdf_common_lib.glsl"

/* ---------------------------------------------------------------------- */
/** \name Utiltex
 *
 * Utiltex is a sampler2DArray that stores a number of useful small utilitary textures and lookup
 * tables.
 * \{ */

#if !defined(USE_GPU_SHADER_CREATE_INFO)

uniform sampler2DArray utilTex;

#endif

#define LUT_SIZE 64

#define LTC_MAT_LAYER 0
#define LTC_BRDF_LAYER 3
#define LTC_DISK_LAYER 3
#define BRDF_LUT_LAYER 1
#define NOISE_LAYER 2

/* Layers 4 to 20 are for BTDF LUT. */
#define lut_btdf_layer_first 4.0f
#define lut_btdf_layer_count 16.0f

/**
 * Reminder: The 4 noise values are based of 3 uncorrelated blue noises:
 * x : Uniformly distributed value [0..1f] (noise 1).
 * y : Uniformly distributed value [0..1f] (noise 2).
 * z,w : Uniformly distributed point on the unit circle [-1..1f] (noise 3).
 */
#define texelfetch_noise_tex(coord) texelFetch(utilTex, int3(int2(coord) % LUT_SIZE, 2.0f), 0)

/* Return texture coordinates to sample Surface LUT. */
float2 lut_coords(float cos_theta, float roughness)
{
  float2 coords = float2(roughness, sqrt(1.0f - cos_theta));
  /* scale and bias coordinates, for correct filtered lookup */
  return coords * (LUT_SIZE - 1.0f) / LUT_SIZE + 0.5f / LUT_SIZE;
}

/* Returns the GGX split-sum precomputed in LUT. */
float2 brdf_lut(float cos_theta, float roughness)
{
  return textureLod(utilTex, float3(lut_coords(cos_theta, roughness), BRDF_LUT_LAYER), 0.0f).rg;
}

void brdf_f82_tint_lut(float3 F0,
                       float3 F82,
                       float cos_theta,
                       float roughness,
                       bool do_multiscatter,
                       out float3 reflectance)
{
  float2 uv = lut_coords(cos_theta, roughness);
  float3 split_sum = textureLod(utilTex, float3(uv, BRDF_LUT_LAYER), 0.0f).rgb;

  reflectance = do_multiscatter ? F_brdf_multi_scatter(F0, float3(1.0f), split_sum.xy) :
                                  F_brdf_single_scatter(F0, float3(1.0f), split_sum.xy);

  /* Precompute the F82 term factor for the Fresnel model.
   * In the classic F82 model, the F82 input directly determines the value of the Fresnel
   * model at ~82°, similar to F0 and F90.
   * With F82-Tint, on the other hand, the value at 82° is the value of the classic Schlick
   * model multiplied by the tint input.
   * Therefore, the factor follows by setting `F82Tint(cosI) = FSchlick(cosI) - b*cosI*(1-cosI)^6`
   * and `F82Tint(acos(1/7)) = FSchlick(acos(1/7)) * f82_tint` and solving for `b`. */
  constexpr float f = 6.0f / 7.0f;
  constexpr float f5 = (f * f) * (f * f) * f;
  constexpr float f6 = (f * f) * (f * f) * (f * f);
  float3 F_schlick = mix(F0, float3(1.0f), f5);
  float3 b = F_schlick * (7.0f / f6) * (1.0f - F82);
  reflectance -= b * split_sum.z;
}

float4 sample_3D_texture(sampler2DArray tex, float3 coords)
{
  float layer_floored;
  float interp = modf(coords.z, layer_floored);

  coords.z = layer_floored;
  float4 tex_low = textureLod(tex, coords, 0.0f);

  coords.z += 1.0f;
  float4 tex_high = textureLod(tex, coords, 0.0f);

  /* Manual trilinear interpolation. */
  return mix(tex_low, tex_high, interp);
}

/* Return texture coordinates to sample Surface LUT. */
float3 lut_coords_btdf(float cos_theta, float roughness, float ior)
{
  float3 coords = float3(sqrt((ior - 1.0f) / (ior + 1.0f)), sqrt(1.0f - cos_theta), roughness);

  /* scale and bias coordinates, for correct filtered lookup */
  coords.xy = coords.xy * (LUT_SIZE - 1.0f) / LUT_SIZE + 0.5f / LUT_SIZE;
  coords.z = coords.z * lut_btdf_layer_count + lut_btdf_layer_first;

  return coords;
}

/* Return texture coordinates to sample BSDF LUT. */
float3 lut_coords_bsdf(float cos_theta, float roughness, float ior)
{
  /* ior is sin of critical angle. */
  float critical_cos = sqrt(1.0f - ior * ior);

  float3 coords;
  coords.x = sqr(ior);
  coords.y = cos_theta;
  coords.y -= critical_cos;
  coords.y /= (coords.y > 0.0f) ? (1.0f - critical_cos) : critical_cos;
  coords.y = coords.y * 0.5f + 0.5f;
  coords.z = roughness;

  coords = saturate(coords);

  /* scale and bias coordinates, for correct filtered lookup */
  coords.xy = coords.xy * (LUT_SIZE - 1.0f) / LUT_SIZE + 0.5f / LUT_SIZE;
  coords.z = coords.z * lut_btdf_layer_count + lut_btdf_layer_first;

  return coords;
}

/* Computes the reflectance and transmittance based on the tint (`f0`, `f90`, `transmission_tint`)
 * and the BSDF LUT. */
void bsdf_lut(float3 F0,
              float3 F90,
              float3 transmission_tint,
              float cos_theta,
              float roughness,
              float ior,
              bool do_multiscatter,
              out float3 reflectance,
              out float3 transmittance)
{
  if (ior == 1.0f) {
    reflectance = float3(0.0f);
    transmittance = transmission_tint;
    return;
  }

  float2 split_sum;
  float transmission_factor;

  if (ior > 1.0f) {
    split_sum = brdf_lut(cos_theta, roughness);
    transmission_factor = sample_3D_texture(utilTex, lut_coords_btdf(cos_theta, roughness, ior)).a;
    /* Gradually increase `f90` from 0 to 1 when IOR is in the range of [1.0f, 1.33f], to avoid harsh
     * transition at `IOR == 1`. */
    if (all(equal(F90, float3(1.0f)))) {
      F90 = float3(saturate(2.33f / 0.33f * (ior - 1.0f) / (ior + 1.0f)));
    }
  }
  else {
    float3 bsdf = sample_3D_texture(utilTex, lut_coords_bsdf(cos_theta, roughness, ior)).rgb;
    split_sum = bsdf.rg;
    transmission_factor = bsdf.b;
  }

  reflectance = F_brdf_single_scatter(F0, F90, split_sum);
  transmittance = (float3(1.0f) - F0) * transmission_factor * transmission_tint;

  if (do_multiscatter) {
    float real_F0 = F0_from_ior(ior);
    float Ess = real_F0 * split_sum.x + split_sum.y + (1.0f - real_F0) * transmission_factor;
    float Ems = 1.0f - Ess;
    /* Assume that the transmissive tint makes up most of the overall color if it's not zero. */
    float3 Favg = all(equal(transmission_tint, float3(0.0f))) ? F0 + (F90 - F0) / 21.0f :
                                                           transmission_tint;

    float3 scale = 1.0f / (1.0f - Ems * Favg);
    reflectance *= scale;
    transmittance *= scale;
  }

  return;
}

/* Computes the reflectance and transmittance based on the BSDF LUT. */
float2 bsdf_lut(float cos_theta, float roughness, float ior, bool do_multiscatter)
{
  float F0 = F0_from_ior(ior);
  float3 color = float3(1.0f);
  float3 reflectance, transmittance;
  bsdf_lut(float3(F0),
           color,
           color,
           cos_theta,
           roughness,
           ior,
           do_multiscatter,
           reflectance,
           transmittance);
  return float2(reflectance.r, transmittance.r);
}

/** \} */
