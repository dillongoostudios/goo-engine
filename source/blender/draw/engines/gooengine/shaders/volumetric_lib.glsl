/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "lights_lib.glsl"
// #include "lightprobe_lib.glsl"
#include "irradiance_lib.glsl"

/* Based on Frosbite Unified Volumetric.
 * https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite */

/* Volume slice to view space depth. */
float volume_z_to_view_z(float z)
{
  if (ProjectionMatrixGoo[3][3] == 0.0f) {
    /* Exponential distribution */
    return (exp2(z / volDepthParameters.z) - volDepthParameters.x) / volDepthParameters.y;
  }
  else {
    /* Linear distribution */
    return mix(volDepthParameters.x, volDepthParameters.y, z);
  }
}

float view_z_to_volume_z(float depth)
{
  if (ProjectionMatrixGoo[3][3] == 0.0f) {
    /* Exponential distribution */
    return volDepthParameters.z * log2(depth * volDepthParameters.y + volDepthParameters.x);
  }
  else {
    /* Linear distribution */
    return (depth - volDepthParameters.x) * volDepthParameters.z;
  }
}

/* Volume texture normalized coordinates to NDC (special range [0, 1]). */
float3 volume_to_ndc(float3 cos)
{
  cos.z = volume_z_to_view_z(cos.z);
  cos.z = get_depth_from_view_z(cos.z);
  cos.xy /= volCoordScale.xy;
  return cos;
}

float3 ndc_to_volume(float3 cos)
{
  cos.z = get_view_z_from_depth(cos.z);
  cos.z = view_z_to_volume_z(cos.z);
  cos.xy *= volCoordScale.xy;
  return cos;
}

float phase_function_isotropic()
{
  return 1.0f / (4.0f * M_PI);
}

float phase_function(float3 v, float3 l, float g)
{
  /* Henyey-Greenstein */
  float cos_theta = dot(v, l);
  g = clamp(g, -1.0f + 1e-3f, 1.0f - 1e-3f);
  float sqr_g = g * g;
  return (1 - sqr_g) / max(1e-8f, 4.0f * M_PI * pow(1 + sqr_g - 2 * g * cos_theta, 3.0f / 2.0f));
}

float3 light_volume(LightData ld, float4 l_vector)
{
  float power = 1.0f;
  if (ld.l_type != SUN) {
    /**
     * Using "Point Light Attenuation Without Singularity" from Cem Yuksel
     * http://www.cemyuksel.com/research/pointlightattenuation/pointlightattenuation.pdf
     * http://www.cemyuksel.com/research/pointlightattenuation/
     */
    float d = l_vector.w;
    float d_sqr = sqr(d);
    float r_sqr = ld.l_volume_radius;
    /* Using reformulation that has better numerical precision. */
    power = 2.0f / (d_sqr + r_sqr + d * sqrt(d_sqr + r_sqr));

    if (ld.l_type == AREA_RECT || ld.l_type == AREA_ELLIPSE) {
      /* Modulate by light plane orientation / solid angle. */
      power *= saturate(dot(-ld.l_forward, l_vector.xyz / l_vector.w));
    }
  }
  return ld.l_color * ld.l_volume * power;
}

float3 light_volume_light_vector(LightData ld, float3 P)
{
  if (ld.l_type == SUN) {
    return -ld.l_forward;
  }
  else if (ld.l_type == AREA_RECT || ld.l_type == AREA_ELLIPSE) {
    float3 L = P - ld.l_position;
    float2 closest_point = float2(dot(ld.l_right, L), dot(ld.l_up, L));
    float2 max_pos = float2(ld.l_sizex, ld.l_sizey);
    closest_point /= max_pos;

    if (ld.l_type == AREA_ELLIPSE) {
      closest_point /= max(1.0f, length(closest_point));
    }
    else {
      closest_point = clamp(closest_point, -1.0f, 1.0f);
    }
    closest_point *= max_pos;

    float3 L_prime = ld.l_right * closest_point.x + ld.l_up * closest_point.y;
    return L_prime - L;
  }
  else {
    return ld.l_position - P;
  }
}

