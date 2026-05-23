/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "engine_eevee_shared_defines.hh"
#include "engine_eevee_legacy_shared.hh"
#include "draw_math_geom_lib.glsl"
#include "raytrace_lib.glsl"
#include "ltc_lib.glsl"

/* ---------------------------------------------------------------------- */
/** \name Structure
 * \{ */

/* convenience aliases */
#define l_color color_influence_volume.rgb
#define l_diff diff_spec_volume.x
#define l_spec diff_spec_volume.y
#define l_volume diff_spec_volume.z
#define l_volume_radius diff_spec_volume.w
#define l_position position_influence.xyz
#define l_influence position_influence.w
#define l_influence_volume color_influence_volume.w
#define l_sizex rightvec_sizex.w
#define l_sizey upvec_sizey.w
#define l_right rightvec_sizex.xyz
#define l_up upvec_sizey.xyz
#define l_forward forwardvec_type.xyz
#define l_type forwardvec_type.w
#define l_spot_size spotdata_radius_shadow.x
#define l_spot_blend spotdata_radius_shadow.y
#define l_radius spotdata_radius_shadow.z
#define l_shadowid spotdata_radius_shadow.w

/* convenience aliases */
#define sh_near near_far_bias_id.x
#define sh_far near_far_bias_id.y
#define sh_bias near_far_bias_id.z
#define sh_data_index near_far_bias_id.w
#define sh_contact_dist contact_shadow_data.x
#define sh_contact_offset contact_shadow_data.y
#define sh_contact_spread contact_shadow_data.z
#define sh_contact_thickness contact_shadow_data.w
#define sh_shadow_vec shadow_vec_id.xyz
#define sh_tex_index shadow_vec_id.w

/** \} */

/* ---------------------------------------------------------------------- */
/** \name Resources
 * \{ */

#if !defined(USE_GPU_SHADER_CREATE_INFO)

layout(std140) uniform shadow_block
{
  ShadowBlock _shadow_block;
};

layout(std140) uniform light_block
{
  LightBlock _light_block;
};

uniform depth2DArrayShadow shadowCubeTexture;
uniform depth2DArrayShadow shadowCascadeTexture;

/* Keep original syntax: ShadowBlock */
#  define shadows_data _shadow_block.shadows_data
#  define shadows_cube_data _shadow_block.shadows_cube_data
#  define shadows_cascade_data _shadow_block.shadows_cascade_data

/* Keep original syntax: LightBlock */
#  define lights_data _light_block.lights_data

#else
#  ifndef EEVEE_SHADER_SHARED_H
#    error Ensure eevee_legacy_common_lib is included.
#  endif
#endif

/** \} */

/* ---------------------------------------------------------------------- */
/** \name Shadow Functions
 * \{ */

/* Type. Consistent with DNA `Light::type`, except for `OMNI_DISK`/`SPOT_DISK` and `AREA_ELLIPSE`,
 * which are reinterpreted from `light.mode` and `light.area_type`. The decimal numbers are chosen
 * so that they can be exactly represented by float, and guarantee an increasing sequence. */
#define OMNI_SPHERE 0.0f
#define OMNI_DISK 0.5f

#define SUN 1.0f

#define SPOT_SPHERE 2.0f
#define SPOT_DISK 2.5f

#define AREA_RECT 4.0f
/* Used to define the area light shape, doesn't directly correspond to a Blender light type. */
#define AREA_ELLIPSE 100.0f

float cubeFaceIndexEEVEE(float3 P)
{
  float3 aP = abs(P);
  if (all(greaterThan(aP.xx, aP.yz))) {
    return (P.x > 0.0f) ? 0.0f : 1.0f;
  }
  else if (all(greaterThan(aP.yy, aP.xz))) {
    return (P.y > 0.0f) ? 2.0f : 3.0f;
  }
  else {
    return (P.z > 0.0f) ? 4.0f : 5.0f;
  }
}

