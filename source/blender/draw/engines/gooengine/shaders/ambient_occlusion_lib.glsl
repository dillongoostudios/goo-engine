/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "draw_math_geom_lib.glsl"
#include "raytrace_lib.glsl"
#include "surface_lib.glsl"

/* Based on Practical Realtime Strategies for Accurate Indirect Occlusion
 * http://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf
 * http://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pptx
 */

#if defined(MESH_SHADER)
#  if !defined(USE_ALPHA_HASH)
#    if !defined(DEPTH_SHADER)
#      if !defined(USE_ALPHA_BLEND)
#        if !defined(USE_REFRACTION)
#          define ENABLE_DEFERED_AO
#        endif
#      endif
#    endif
#  endif
#endif

#ifndef ENABLE_DEFERED_AO
#  if defined(STEP_RESOLVE)
#    define ENABLE_DEFERED_AO
#  endif
#endif

#ifndef GPU_FRAGMENT_SHADER
#  define gl_FragCoord float4(0.0f)
#endif

/* aoSettings flags */
#define USE_AO 1
#define USE_BENT_NORMAL 2
#define USE_DENOISE 4

#define NO_OCCLUSION_DATA OcclusionData(float4(M_PI, -M_PI, M_PI, -M_PI), 1.0f)

struct OcclusionData {
  /* 4 horizons angles, one in each direction around the view vector to form a cross pattern. */
  float4 horizons;
  /* Custom large scale occlusion. */
  float custom_occlusion;

#ifdef GPU_METAL
  /* Constructors required for OcclusionData(..) syntax. */
  inline OcclusionData() = default;
  inline OcclusionData(float4 in_horizons, float in_custom_occlusion)
      : horizons(in_horizons), custom_occlusion(in_custom_occlusion)
  {
  }
#endif
};

float4 pack_occlusion_data(OcclusionData data)
{
  return float4(1.0f - data.horizons * float4(1, -1, 1, -1) * M_1_PI);
}

OcclusionData unpack_occlusion_data(float4 v)
{
  return OcclusionData((1.0f - v) * float4(1, -1, 1, -1) * M_PI, 0.0f);
}

float2 get_ao_noise(void)
{
  float2 noise = texelfetch_noise_tex(gl_FragCoord.xy).xy;
  /* Decorrelate noise from AA. */
  /* TODO(@fclem): we should use a more general approach for more random number dimensions. */
  noise = fract(noise * 6.1803402007f);
  return noise;
}

float2 get_ao_dir(float jitter)
{
  /* Only a quarter of a turn because we integrate using 2 slices.
   * We use this instead of using utiltex circle noise to improve cache hits
   * since all tracing direction will be in the same quadrant. */
  jitter *= M_PI_2;
  return float2(cos(jitter), sin(jitter));
}

/* Certain intel drivers on macOS lose precision, resulting in rendering artifacts,
 * when using standard pow(A, B) function.
 * Using logarithmic identity provides higher precision results. */
#if defined(GPU_INTEL) && defined(OS_MAC)
float occlusion_pow(float a, float b)
{
  return exp(b * log(a));
}
#else
float occlusion_pow(float a, float b)
{
  return pow(a, b);
}
#endif

/* Return horizon angle cosine. */
#if (defined(GPU_METAL) && defined(GPU_ATI))
__attribute__((noinline))
#endif
float search_horizon(float3 vI,
                     float3 vP,
                     float noise,
                     ScreenSpaceRay ssray,
                     sampler2D depth_tx,
                     const float inverted,
                     float radius,
                     const float sample_count)
{
  /* Init at cos(M_PI). */
  float h = (inverted != 0.0f) ? 1.0f : -1.0f;

  ssray.max_time -= 1.0f;

  if (ssray.max_time <= 2.0f) {
    /* Produces self shadowing under this threshold. */
    return fast_acos(h);
  }

  float prev_time, time = 0.0f;
  int i_sample_count = int(sample_count);
  for (int iter = 0; time < ssray.max_time && iter < i_sample_count; iter++) {
    prev_time = time;
    /* Gives us good precision at center and ensure we cross at least one pixel per iteration. */
    float fl_iter = float(iter);
    time = 1.0f + fl_iter + sqr((fl_iter + noise) / sample_count) * ssray.max_time;
    float stride = time - prev_time;
    float lod = (log2(stride) - noise) / (1.0f + aoQuality);

    float2 uv = ssray.origin.xy + ssray.direction.xy * time;
    float depth = textureLod(depth_tx, uv * hizUvScale.xy, floor(lod)).r;

    if (depth == 1.0f && inverted == 0.0f) {
      /* Skip background. Avoids making shadow on the geometry near the far plane. */
      continue;
    }

    /* Bias depth a bit to avoid self shadowing issues. */
    constexpr float bias = 2.0 * 2.4e-7f;
    depth += (inverted != 0.0f) ? -bias : bias;

    float3 s = get_view_space_from_depth(uv, depth);
    float3 omega_s = s - vP;
    float len = length(omega_s);
    /* Sample's horizon angle cosine. */
    float s_h = dot(vI, omega_s / len);
    /* Blend weight to fade artifacts. */
    float dist_ratio = abs(len) / radius;
    /* Sphere falloff. */
    float dist_fac = sqr(saturate(dist_ratio));
    /* Unbiased, gives too much hard cut behind objects */
    // float dist_fac = step(0.999f, dist_ratio);

    if (inverted != 0.0f) {
      h = min(h, s_h);
    }
    else {
      h = mix(max(h, s_h), h, dist_fac);
    }
  }
  return fast_acos(h);
}

