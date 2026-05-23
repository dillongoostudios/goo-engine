/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw
 */
#ifndef EEVEE_SHADER_SHARED_H
#define EEVEE_SHADER_SHARED_H
#ifndef GPU_SHADER
typedef struct CommonUniformBlock CommonUniformBlock;
#endif

#ifdef GPU_SHADER
/* Catch for non-create info case. */
#  ifndef BLI_STATIC_ASSERT_ALIGN
#    define BLI_STATIC_ASSERT_ALIGN(type, alignment)
#  endif
#endif

/* NOTE: AMD-based macOS platforms experience performance and correctness issues with EEVEE
 * material closure evaluation. Using singular closure evaluation, rather than the compound
 * function calls reduces register overflow, by limiting the simultaneous number of live
 * registers used by the virtual GPU function stack. */
#if (defined(GPU_METAL) && defined(GPU_ATI))
#  define DO_SPLIT_CLOSURE_EVAL 1
#endif

struct CommonUniformBlock {
  float4x4 _pastViewProjectionMatrix;
  float4 _hizUvScale; /* To correct mip level texel misalignment */
  /* Ambient Occlusion */
  float4 _aoParameters[2];
  /* Volumetric */
  int4 _volTexSize;
  float4 _volDepthParameters; /* Parameters to the volume Z equation */
  float4 _volInvTexSize;
  float4 _volJitter;
  float4 _volCoordScale; /* To convert volume uvs to screen uvs */
  float _volHistoryAlpha;
  float _volShadowSteps;
  bool32_t _volUseLights;
  bool32_t _volUseSoftShadows;
  /* Screen Space Reflections */
  float4 _ssrParameters;
  float _ssrBorderFac;
  float _ssrMaxRoughness;
  float _ssrFireflyFac;
  float _ssrBrdfBias;
  bool32_t _ssrToggle;
  bool32_t _ssrefractToggle;
  /* SubSurface Scattering */
  float _sssJitterThreshold;
  bool32_t _sssToggle;
  /* Specular */
  bool32_t _specToggle;
  /* Lights */
  int _laNumLight;
  /* Probes */
  int _prbNumPlanar;
  int _prbNumRenderCube;
  int _prbNumRenderGrid;
  int _prbIrradianceVisSize;
  float _prbIrradianceSmooth;
  float _prbLodCubeMax;
  /* Misc */
  int _rayType;
  float _rayDepth;
  float _alphaHashOffset;
  float _alphaHashScale;
  float4 _cameraUvScaleBias;
  float4 _planarClipPlane;
};
BLI_STATIC_ASSERT_ALIGN(CommonUniformBlock, 16)

struct CubeData {
  float4 position_type;
  float4 attenuation_fac_type;
  float4x4 influencemat;
  float4x4 parallaxmat;
};
BLI_STATIC_ASSERT_ALIGN(CubeData, 16)

struct PlanarData {
  float4 plane_equation;
  float4 clip_vec_x_fade_scale;
  float4 clip_vec_y_fade_bias;
  float4 clip_edges;
  float4 facing_scale_bias;
  float4x4 reflectionmat; /* transform world space into reflection texture space */
  float4x4 unused;
};
BLI_STATIC_ASSERT_ALIGN(PlanarData, 16)

struct GridData {
  float4x4 localmat;
  int4 resolution_offset;
  float4 ws_corner_atten_scale;     /* world space corner position */
  float4 ws_increment_x_atten_bias; /* world space vector between 2 opposite cells */
  float4 ws_increment_y_lvl_bias;
  float4 ws_increment_z;
  float4 vis_bias_bleed_range;
};
BLI_STATIC_ASSERT_ALIGN(GridData, 16)

struct ProbeBlock {
  CubeData _probes_data[MAX_PROBE];
};
BLI_STATIC_ASSERT_ALIGN(ProbeBlock, 16)

struct GridBlock {
  GridData _grids_data[MAX_GRID];
};
BLI_STATIC_ASSERT_ALIGN(GridBlock, 16)

struct PlanarBlock {
  PlanarData _planars_data[MAX_PLANAR];
};
BLI_STATIC_ASSERT_ALIGN(PlanarBlock, 16)

#ifndef MAX_CASCADE_NUM
#  define MAX_CASCADE_NUM 4
#endif

struct ShadowData {
  float4 near_far_bias_id;
  float4 contact_shadow_data;
};
BLI_STATIC_ASSERT_ALIGN(ShadowData, 16)

struct ShadowCubeData {
  float4x4 shadowmat;
  float4 position;
};
BLI_STATIC_ASSERT_ALIGN(ShadowCubeData, 16)

struct ShadowCascadeData {
  float4x4 shadowmat[MAX_CASCADE_NUM];
  float4 split_start_distances;
  float4 split_end_distances;
  float4 shadow_vec_id;
};
BLI_STATIC_ASSERT_ALIGN(ShadowCascadeData, 16)

struct ShadowBlock {
  ShadowData _shadows_data[MAX_SHADOW];
  ShadowCubeData _shadows_cube_data[MAX_SHADOW_CUBE];
  ShadowCascadeData _shadows_cascade_data[MAX_SHADOW_CASCADE];
};
BLI_STATIC_ASSERT_ALIGN(ShadowBlock, 16)