float2 cubeFaceCoordEEVEE(float3 P, float face, float scale)
{
  if (face < 2.0f) {
    return (P.zy / P.x) * scale * float2(-0.5f, -sign(P.x) * 0.5f) + 0.5f;
  }
  else if (face < 4.0f) {
    return (P.xz / P.y) * scale * float2(sign(P.y) * 0.5f, 0.5f) + 0.5f;
  }
  else {
    return (P.xy / P.z) * scale * float2(0.5f, -sign(P.z) * 0.5f) + 0.5f;
  }
}

float2 cubeFaceCoordEEVEE(float3 P, float face, sampler2DArray tex)
{
  /* Scaling to compensate the 1px border around the face. */
  float cube_res = float(textureSize(tex, 0).x);
  float scale = (cube_res) / (cube_res + 1.0f);
  return cubeFaceCoordEEVEE(P, face, scale);
}

float2 cubeFaceCoordEEVEE(float3 P, float face, sampler2DArrayShadow tex)
{
  /* Scaling to compensate the 1px border around the face. */
  float cube_res = float(textureSize(tex, 0).x);
  float scale = (cube_res) / (cube_res + 1.0f);
  return cubeFaceCoordEEVEE(P, face, scale);
}

float4 sample_cube(sampler2DArray tex, float3 cubefloat, float cube)
{
  /* Manual Shadow Cube Layer indexing. */
  float face = cubeFaceIndexEEVEE(cubefloat);
  float2 uv = cubeFaceCoordEEVEE(cubefloat, face, tex);

  float3 coord = float3(uv, cube * 6.0f + face);
  return texture(tex, coord);
}

float4 sample_cascade(sampler2DArray tex, float2 co, float cascade_id)
{
  return texture(tex, float3(co, cascade_id));
}

/* Some driver poorly optimize this code. Use direct reference to matrices. */
#define sd(x) shadows_data[x]
#define scube(x) shadows_cube_data[x]
#define scascade(x) shadows_cascade_data[x]

/*
  Gather samples and manually compare against the resource_id, then interpolate the results.
*/
#ifdef USE_SHADOW_ID
float sample_ID_texture(usampler2DArray TEX_ID, float3 coord, bool match)
{
  ufloat4 id_kernel = textureGather(TEX_ID, coord);
  float4 matches;
  if (match) {
    matches = float4(equal(id_kernel, ufloat4(resource_id)));
  } else {
    matches = float4(notEqual(id_kernel, ufloat4(resource_id)));
  }

  int3 tex_size = textureSize(TEX_ID, 0);
  // WHY THE FLYING FUCK DO WE NEED AN EXTRA 0.00195f?
  float2 fra = fract((coord.xy * tex_size.xy) + float2(0.50195f, 0.50195f));

  return mix(
    mix(matches.w, matches.z, fra.x),
    mix(matches.x, matches.y, fra.x),
    fra.y
  );
}
#else
float sample_ID_texture(usampler2DArray TEX_ID, float3 coord, bool match) {
  return 0.0f;
}
#endif


float sample_cube_shadow(int shadow_id, float3 P, bool match_shadow_id)
{
  int data_id = int(sd(shadow_id).sh_data_index);
  float3 cubefloat = transform_point(scube(data_id).shadowmat, P);
  float dist = max(sd(shadow_id).sh_near, max_v3(abs(cubefloat)) - sd(shadow_id).sh_bias);
  dist = buffer_depth(true, dist, sd(shadow_id).sh_far, sd(shadow_id).sh_near);
  /* Manual Shadow Cube Layer indexing. */
  /* TODO: Shadow Cube Array. */
  float face = cubeFaceIndexEEVEE(cubefloat);
  float2 coord = cubeFaceCoordEEVEE(cubefloat, face, shadowCubeTexture);
  /* `tex_id == data_id` for cube shadow-map. */
  float tex_id = float(data_id);
  float4 coord_f = float4(coord, tex_id * 6.0f + face, dist);

#ifdef USE_SHADOW_ID
  return min(sample_ID_texture(shadowCubeIDTexture, coord_f.xyz, match_shadow_id) + texture(shadowCubeTexture, coord_f), 1.0f);
#else
  return texture(shadowCubeTexture, coord_f);
#endif
}