OcclusionData occlusion_search(
    float3 vP, sampler2D depth_tx, float radius, const float inverted, const float dir_sample_count)
{
  if ((int(aoSettings) & USE_AO) == 0) {
    return NO_OCCLUSION_DATA;
  }

  float2 noise = get_ao_noise();
  float2 dir = get_ao_dir(noise.x);
  float2 uv = get_uvs_from_view(vP);
  float3 vI = ((ProjectionMatrixGoo[3][3] == 0.0f) ? normalize(-vP) : float3(0.0f, 0.0f, 1.0f));
  float3 avg_dir = float3(0.0f);
  float avg_apperture = 0.0f;

  OcclusionData data = (inverted != 0.0f) ? OcclusionData(float4(0, 0, 0, 0), 1.0f) :
                                           NO_OCCLUSION_DATA;

  for (int i = 0; i < 2; i++) {
    Ray ray;
    ray.origin = vP;
    ray.direction = float3(dir * radius, 0.0f);

    ScreenSpaceRay ssray;

    ssray = raytrace_screenspace_ray_create(ray);
    data.horizons[0 + i * 2] = search_horizon(
        vI, vP, noise.y, ssray, depth_tx, inverted, radius, dir_sample_count);

    ray.direction = -ray.direction;

    ssray = raytrace_screenspace_ray_create(ray);
    data.horizons[1 + i * 2] = -search_horizon(
        vI, vP, noise.y, ssray, depth_tx, inverted, radius, dir_sample_count);

    /* Rotate 90 degrees. */
    dir = float2(-dir.y, dir.x);
  }

  return data;
}

float2 clamp_horizons_to_hemisphere(float2 horizons, float angle_N, const float inverted)
{
  /* Add a little bias to fight self shadowing. */
  constexpr float max_angle = M_PI_2 - 0.05f;

  if (inverted != 0.0f) {
    horizons.x = max(horizons.x, angle_N + max_angle);
    horizons.y = min(horizons.y, angle_N - max_angle);
  }
  else {
    horizons.x = min(horizons.x, angle_N + max_angle);
    horizons.y = max(horizons.y, angle_N - max_angle);
  }
  return horizons;
}

