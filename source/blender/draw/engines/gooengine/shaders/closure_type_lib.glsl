/* SPDX-FileCopyrightText: 2020-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_shader_codegen_lib.glsl"
/* #include "draw_math_geom_lib.glsl" */
/* #include "common_uniforms_lib.glsl" */
/* #include "renderpass_lib.glsl" */

struct Closure {
#ifdef VOLUMETRICS
  float3 absorption;
  float3 scatter;
  float3 emission;
  float anisotropy;

#else /* SURFACE */
  float3 radiance;
  float3 transmittance;
  float holdout;
#endif

/* Metal Default Constructor - Required for C++ constructor syntax. */
#ifdef GPU_METAL
  inline Closure() = default;
#  ifdef VOLUMETRICS
  /* Explicit Closure constructors -- To support GLSL syntax */
  inline Closure(float3 in_absorption, float3 in_scatter, float3 in_emission, float in_anisotropy)
      : absorption(in_absorption),
        scatter(in_scatter),
        emission(in_emission),
        anisotropy(in_anisotropy)
  {
  }
#  else
  /* Explicit Closure constructors -- To support GLSL syntax */
  inline Closure(float3 in_radiance, float3 in_transmittance, float in_holdout)
      : radiance(in_radiance), transmittance(in_transmittance), holdout(in_holdout)
  {
  }
#  endif /* VOLUMETRICS */
#endif   /* GPU_METAL */
};

#ifndef GPU_METAL
/* Prototype */
Closure nodetree_exec();
float4 closure_to_rgba(Closure cl);

/* Goo-engine node internals */
void calc_shader_info(float3 position, float3 normal, int4 light_groups, int4 light_group_shadows, out float4 half_light, out float shadows, out float self_shadows, out float4 ambient, out float half_lambert);
void calc_shader_info(float3 position, float3 normal, out float4 half_light, out float shadows, out float self_shadows, out float4 ambient, out float half_lambert);
void screenspace_info(float3 viewPos, out float4 scene_col, out float scene_depth);
void screenspace_curvature(float iiterations, float sample_scale, float clamp_dist, float3 scale, out float scene_curvature, out float scene_rim);

void output_aov(float4 color, float value, uint hash);
float3 coordinate_camera(float3 P);
float3 coordinate_screen(float3 P);
float3 coordinate_reflect(float3 P, float3 N);
float3 coordinate_incoming(float3 P);
float texture_lod_bias_get();

/* Single BSDFs. */
Closure closure_eval(ClosureDiffuse diffuse);
Closure closure_eval(ClosureSubsurface diffuse);
Closure closure_eval(ClosureTranslucent translucent);
Closure closure_eval(ClosureReflection reflection);
Closure closure_eval(ClosureRefraction refraction);
Closure closure_eval(ClosureEmission emission);
Closure closure_eval(ClosureTransparency transparency);
Closure closure_eval(ClosureVolumeScatter volume_scatter);
Closure closure_eval(ClosureVolumeAbsorption volume_absorption);
Closure closure_eval(ClosureHair hair);

/* Glass BSDF. */
Closure closure_eval(ClosureReflection reflection, ClosureRefraction refraction);
/* Dielectric BSDF. */
Closure closure_eval(ClosureSubsurface diffuse, ClosureReflection reflection);
/* Coat BSDF. */
Closure closure_eval(ClosureReflection reflection, ClosureReflection coat);
/* Volume BSDF. */
Closure closure_eval(ClosureVolumeScatter volume_scatter,
                     ClosureVolumeAbsorption volume_absorption,
                     ClosureEmission emission);
/* Specular BSDF. */
Closure closure_eval(ClosureSubsurface diffuse,
                     ClosureReflection reflection,
                     ClosureReflection coat);
/* Principled BSDF. */
Closure closure_eval(ClosureSubsurface diffuse,
                     ClosureReflection reflection,
                     ClosureReflection coat,
                     ClosureRefraction refraction);

Closure closure_add(inout Closure cl1, inout Closure cl2);
Closure closure_mix(inout Closure cl1, inout Closure cl2, float fac);

float ambient_occlusion_eval(float3 normal,
                             float distance,
                             const float inverted,
                             const float sample_count);

/* WORKAROUND: Included later with libraries. This is because we are mixing include systems. */
float3 safe_normalize(float3 N);
float fast_sqrt(float a);
float3 cameraVec(float3 P);
float2 bsdf_lut(float a, float b, float c, bool d);
void bsdf_lut(float3 F0,
              float3 F90,
              float3 transmission_tint,
              float cos_theta,
              float roughness,
              float ior,
              bool do_multiscatter,
              out float3 reflectance,
              out float3 transmittance);
float2 brdf_lut(float a, float b);
void brdf_f82_tint_lut(float3 F0,
                       float3 F82,
                       float cos_theta,
                       float roughness,
                       bool do_multiscatter,
                       out float3 reflectance);
float3 F_brdf_multi_scatter(float3 a, float3 b, float2 c);
float3 F_brdf_single_scatter(float3 a, float3 b, float2 c);
float F_eta(float a, float b);
float F0_from_ior(float a);
#endif

#ifdef VOLUMETRICS
#  define CLOSURE_DEFAULT Closure(float3(0), float3(0), float3(0), 0.0f)
#else
#  define CLOSURE_DEFAULT Closure(float3(0), float3(0), 0.0f)
#endif
