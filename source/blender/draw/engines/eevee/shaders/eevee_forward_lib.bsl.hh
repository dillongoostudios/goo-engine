/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/**
 * Forward lighting evaluation: Lighting is evaluated during the geometry rasterization.
 *
 * This is used by alpha blended materials and materials using Shader to RGB nodes.
 */

#include "infos/eevee_geom_infos.hh"

#include "draw_model.bsl.hh"
#include "eevee_colorspace_lib.bsl.hh"
#include "eevee_light_eval.bsl.hh"
#include "eevee_lightprobe.bsl.hh"
#include "eevee_lightprobe_plane.bsl.hh"
#include "eevee_nodetree_closures_lib.glsl"
#include "eevee_ray_trace_screen_lib.bsl.hh"
#include "eevee_reverse_z_lib.bsl.hh"
#include "eevee_sampling_lib.bsl.hh"
#include "eevee_subsurface_lib.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"

#ifdef GLSL_CPP_STUBS
#  define MAT_REFLECTION
#endif

/* Allow static compilation of forward materials. */
#ifndef CLOSURE_BIN_COUNT
#  define CLOSURE_BIN_COUNT SRT_CONSTANT_light_closure_eval_count
#endif

#ifndef GLSL_CPP_STUBS
#  if CLOSURE_BIN_COUNT != SRT_CONSTANT_light_closure_eval_count && \
      SRT_CONSTANT_light_closure_eval_count != 0
#    error Closure data count and eval count must match
#  endif
#endif

