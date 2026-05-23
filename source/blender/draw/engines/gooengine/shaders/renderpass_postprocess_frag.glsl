/* SPDX-FileCopyrightText: 2019-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "goo_common_view_lib.glsl"
#include "draw_math_geom_lib.glsl"

#define PASS_POST_UNDEFINED 0
#define PASS_POST_ACCUMULATED_COLOR 1
#define PASS_POST_ACCUMULATED_COLOR_ALPHA 2
#define PASS_POST_ACCUMULATED_LIGHT 3
#define PASS_POST_ACCUMULATED_VALUE 4
#define PASS_POST_DEPTH 5
#define PASS_POST_AO 6
#define PASS_POST_NORMAL 7
#define PASS_POST_TWO_LIGHT_BUFFERS 8
#define PASS_POST_ACCUMULATED_TRANSMITTANCE_COLOR 9

float3 safe_divide_even_color(float3 a, float3 b)
{
  float3 result = float3((b.r != 0.0f) ? a.r / b.r : 0.0f,
                     (b.g != 0.0f) ? a.g / b.g : 0.0f,
                     (b.b != 0.0f) ? a.b / b.b : 0.0f);
  /* try to get gray even if b is zero */
  if (b.r == 0.0f) {
    if (b.g == 0.0f) {
      result = result.bbb;
    }
    else if (b.b == 0.0f) {
      result = result.ggg;
    }
    else {
      result.r = 0.5f * (result.g + result.b);
    }
  }
  else if (b.g == 0.0f) {
    if (b.b == 0.0f) {
      result = result.rrr;
    }
    else {
      result.g = 0.5f * (result.r + result.b);
    }
  }
  else if (b.b == 0.0f) {
    result.b = 0.5f * (result.r + result.g);
  }

  return result;
}

void main()
{
  float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);
  int2 texel = int2(gl_FragCoord.xy);

  if (postProcessType == PASS_POST_DEPTH) {
    float depth = texelFetch(depthBuffer, texel, 0).r;
    if (depth == 1.0f) {
      depth = 1e10f;
    }
    else {
      depth = -get_view_z_from_depth(depth);
    }
    color.rgb = float3(depth);
  }
  else if (postProcessType == PASS_POST_AO) {
    float ao_accum = texelFetch(inputBuffer, texel, 0).r;
    color.rgb = float3(min(1.0f, ao_accum / currentSample));
  }
  else if (postProcessType == PASS_POST_NORMAL) {
    float depth = texelFetch(depthBuffer, texel, 0).r;
    float2 encoded_normal = texelFetch(inputBuffer, texel, 0).rg;
    /* decode the normals only when they are valid. otherwise the result buffer will be filled
     * with NaN's */
    if (depth != 1.0f && any(notEqual(encoded_normal, float2(0.0f)))) {
      float3 decoded_normal = normal_decode(texelFetch(inputBuffer, texel, 0).rg, float3(0.0f));
      float3 world_normal = transform_direction(ViewMatrixInverseGoo, decoded_normal);
      color.rgb = world_normal;
    }
    else {
      color.rgb = float3(0.0f);
    }
  }
  else if (postProcessType == PASS_POST_ACCUMULATED_VALUE) {
    float accumulated_value = texelFetch(inputBuffer, texel, 0).r;
    color.rgb = float3(accumulated_value / currentSample);
  }
  else if (postProcessType == PASS_POST_ACCUMULATED_COLOR) {
    float3 accumulated_color = texelFetch(inputBuffer, texel, 0).rgb;
    color.rgb = (accumulated_color / currentSample);
  }
  else if (postProcessType == PASS_POST_ACCUMULATED_COLOR_ALPHA) {
    float4 accumulated_color = texelFetch(inputBuffer, texel, 0);
    color = (accumulated_color / currentSample);
  }
  else if (postProcessType == PASS_POST_ACCUMULATED_TRANSMITTANCE_COLOR) {
    float3 accumulated_color = texelFetch(inputBuffer, texel, 0).rgb;
    float3 transmittance = texelFetch(inputTransmittanceBuffer, texel, 0).rgb;
    color.rgb = (accumulated_color / currentSample) * (transmittance / currentSample);
  }
  else if (postProcessType == PASS_POST_ACCUMULATED_LIGHT) {
    float3 accumulated_light = texelFetch(inputBuffer, texel, 0).rgb;
    float3 accumulated_color = texelFetch(inputColorBuffer, texel, 0).rgb;
    color.rgb = safe_divide_even_color(accumulated_light, accumulated_color);
  }
  else if (postProcessType == PASS_POST_TWO_LIGHT_BUFFERS) {
    float3 accumulated_light = texelFetch(inputBuffer, texel, 0).rgb +
                             texelFetch(inputSecondLightBuffer, texel, 0).rgb;
    float3 accumulated_color = texelFetch(inputColorBuffer, texel, 0).rgb;
    color.rgb = safe_divide_even_color(accumulated_light, accumulated_color);
  }
  else {
    /* Output error color: Unknown how to post process this pass. */
    color.rgb = float3(1.0f, 0.0f, 1.0f);
  }

  FragColor = color;
}
