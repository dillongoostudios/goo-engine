/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * This shader only compute maximum horizon angles for each directions.
 * The final integration is done at the resolve stage with the shading normal.
 */

#include "goo_common_view_lib.glsl"
#include "draw_math_geom_lib.glsl"
#include "common_utiltex_lib.glsl"
#include "ambient_occlusion_lib.glsl"

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
  H.x = textureLod(maxzBuffer, uv1, 0.0f).r;
  H.y = textureLod(maxzBuffer, uv2, 0.0f).r;
  H.z = textureLod(maxzBuffer, uv3, 0.0f).r;
  H.w = textureLod(maxzBuffer, uv4, 0.0f).r;
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
  float depth_center = textureLod(maxzBuffer, uvs, 0.0f).r;

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

#ifdef DEBUG_AO

void main()
{
  float3 vP, vNg;
  float2 uvs = screen_uv;

  if (!reconstruct_view_position_and_normal_from_depth(uvs * hizUvScale.xy, vP, vNg)) {
    /* Handle Background case. Prevent artifact due to uncleared Horizon Render Target. */
    FragColor = float4(0.0f);
  }
  else {
    float3 P = transform_point(ViewMatrixInverseGoo, vP);
    float3 V = cameraVec(P);
    float3 vV = viewCameraVec(vP);
    float3 vN = normal_decode(texture(normalBuffer, uvs).rg, vV);
    float3 N = transform_direction(ViewMatrixInverseGoo, vN);
    float3 Ng = transform_direction(ViewMatrixInverseGoo, vNg);

    OcclusionData data = occlusion_load(vP, 1.0f);

    if (min_v4(abs(data.horizons)) != M_PI) {
      FragColor = float4(diffuse_occlusion(data, V, N, Ng));
    }
    else {
      FragColor = float4(1.0f);
    }
  }
}

#else

void main()
{
  float2 uvs = screen_uv;
  float depth = textureLod(maxzBuffer, uvs * hizUvScale.xy, 0.0f).r;
  float3 vP = get_view_space_from_depth(uvs, depth);

  OcclusionData data = NO_OCCLUSION_DATA;
  /* Do not trace for background */
  if (depth != 1.0f) {
    data = occlusion_search(vP, maxzBuffer, aoDistance, 0.0f, 8.0f);
  }

  FragColor = pack_occlusion_data(data);
}
#endif
