/* SPDX-FileCopyrightText: 2015-2016 Keijiro Takahashi
 * SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: MIT AND GPL-2.0-or-later */

/* Original implementation by Keijiro Takahashi (MIT license).
 * Blender integration by Clément Foucault. */

#include "draw_math_geom_lib.glsl"

/* -------------- Utils ------------- */

/* 3-tap median filter */
float3 median(float3 a, float3 b, float3 c)
{
  return a + b + c - min(min(a, b), c) - max(max(a, b), c);
}

/* ------------- Filters ------------ */

float3 downsample_filter_high(sampler2D tex, float2 uv, float2 texelSize)
{
  /* Downsample with a 4x4 box filter + anti-flicker filter */
  float4 d = texelSize.xyxy * float4(-1, -1, +1, +1);

  float3 s1 = textureLod(tex, uv + d.xy, 0.0f).rgb;
  float3 s2 = textureLod(tex, uv + d.zy, 0.0f).rgb;
  float3 s3 = textureLod(tex, uv + d.xw, 0.0f).rgb;
  float3 s4 = textureLod(tex, uv + d.zw, 0.0f).rgb;

  /* Karis's luma weighted average (using brightness instead of luma) */
  float s1w = 1.0f / (max_v3(s1) + 1.0f);
  float s2w = 1.0f / (max_v3(s2) + 1.0f);
  float s3w = 1.0f / (max_v3(s3) + 1.0f);
  float s4w = 1.0f / (max_v3(s4) + 1.0f);
  float one_div_wsum = 1.0f / (s1w + s2w + s3w + s4w);

  return (s1 * s1w + s2 * s2w + s3 * s3w + s4 * s4w) * one_div_wsum;
}

float3 downsample_filter(sampler2D tex, float2 uv, float2 texelSize)
{
  /* Downsample with a 4x4 box filter */
  float4 d = texelSize.xyxy * float4(-1, -1, +1, +1);

  float3 s;
  s = textureLod(tex, uv + d.xy, 0.0f).rgb;
  s += textureLod(tex, uv + d.zy, 0.0f).rgb;
  s += textureLod(tex, uv + d.xw, 0.0f).rgb;
  s += textureLod(tex, uv + d.zw, 0.0f).rgb;

  return s * (1.0f / 4);
}

float3 upsample_filter_high(sampler2D tex, float2 uv, float2 texelSize)
{
  /* 9-tap bilinear upsampler (tent filter) */
  float4 d = texelSize.xyxy * float4(1, 1, -1, 0) * sampleScale;

  float3 s;
  s = textureLod(tex, uv - d.xy, 0.0f).rgb;
  s += textureLod(tex, uv - d.wy, 0.0f).rgb * 2;
  s += textureLod(tex, uv - d.zy, 0.0f).rgb;

  s += textureLod(tex, uv + d.zw, 0.0f).rgb * 2;
  s += textureLod(tex, uv, 0.0f).rgb * 4;
  s += textureLod(tex, uv + d.xw, 0.0f).rgb * 2;

  s += textureLod(tex, uv + d.zy, 0.0f).rgb;
  s += textureLod(tex, uv + d.wy, 0.0f).rgb * 2;
  s += textureLod(tex, uv + d.xy, 0.0f).rgb;

  return s * (1.0f / 16.0f);
}

float3 upsample_filter(sampler2D tex, float2 uv, float2 texelSize)
{
  /* 4-tap bilinear upsampler */
  float4 d = texelSize.xyxy * float4(-1, -1, +1, +1) * (sampleScale * 0.5f);

  float3 s;
  s = textureLod(tex, uv + d.xy, 0.0f).rgb;
  s += textureLod(tex, uv + d.zy, 0.0f).rgb;
  s += textureLod(tex, uv + d.xw, 0.0f).rgb;
  s += textureLod(tex, uv + d.zw, 0.0f).rgb;

  return s * (1.0f / 4.0f);
}

/* ----------- Steps ----------- */

float4 step_blit(void)
{
  float2 uv = screen_uv + sourceBufferTexelSize.xy * 0.5f;

#ifdef HIGH_QUALITY /* Anti flicker */
  float3 d = sourceBufferTexelSize.xyx * float3(1, 1, 0);
  float3 s0 = safe_color(textureLod(sourceBuffer, screen_uv, 0.0f).rgb);
  float3 s1 = safe_color(textureLod(sourceBuffer, screen_uv - d.xz, 0.0f).rgb);
  float3 s2 = safe_color(textureLod(sourceBuffer, screen_uv + d.xz, 0.0f).rgb);
  float3 s3 = safe_color(textureLod(sourceBuffer, screen_uv - d.zy, 0.0f).rgb);
  float3 s4 = safe_color(textureLod(sourceBuffer, screen_uv + d.zy, 0.0f).rgb);
  float3 m = median(median(s0.rgb, s1, s2), s3, s4);
#else
  float3 s0 = safe_color(textureLod(sourceBuffer, screen_uv, 0.0f).rgb);
  float3 m = s0.rgb;
#endif

  /* Pixel brightness */
  float br = max_v3(m);

  /* Under-threshold part: quadratic curve */
  float rq = clamp(br - curveThreshold.x, 0.0f, curveThreshold.y);
  rq = curveThreshold.z * rq * rq;

  /* Combine and apply the brightness response curve. */
  m *= max(rq, br - curveThreshold.w) / max(1e-5f, br);

  /* Clamp pixel intensity if clamping enabled */
  if (clampIntensity > 0.0f) {
    br = max(1e-5f, max_v3(m));
    m *= 1.0f - max(0.0f, br - clampIntensity) / br;
  }

  return float4(m, 1.0f);
}

float4 step_downsample(void)
{
#ifdef HIGH_QUALITY /* Anti flicker */
  float3 samp = downsample_filter_high(sourceBuffer, screen_uv, sourceBufferTexelSize);
#else
  float3 samp = downsample_filter(sourceBuffer, screen_uv, sourceBufferTexelSize);
#endif
  return float4(samp, 1.0f);
}

float4 step_upsample(void)
{
#ifdef HIGH_QUALITY
  float3 blur = upsample_filter_high(sourceBuffer, screen_uv, sourceBufferTexelSize);
#else
  float3 blur = upsample_filter(sourceBuffer, screen_uv, sourceBufferTexelSize);
#endif
  float3 base = textureLod(baseBuffer, screen_uv, 0.0f).rgb;
  return float4(base + blur, 1.0f);
}

float4 step_resolve(void)
{
#ifdef HIGH_QUALITY
  float3 blur = upsample_filter_high(sourceBuffer, screen_uv, sourceBufferTexelSize);
#else
  float3 blur = upsample_filter(sourceBuffer, screen_uv, sourceBufferTexelSize);
#endif
  float4 base = bloomAddBase ? textureLod(baseBuffer, screen_uv, 0.0f) : float4(0.0f);
  float3 cout = base.rgb + blur * bloomColor;
  return float4(cout, base.a);
}

void main(void)
{
#if defined(STEP_BLIT)
  FragColor = step_blit();
#elif defined(STEP_DOWNSAMPLE)
  FragColor = step_downsample();
#elif defined(STEP_UPSAMPLE)
  FragColor = step_upsample();
#elif defined(STEP_RESOLVE)
  FragColor = step_resolve();
#endif
}