struct LightData {
  float4 position_influence;     /* W:   InfluenceRadius (inversed and squared) */
  float4 color_influence_volume; /* W:   InfluenceRadius but for Volume power */
  float4 spotdata_radius_shadow; /* X:   spot size, y : spot blend, z : radius, w: shadow id */
  float4 rightvec_sizex;         /* XYZ: Normalized up vector, w: area size X or spot scale X */
  float4 upvec_sizey;            /* XYZ: Normalized right vector, w: area size Y or spot scale Y */
  float4 forwardvec_type;        /* XYZ: Normalized forward vector, w: Light Type */
  float4 diff_spec_volume;       /* XYZ: Diffuse/Spec/Volume power, w: radius for volumetric. */
  int4 light_group_bits;      /* xyzw: Light groups bitfield (Goo-engine) */
};
BLI_STATIC_ASSERT_ALIGN(LightData, 16)

struct LightBlock {
  LightData _lights_data[MAX_LIGHT];
};
BLI_STATIC_ASSERT_ALIGN(LightBlock, 16)

struct RenderpassBlock {
  bool32_t _renderPassDiffuse;
  bool32_t _renderPassDiffuseLight;
  bool32_t _renderPassGlossy;
  bool32_t _renderPassGlossyLight;
  bool32_t _renderPassEmit;
  bool32_t _renderPassSSSColor;
  bool32_t _renderPassEnvironment;
  bool32_t _renderPassAOV;
  uint _renderPassAOVActive;
};
BLI_STATIC_ASSERT_ALIGN(RenderpassBlock, 16)

#define MAX_SSS_SAMPLES 65
#define SSS_LUT_SIZE 64.0
#define SSS_LUT_SCALE ((SSS_LUT_SIZE - 1.0) / float(SSS_LUT_SIZE))
#define SSS_LUT_BIAS (0.5 / float(SSS_LUT_SIZE))

struct SSSProfileBlock {
  float4 _sss_kernel[MAX_SSS_SAMPLES];
  float4 _radii_max_radius;
  float _avg_inv_radius;
  int _sss_samples;
};
BLI_STATIC_ASSERT_ALIGN(SSSProfileBlock, 16)

#ifdef GPU_SHADER

#  if defined(USE_GPU_SHADER_CREATE_INFO)

/* Keep compatibility_with old global scope syntax. */
#    define pastViewProjectionMatrix common_block._pastViewProjectionMatrix
#    define hizUvScale common_block._hizUvScale
#    define aoParameters common_block._aoParameters
#    define volTexSize common_block._volTexSize
#    define volDepthParameters common_block._volDepthParameters
#    define volInvTexSize common_block._volInvTexSize
#    define volJitter common_block._volJitter
#    define volCoordScale common_block._volCoordScale
#    define volHistoryAlpha common_block._volHistoryAlpha
#    define volShadowSteps common_block._volShadowSteps
#    define volUseLights common_block._volUseLights
#    define volUseSoftShadows common_block._volUseSoftShadows
#    define ssrParameters common_block._ssrParameters
#    define ssrBorderFac common_block._ssrBorderFac
#    define ssrMaxRoughness common_block._ssrMaxRoughness
#    define ssrFireflyFac common_block._ssrFireflyFac
#    define ssrBrdfBias common_block._ssrBrdfBias
#    define ssrToggle common_block._ssrToggle
#    define ssrefractToggle common_block._ssrefractToggle
#    define sssJitterThreshold common_block._sssJitterThreshold
#    define sssToggle common_block._sssToggle
#    define specToggle common_block._specToggle
#    define laNumLight common_block._laNumLight
#    define prbNumPlanar common_block._prbNumPlanar
#    define prbNumRenderCube common_block._prbNumRenderCube
#    define prbNumRenderGrid common_block._prbNumRenderGrid
#    define prbIrradianceVisSize common_block._prbIrradianceVisSize
#    define prbIrradianceSmooth common_block._prbIrradianceSmooth
#    define prbLodCubeMax common_block._prbLodCubeMax
#    define rayType common_block._rayType
#    define rayDepth common_block._rayDepth
#    define alphaHashOffset common_block._alphaHashOffset
#    define alphaHashScale common_block._alphaHashScale
#    define cameraUvScaleBias common_block._cameraUvScaleBias
#    define planarClipPlane common_block._planarClipPlane
#    define lightGroups light_groups_in
#    define lightGroupShadows light_group_shadows_in

/* ProbeBlock */
#    define probes_data probe_block._probes_data

/* GridBlock */
#    define grids_data grid_block._grids_data

/* PlanarBlock */
#    define planars_data planar_block._planars_data

/* ShadowBlock */
#    define shadows_data shadow_block._shadows_data
#    define shadows_cube_data shadow_block._shadows_cube_data
#    define shadows_cascade_data shadow_block._shadows_cascade_data

/* LightBlock */
#    define lights_data light_block._lights_data

/* RenderpassBlock */
#    define renderPassDiffuse renderpass_block._renderPassDiffuse
#    define renderPassDiffuseLight renderpass_block._renderPassDiffuseLight
#    define renderPassGlossy renderpass_block._renderPassGlossy
#    define renderPassGlossyLight renderpass_block._renderPassGlossyLight
#    define renderPassEmit renderpass_block._renderPassEmit
#    define renderPassSSSColor renderpass_block._renderPassSSSColor
#    define renderPassEnvironment renderpass_block._renderPassEnvironment
#    define renderPassAOV renderpass_block._renderPassAOV
#    define renderPassAOVActive renderpass_block._renderPassAOVActive

/* SSSProfileBlock */
#    define sss_kernel sssProfile._sss_kernel
#    define radii_max_radius sssProfile._radii_max_radius
#    define avg_inv_radius sssProfile._avg_inv_radius
#    define sss_samples sssProfile._sss_samples

#  endif /* USE_GPU_SHADER_CREATE_INFO */

#endif
#endif
