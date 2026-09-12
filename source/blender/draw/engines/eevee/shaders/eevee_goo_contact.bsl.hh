/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "draw_view.bsl.hh"
#include "eevee_light_shared.hh"
#include "eevee_reverse_z_lib.bsl.hh"
#include "eevee_sampling_lib.bsl.hh"
namespace eevee {
/** Same legacy screen-space march as Shader Info, with explicit view/depth inputs. */
float goo_contact_shadow_trace(sampler2D raycast_depth_tx,
                               const ViewMatrices view,
                               [[resource_table]] const Sampling &samp,
                               float2 pixel,
                               LightData light,
                               float3 P,
                               float3 Ng,
                               float3 L)
{
  /* Same origin offset as Goo (`ray.origin = vP + vNg * sh_contact_offset`). */
  float3 ws_start = P + Ng * light.contact_bias;
  float3 ws_end = ws_start + L * light.contact_dist;
  {

    /* Clip against the near plane in view space to keep the screen projection valid (the culling
     * frustum UBO is not bound in every pipeline, so no `clip_ray` here). Out-of-screen steps are
     * handled by the in-loop bounds check. */
    float3 vs_start = view.point_world_to_view(ws_start);
    float3 vs_end = view.point_world_to_view(ws_end);
    constexpr float z_near_eps = -1e-4f;
    if (vs_start.z > z_near_eps) {
      return 1.0f;
    }
    if (vs_end.z > z_near_eps) {
      float t_clip = (z_near_eps - vs_start.z) / (vs_end.z - vs_start.z);
      vs_end = mix(vs_start, vs_end, t_clip);
    }

    /* Faithful port of Goo's `raytrace()` (raytrace_lib.glsl) with contact parameters:
     * - The ray is marched in screen space, ~one pixel per base step, with a stride that grows by
     *   `trace_quality` (0.1) every step: dense near the contact point, sparse further away. This
     *   is what gives Goo contact shadows their tight roots and soft distance falloff.
     * - The per-light view-space `contact_thickness` is interpolated in screen space through the
     *   endpoint `w` components (depth of `view_z - thickness`), so occluders act as slabs. */
    float3 ss_start = view.point_view_to_screen(vs_start);
    float3 ss_end = view.point_view_to_screen(vs_end);
    float thickness = max(light.contact_thickness, 1e-4f);
    float4 ss_ray_start = float4(ss_start, view.depth_view_to_screen(vs_start.z - thickness));
    float4 ss_ray_end = float4(ss_end, view.depth_view_to_screen(vs_end.z - thickness));

    const float2 extent = float2(textureSize(raycast_depth_tx, 0).xy);
    float4 ss_delta = ss_ray_end - ss_ray_start;
    /* Normalize to advance one pixel per time unit (Goo `raytrace_screenspace_ray_finalize`). */
    float2 px_delta = abs(ss_delta.xy) * extent;
    float pixel_len = max(max(px_delta.x, px_delta.y), 1e-4f);
    float4 ss_step = ss_delta / pixel_len;
    float max_time = pixel_len;
    /* Goo: rays shorter than ~one pixel never hit (`max_time < 1.1`). */
    if (max_time < 1.1f) {
      return 1.0f;
    }

    /* Per-sample jitter; TAA accumulation turns it into a soft edge like Goo's `rand_x`. */
    float noise_offset = samp.rng_1D_get(SAMPLING_RAYTRACE_W);
    float jitter = interleaved_gradient_noise(pixel, 1.0f, noise_offset);

    constexpr float trace_quality = 0.1f; /* Goo `light_contact_shadows`. */
    /* Goo allows up to 255 steps; 128 with the growing stride covers ~950px which is enough for
     * close-ups while keeping the bridge loop cheap. */
    constexpr int max_steps = 128;
    float t = 1.001f;
    float time = 1.001f;
    for (int iter = 1; (time < max_time) && (iter < max_steps); iter++) {
      float stride = 1.0f + float(iter) * trace_quality;
      time = min(t + stride * jitter, max_time);
      t += stride;

      float4 ss_p = ss_ray_start + ss_step * time;
      if (ss_p.x < 0.0f || ss_p.x > 1.0f || ss_p.y < 0.0f || ss_p.y > 1.0f) {
        break;
      }
      float depth_sample = reverse_z::read(textureLod(raycast_depth_tx, ss_p.xy, 0.0f).r);
      if (depth_sample >= 1.0f) {
        continue; /* Background. */
      }
      float delta = depth_sample - ss_p.z;
      /* Below the surface but within the thickness slab (or step-sized tolerance). */
      if (delta < 0.0f && (delta > ss_p.z - ss_p.w || abs(delta) < abs(ss_step.z * stride * 2.0f)))
      {
        return 0.0f;
      }
    }
  }
  return 1.0f;
}
struct GooContact {
  [[sampler(GOO_CONTACT_DEPTH_TEX_SLOT)]] sampler2D goo_contact_depth_tx;
};
}  // namespace eevee