void occlusion_eval(OcclusionData data,
                    float3 V,
                    float3 N,
                    float3 Ng,
                    const float inverted,
                    out float visibility,
                    out float visibility_error,
                    out float3 bent_normal)
{
  /* No error by default. */
  visibility_error = 1.0f;

  if ((int(aoSettings) & USE_AO) == 0) {
    visibility = data.custom_occlusion;
    bent_normal = N;
    return;
  }

  bool early_out = (inverted != 0.0f) ? (max_v4(abs(data.horizons)) == 0.0f) :
                                       (min_v4(abs(data.horizons)) == M_PI);
  if (early_out) {
    visibility = saturate(dot(N, Ng) * 0.5f + 0.5f);
    visibility = min(visibility, data.custom_occlusion);

    if ((int(aoSettings) & USE_BENT_NORMAL) == 0) {
      bent_normal = N;
    }
    else {
      bent_normal = safe_normalize(N + Ng);
    }
    return;
  }

  float2 noise = get_ao_noise();
  float2 dir = get_ao_dir(noise.x);

  visibility_error = 0.0f;
  visibility = 0.0f;
  bent_normal = N * 0.001f;

  for (int i = 0; i < 2; i++) {
    float3 T = transform_direction(ViewMatrixInverseGoo, float3(dir, 0.0f));
    /* Setup integration domain around V. */
    float3 B = normalize(cross(V, T));
    T = normalize(cross(B, V));

    float proj_N_len;
    float3 proj_N = normalize_len(N - B * dot(N, B), proj_N_len);
    float3 proj_Ng = normalize(Ng - B * dot(Ng, B));

    float2 h = (i == 0) ? data.horizons.xy : data.horizons.zw;

    float N_sin = dot(proj_N, T);
    float Ng_sin = dot(proj_Ng, T);
    float N_cos = saturate(dot(proj_N, V));
    float Ng_cos = saturate(dot(proj_Ng, V));
    /* Gamma, angle between normalized projected normal and view vector. */
    float angle_Ng = sign(Ng_sin) * fast_acos(Ng_cos);
    float angle_N = sign(N_sin) * fast_acos(N_cos);
    /* Clamp horizons to hemisphere around shading normal. */
    h = clamp_horizons_to_hemisphere(h, angle_N, inverted);

    float bent_angle = (h.x + h.y) * 0.5f;
    /* NOTE: here we multiply z by 0.5f as it shows less difference with the geometric normal.
     * Also modulate by projected normal length to reduce issues with slanted surfaces.
     * All of this is ad-hoc and not really grounded. */
    bent_normal += proj_N_len * (T * sin(bent_angle) + V * 0.5f * cos(bent_angle));

    /* Clamp to geometric normal only for integral to keep smooth bent normal. */
    /* This is done to match Cycles ground truth but adds some computation. */
    h = clamp_horizons_to_hemisphere(h, angle_Ng, inverted);

    /* Inner integral (Eq. 7). */
    float a = dot(-cos(2.0f * h - angle_N) + N_cos + 2.0f * h * N_sin, float2(0.25f));
    /* Correct normal not on plane (Eq. 8). */
    visibility += proj_N_len * a;
    /* Using a very low number of slices (2) leads to over-darkening of surfaces orthogonal to
     * the view. This is particularly annoying for sharp reflections occlusion. So we compute how
     * much the error is and correct the visibility later. */
    visibility_error += proj_N_len;

    /* Rotate 90 degrees. */
    dir = float2(-dir.y, dir.x);
  }
  /* We integrated 2 directions. */
  visibility *= 0.5f;
  visibility_error *= 0.5f;

  visibility = min(visibility, data.custom_occlusion);

  if ((int(aoSettings) & USE_BENT_NORMAL) == 0) {
    bent_normal = N;
  }
  else {
    /* NOTE: using pow(visibility, 6.0f) produces NaN (see #87369). */
    float tmp = saturate(pow6(visibility));
    bent_normal = normalize(mix(bent_normal, N, tmp));
  }
}

/* Multi-bounce approximation base on surface albedo.
 * Page 78 in the PDF version. */
float gtao_multibounce(float visibility, float3 albedo)
{
  if (aoBounceFac == 0.0f) {
    return visibility;
  }

  /* Median luminance. Because Colored multibounce looks bad. */
  float lum = dot(albedo, float3(0.3333f));

  float a = 2.0404f * lum - 0.3324f;
  float b = -4.7951f * lum + 0.6417f;
  float c = 2.7552f * lum + 0.6903f;

  float x = visibility;
  return max(x, ((x * a + b) * x + c) * x);
}

float diffuse_occlusion(OcclusionData data, float3 V, float3 N, float3 Ng)
{
  float3 unused;
  float unused_error;
  float visibility;
  occlusion_eval(data, V, N, Ng, 0.0f, visibility, unused_error, unused);
  /* Scale by user factor */
  visibility = occlusion_pow(saturate(visibility), aoFactor);
  return visibility;
}

float diffuse_occlusion(
    OcclusionData data, float3 V, float3 N, float3 Ng, float3 albedo, out float3 bent_normal)
{
  float visibility;
  float unused_error;
  occlusion_eval(data, V, N, Ng, 0.0f, visibility, unused_error, bent_normal);

  visibility = gtao_multibounce(visibility, albedo);
  /* Scale by user factor */
  visibility = occlusion_pow(saturate(visibility), aoFactor);
  return visibility;
}

