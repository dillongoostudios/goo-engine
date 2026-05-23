/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "goo_common_view_lib.glsl"
#include "draw_math_geom_lib.glsl"
#include "common_uniforms_lib.glsl"

/* Fix for #104266 wherein AMD GPUs running Metal erroneously discard a successful hit. */
#if defined(GPU_METAL) && defined(GPU_ATI)
#  define METAL_AMD_RAYTRACE_WORKAROUND 1
#endif

/**
 * Screen-Space Ray-tracing functions.
 */

struct Ray {
  float3 origin;
  /* Ray direction pre-multiplied by its maximum length. */
  float3 direction;
};

/* Inputs expected to be in view-space. */
void raytrace_clip_ray_to_near_plane(inout Ray ray)
{
  float near_dist = get_view_z_from_depth(0.0f);
  if ((ray.origin.z + ray.direction.z) > near_dist) {
    ray.direction *= abs((near_dist - ray.origin.z) / ray.direction.z);
  }
}

/* Screen-space ray ([0..1f] "uv" range) where direction is normalize to be as small as one
 * full-resolution pixel. The ray is also clipped to all frustum sides.
 */
struct ScreenSpaceRay {
  float4 origin;
  float4 direction;
  float max_time;
};

void raytrace_screenspace_ray_finalize(inout ScreenSpaceRay ray)
{
  /* Constant bias (due to depth buffer precision). Helps with self intersection. */
  /* Magic numbers for 24bits of precision.
   * From http://terathon.com/gdc07_lengyel.pdf (slide 26) */
  constexpr float bias = -2.4e-7f * 2.0f;
  ray.origin.zw += bias;
  ray.direction.zw += bias;

  ray.direction -= ray.origin;
  /* If the line is degenerate, make it cover at least one pixel
   * to not have to handle zero-pixel extent as a special case later */
  if (len_squared(ray.direction.xy) < 0.00001f) {
    ray.direction.xy = float2(0.0f, 0.00001f);
  }
  float ray_len_sqr = len_squared(ray.direction.xyz);
  /* Make ray.direction cover one pixel. */
  bool is_more_vertical = abs(ray.direction.x / ssrPixelSize.x) <
                          abs(ray.direction.y / ssrPixelSize.y);
  ray.direction /= (is_more_vertical) ? abs(ray.direction.y) : abs(ray.direction.x);
  ray.direction *= (is_more_vertical) ? ssrPixelSize.y : ssrPixelSize.x;
  /* Clip to segment's end. */
  ray.max_time = sqrt(ray_len_sqr * safe_rcp(len_squared(ray.direction.xyz)));
  /* Clipping to frustum sides. */
  float clip_dist = line_unit_box_intersect_dist_safe(ray.origin.xyz, ray.direction.xyz);
  ray.max_time = min(ray.max_time, clip_dist);
  /* Convert to texture coords [0..1f] range. */
  ray.origin = ray.origin * 0.5f + 0.5f;
  ray.direction *= 0.5f;
}

ScreenSpaceRay raytrace_screenspace_ray_create(Ray ray)
{
  ScreenSpaceRay ssray;
  ssray.origin.xyz = project_point(ProjectionMatrixGoo, ray.origin);
  ssray.direction.xyz = project_point(ProjectionMatrixGoo, ray.origin + ray.direction);

  raytrace_screenspace_ray_finalize(ssray);
  return ssray;
}

ScreenSpaceRay raytrace_screenspace_ray_create(Ray ray, float thickness)
{
  ScreenSpaceRay ssray;
  ssray.origin.xyz = project_point(ProjectionMatrixGoo, ray.origin);
  ssray.direction.xyz = project_point(ProjectionMatrixGoo, ray.origin + ray.direction);
  /* Interpolate thickness in screen space.
   * Calculate thickness further away to avoid near plane clipping issues. */
  ssray.origin.w = get_depth_from_view_z(ray.origin.z - thickness) * 2.0f - 1.0f;
  ssray.direction.w = get_depth_from_view_z(ray.origin.z + ray.direction.z - thickness) * 2.0f -
                      1.0f;

  raytrace_screenspace_ray_finalize(ssray);
  return ssray;
}

struct RayTraceParameters {
  /** ViewSpace thickness the objects. */
  float thickness;
  /** Jitter along the ray to avoid banding artifact when steps are too large. */
  float jitter;
  /** Determine how fast the sample steps are getting bigger. */
  float trace_quality;
  /** Determine how we can use lower depth mipmaps to make the tracing faster. */
  float roughness;
};

