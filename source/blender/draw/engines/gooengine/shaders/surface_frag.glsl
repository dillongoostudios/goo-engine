/* SPDX-FileCopyrightText: 2020-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Required by some nodes. */
#include "gooengine_nodetree_lib.glsl"
#include "draw_curves_lib.glsl"
#include "common_utiltex_lib.glsl"

#include "closure_eval_surface_lib.glsl"

#include "surface_lib.glsl"
#include "renderpass_lib.glsl"

#ifdef EEVEE_DISPLACEMENT_BUMP

#  ifndef GPU_METAL
/* Prototype. */
float3 displacement_exec();
#  endif

/* Return new shading normal. */
float3 displacement_bump()
{
#  ifdef HAIR_SHADER
  /* Not supported. */
  return normalize(g_data.N);
#  else
  /* This is the filter width for automatic displacement + bump mapping, which is fixed.
   * NOTE: keep the same as default bump node filter width. */
  constexpr float bump_filter_width = 0.1f;

  float2 dHd;
  dF_branch(dot(displacement_exec(), g_data.N + dF_impl(g_data.N)), bump_filter_width, dHd);
  // dF_branch(dot(displacement_exec(), g_data.N + dF_impl(g_data.N)), dHd);

  float3 dPdx = gpu_dfdx(g_data.P);
  float3 dPdy = gpu_dfdy(g_data.P);

  /* Get surface tangents from normal. */
  float3 Rx = cross(dPdy, g_data.N);
  float3 Ry = cross(g_data.N, dPdx);

  /* Compute surface gradient and determinant. */
  float det = dot(dPdx, Rx);

  float3 surfgrad = dHd.x * Rx + dHd.y * Ry;

  float facing = FrontFacing ? 1.0f : -1.0f;
  return normalize(bump_filter_width * abs(det) * g_data.N - facing * sign(det) * surfgrad);
#  endif
}

#endif

void main()
{
  g_data = init_globals();

#ifdef EEVEE_DISPLACEMENT_BUMP
  g_data.N = displacement_bump();
#endif

#if defined(WORLD_BACKGROUND) || defined(PROBE_CAPTURE)

/* Skip attribute load for shaders which do not require this part of the path. E.g. Cryptomatte. */
#  ifndef NO_ATTRIB_LOAD
  attrib_load();
#  endif
#endif

  out_ssr_color = float3(0.0f);
  out_ssr_roughness = 0.0f;
  out_ssr_N = g_data.N;

  out_sss_radiance = float3(0.0f);
  out_sss_radius = 0.0f;
  out_sss_color = float3(0.0f);

  Closure cl = nodetree_exec();

#ifdef WORLD_BACKGROUND
  if (!renderPassEnvironment) {
    cl.holdout += 1.0f - backgroundAlpha;
    cl.radiance *= backgroundAlpha;
  }
#endif

  float holdout = saturate(1.0f - cl.holdout);
  float transmit = saturate(avg(cl.transmittance));
  float alpha = 1.0f - transmit;
  
#ifdef OBINFO_LIB
  int flag = int(abs(ObjectFlag));
  if ((flag & DRW_BASE_HOLDOUT) != 0) {
#ifdef USE_ALPHA_BLEND
    /* A bit of a mess, but we basically need to remove all but the holdout output. */
    cl.transmittance = float3(1.0f);
    cl.radiance = float3(0.0f);
    transmit = 1.0f;
    holdout = 1.0f - alpha;
    alpha = 0.0f;
#else
    /* We want "default" holdout behaviour, fully invisible */
    cl.radiance = float3(0.0f);
    holdout = 0.0f;
#endif
  }
#endif

#ifdef USE_ALPHA_BLEND
  float2 uvs = gl_FragCoord.xy * volCoordScale.zw;

  cl.radiance = cl.radiance * holdout;
  outRadiance = float4(cl.radiance, alpha * holdout);
  outTransmittance = float4(cl.transmittance, transmit) * holdout;
#else
  outRadiance = float4(cl.radiance, holdout);
  ssrNormals = normal_encode(normalize(float3x3(ViewMatrixGoo) * out_ssr_N), float3(0.0f));
  ssrData = float4(out_ssr_color, out_ssr_roughness);
  sssIrradiance = out_sss_radiance;
  sssRadius = out_sss_radius;
  sssAlbedo = out_sss_color;
#endif

#ifdef USE_REFRACTION
  /* SSRefraction pass is done after the SSS pass.
   * In order to not lose the diffuse light totally we
   * need to merge the SSS radiance to the main radiance. */
  constexpr bool use_refraction = true;
#else
  constexpr bool use_refraction = false;
#endif
  /* For Probe capture */
  if (!sssToggle || use_refraction) {
    outRadiance.rgb += out_sss_radiance * out_sss_color;
  }

#ifndef USE_ALPHA_BLEND
  float alpha_div = safe_rcp(alpha);
  outRadiance.rgb *= alpha_div;
  ssrData.rgb *= alpha_div;
  sssAlbedo.rgb *= alpha_div;

  if (renderPassAOV) {
    if (aov_is_valid) {
      outRadiance = float4(out_aov, 1.0f);
    }
    else {
      outRadiance = float4(0.0f);
    }
  }
#endif

#ifdef LOOKDEV
  /* Lookdev spheres are rendered in front. */
  gl_FragDepth = 0.0f;
#endif
}

/* Only supported attrib for world/background shaders. */
float3 attr_load_orco(float4 orco)
{
  /* Retain precision better than g_data.P (see #99128). */
  return -normal_view_to_world(viewCameraVec(viewPosition));
}
/* Unsupported. */
float4 attr_load_tangent(float4 tangent)
{
  return float4(0);
}
float4 attr_load_vec4(float4 attr)
{
  return float4(0);
}
float3 attr_load_vec3(float3 attr)
{
  return float3(0);
}
float2 attr_load_vec2(float2 attr)
{
  return float2(0);
}

/* Passthrough. */
float attr_load_temperature_post(float attr)
{
  return attr;
}
float4 attr_load_color_post(float4 attr)
{
  return attr;
}
float4 attr_load_uniform(float4 attr, const uint attr_hash)
{
  return attr;
}