float sample_cascade_shadow(int shadow_id, float3 P, bool match_shadow_id)
{
  int data_id = int(sd(shadow_id).sh_data_index);
  float tex_id = scascade(data_id).sh_tex_index;
  float4 view_z = float4(dot(P - cameraPos, cameraForward));
  float4 weights = 1.0f - smoothstep(scascade(data_id).split_end_distances,
                                  scascade(data_id).split_start_distances.yzwx,
                                  view_z);
  float tot_weight = dot(weights.xyz, float3(1.0f));

  int cascade = int(clamp(tot_weight, 0.0f, 3.0f));
  float blend = fract(tot_weight);
  float vis = weights.w;
  float4 coord, shpos;
  /* Main cascade. */
  shpos = scascade(data_id).shadowmat[cascade] * float4(P, 1.0f);
  coord = float4(shpos.xy, tex_id + float(cascade), shpos.z - sd(shadow_id).sh_bias);
#ifdef USE_SHADOW_ID
  float id_sample = sample_ID_texture(shadowCascadeIDTexture, coord.xyz, match_shadow_id);
  vis += min(texture(shadowCascadeTexture, coord) + id_sample, 1.0f)  * (1.0f - blend);
#else
  vis += texture(shadowCascadeTexture, coord) * (1.0f - blend);
#endif

  cascade = min(3, cascade + 1);
  /* Second cascade. */
  shpos = scascade(data_id).shadowmat[cascade] * float4(P, 1.0f);
  coord = float4(shpos.xy, tex_id + float(cascade), shpos.z - sd(shadow_id).sh_bias);
#ifdef USE_SHADOW_ID
  id_sample = sample_ID_texture(shadowCascadeIDTexture, coord.xyz, match_shadow_id);
  vis += min(texture(shadowCascadeTexture, coord) + id_sample, 1.0f) * blend;
#else
  vis += texture(shadowCascadeTexture, coord) * blend;
#endif

  return saturate(vis);
}
#undef sd
#undef scube
#undef scsmd

/** \} */

/* ---------------------------------------------------------------------- */
/** \name Light Functions
 * \{ */

/* From Frostbite PBR Course
 * Distance based attenuation
 * http://www.frostbite.com/wp-content/uploads/2014/11/course_notes_moving_frostbite_to_pbr.pdf */
float distance_attenuation(float dist_sqr, float inv_sqr_influence)
{
  float factor = dist_sqr * inv_sqr_influence;
  float fac = saturate(1.0f - factor * factor);
  return fac * fac;
}

float spot_attenuation(LightData ld, float3 l_vector)
{
  float z = dot(ld.l_forward, l_vector.xyz);
  float3 lL = l_vector.xyz / z;
  float x = dot(ld.l_right, lL) / ld.l_sizex;
  float y = dot(ld.l_up, lL) / ld.l_sizey;
  float ellipse = inversesqrt(1.0f + x * x + y * y);
  float spotmask = smoothstep(0.0f, 1.0f, (ellipse - ld.l_spot_size) / ld.l_spot_blend);
  return spotmask;
}

float light_attenuation(LightData ld, float4 l_vector, int4 light_groups)
{
  float vis = 1.0f;
#if !defined(VOLUME_LIGHTING) // && !defined(STEP_RESOLVE)
  if (
       (ld.light_group_bits.x & light_groups.x) == 0
    && (ld.light_group_bits.y & light_groups.y) == 0
    && (ld.light_group_bits.z & light_groups.z) == 0
    && (ld.light_group_bits.w & light_groups.w) == 0
  ) {
    return 0.0f;
  }
#endif
  if (ld.l_type == SPOT_SPHERE || ld.l_type == SPOT_DISK) {
    vis *= spot_attenuation(ld, l_vector.xyz);
  }
  if (ld.l_type >= SPOT_SPHERE) {
    vis *= step(0.0f, -dot(l_vector.xyz, ld.l_forward));
  }
  if (ld.l_type != SUN) {
#ifdef VOLUME_LIGHTING
    vis *= distance_attenuation(l_vector.w * l_vector.w, ld.l_influence_volume);
#else
    vis *= distance_attenuation(l_vector.w * l_vector.w, ld.l_influence);
#endif
  }
  return vis;
}