/* Returns true on hit. */
/* TODO(fclem): remove the back-face check and do it the SSR resolve code. */
#ifdef METAL_AMD_RAYTRACE_WORKAROUND
__attribute__((noinline))
#endif
bool raytrace(Ray ray,
              RayTraceParameters params,
              const bool discard_backface,
              const bool allow_self_intersection,
              out float3 hit_position)
{
  /* Clip to near plane for perspective view where there is a singularity at the camera origin. */
  if (ProjectionMatrixGoo[3][3] == 0.0f) {
    raytrace_clip_ray_to_near_plane(ray);
  }

  ScreenSpaceRay ssray = raytrace_screenspace_ray_create(ray, params.thickness);
  /* Avoid no iteration. */
  if (!allow_self_intersection && ssray.max_time < 1.1f) {
    hit_position = ssray.origin.xyz + ssray.direction.xyz;
    return false;
  }

  ssray.max_time = max(1.1f, ssray.max_time);

  float prev_delta = 0.0f, prev_time = 0.0f;
  float depth_sample = get_depth_from_view_z(ray.origin.z);
  float delta = depth_sample - ssray.origin.z;

  float lod_fac = saturate(fast_sqrt(params.roughness) * 2.0f - 0.4f);

  /* Cross at least one pixel. */
  float t = 1.001f, time = 1.001f;
  bool hit = false;
#ifdef METAL_AMD_RAYTRACE_WORKAROUND
  bool hit_failsafe = true;
#endif
  constexpr int max_steps = 255;
  for (int iter = 1; !hit && (time < ssray.max_time) && (iter < max_steps); iter++) {
    float stride = 1.0f + float(iter) * params.trace_quality;
    float lod = log2(stride) * lod_fac;

    prev_time = time;
    prev_delta = delta;

    time = min(t + stride * params.jitter, ssray.max_time);
    t += stride;

    float4 ss_p = ssray.origin + ssray.direction * time;
    depth_sample = textureLod(maxzBuffer, ss_p.xy * hizUvScale.xy, floor(lod)).r;

    delta = depth_sample - ss_p.z;
    /* Check if the ray is below the surface ... */
    hit = (delta < 0.0f);
    /* ... and above it with the added thickness. */
    hit = hit && (delta > ss_p.z - ss_p.w || abs(delta) < abs(ssray.direction.z * stride * 2.0f));

#ifdef METAL_AMD_RAYTRACE_WORKAROUND
    /* For workaround, perform discard back-face and background check only within
     * the iteration where the first successful ray intersection is registered.
     * We flag failures to discard ray hits later. */
    bool hit_valid = !(discard_backface && prev_delta < 0.0f) && (depth_sample != 1.0f);
    if (hit && !hit_valid) {
      hit_failsafe = false;
    }
#endif
  }

#ifndef METAL_AMD_RAYTRACE_WORKAROUND
  /* Discard back-face hits. */
  hit = hit && !(discard_backface && prev_delta < 0.0f);
  /* Reject hit if background. */
  hit = hit && (depth_sample != 1.0f);
#endif
  /* Refine hit using intersection between the sampled height-field and the ray.
   * This simplifies nicely to this single line. */
  time = mix(prev_time, time, saturate(prev_delta / (prev_delta - delta)));

  hit_position = ssray.origin.xyz + ssray.direction.xyz * time;

#ifdef METAL_AMD_RAYTRACE_WORKAROUND
  /* Check failed ray flag to discard bad hits. */
  if (!hit_failsafe) {
    return false;
  }
#endif
  return hit;
}

bool raytrace_planar(Ray ray, RayTraceParameters params, int planar_ref_id, out float3 hit_position)
{
  /* Clip to near plane for perspective view where there is a singularity at the camera origin. */
  if (ProjectionMatrixGoo[3][3] == 0.0f) {
    raytrace_clip_ray_to_near_plane(ray);
  }

  ScreenSpaceRay ssray = raytrace_screenspace_ray_create(ray);

  /* Planar Reflections have X mirrored. */
  ssray.origin.x = 1.0f - ssray.origin.x;
  ssray.direction.x = -ssray.direction.x;

  float prev_delta = 0.0f, prev_time = 0.0f;
  float depth_sample = texture(planarDepth, float3(ssray.origin.xy, planar_ref_id)).r;
  float delta = depth_sample - ssray.origin.z;

  float t = 0.0f, time = 0.0f;
  /* On very sharp reflections, the ray can be perfectly aligned with the view direction
   * making the tracing useless. Bypass tracing in this case. */
  bool hit = false;
  constexpr int max_steps = 255;
  for (int iter = 1; !hit && (time < ssray.max_time) && (iter < max_steps); iter++) {
    float stride = 1.0f + float(iter) * params.trace_quality;

    prev_time = time;
    prev_delta = delta;

    time = min(t + stride * params.jitter, ssray.max_time);
    t += stride;

    float4 ss_ray = ssray.origin + ssray.direction * time;

    depth_sample = texture(planarDepth, float3(ss_ray.xy, planar_ref_id)).r;

    delta = depth_sample - ss_ray.z;
    /* Check if the ray is below the surface. */
    hit = (delta < 0.0f);
  }
  /* Reject hit if background. */
  hit = hit && (depth_sample != 1.0f);
  /* Refine hit using intersection between the sampled height-field and the ray.
   * This simplifies nicely to this single line. */
  time = mix(prev_time, time, saturate(prev_delta / (prev_delta - delta)));

  hit_position = ssray.origin.xyz + ssray.direction.xyz * time;
  /* Planar Reflections have X mirrored. */
  hit_position.x = 1.0f - hit_position.x;

  return hit;
}

float screen_border_mask(float2 hit_co)
{
  constexpr float margin = 0.003f;
  float atten = ssrBorderFac + margin; /* Screen percentage */
  hit_co = smoothstep(0.0f, atten, hit_co) * (1.0f - smoothstep(1.0f - atten, 1.0f, hit_co));

  float screenfade = hit_co.x * hit_co.y;

  return screenfade;
}
