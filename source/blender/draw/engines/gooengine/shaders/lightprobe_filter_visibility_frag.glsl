/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0or-later */

#include "random_lib.glsl"
#include "draw_math_geom_lib.glsl"
#include "bsdf_sampling_lib.glsl"
#include "irradiance_lib.glsl"

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

float linear_depth(float z)
{
  return (nearClip * farClip) / (z * (nearClip - farClip) + farClip);
}

float get_world_distance(float depth, float3 cos)
{
  float is_background = step(1.0f, depth);
  depth = linear_depth(depth);
  depth += 1e1f * is_background;
  cos = normalize(abs(cos));
  float cos_float = max(cos.x, max(cos.y, cos.z));
  return depth / cos_float;
}

void main()
{
  int2 texel = int2(gl_FragCoord.xy) % int2(outputSize);

  float3 cos;

  cos.xy = (float2(texel) + 0.5f) * storedTexelSize;

  /* add a 2 pixel border to ensure filtering is correct */
  cos.xy = (cos.xy - storedTexelSize) / (1.0f - 2.0f * storedTexelSize);

  float pattern = 1.0f;

  /* edge mirroring : only mirror if directly adjacent
   * (not diagonally adjacent) */
  float2 m = abs(cos.xy - 0.5f) + 0.5f;
  float2 f = floor(m);
  if (f.x - f.y != 0.0f) {
    cos.xy = 1.0f - cos.xy;
  }

  /* clamp to [0-1] */
  cos.xy = fract(cos.xy);

  /* Get cube-map vector. */
  cos = normalize(octahedral_to_cubemap_proj(cos.xy));

  float3 T, B;
  make_orthonormal_basis(cos, T, B); /* Generate tangent space */

  float2 accum = float2(0.0f);

  for (float i = 0; i < sampleCount; i++) {
    float3 Xi = rand2d_to_cylinder(hammersley_2d(i, sampleCount));

    float3 samp = sample_uniform_cone(Xi, M_PI_2 * visibilityBlur, cos, T, B);
    float depth = texture(probeDepth, samp).r;
    depth = get_world_distance(depth, samp);
    accum += float2(depth, depth * depth);
  }

  accum /= sampleCount;
  accum = abs(accum);

  /* Encode to normalized RGBA 8 */
  FragColor = visibility_encode(accum, visibilityRange);
}
