/* SPDX-FileCopyrightText: 2020-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "common_math_lib.glsl"
#include "common_utiltex_lib.glsl"
#include "lights_lib.glsl"

void main()
{
  if (laNumLight == 0) {
    /* Early exit: No lights in scene */
    FragColor.r = 1.0;
    return;
  }

  ivec2 texel = ivec2(gl_FragCoord.xy);
  float depth = texelFetch(depthBuffer, texel, 0).r;
  if (depth == 1.0f) {
    /* Early exit background does not receive shadows */
    FragColor.r = 1.0;
    return;
  }

  vec2 texel_size = 1.0 / vec2(textureSize(depthBuffer, 0)).xy;
  vec2 uvs = saturate(gl_FragCoord.xy * texel_size);
  vec4 rand = texelfetch_noise_tex(texel);

  float accum_light = 0.0;

  vec3 vP = get_view_space_from_depth(uvs, depth);
  vec3 P = transform_point(ViewMatrixInverse, vP);

  vec3 vNg = safe_normalize(cross(dFdx(vP), dFdy(vP)));

  for (int i = 0; i < MAX_LIGHT && i < laNumLight; i++) {
    LightData ld = lights_data[i];

    vec4 l_vector; /* Non-Normalized Light Vector with length in last component. */
    l_vector.xyz = ld.l_position - P;
    l_vector.w = length(l_vector.xyz);

    float l_vis = light_shadowing(ld, P, 1.0, ivec4(-1), 1.0);

    l_vis *= light_contact_shadows(ld, P, vP, vNg, rand.x, 1.0, ivec4(-1));

    accum_light += l_vis;
  }

  FragColor.r = accum_light / float(laNumLight);
}
