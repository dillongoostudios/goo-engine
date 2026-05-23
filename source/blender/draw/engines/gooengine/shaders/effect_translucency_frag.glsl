/* SPDX-FileCopyrightText: 2019-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "goo_common_view_lib.glsl"
#include "draw_math_geom_lib.glsl"
#include "common_utiltex_lib.glsl"
#include "lights_lib.glsl"

float3 sss_profile(float s)
{
  s /= radii_max_radius.w * avg_inv_radius;
  return texture(sssTexProfile, saturate(s) * SSS_LUT_SCALE + SSS_LUT_BIAS).rgb;
}

float light_translucent_power_with_falloff(LightData ld, float3 N, float4 l_vector)
{
  float power, falloff;
  /* XXX: Removing Area Power. */
  /* TODO: put this out of the shader. */
  if (ld.l_type >= AREA_RECT) {
    power = (ld.l_sizex * ld.l_sizey * 4.0f * M_PI) * (1.0f / 80.0f);
    if (ld.l_type == AREA_ELLIPSE) {
      power *= M_PI_4;
    }
    power *= 0.3f * 20.0f *
             max(0.0f, dot(-ld.l_forward, l_vector.xyz / l_vector.w)); /* XXX ad hoc, empirical */
    power /= (l_vector.w * l_vector.w);
    falloff = dot(N, l_vector.xyz / l_vector.w);
  }
  else if (ld.l_type == SUN) {
    power = 1.0f / (1.0f + (ld.l_radius * ld.l_radius * 0.5f));
    power *= ld.l_radius * ld.l_radius * M_PI; /* Removing area light power. */
    power *= M_2PI * 0.78f;                     /* Matching cycles with point light. */
    power *= 0.082f;                            /* XXX ad hoc, empirical */
    falloff = dot(N, -ld.l_forward);
  }
  else {
    power = (4.0f * ld.l_radius * ld.l_radius) * (1.0f / 10.0f);
    power *= 1.5f; /* XXX ad hoc, empirical */
    power /= (l_vector.w * l_vector.w);
    falloff = dot(N, l_vector.xyz / l_vector.w);
  }
  /* No transmittance at grazing angle (hide artifacts) */
  return power * saturate(falloff * 2.0f);
}

/* Some driver poorly optimize this code. Use direct reference to matrices. */
#define sd(x) shadows_data[x]
#define scube(x) shadows_cube_data[x]
#define scascade(x) shadows_cascade_data[x]

float shadow_cube_radial_depth(float3 cubefloat, float tex_id, int shadow_id)
{
  float depth = sample_cube(sssShadowCubes, cubefloat, tex_id).r;
  /* To reverting the constant bias from shadow rendering. (Tweaked for 16bit shadow-maps) */
  constexpr float depth_bias = 3.1e-5f;
  depth = saturate(depth - depth_bias);

  depth = linear_depth(true, depth, sd(shadow_id).sh_far, sd(shadow_id).sh_near);
  depth *= length(cubefloat / max_v3(abs(cubefloat)));
  return depth;
}

float3 light_translucent(LightData ld, float3 P, float3 N, float4 l_vector, float2 rand, float sss_scale)
{
  int shadow_id = int(ld.l_shadowid);

  float4 L = (ld.l_type != SUN) ? l_vector : float4(-ld.l_forward, 1.0f);

  /* We use the full l_vector.xyz so that the spread is minimize
   * if the shading point is further away from the light source */
  /* TODO(fclem): do something better than this. */
  float3 T, B;
  make_orthonormal_basis(L.xyz / L.w, T, B);

  float3 n;
  float4 depths;
  float d, dist;
  int data_id = int(sd(shadow_id).sh_data_index);
  if (ld.l_type == SUN) {
    float4 view_z = float4(dot(P - cameraPos, cameraForward));

    float4 weights = step(scascade(data_id).split_end_distances, view_z);
    float id = abs(4.0f - dot(weights, weights));
    if (id > 3.0f) {
      return float3(0.0f);
    }

    /* Same factor as in get_cascade_world_distance(). */
    float range = abs(sd(shadow_id).sh_far - sd(shadow_id).sh_near);

    float4 shpos = scascade(data_id).shadowmat[int(id)] * float4(P, 1.0f);
    dist = shpos.z * range;

    if (shpos.z > 1.0f || shpos.z < 0.0f) {
      return float3(0.0f);
    }

    float tex_id = scascade(data_id).sh_tex_index + id;

    /* Assume cascades have same height and width. */
    float2 ofs = float2(1.0f, 0.0f) / float(textureSize(sssShadowCascades, 0).x);
    d = sample_cascade(sssShadowCascades, shpos.xy, tex_id).r;
    depths.x = sample_cascade(sssShadowCascades, shpos.xy + ofs.xy, tex_id).r;
    depths.y = sample_cascade(sssShadowCascades, shpos.xy + ofs.yx, tex_id).r;
    depths.z = sample_cascade(sssShadowCascades, shpos.xy - ofs.xy, tex_id).r;
    depths.w = sample_cascade(sssShadowCascades, shpos.xy - ofs.yx, tex_id).r;

    /* To reverting the constant bias from shadow rendering. (Tweaked for 16bit shadow-maps) */
    float depth_bias = 3.1e-5f;
    depths = saturate(depths - depth_bias);
    d = saturate(d - depth_bias);

    /* Size of a texel in world space.
     * FIXME This is only correct if l_right is the same right vector used for shadow-map creation.
     * This won't work if the shadow matrix is rotated (soft shadows).
     * TODO: precompute. */
    float unit_world_in_uv_space = length(float3x3(scascade(data_id).shadowmat[int(id)]) * ld.l_right);
    float dx_scale = 2.0f * ofs.x / unit_world_in_uv_space;

    d *= range;
    depths *= range;

    /* This is the normal of the occluder in world space. */
    // float3 T = ld.l_forward * dx + ld.l_right * dx_scale;
    // float3 B = ld.l_forward * dy + ld.l_up * dx_scale;
    // n = normalize(cross(T, B));
  }
  else {
    float ofs = 1.0f / float(textureSize(sssShadowCubes, 0).x);

    float3 cubefloat = transform_point(scube(data_id).shadowmat, P);
    dist = length(cubefloat);
    cubefloat /= dist;
    /* `tex_id == data_id` for cube shadow-map. */
    float tex_id = float(data_id);
    d = shadow_cube_radial_depth(cubefloat, tex_id, shadow_id);
    /* NOTE: The offset is irregular in respect to cube-face uvs. But it has
     * a much more uniform behavior than biasing based on face derivatives. */
    depths.x = shadow_cube_radial_depth(cubefloat + T * ofs, tex_id, shadow_id);
    depths.y = shadow_cube_radial_depth(cubefloat + B * ofs, tex_id, shadow_id);
    depths.z = shadow_cube_radial_depth(cubefloat - T * ofs, tex_id, shadow_id);
    depths.w = shadow_cube_radial_depth(cubefloat - B * ofs, tex_id, shadow_id);
  }

  float dx = depths.x - depths.z;
  float dy = depths.y - depths.w;

  float s = min(d, min_v4(depths));

  /* To avoid light leak from depth discontinuity and shadow-map aliasing. */
  float slope_bias = (abs(dx) + abs(dy)) * 0.5f;
  s -= slope_bias;

  float delta = dist - s;

  float power = light_translucent_power_with_falloff(ld, N, l_vector);

  return power * sss_profile(abs(delta) / sss_scale);
}