/**
 * Approximate the area of intersection of two spherical caps
 * radius1 : First cap’s radius (arc length in radians)
 * radius2 : Second caps’ radius (in radians)
 * dist : Distance between caps (radians between centers of caps)
 * NOTE: Result is divided by pi to save one multiply.
 */
float spherical_cap_intersection(float radius1, float radius2, float dist)
{
  /* From "Ambient Aperture Lighting" by Chris Oat
   * Slide 15. */
  float max_radius = max(radius1, radius2);
  float min_radius = min(radius1, radius2);
  float sum_radius = radius1 + radius2;
  float area;
  if (dist <= max_radius - min_radius) {
    /* One cap in completely inside the other */
    area = 1.0f - cos(min_radius);
  }
  else if (dist >= sum_radius) {
    /* No intersection exists */
    area = 0;
  }
  else {
    float diff = max_radius - min_radius;
    area = smoothstep(0.0f, 1.0f, 1.0f - saturate((dist - diff) / (sum_radius - diff)));
    area *= 1.0f - cos(min_radius);
  }
  return area;
}

float specular_occlusion(
    OcclusionData data, float3 V, float3 N, float roughness, inout float3 specular_dir)
{
  float3 visibility_dir;
  float visibility_error;
  float visibility;
  occlusion_eval(data, V, N, N, 0.0f, visibility, visibility_error, visibility_dir);

  /* Correct visibility error for very sharp surfaces. */
  visibility *= mix(safe_rcp(visibility_error), 1.0f, roughness);

  specular_dir = normalize(mix(specular_dir, visibility_dir, roughness * (1.0f - visibility)));

  /* Visibility to cone angle (eq. 18). */
  float vis_angle = fast_acos(sqrt(1 - visibility));
  /* Roughness to cone angle (eq. 26). */
  float spec_angle = max(0.00990998744964599609375f, fast_acos(cone_cosine(roughness)));
  /* Angle between cone axes. */
  float cone_cone_dist = fast_acos(saturate(dot(visibility_dir, specular_dir)));
  float cone_nor_dist = fast_acos(saturate(dot(N, specular_dir)));

  float isect_solid_angle = spherical_cap_intersection(vis_angle, spec_angle, cone_cone_dist);
  float specular_solid_angle = spherical_cap_intersection(M_PI_2, spec_angle, cone_nor_dist);
  float specular_occlusion = isect_solid_angle / specular_solid_angle;
  /* Mix because it is unstable in unoccluded areas. */
  float tmp = saturate(pow8(visibility));
  visibility = mix(specular_occlusion, 1.0f, tmp);

  /* Scale by user factor */
  visibility = occlusion_pow(saturate(visibility), aoFactor);
  return visibility;
}

/* Use the right occlusion. */
OcclusionData occlusion_load(float3 vP, float custom_occlusion)
{
  /* Default to fully opened cone. */
  OcclusionData data = NO_OCCLUSION_DATA;

#ifdef ENABLE_DEFERED_AO
  if ((int(aoSettings) & USE_AO) != 0) {
    data = unpack_occlusion_data(texelFetch(horizonBuffer, int2(gl_FragCoord.xy), 0));
  }
#else
  /* For blended surfaces. */
  data = occlusion_search(vP, maxzBuffer, aoDistance, 0.0f, 8.0f);
#endif

  data.custom_occlusion = custom_occlusion;

  return data;
}

#ifndef GPU_FRAGMENT_SHADER
#  undef gl_FragCoord
#endif

float ambient_occlusion_eval(float3 normal,
                             float max_distance,
                             const float inverted,
                             const float sample_count)
{
  /* Avoid multi-line define causing compiler issues. */
  /* clang-format off */
#if defined(GPU_FRAGMENT_SHADER) && (defined(MESH_SHADER) || defined(HAIR_SHADER)) && !defined(DEPTH_SHADER) && !defined(VOLUMETRICS)
  /* clang-format on */
  float3 bent_normal;
  float4 rand = texelfetch_noise_tex(gl_FragCoord.xy);
  OcclusionData data = occlusion_search(
      viewPosition, maxzBuffer, max_distance, inverted, sample_count);

  float3 V = cameraVec(worldPosition);
  float3 N = normalize(normal);
  float3 Ng = safe_normalize(cross(gpu_dfdx(worldPosition), gpu_dfdy(worldPosition)));

  float unused_error, visibility;
  float3 unused;
  occlusion_eval(data, V, N, Ng, inverted, visibility, unused_error, unused);
  return visibility;
#else
  return 1.0f;
#endif
}
