/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "random_lib.glsl"
#include "bsdf_sampling_lib.glsl"
#include "draw_math_geom_lib.glsl"
#include "irradiance_lib.glsl"

#define M_4PI 12.5663706143591729f

float3 get_cubemap_vector(float2 co, int face)
{
  /* NOTE(Metal): Declaring constant array in function scope to avoid increasing local shader
   * memory pressure. */
  float3x3 CUBE_ROTATION;

  switch (face) {
    case 0:
      CUBE_ROTATION = float3x3(float3(0.0f, 0.0f, -1.0f), float3(0.0f, -1.0f, 0.0f), float3(-1.0f, 0.0f, 0.0f));
      break;
    case 1:
      CUBE_ROTATION = float3x3(float3(0.0f, 0.0f, 1.0f), float3(0.0f, -1.0f, 0.0f), float3(1.0f, 0.0f, 0.0f));
      break;
    case 2:
      CUBE_ROTATION = float3x3(float3(1.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), float3(0.0f, -1.0f, 0.0f));
      break;
    case 3:
      CUBE_ROTATION = float3x3(float3(1.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, -1.0f), float3(0.0f, 1.0f, 0.0f));
      break;
    case 4:
      CUBE_ROTATION = float3x3(float3(1.0f, 0.0f, 0.0f), float3(0.0f, -1.0f, 0.0f), float3(0.0f, 0.0f, -1.0f));
      break;
    case 5:
      CUBE_ROTATION = float3x3(float3(-1.0f, 0.0f, 0.0f), float3(0.0f, -1.0f, 0.0f), float3(0.0f, 0.0f, 1.0f));
      break;
  }

  return normalize(CUBE_ROTATION * float3(co * 2.0f - 1.0f, 1.0f));
}

float area_element(float x, float y)
{
  return atan(x * y, sqrt(x * x + y * y + 1));
}

float texel_solid_angle(float2 co, float halfpix)
{
  float2 v1 = (co - float2(halfpix)) * 2.0f - 1.0f;
  float2 v2 = (co + float2(halfpix)) * 2.0f - 1.0f;

  return area_element(v1.x, v1.y) - area_element(v1.x, v2.y) - area_element(v2.x, v1.y) +
         area_element(v2.x, v2.y);
}

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
#if defined(IRRADIANCE_SH_L2)
  float pixstep = 1.0f / probeSize;
  float halfpix = pixstep / 2.0f;

  /* Downside: leaks negative values, very bandwidth consuming */
  int comp = int(gl_FragCoord.x) % 3 + (int(gl_FragCoord.y) % 3) * 3;

  float weight_accum = 0.0f;
  float3 sh = float3(0.0f);

  for (int face = 0; face < 6; face++) {
    for (float x = halfpix; x < 1.0f; x += pixstep) {
      for (float y = halfpix; y < 1.0f; y += pixstep) {
        float weight, coef;
        float2 facecoord = float2(x, y);
        float3 cubefloat = get_cubemap_vector(facecoord, face);

        if (comp == 0) {
          coef = 0.282095f;
        }
        else if (comp == 1) {
          coef = -0.488603f * cubefloat.z * 2.0f / 3.0f;
        }
        else if (comp == 2) {
          coef = 0.488603f * cubefloat.y * 2.0f / 3.0f;
        }
        else if (comp == 3) {
          coef = -0.488603f * cubefloat.x * 2.0f / 3.0f;
        }
        else if (comp == 4) {
          coef = 1.092548f * cubefloat.x * cubefloat.z * 1.0f / 4.0f;
        }
        else if (comp == 5) {
          coef = -1.092548f * cubefloat.z * cubefloat.y * 1.0f / 4.0f;
        }
        else if (comp == 6) {
          coef = 0.315392f * (3.0f * cubefloat.y * cubefloat.y - 1.0f) * 1.0f / 4.0f;
        }
        else if (comp == 7) {
          coef = -1.092548f * cubefloat.x * cubefloat.y * 1.0f / 4.0f;
        }
        else { /* (comp == 8) */
          coef = 0.546274f * (cubefloat.x * cubefloat.x - cubefloat.z * cubefloat.z) * 1.0f / 4.0f;
        }

        weight = texel_solid_angle(facecoord, halfpix);

        float4 samp = textureLod(probeHdr, cubefloat, lodMax);
        sh += samp.rgb * coef * weight;
        weight_accum += weight;
      }
    }
  }
  sh *= M_4PI / weight_accum;

  FragColor = float4(sh, 1.0f);
#else
#  if defined(IRRADIANCE_HL2)
  /* Downside: extremely low resolution (6 texels), bleed lighting because of interpolation */
  int x = int(gl_FragCoord.x) % 3;
  int y = int(gl_FragCoord.y) % 2;

  float3 cubefloat = float3(1.0f, 0.0f, 0.0f);

  if (x == 1) {
    cubefloat = cubefloat.yxy;
  }
  else if (x == 2) {
    cubefloat = cubefloat.yyx;
  }

  if (y == 1) {
    cubefloat = -cubefloat;
  }
#  endif

  float3 N, T, B, V;

  N = normalize(cubefloat);

  make_orthonormal_basis(N, T, B); /* Generate tangent space */

  /* Integrating environment-map. */
  float weight = 0.0f;
  float3 out_radiance = float3(0.0f);
  for (float i = 0; i < sampleCount; i++) {
    float3 Xi = rand2d_to_cylinder(hammersley_2d(i, sampleCount));

    float pdf;
    float3 L = sample_uniform_hemisphere(Xi, N, T, B, pdf);
    float NL = dot(N, L);

    if (NL > 0.0f) {
      /* Coarse Approximation of the mapping distortion
       * Unit Sphere -> Cube-map Face. */
      constexpr float dist = 4.0f * M_PI / 6.0f;
      /* http://http.developer.nvidia.com/GPUGems3/gpugems3_ch20.html : Equation 13 */
      float lod = clamp(lodFactor - 0.5f * log2(pdf * dist), 0.0f, lodMax);

      out_radiance += textureLod(probeHdr, L, lod).rgb * NL;
      weight += NL;
    }
  }

  FragColor = irradiance_encode(intensityFac * out_radiance / weight);
#endif
}
