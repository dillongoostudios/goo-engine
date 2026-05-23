/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Shader that down-sample depth buffer,
 * saving min and max value of each texel in the above mipmaps.
 * Adapted from http://rastergrid.com/blog/2010/10/hierarchical-z-map-based-occlusion-culling/
 *
 * Major simplification has been made since we pad the buffer to always be bigger than input to
 * avoid mipmapping misalignment.
 */

#ifdef LAYERED
#  define sampleLowerMip(t) texture(depthBuffer, float3(t, depthLayer)).r
#  define gatherLowerMip(t) textureGather(depthBuffer, float3(t, depthLayer))
#else
#  define sampleLowerMip(t) texture(depthBuffer, t).r
#  define gatherLowerMip(t) textureGather(depthBuffer, t)
#endif

#ifdef MIN_PASS
#  define minmax2(a, b) min(a, b)
#  define minmax3(a, b, c) min(min(a, b), c)
#  define minmax4(a, b, c, d) min(min(min(a, b), c), d)
#else /* MAX_PASS */
#  define minmax2(a, b) max(a, b)
#  define minmax3(a, b, c) max(max(a, b), c)
#  define minmax4(a, b, c, d) max(max(max(a, b), c), d)
#endif

void main()
{
  float2 texel = gl_FragCoord.xy;

#ifdef COPY_DEPTH
  float2 uv = texel / float2(textureSize(depthBuffer, 0).xy);

  float val = sampleLowerMip(uv);
#else
  /* NOTE(@fclem): textureSize() does not work the same on all implementations
   * when changing the min and max texture levels. Use uniform instead (see #87801). */
  float2 uv = texel * 2.0f * texelSize;

  float4 samp;
#  ifdef GPU_ARB_texture_gather
  samp = gatherLowerMip(uv);
#  else
  samp.x = sampleLowerMip(uv + float2(-0.5f, -0.5f) * texelSize);
  samp.y = sampleLowerMip(uv + float2(-0.5f, 0.5f) * texelSize);
  samp.z = sampleLowerMip(uv + float2(0.5f, -0.5f) * texelSize);
  samp.w = sampleLowerMip(uv + float2(0.5f, 0.5f) * texelSize);
#  endif

  float val = minmax4(samp.x, samp.y, samp.z, samp.w);
#endif

#if (defined(GPU_INTEL) || defined(GPU_ATI)) && defined(GPU_OPENGL)
  /* Use color format instead of 24bit depth texture */
  gl_FragColor = float4(val);
#endif

#if !(defined(GPU_INTEL) && defined(GPU_OPENGL))
  /* If using Intel workaround, do not write out depth as there will be no depth target and this is
   * invalid. */
  gl_FragDepth = val;
#endif
}