#undef sd
#undef scube
#undef scsmd

/* Similar to https://atyuwen.github.io/posts/normal-reconstruction/.
 * This samples the depth buffer 4 time for each direction to get the most correct
 * implicit normal reconstruction out of the depth buffer. */
float3 view_position_derivative_from_depth(float2 uvs, float2 ofs, float3 vP, float depth_center)
{
  float2 uv1 = uvs - ofs * 2.0f;
  float2 uv2 = uvs - ofs;
  float2 uv3 = uvs + ofs;
  float2 uv4 = uvs + ofs * 2.0f;
  float4 H;
  H.x = textureLod(depthBuffer, uv1, 0.0f).r;
  H.y = textureLod(depthBuffer, uv2, 0.0f).r;
  H.z = textureLod(depthBuffer, uv3, 0.0f).r;
  H.w = textureLod(depthBuffer, uv4, 0.0f).r;
  /* Fix issue with depth precision. Take even larger diff. */
  float4 diff = abs(float4(depth_center, H.yzw) - H.x);
  if (max_v4(diff) < 2.4e-7f && all(lessThan(diff.xyz, diff.www))) {
    return 0.25f * (get_view_space_from_depth(uv3, H.w) - get_view_space_from_depth(uv1, H.x));
  }
  /* Simplified (H.xw + 2.0f * (H.yz - H.xw)) - depth_center */
  float2 deltas = abs((2.0f * H.yz - H.xw) - depth_center);
  if (deltas.x < deltas.y) {
    return vP - get_view_space_from_depth(uv2, H.y);
  }
  else {
    return get_view_space_from_depth(uv3, H.z) - vP;
  }
}

/* TODO(@fclem): port to a common place for other effects to use. */
bool reconstruct_view_position_and_normal_from_depth(float2 uvs, out float3 vP, out float3 vNg)
{
  float2 texel_size = float2(abs(gpu_dfdx(uvs.x)), abs(gpu_dfdy(uvs.y)));
  float depth_center = textureLod(depthBuffer, uvs, 0.0f).r;

  vP = get_view_space_from_depth(uvs, depth_center);

  float3 dPdx = view_position_derivative_from_depth(uvs, texel_size * float2(1, 0), vP, depth_center);
  float3 dPdy = view_position_derivative_from_depth(uvs, texel_size * float2(0, 1), vP, depth_center);

  vNg = safe_normalize(cross(dPdx, dPdy));

  /* Background case. */
  if (depth_center == 1.0f) {
    return false;
  }

  return true;
}

void main(void)
{
  float2 uvs = screen_uv;
  float sss_scale = texture(sssRadius, uvs).r;

  float3 rand = texelfetch_noise_tex(gl_FragCoord.xy).zwy;
  rand.xy *= fast_sqrt(rand.z);

  float3 vP, vNg;
  reconstruct_view_position_and_normal_from_depth(uvs, vP, vNg);

  float3 P = point_view_to_world(vP);
  float3 Ng = normal_view_to_world(vNg);

  float3 accum = float3(0.0f);
  for (int i = 0; i < MAX_LIGHT && i < laNumLight; i++) {
    LightData ld = lights_data[i];

    /* Only shadowed light can produce translucency */
    if (ld.l_shadowid < 0.0f) {
      continue;
    }

    float4 l_vector; /* Non-Normalized Light Vector with length in last component. */
    l_vector.xyz = ld.l_position - P;
    l_vector.w = length(l_vector.xyz);

    float att = light_attenuation(ld, l_vector, int4(0,0,0,1));
    if (att < 1e-8f) {
      continue;
    }

    accum += att * ld.l_color * light_translucent(ld, P, -Ng, l_vector, rand.xy, sss_scale);
  }

  FragColor = float4(accum, 1.0f);
}