namespace eevee {

void forward_lighting_eval(const ViewMatrices view,
                           uint resource_id,
                           Thickness thickness,
                           float2 frag_co,
                           float3 &radiance,
                           float3 &transmittance)
{
  [[resource_table]] LightEvalIterator &lights = resource_table_get(eevee::LightEvalIterator);
  [[resource_table]] UtilityTexture &util_tx = resource_table_get(UtilityTexture);
  [[resource_table]] const Uniform &uni = resource_table_get(eevee::Uniform);
  /* clang-format off */ /* Multi-line macro breaks error line counting. */
  [[resource_table]] LightprobeRenderData &lightprobes = resource_table_get(eevee::LightprobeRenderData);
  [[resource_table]] LightprobePlaneRenderData &lightprobe_planes = resource_table_get(eevee::LightprobePlaneRenderData);
  /* clang-format on */
  [[resource_table]] LightEvalData &srt = lights.inner;
  [[resource_table]] draw::Infos &infos = resource_table_get(draw::Infos);

  float vPz = dot(view.forward(), g_data.P) - dot(view.forward(), view.position());
  float3 V = view.world_incident_vector(g_data.P);
  /* Capture before any closure tree reset. Legacy OPAQUE uses this only to recover old internal
   * energy; its external transmittance remains zero and therefore does not blend or attenuate
   * volume scattering. */
  float surface_alpha_rcp = surface_internal_alpha_rcp();

  light::EvalCtx<false> ctx;
  ctx.material_groups = goo_surface_groups();
#ifdef MAT_SHADOW_ID
  ctx.shadow_id_filter = shadow_id_filter_ignore_self(resource_id);
#else
  ctx.shadow_id_filter = shadow_id_filter_disabled();
#endif
  for (uint i = 0u; i < 3; i++) [[unroll]] {
    if (srt.light_closure_eval_count_reflect > i) [[static_branch]] {
      ClosureUndetermined cl = g_closure_get(uchar(i));
      ctx.stack.cl[i] = closure_light_new(util_tx, cl, V);
    }
  }

  ctx.P = g_data.P;
  ctx.Ng = g_data.Ng;
  ctx.V = V;
  ctx.texel = frag_co;
  ctx.thickness = thickness;

  /* TODO(fclem): If transmission (no SSS) is present, we could reduce LIGHT_CLOSURE_EVAL_COUNT
   * by 1 for this evaluation and skip evaluating the transmission closure twice. */
  ObjectInfos object_infos = infos.get(resource_id);
  ctx.receiver_light_set = receiver_light_set_get(object_infos);
  ctx.terminator_normal_offset = object_infos.shadow_terminator_normal_offset;
  ctx.terminator_geometry_offset = object_infos.shadow_terminator_geometry_offset;

  lights.eval_reflection(ctx, vPz);

  if (srt.light_closure_eval_count_transmit > 0) [[static_branch]] {
    ClosureUndetermined cl_transmit = g_closure_get(0);
    if (closure_has_transmission(cl_transmit.type) || cl_transmit.type == CLOSURE_BSSRDF_BURLEY_ID)
    {
      light::EvalCtx<true> ctx_tr = light::init_from_reflect_ctx(ctx);
      ctx_tr.stack.cl[0] = closure_light_new(util_tx, cl_transmit, V, thickness);

      /* NOTE: Only evaluates `stack.cl[0]`. */
      lights.eval_transmission(ctx_tr, vPz);

      if (cl_transmit.type == CLOSURE_BSSRDF_BURLEY_ID) {
#if defined(GLSL_CPP_STUBS) || defined(MAT_SUBSURFACE)
        /* Apply transmission profile onto transmitted light and sum with reflected light. */
        float3 sss_profile = subsurface_transmission(
            util_tx, to_closure_subsurface(cl_transmit).sss_radius, thickness.value());
        ctx.stack.cl[0].light_shadowed += ctx_tr.stack.cl[0].light_shadowed * sss_profile;
        ctx.stack.cl[0].light_unshadowed += ctx_tr.stack.cl[0].light_unshadowed * sss_profile;
#endif
      }
      else {
        ctx.stack.cl[0].light_shadowed = ctx_tr.stack.cl[0].light_shadowed;
        ctx.stack.cl[0].light_unshadowed = ctx_tr.stack.cl[0].light_unshadowed;
      }
    }
  }

  LightProbeSample samp = lightprobes.load(frag_co, g_data.P, g_data.N, V);

  float clamp_indirect_sh = uni.uniform_buf.clamp.surface_indirect;
  samp.volume_irradiance = spherical_harmonics::clamp_energy(samp.volume_irradiance,
                                                             clamp_indirect_sh);

#ifdef MAT_REFLECTION /* Disable if only rough surfaces. */
  /* Planar reflection. */
  float3 planar_probe_radiance = float3(0.0f);
  float3 average_N = g_data.Ng * 0.001f;
  {
    /* Get average normal.  */
    for (uint i = 0u; i < 3; i++) [[unroll]] {
      if (srt.light_closure_eval_count_reflect > i) [[static_branch]] {
        ClosureUndetermined cl = g_closure_get(uchar(i));
        average_N += cl.N * cl.weight;
      }
    }
    average_N = safe_normalize(average_N);

    const int planar_id = lightprobe_planes.select_probe(g_data.P, average_N);

    if (planar_id == -1) {
      average_N = float3(0.0f);
    }
    else {
      float3 P_reflected = lightprobe::plane::parallax(
          lightprobe_planes.probe_planar_buf[planar_id], g_data.P, average_N, V);

      float2 ndc_P_reflected = view.point_world_to_ndc(P_reflected).xy;
      /* Planar probes are rendered upside down. */
      ndc_P_reflected.y = -ndc_P_reflected.y;
      float2 texel = view.ndc_to_screen(ndc_P_reflected);

      planar_probe_radiance =
          textureLod(lightprobe_planes.planar_radiance_tx, float3(texel, planar_id), 0.0).rgb;
      /* Discard background hits. */
      if (textureLod(lightprobe_planes.planar_depth_tx, float3(texel, planar_id), 0.0).r ==
          reverse_z::read(1.0f))
      {
        average_N = float3(0.0f);
      }
    }
  }
#endif

  /* Combine all radiance. */
  float3 radiance_direct = float3(0.0f);
  float3 radiance_indirect = float3(0.0f);

  for (uint i = 0u; i < 3; i++) [[unroll]] {
    if (srt.light_closure_eval_count_reflect > i) [[static_branch]] {
      ClosureUndetermined cl = g_closure_get_resolved(uchar(i), 1.0f);
      if (cl.weight > CLOSURE_WEIGHT_CUTOFF) {
        float3 direct_light = ctx.stack.cl[i].light_shadowed;
        float3 indirect_light = lightprobes.eval(samp, cl, g_data.P, V, thickness);

#ifdef MAT_REFLECTION
        if (cl.type == CLOSURE_BSDF_MICROFACET_GGX_REFLECTION_ID) {
          const float blend = saturate(to_closure_reflection(cl).roughness * -10.0f + 1.0f) *
                              saturate(dot(average_N, cl.N) * 100.0f - 99.0f);
          indirect_light = mix(indirect_light, planar_probe_radiance, blend);
        }
#endif

        if ((cl.type == CLOSURE_BSDF_TRANSLUCENT_ID ||
             cl.type == CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID) &&
            (thickness.value() != 0.0f))
        {
          /* We model two transmission event, so the surface color need to be applied twice. */
          cl.color *= cl.color;
        }

        radiance_direct += direct_light * cl.color;
        radiance_indirect += indirect_light * cl.color;
      }
    }
  }
  /* Light clamping. */
  float clamp_direct = uni.uniform_buf.clamp.surface_direct;
  float clamp_indirect = uni.uniform_buf.clamp.surface_indirect;

  radiance_direct = colorspace::brightness_clamp_max(radiance_direct, clamp_direct);
  radiance_indirect = colorspace::brightness_clamp_max(radiance_indirect, clamp_indirect);

  radiance_direct *= uni.uniform_buf.clamp.direct_scale;
  radiance_indirect *= uni.uniform_buf.clamp.indirect_scale;

  radiance = radiance_direct + radiance_indirect + g_emission;
#ifdef MAT_LEGACY_OPAQUE
  radiance *= surface_alpha_rcp;
#endif

  transmittance = g_transmittance;
}

/* Goo Engine legacy contact shadows (`light_contact_shadows`): short-range screen-space ray
 * march toward the light, multiplied into the per-light shadow visibility of the Shader Info
 * bridge. Uses the raycast prepass textures (bound for Shader Info materials via
 * GPU_MATFLAG_RAYCAST). Returns 0 when a screen-space occluder is found, 1 otherwise. */
float goo_contact_shadow(LightData light, float3 P, float3 Ng, float3 L)
{
#if defined(MAT_RAYCAST) && defined(GPU_FRAGMENT_SHADER)
  [[resource_table]] const eevee::Uniform &uni = resource_table_get(eevee::Uniform);
  if (!uni.pipeline_buf.can_raycast) {
    return 1.0f;
  }
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  [[resource_table]] const Sampling &sampling = resource_table_get(eevee::Sampling);
  const auto &depth_tx = sampler_get(eevee_raycast, raycast_depth_tx);
  return goo_contact_shadow_trace(
      depth_tx, views.get(0), sampling, gl_FragCoord.xy, light, P, Ng, L);
#endif
  return 1.0f;
}

/* Goo Engine `calc_shader_info`: a self-contained forward light loop (independent of the material
 * closures) that reproduces Goo's separable per-light accumulation. Runs in the forward pipeline
 * where the light resources are bound, and stores the result in the g_goo_shader_info bridge
 * global for the Shader Info node to read. Uses the standard LTC diffuse eval so units match a
 * normal render (solid-angle normalized) and shadowed/unshadowed give the cast/self shadow ratio.
 */
struct GooShaderInfoCtx {
  float3 P;
  float3 N;
  float3 Ng;
  float3 V;
  float2 texel;
  /* Per-object shadow terminator offsets (same as the regular forward lighting); without them
   * shadow_eval self-shadows entire curved surfaces on coarse VSM texels (large scenes). */
  float terminator_normal_offset;
  float terminator_geometry_offset;
  /** Full receiver resource ID shared with the shadow caster sidecar. */
  uint receiver_id;
  /** Receiver light-linking set, shared with the normal EEVEE light loop. */
  uchar receiver_light_set;
  /* Per-light records are written straight into g_goo_shader_info; the context only tracks the
   * running count and the always-on overflow accumulator for lights beyond GOO_MAX_LIGHTS. */
  int count;
  float3 overflow_unshadowed;
  float overflow_goo_weight;
  float overflow_cast_occlusion;
  float overflow_self_occlusion;
  float overflow_hl;

