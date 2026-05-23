/* SPDX-FileCopyrightText: 2017-2021 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Sampling distribution routines for Monte-carlo integration.
 */

#pragma once

#include "draw_math_geom_lib.glsl"
#include "bsdf_common_lib.glsl"

/* -------------------------------------------------------------------- */
/** \name Microfacet GGX distribution
 * \{ */

#define USE_VISIBLE_NORMAL 1

float pdf_ggx_reflect(float NH, float NV, float VH, float alpha)
{
  float a2 = sqr(alpha);
#if USE_VISIBLE_NORMAL
  float D = bxdf_ggx_D(NH, a2);
  float G1 = bxdf_ggx_smith_G1(NV, a2);
  return 0.25f * D * G1 / NV;
#else
  return NH * a2 / D_ggx_opti(NH, a2);
#endif
}

float3 sample_ggx(float3 rand, float alpha, float3 Vt)
{
#if USE_VISIBLE_NORMAL
  /* From:
   * "A Simpler and Exact Sampling Routine for the GGXDistribution of Visible Normals"
   * by Eric Heitz.
   * http://jcgt.org/published/0007/04/01/slides.pdf
   * View vector is expected to be in tangent space. */

  /* Stretch view. */
  float3 Th, Bh, Vh = normalize(float3(alpha * Vt.xy, Vt.z));
  make_orthonormal_basis(Vh, Th, Bh);
  /* Sample point with polar coordinates (r, phi). */
  float r = sqrt(rand.x);
  float x = r * rand.y;
  float y = r * rand.z;
  float s = 0.5f * (1.0f + Vh.z);
  y = (1.0f - s) * sqrt(1.0f - x * x) + s * y;
  float z = sqrt(saturate(1.0f - x * x - y * y));
  /* Compute normal. */
  float3 Hh = x * Th + y * Bh + z * Vh;
  /* Unstretch. */
  float3 Ht = normalize(float3(alpha * Hh.xy, saturate(Hh.z)));
  /* Microfacet Normal. */
  return Ht;
#else
  /* Theta is the cone angle. */
  float z = sqrt((1.0f - rand.x) / (1.0f + sqr(alpha) * rand.x - rand.x)); /* cos theta */
  float r = sqrt(max(0.0f, 1.0f - z * z));                                 /* sin theta */
  float x = r * rand.y;
  float y = r * rand.z;
  /* Microfacet Normal */
  return float3(x, y, z);
#endif
}

float3 sample_ggx(float3 rand, float alpha, float3 V, float3 N, float3 T, float3 B, out float pdf)
{
  float3 Vt = world_to_tangent(V, N, T, B);
  float3 Ht = sample_ggx(rand, alpha, Vt);
  float NH = saturate(Ht.z);
  float NV = saturate(Vt.z);
  float VH = saturate(dot(Vt, Ht));
  pdf = pdf_ggx_reflect(NH, NV, VH, alpha);
  return tangent_to_world(Ht, N, T, B);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Uniform Hemisphere
 * \{ */

float pdf_uniform_hemisphere()
{
  return 0.5f * M_1_PI;
}

float3 sample_uniform_hemisphere(float3 rand)
{
  float z = rand.x;                      /* cos theta */
  float r = sqrt(max(0.0f, 1.0f - z * z)); /* sin theta */
  float x = r * rand.y;
  float y = r * rand.z;
  return float3(x, y, z);
}

float3 sample_uniform_hemisphere(float3 rand, float3 N, float3 T, float3 B, out float pdf)
{
  float3 Ht = sample_uniform_hemisphere(rand);
  pdf = pdf_uniform_hemisphere();
  return tangent_to_world(Ht, N, T, B);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Uniform Cone sampling
 * \{ */

float3 sample_uniform_cone(float3 rand, float angle)
{
  float z = cos(angle * rand.x);         /* cos theta */
  float r = sqrt(max(0.0f, 1.0f - z * z)); /* sin theta */
  float x = r * rand.y;
  float y = r * rand.z;
  return float3(x, y, z);
}

float3 sample_uniform_cone(float3 rand, float angle, float3 N, float3 T, float3 B)
{
  float3 Ht = sample_uniform_cone(rand, angle);
  /* TODO: pdf? */
  return tangent_to_world(Ht, N, T, B);
}

/** \} */