float light_shadowing(LightData ld, float3 P, float vis, int4 light_group_shadows)
{
#if !defined(VOLUMETRICS) || defined(VOLUME_SHADOW)
  if (ld.l_shadowid >= 0.0f && vis > 0.001f && !(
        (ld.light_group_bits.x & light_group_shadows.x) == 0
     && (ld.light_group_bits.y & light_group_shadows.y) == 0
     && (ld.light_group_bits.z & light_group_shadows.z) == 0
     && (ld.light_group_bits.w & light_group_shadows.w) == 0)
  ) {
    if (ld.l_type == SUN) {
      vis *= sample_cascade_shadow(int(ld.l_shadowid), P, true);
    }
    else {
      vis *= sample_cube_shadow(int(ld.l_shadowid), P, true);
    }
  }
#endif
  return vis;
}

#ifndef VOLUMETRICS
float light_contact_shadows(LightData ld, float3 P, float3 vP, float3 vNg, float rand_x, float vis, int4 light_group_shadows)
{
  if (ld.l_shadowid >= 0.0f && vis > 0.001f && !(
     (ld.light_group_bits.x & light_group_shadows.x) == 0
  && (ld.light_group_bits.y & light_group_shadows.y) == 0
  && (ld.light_group_bits.z & light_group_shadows.z) == 0
  && (ld.light_group_bits.w & light_group_shadows.w) == 0)
  ) {
    ShadowData sd = shadows_data[int(ld.l_shadowid)];
    /* Only compute if not already in shadow. */
    if (sd.sh_contact_dist > 0.0f) {
      /* Contact Shadows. */
      Ray ray;

      if (ld.l_type == SUN) {
        ray.direction = shadows_cascade_data[int(sd.sh_data_index)].sh_shadow_vec *
                        sd.sh_contact_dist;
      }
      else {
        ray.direction = shadows_cube_data[int(sd.sh_data_index)].position.xyz - P;
        ray.direction *= saturate(sd.sh_contact_dist * safe_rcp(length(ray.direction)));
      }

      ray.direction = transform_direction(ViewMatrixGoo, ray.direction);
      ray.origin = vP + vNg * sd.sh_contact_offset;

      RayTraceParameters params;
      params.thickness = sd.sh_contact_thickness;
      params.jitter = rand_x;
      params.trace_quality = 0.1f;
      params.roughness = 0.001f;

      float3 hit_position_unused;

      if (raytrace(ray, params, false, false, hit_position_unused)) {
        return 0.0f;
      }
    }
  }
  return 1.0f;
}
#endif /* !VOLUMETRICS */

float light_visibility(LightData ld, float3 P, float4 l_vector, int4 light_groups, int4 light_group_shadows)
{
  float l_atten = light_attenuation(ld, l_vector, light_groups);
  return light_shadowing(ld, P, l_atten, light_group_shadows);
}

bool is_sphere_light(float type)
{
  return type == OMNI_SPHERE || type == SPOT_SPHERE;
}