#define VOLUMETRIC_SHADOW_MAX_STEP 128.0f

float3 participating_media_extinction(float3 wpos, sampler3D volume_extinction)
{
  /* Waiting for proper volume shadow-maps and out of frustum shadow map. */
  float3 ndc = project_point(ProjectionMatrixGoo, transform_point(ViewMatrixGoo, wpos));
  float3 volume_co = ndc_to_volume(ndc * 0.5f + 0.5f);

  /* Let the texture be clamped to edge. This reduce visual glitches. */
  return texture(volume_extinction, volume_co).rgb;
}

float3 light_volume_shadow(LightData ld, float3 ray_wpos, float4 l_vector, sampler3D volume_extinction)
{
#if defined(VOLUME_SHADOW)
  /* If light is shadowed, use the shadow vector, if not, reuse the light vector. */
  if (volUseSoftShadows && ld.l_shadowid >= 0.0f) {
    ShadowData sd = shadows_data[int(ld.l_shadowid)];

    if (ld.l_type == SUN) {
      l_vector.xyz = shadows_cascade_data[int(sd.sh_data_index)].sh_shadow_vec;
      /* No need for length, it is recomputed later. */
    }
    else {
      l_vector.xyz = shadows_cube_data[int(sd.sh_data_index)].position.xyz - ray_wpos;
      l_vector.w = length(l_vector.xyz);
    }
  }

  /* Heterogeneous volume shadows */
  float dd = l_vector.w / volShadowSteps;
  float3 L = l_vector.xyz / volShadowSteps;

  if (ld.l_type == SUN) {
    /* For sun light we scan the whole frustum. So we need to get the correct endpoints. */
    float3 ndcP = project_point(ProjectionMatrixGoo, transform_point(ViewMatrixGoo, ray_wpos));
    float3 ndcL = project_point(ProjectionMatrixGoo,
                              transform_point(ViewMatrixGoo, ray_wpos + l_vector.xyz)) -
                ndcP;

    float3 frustum_isect = ndcP + ndcL * line_unit_box_intersect_dist_safe(ndcP, ndcL);

    float4 L_hom = ViewMatrixInverseGoo * (ProjectionMatrixInverseGoo * float4(frustum_isect, 1.0f));
    L = (L_hom.xyz / L_hom.w) - ray_wpos;
    L /= volShadowSteps;
    dd = length(L);
  }

  float3 shadow = float3(1.0f);
  for (float s = 1.0f; s < VOLUMETRIC_SHADOW_MAX_STEP && s <= volShadowSteps; s += 1.0f) {
    float3 pos = ray_wpos + L * s;
    float3 s_extinction = participating_media_extinction(pos, volume_extinction);
    shadow *= exp(-s_extinction * dd);
  }
  return shadow;
#else
  return float3(1.0f);
#endif /* VOLUME_SHADOW */
}

float3 irradiance_volumetric(float3 wpos)
{
#ifdef IRRADIANCE_HL2
  IrradianceData ir_data = load_irradiance_cell(0, float3(1.0f));
  float3 irradiance = ir_data.cubesides[0] + ir_data.cubesides[1] + ir_data.cubesides[2];
  ir_data = load_irradiance_cell(0, float3(-1.0f));
  irradiance += ir_data.cubesides[0] + ir_data.cubesides[1] + ir_data.cubesides[2];
  irradiance *= 0.16666666f; /* 1/6 */
  return irradiance;
#else
  return float3(0.0f);
#endif
}

void volumetric_resolve(float2 frag_uvs,
                        float frag_depth,
                        out float3 transmittance,
                        out float3 scattering)
{
  float3 volume_cos = ndc_to_volume(float3(frag_uvs, frag_depth));

  scattering = texture(inScattering, volume_cos).rgb;
  transmittance = texture(inTransmittance, volume_cos).rgb;
}