  void accumulate([[resource_table]] LightEvalData &srt,
                  LightData light,
                  const bool is_directional)
  {
    /* Goo's exact group rule: a light with all-zero group bits can never match any mask, so it
     * contributes to nothing. This also excludes EEVEE-Next's world-sun placeholder lights
     * (zero-initialized bits), which do not exist in Goo. */
    int4 lbits = light.light_group_bits;
    if ((lbits.x | lbits.y | lbits.z | lbits.w) == 0) {
      return;
    }
    /* Keep the Shader Info bridge on the same receiver-side light-linking contract as ordinary
     * EEVEE lighting. Shadow linking itself remains handled by shadow_eval() through the shadow
     * render-view membership, while this test gates the light's diffuse and shadow records. */
    if (!light::light_linking_affects_receiver(light.light_set_membership, receiver_light_set)) {
      return;
    }
    [[resource_table]] ShadowRenderData &srd = srt.shadow_data;
    [[resource_table]] const Uniform &uni = srd.uniforms;
    [[resource_table]] const UtilityTexture &util = srt.utility_tx;
    const auto &util_tx = util.utility_tx;

    LightVector lv = light_vector_get(light, is_directional, P);
    /* Goo's half-lambert is a pure geometric term: every group-passing light contributes
     * `0.5 * dot(L, N) + 0.5`, including back-facing and weak lights. Compute it before any
     * attenuation gating so the dark-side wrap (0..0.5) is preserved. */
    float hl = 0.5f * dot(lv.L, N) + 0.5f;
    float attenuation = light_attenuation_surface(light, is_directional, lv);
    attenuation *= light_attenuation_facing(light, lv.L, lv.dist, N, false);

    float cast_shadow = 1.0f;
    float self_shadow = 1.0f;
    ClosureLight tmp;
    tmp.light_shadowed = float3(0.0f);
    tmp.light_unshadowed = float3(0.0f);
    if (attenuation >= LIGHT_ATTENUATION_THRESHOLD) {
      /* Only lit lights pay for shadow tracing + LTC eval; back-facing lights still get a
       * record (unshadowed = 0) so their half-lambert term is counted like in Goo. */
      if (light.tilemap_index != LIGHT_NO_SHADOW) {
        int ray_count = uni.uniform_buf.shadow.ray_count;
        int ray_step_count = uni.uniform_buf.shadow.step_count;
        /* Goo legacy shadow bias: legacy EEVEE subtracts `la->bias * 0.05` from the occluder
         * distance (5cm at the DNA default of 1.0, which Goo 4.4 does not expose in the UI), so
         * any occluder closer than that along the light direction never shadows. This immunity is
         * what keeps Goo's Cast Shadows clean on curved skin (VSM facet acne / self-shadowing)
         * and lets a clean sun dilute other lights' shadows in the weighted formula. Reproduce it
         * by pushing the sampling point toward the light; clamped for very close punctual lights.
         * Bridge-only: regular pipeline shadows are untouched. */
        float goo_shadow_bias = min(0.05f, lv.dist * 0.5f);
#ifdef MAT_SHADOW_ID
        /* The two traces intentionally share every non-ID input. Shadow sampling is derived from
         * immutable sampling state, so the second call does not advance a mutable RNG. */
        cast_shadow = shadow_eval(srd,
                                  light,
                                  is_directional,
                                  false,
                                  false,
                                  shadow_id_filter_ignore_self(receiver_id),
                                  texel,
                                  Thickness::zero(),
                                  P + lv.L * goo_shadow_bias,
                                  Ng,
                                  N,
                                  terminator_normal_offset,
                                  terminator_geometry_offset,
                                  ray_count,
                                  ray_step_count);
        self_shadow = shadow_eval(srd,
                                  light,
                                  is_directional,
                                  false,
                                  false,
                                  shadow_id_filter_only_self(receiver_id),
                                  texel,
                                  Thickness::zero(),
                                  P + lv.L * goo_shadow_bias,
                                  Ng,
                                  N,
                                  terminator_normal_offset,
                                  terminator_geometry_offset,
                                  ray_count,
                                  ray_step_count);
#else
        /* Disabled materials preserve the legacy bridge cost and exact Cast/Self equivalence. */
        cast_shadow = shadow_eval(srd,
                                  light,
                                  is_directional,
                                  false,
                                  false,
                                  shadow_id_filter_disabled(),
                                  texel,
                                  Thickness::zero(),
                                  P + lv.L * goo_shadow_bias,
                                  Ng,
                                  N,
                                  terminator_normal_offset,
                                  terminator_geometry_offset,
                                  ray_count,
                                  ray_step_count);
        self_shadow = cast_shadow;
#endif
      }
      /* Goo legacy contact shadows belong only to Cast Shadows. Self Shadows is the VSM
       * OnlySelf result and deliberately excludes screen-space contact occlusion. */
      if (light.contact_dist > 0.0f && cast_shadow > 0.0f) {
        cast_shadow *= goo_contact_shadow(light, P, Ng, lv.L);
      }
      LightVertices vertices = light_shape_corners(light, lv);
      tmp.ltc_mat = eevee::lut::ltc::identity();
      tmp.N = N;
      tmp.type = LIGHT_DIFFUSE;
      light::eval_single_closure(util_tx, light, lv, vertices, tmp, V, attenuation, cast_shadow);
      /* Goo 4.4 applied an additional one-sided Area mask only to Shader Info's Diffuse Shading.
       * LightData::z_axis() is the opposite of Goo's legacy l_forward, so the equivalent test is
       * dot(L, z_axis()) > 0. Do not route this through the shared EEVEE attenuation/facing path:
       * normal materials, VSM, LTC, and the Cast/Self weight accumulators must remain unchanged.
       */
      if (light.type == LIGHT_RECT || light.type == LIGHT_ELLIPSE) {
        const float goo_area_facing = float(dot(lv.L, light.z_axis()) > 0.0f);
        tmp.light_unshadowed *= goo_area_facing;
      }
    }

    /* Goo 4.4 normalizes Cast/Self visibility with the raw light color and diffuse factor, not
     * with the current EEVEE LTC radiance. Keep that scalar separate from Diffuse Shading. */
    const float goo_weight = light.goo_shader_info_weight;
    /* Store one record per visible light: unshadowed diffuse lighting, scalar shadow visibility,
     * and the legacy Goo normalization weight. Overflow lights retain the existing always-on
     * accumulator policy, but use the same weight formula. */
    if (count < GOO_MAX_LIGHTS) {
      g_goo_shader_info.light_group_bits[count] = light.light_group_bits;
      g_goo_shader_info.light_unshadowed[count] = tmp.light_unshadowed;
      g_goo_shader_info.light_cast_shadow[count] = cast_shadow;
      g_goo_shader_info.light_self_shadow[count] = self_shadow;
      g_goo_shader_info.light_hl[count] = hl;
      g_goo_shader_info.light_goo_weight[count] = goo_weight;
      count += 1;
    }
    else {
      overflow_unshadowed += tmp.light_unshadowed;
      overflow_goo_weight += goo_weight;
      overflow_cast_occlusion += (1.0f - cast_shadow) * goo_weight;
      overflow_self_occlusion += (1.0f - self_shadow) * goo_weight;
      overflow_hl += hl;
    }
  }
  void eval_directional([[resource_table]] LightEvalData &srt, uint /*l_idx*/, LightData light)
  {
    accumulate(srt, light, true);
  }
  void eval_local([[resource_table]] LightEvalData &srt, uint /*l_idx*/, LightData light)
  {
    accumulate(srt, light, false);
  }
};

template void light::foreach<GooShaderInfoCtx, LightEvalData>(const LightRenderData &,
                                                              GooShaderInfoCtx &,
                                                              LightEvalData &);

void goo_shader_info_compute(const ViewMatrices view, uint resource_id, float2 frag_co)
{
  [[resource_table]] LightEvalIterator &lights = resource_table_get(eevee::LightEvalIterator);
  /* clang-format off */ /* Multi-line macro breaks error line counting. */
  [[resource_table]] LightprobeRenderData &lightprobes = resource_table_get(eevee::LightprobeRenderData);
  /* clang-format on */
  [[resource_table]] LightEvalData &srt = lights.inner;
  [[resource_table]] draw::Infos &infos = resource_table_get(draw::Infos);

  float3 P = g_data.P;
  float3 N = safe_normalize(g_data.N);
  float3 V = view.world_incident_vector(P);

  GooShaderInfoCtx ctx;
  ctx.P = P;
  ctx.N = N;
  ctx.Ng = g_data.Ng;
  ctx.V = V;
  ctx.texel = frag_co;
  ObjectInfos object_infos = infos.get(resource_id);
  ctx.terminator_normal_offset = object_infos.shadow_terminator_normal_offset;
  ctx.terminator_geometry_offset = object_infos.shadow_terminator_geometry_offset;
  ctx.receiver_id = resource_id;
  ctx.receiver_light_set = receiver_light_set_get(object_infos);
  ctx.count = 0;
  ctx.overflow_unshadowed = float3(0.0f);
  ctx.overflow_goo_weight = 0.0f;
  ctx.overflow_cast_occlusion = 0.0f;
  ctx.overflow_self_occlusion = 0.0f;
  ctx.overflow_hl = 0.0f;

  /* Goo iterates every scene light regardless of power or screen tile (its half-lambert is
   * power-independent), so use the un-culled iterator like the surfel pipeline. */
  light::foreach(lights.light_data, ctx, srt);

  LightProbeSample samp = lightprobes.load(frag_co, P, N, V);

  g_goo_shader_info.light_count = ctx.count;
  g_goo_shader_info.overflow_unshadowed = ctx.overflow_unshadowed;
  g_goo_shader_info.overflow_goo_weight = ctx.overflow_goo_weight;
  g_goo_shader_info.overflow_cast_occlusion = ctx.overflow_cast_occlusion;
  g_goo_shader_info.overflow_self_occlusion = ctx.overflow_self_occlusion;
  g_goo_shader_info.overflow_hl = ctx.overflow_hl;
  g_goo_shader_info.ambient = max(samp.volume_irradiance.evaluate_lambert(N).rgb, float3(0.0f));
  g_goo_shader_info.valid = true;
}

}  // namespace eevee