float light_diffuse(LightData ld, float3 N, float3 V, float4 l_vector)
{
  if (ld.l_type == AREA_RECT) {
    float3 corners[4];
    corners[0] = normalize((l_vector.xyz + ld.l_right * -ld.l_sizex) + ld.l_up * ld.l_sizey);
    corners[1] = normalize((l_vector.xyz + ld.l_right * -ld.l_sizex) + ld.l_up * -ld.l_sizey);
    corners[2] = normalize((l_vector.xyz + ld.l_right * ld.l_sizex) + ld.l_up * -ld.l_sizey);
    corners[3] = normalize((l_vector.xyz + ld.l_right * ld.l_sizex) + ld.l_up * ld.l_sizey);

    return ltc_evaluate_quad(corners, N);
  }
  else if (ld.l_type == AREA_ELLIPSE) {
    float3 points[3];
    points[0] = (l_vector.xyz + ld.l_right * -ld.l_sizex) + ld.l_up * -ld.l_sizey;
    points[1] = (l_vector.xyz + ld.l_right * ld.l_sizex) + ld.l_up * -ld.l_sizey;
    points[2] = (l_vector.xyz + ld.l_right * ld.l_sizex) + ld.l_up * ld.l_sizey;

    return ltc_evaluate_disk(N, V, float3x3(1.0f), points);
  }
  else if (is_sphere_light(ld.l_type)) {
    if (l_vector.w < ld.l_radius) {
      /* Inside, treat as hemispherical light. */
      return 1.0f;
    }
    else {
      /* Outside, treat as disk light spanning the same solid angle. */
      /* The result is the same as passing the scaled radius to #ltc_evaluate_disk_simple (see
       * #light_specular), using simplified math here. */
      float r_sq = sqr(ld.l_radius / l_vector.w);
      float3 L = l_vector.xyz / l_vector.w;
      return r_sq * diffuse_sphere_integral(dot(N, L), r_sq);
    }
  }
  else {
    /* Sun light or omni disk light. */
    float radius = ld.l_radius;
    radius /= (ld.l_type == SUN) ? 1.0f : l_vector.w;
    float3 L = (ld.l_type == SUN) ? -ld.l_forward : (l_vector.xyz / l_vector.w);

    return ltc_evaluate_disk_simple(radius, dot(N, L));
  }
}

float light_specular(LightData ld, float4 ltc_mat, float3 N, float3 V, float4 l_vector)
{
  if (ld.l_type == AREA_RECT) {
    float3 corners[4];
    corners[0] = (l_vector.xyz + ld.l_right * -ld.l_sizex) + ld.l_up * ld.l_sizey;
    corners[1] = (l_vector.xyz + ld.l_right * -ld.l_sizex) + ld.l_up * -ld.l_sizey;
    corners[2] = (l_vector.xyz + ld.l_right * ld.l_sizex) + ld.l_up * -ld.l_sizey;
    corners[3] = (l_vector.xyz + ld.l_right * ld.l_sizex) + ld.l_up * ld.l_sizey;

    ltc_transform_quad(N, V, ltc_matrix(ltc_mat), corners);

    return ltc_evaluate_quad(corners, float3(0.0f, 0.0f, 1.0f));
  }
  else if (is_sphere_light(ld.l_type) && l_vector.w < ld.l_radius) {
    /* Inside the sphere light, integrate over the hemisphere. */
    return 1.0f;
  }
  else {
    bool is_ellipse = (ld.l_type == AREA_ELLIPSE);
    float radius_x = is_ellipse ? ld.l_sizex : ld.l_radius;
    float radius_y = is_ellipse ? ld.l_sizey : ld.l_radius;

    if (is_sphere_light(ld.l_type)) {
      /* The sine of the half-angle spanned by a sphere light is equal to the tangent of the
       * half-angle spanned by a disk light with the same radius. */
      radius_x *= inversesqrt(1.0f - sqr(radius_x / l_vector.w));
      radius_y *= inversesqrt(1.0f - sqr(radius_y / l_vector.w));
    }

    float3 L = (ld.l_type == SUN) ? -ld.l_forward : l_vector.xyz;
    float3 Px, Py;

    if (ld.l_type == SUN || ld.l_type == AREA_ELLIPSE) {
      Px = ld.l_right;
      Py = ld.l_up;
    }
    else {
      /* Omni and spot lights. */
      make_orthonormal_basis(l_vector.xyz / l_vector.w, Px, Py);
    }

    float3 points[3];
    points[0] = (L + Px * -radius_x) + Py * -radius_y;
    points[1] = (L + Px * radius_x) + Py * -radius_y;
    points[2] = (L + Px * radius_x) + Py * radius_y;

    return ltc_evaluate_disk(N, V, ltc_matrix(ltc_mat), points);
  }
}

/** \} */
