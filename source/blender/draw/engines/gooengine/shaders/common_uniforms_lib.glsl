/* SPDX-FileCopyrightText: 2018-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#define COMMON_UNIFORMS_LIB

#if !defined(USE_GPU_SHADER_CREATE_INFO)
/* keep in sync with CommonUniformBlock */
layout(std140) uniform common_block
{
  float4x4 pastViewProjectionMatrix;
  float4 hizUvScale; /* To correct mip level texel misalignment */
  /* Ambient Occlusion */
  float4 aoParameters[2];
  /* Volumetric */
  int4 volTexSize;
  float4 volDepthParameters; /* Parameters to the volume Z equation */
  float4 volInvTexSize;
  float4 volJitter;
  float4 volCoordScale; /* To convert volume uvs to screen uvs */
  float volHistoryAlpha;
  float volShadowSteps;
  bool32_t volUseLights;
  bool32_t volUseSoftShadows;
  /* Screen Space Reflections */
  float4 ssrParameters;
  float ssrBorderFac;
  float ssrMaxRoughness;
  float ssrFireflyFac;
  float ssrBrdfBias;
  bool32_t ssrToggle;
  bool32_t ssrefractToggle;
  /* SubSurface Scattering */
  float sssJitterThreshold;
  bool32_t sssToggle;
  /* Specular */
  bool32_t specToggle;
  /* Lights */
  int laNumLight;
  /* Probes */
  int prbNumPlanar;
  int prbNumRenderCube;
  int prbNumRenderGrid;
  int prbIrradianceVisSize;
  float prbIrradianceSmooth;
  float prbLodCubeMax;
  /* Misc */
  int rayType;
  float rayDepth;
  float alphaHashOffset;
  float alphaHashScale;
  /* Misc */
  float4 cameraUvScaleBias;
  float4 planarClipPlane;
  bool shadow_id_high_bitdepth;
};

#endif /* !USE_GPU_SHADER_CREATE_INFO */

#ifdef USE_GPU_SHADER_CREATE_INFO
#  ifndef EEVEE_SHADER_SHARED_H
#    error Missing eevee_legacy_common_lib additional create info on shader create info
#  endif
#endif

/* rayType (keep in sync with ray_type) */
#define EEVEE_RAY_CAMERA 0
#define EEVEE_RAY_SHADOW 1
#define EEVEE_RAY_DIFFUSE 2
#define EEVEE_RAY_GLOSSY 3

/* aoParameters */
#define aoDistance aoParameters[0].x
#define aoSamples aoParameters[0].y /* UNUSED */
#define aoFactor aoParameters[0].z
#define aoInvSamples aoParameters[0].w /* UNUSED */

#define aoOffset aoParameters[1].x /* UNUSED */
#define aoBounceFac aoParameters[1].y
#define aoQuality aoParameters[1].z
#define aoSettings aoParameters[1].w

/* ssrParameters */
#define ssrQuality ssrParameters.x
#define ssrThickness ssrParameters.y
#define ssrPixelSize ssrParameters.zw

#define ssrUvScale hizUvScale.zw
