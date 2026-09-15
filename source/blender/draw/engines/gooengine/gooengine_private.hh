/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup DNA
 */

#pragma once

#include "DRW_render.hh"
#include "draw_handle.hh"
#include "draw_view_data.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"

#include "gooengine_shadermod.hh"

#include "BLI_bitmap.h"

#include "DNA_lightprobe_types.h"

#include "GPU_viewport.hh"

#include "BKE_camera.h"

#include "engine_eevee_shared_defines.hh"

struct BLI_memblock;
struct CryptomatteSession;
struct DrawEngine;
struct EEVEE_EffectsInfo;
struct EEVEE_PrivateData;
struct EEVEE_ShadowCasterBuffer;
struct GPUFrameBuffer;
struct Object;
struct RenderLayer;

using blender::draw::Framebuffer;
using blender::draw::PassSimple;
using blender::draw::ResourceHandle;
using blender::draw::Texture;
using blender::draw::View;

/* Special value chosen to not be altered by depth of field sample count. */
#define TAA_MAX_SAMPLE 10000926

// #define DEBUG_SHADOW_DISTRIBUTION

/* Only define one of these. */
// #define IRRADIANCE_SH_L2
#define IRRADIANCE_HL2

#if defined(IRRADIANCE_SH_L2)
#  define SHADER_IRRADIANCE "#define IRRADIANCE_SH_L2\n"
#elif defined(IRRADIANCE_HL2)
#  define SHADER_IRRADIANCE "#define IRRADIANCE_HL2\n"
#endif

#define EEVEE_PROBE_MAX min_ii(MAX_PROBE, GPU_max_texture_layers() / 6)
#define USE_VOLUME_OPTI true

#define SWAP_DOUBLE_BUFFERS() \
  { \
    if (effects->swap_double_buffer) { \
      std::swap(inst->goo_data->main_fb, inst->goo_data->double_buffer_fb); \
      std::swap(inst->goo_data->main_color_fb, inst->goo_data->double_buffer_color_fb); \
      std::swap(inst->goo_data->color, inst->goo_data->color_double_buffer); \
      effects->swap_double_buffer = false; \
    } \
  } \
  ((void)0)

#define SWAP_BUFFERS() \
  { \
    if (effects->target_buffer == inst->goo_data->effect_color_fb) { \
      SWAP_DOUBLE_BUFFERS(); \
      effects->source_buffer = inst->goo_data->color_post; \
      effects->target_buffer = inst->goo_data->main_color_fb; \
    } \
    else { \
      SWAP_DOUBLE_BUFFERS(); \
      effects->source_buffer = inst->goo_data->color; \
      effects->target_buffer = inst->goo_data->effect_color_fb; \
    } \
  } \
  ((void)0)

#define SWAP_BUFFERS_TAA() \
  { \
    if (effects->target_buffer == inst->goo_data->effect_color_fb) { \
      std::swap(inst->goo_data->effect_fb, inst->goo_data->taa_history_fb); \
      std::swap(inst->goo_data->effect_color_fb, inst->goo_data->taa_history_color_fb); \
      std::swap(inst->goo_data->color_post, inst->goo_data->taa_history); \
      effects->source_buffer = inst->goo_data->taa_history; \
      effects->target_buffer = inst->goo_data->effect_color_fb; \
    } \
    else { \
      std::swap(inst->goo_data->main_fb, inst->goo_data->taa_history_fb); \
      std::swap(inst->goo_data->main_color_fb, inst->goo_data->taa_history_color_fb); \
      std::swap(inst->goo_data->color, inst->goo_data->taa_history); \
      effects->source_buffer = inst->goo_data->taa_history; \
      effects->target_buffer = inst->goo_data->main_color_fb; \
    } \
  } \
  ((void)0)

BLI_INLINE bool eevee_hdri_preview_overlay_enabled(const View3D *v3d)
{
  /* Only show the HDRI Preview in Shading Preview in the Viewport. */
  if (v3d == nullptr || v3d->shading.type != OB_MATERIAL) {
    return false;
  }

  /* Only show the HDRI Preview when viewing the Combined render pass */
  if (v3d->shading.render_pass != SCE_PASS_COMBINED) {
    return false;
  }

  return ((v3d->flag2 & V3D_HIDE_OVERLAYS) == 0) && (v3d->overlay.flag & V3D_OVERLAY_LOOK_DEV);
}

#define USE_SCENE_LIGHT(v3d) \
  ((!v3d) || \
   ((v3d->shading.type == OB_MATERIAL) && (v3d->shading.flag & V3D_SHADING_SCENE_LIGHTS)) || \
   ((v3d->shading.type == OB_RENDER) && (v3d->shading.flag & V3D_SHADING_SCENE_LIGHTS_RENDER)))
#define LOOK_DEV_STUDIO_LIGHT_ENABLED(v3d) \
  ((v3d) && (((v3d->shading.type == OB_MATERIAL) && \
              ((v3d->shading.flag & V3D_SHADING_SCENE_WORLD) == 0)) || \
             ((v3d->shading.type == OB_RENDER) && \
              ((v3d->shading.flag & V3D_SHADING_SCENE_WORLD_RENDER) == 0))))

#define MIN_CUBE_LOD_LEVEL 3
#define MAX_SCREEN_BUFFERS_LOD_LEVEL 6

/* All the renderpasses that use the GPUMaterial for accumulation */
#define EEVEE_RENDERPASSES_MATERIAL \
  (EEVEE_RENDER_PASS_EMIT | EEVEE_RENDER_PASS_DIFFUSE_COLOR | EEVEE_RENDER_PASS_DIFFUSE_LIGHT | \
   EEVEE_RENDER_PASS_SPECULAR_COLOR | EEVEE_RENDER_PASS_SPECULAR_LIGHT | \
   EEVEE_RENDER_PASS_ENVIRONMENT | EEVEE_RENDER_PASS_AOV)
#define EEVEE_AOV_HASH_ALL -1
#define EEVEE_AOV_HASH_COLOR_TYPE_MASK 1
#define MAX_CRYPTOMATTE_LAYERS 3

namespace blender::gooengine {

/* Material shader variations */
enum {
  VAR_MAT_MESH = (1 << 0),
  VAR_MAT_VOLUME = (1 << 1),
  VAR_MAT_HAIR = (1 << 2),
  VAR_MAT_POINTCLOUD = (1 << 3),
  VAR_MAT_BLEND = (1 << 4),
  VAR_MAT_LOOKDEV = (1 << 5),
  VAR_MAT_HOLDOUT = (1 << 6),
  VAR_MAT_HASH = (1 << 7),
  VAR_MAT_DEPTH = (1 << 8),
  VAR_MAT_REFRACT = (1 << 9),
  VAR_WORLD_BACKGROUND = (1 << 10),
  VAR_WORLD_PROBE = (1 << 11),
  VAR_WORLD_VOLUME = (1 << 12),
  VAR_MAT_SHADOW_ID = (1 << 13),
  VAR_DEFAULT = (1 << 14),
};

/* Material shader cache keys */
enum {
  /* HACK: This assumes the struct GPUShader will never be smaller than our variations.
   * This allow us to only keep one #GHash and avoid bigger keys comparisons/hashing.
   * We combine the #GPUShader pointer with the key. */
  KEY_CULL = (1 << 0),
  KEY_REFRACT = (1 << 1),
  KEY_HAIR = (1 << 2),
  KEY_SHADOW = (1 << 3),
};

/* DOF Gather pass shader variations */
typedef enum EEVEE_DofGatherPass {
  DOF_GATHER_FOREGROUND = 0,
  DOF_GATHER_BACKGROUND = 1,
  DOF_GATHER_HOLEFILL = 2,

  DOF_GATHER_MAX_PASS,
} EEVEE_DofGatherPass;

/* ************ PROBE UBO ************* */

/* They are the same struct as their Cache siblings.
 * typedef'ing just to keep the naming consistent with
 * other eevee types. */
typedef LightProbeCache EEVEE_LightProbe;
typedef LightGridCache EEVEE_LightGrid;

struct EEVEE_PlanarReflection {
  float plane_equation[4];
  float clip_vec_x[3], attenuation_scale;
  float clip_vec_y[3], attenuation_bias;
  float clip_edge_x_pos, clip_edge_x_neg;
  float clip_edge_y_pos, clip_edge_y_neg;
  float facing_scale, facing_bias, clipsta, pad;
  float reflectionmat[4][4]; /* Used for sampling the texture. */
  float mtx[4][4];           /* Not used in shader. TODO: move elsewhere. */
};

/* --------------------------------------- */

struct EEVEE_BoundBox {
  float center[3], halfdim[3];
};

/* ************ RENDERPASS UBO ************* */
struct EEVEE_RenderPassData {
  int renderPassDiffuse;
  int renderPassDiffuseLight;
  int renderPassGlossy;
  int renderPassGlossyLight;
  int renderPassEmit;
  int renderPassSSSColor;
  int renderPassEnvironment;
  int renderPassAOV;
  uint renderPassAOVActive;
  int _pad[3];
};

/* ************ LIGHT UBO ************* */
struct EEVEE_Light {
  float position[3], invsqrdist;
  float color[3], invsqrdist_volume;
  float spotsize, spotblend, radius, shadow_id;
  float rightvec[3], sizex;
  float upvec[3], sizey;
  float forwardvec[3], light_type;
  float diff, spec, volume, volume_radius;
  int light_group_bits[4];
};

/* Special type for elliptic area lights and point/spot disk lights, matches lights_lib.glsl */
#define LAMPTYPE_OMNI_DISK 0.5f
#define LAMPTYPE_SPOT_DISK 2.5f
#define LAMPTYPE_AREA_ELLIPSE 100.0f

struct EEVEE_Shadow {
  float near, far, bias, type_data_id;
  float contact_dist, contact_bias, contact_spread, contact_thickness;
};

struct EEVEE_ShadowCube {
  float shadowmat[4][4];
  float position[3], _pad0[1];
};

struct EEVEE_ShadowCascade {
  /* World->Light->NDC->Tex : used for sampling the shadow map. */
  float shadowmat[MAX_CASCADE_NUM][4][4];
  float split_start[4];
  float split_end[4];
  float shadow_vec[3], tex_id;
};

struct EEVEE_ShadowCascadeRender {
  /* World->Light->NDC : used for rendering the shadow map. */
  float projmat[MAX_CASCADE_NUM][4][4];
  float viewmat[4][4], viewinv[4][4];
  float radius[MAX_CASCADE_NUM];
  float original_bias;
  float cascade_max_dist;
  float cascade_exponent;
  float cascade_fade;
  int cascade_count;
};

BLI_STATIC_ASSERT_ALIGN(EEVEE_Light, 16)
BLI_STATIC_ASSERT_ALIGN(EEVEE_Shadow, 16)
BLI_STATIC_ASSERT_ALIGN(EEVEE_ShadowCube, 16)
BLI_STATIC_ASSERT_ALIGN(EEVEE_ShadowCascade, 16)
BLI_STATIC_ASSERT_ALIGN(EEVEE_RenderPassData, 16)

BLI_STATIC_ASSERT(sizeof(EEVEE_Shadow) * MAX_SHADOW +
                          sizeof(EEVEE_ShadowCascade) * MAX_SHADOW_CASCADE +
                          sizeof(EEVEE_ShadowCube) * MAX_SHADOW_CUBE <
                      16384,
                  "Shadow UBO is too big!!!")

struct EEVEE_ShadowCasterBuffer {
  EEVEE_BoundBox *bbox;
  BLI_bitmap *update;
  uint alloc_count;
  uint count;
};

/* ************ LIGHT DATA ************* */
struct EEVEE_LightsInfo {
  int num_light, cache_num_light;
  int num_cube_layer, cache_num_cube_layer;
  int num_cascade_layer, cache_num_cascade_layer;
  int cube_len, cascade_len, shadow_len;
  int shadow_cube_size, shadow_cascade_size;
  bool shadow_high_bitdepth, shadow_id_high_bitdepth, soft_shadows;
  /* UBO Storage : data used by UBO */
  EEVEE_Light light_data[MAX_LIGHT];
  EEVEE_Shadow shadow_data[MAX_SHADOW];
  EEVEE_ShadowCube shadow_cube_data[MAX_SHADOW_CUBE];
  EEVEE_ShadowCascade shadow_cascade_data[MAX_SHADOW_CASCADE];
  /* Additional rendering info for cascade. */
  EEVEE_ShadowCascadeRender shadow_cascade_render[MAX_SHADOW_CASCADE];
  /* Back index in light_data. */
  uchar shadow_cube_light_indices[MAX_SHADOW_CUBE];
  uchar shadow_cascade_light_indices[MAX_SHADOW_CASCADE];
  /* Update bitmap. */
  BLI_bitmap sh_cube_update[BLI_BITMAP_SIZE(MAX_SHADOW_CUBE)];
  /* Lights tracking */
  BoundSphere shadow_bounds[MAX_LIGHT]; /* Tightly packed light bounds. */
                                        /* List of bbox and update bitmap. Double buffered. */
  EEVEE_ShadowCasterBuffer *shcaster_frontbuffer, *shcaster_backbuffer;
  /* AABB of all shadow casters combined. */
  struct {
    float min[3], max[3];
  } shcaster_aabb;
};

/* ************ PROBE DATA ************* */
struct EEVEE_LightProbeVisTest {
  Collection *collection; /* Skip test if nullptr */
  bool invert;
  bool cached; /* Reuse last test results */
};

struct EEVEE_LightProbesInfo {
  int num_cube, cache_num_cube;
  int num_grid, cache_num_grid;
  int num_planar, cache_num_planar;
  int total_irradiance_samples; /* Total for all grids */
  int cache_irradiance_size[3];
  int update_flag;
  int updated_bounce;
  int num_bounce;
  int cubemap_res;
  /* Update */
  bool do_cube_update;
  bool do_grid_update;
  /* For rendering probes */
  float probemat[6][4][4];
  int layer;
  float texel_size;
  float padding_size;
  float samples_len;
  float near_clip;
  float far_clip;
  float roughness;
  float firefly_fac;
  float lodfactor;
  float lod_rt_max, lod_cube_max;
  float visibility_range;
  float visibility_blur;
  float intensity_fac;
  int shres;
  EEVEE_LightProbeVisTest planar_vis_tests[MAX_PLANAR];
  /* UBO Storage : data used by UBO */
  EEVEE_LightProbe probe_data[MAX_PROBE];
  EEVEE_LightGrid grid_data[MAX_GRID];
  EEVEE_PlanarReflection planar_data[MAX_PLANAR];
  /* Probe Visibility Collection */
  EEVEE_LightProbeVisTest vis_data;
};

/* EEVEE_LightProbesInfo->update_flag */
enum {
  PROBE_UPDATE_CUBE = (1 << 0),
  PROBE_UPDATE_GRID = (1 << 1),
  PROBE_UPDATE_ALL = 0xFFFFFF,
};

/* ************** MOTION BLUR ************ */

#define MB_PREV 0
#define MB_NEXT 1
#define MB_CURR 2

/* ************ EFFECTS DATA ************* */

enum EEVEE_EffectsFlag {
  EFFECT_MOTION_BLUR = (1 << 0),
  EFFECT_BLOOM = (1 << 1),
  EFFECT_DOF = (1 << 2),
  EFFECT_VOLUMETRIC = (1 << 3),
  EFFECT_SSR = (1 << 4),
  EFFECT_DOUBLE_BUFFER = (1 << 5), /* Not really an effect but a feature */
  EFFECT_REFRACT = (1 << 6),
  EFFECT_GTAO = (1 << 7),
  EFFECT_TAA = (1 << 8),
  EFFECT_POST_BUFFER = (1 << 9),      /* Not really an effect but a feature */
  EFFECT_NORMAL_BUFFER = (1 << 10),   /* Not really an effect but a feature */
  EFFECT_RADIANCE_BUFFER = (1 << 11), /* Not really an effect but a feature */
  EFFECT_SSS = (1 << 12),
  EFFECT_VELOCITY_BUFFER = (1 << 13),     /* Not really an effect but a feature */
  EFFECT_TAA_REPROJECT = (1 << 14),       /* should be mutually exclusive with EFFECT_TAA */
  EFFECT_DEPTH_DOUBLE_BUFFER = (1 << 15), /* Not really an effect but a feature */
};
ENUM_OPERATORS(EEVEE_EffectsFlag, EFFECT_DEPTH_DOUBLE_BUFFER)

struct EEVEE_EffectsInfo {
  EEVEE_EffectsFlag enabled_effects;
  bool swap_double_buffer;
  /* SSSS */
  int sss_sample_count;
  int sss_surface_count;
  GPUTexture *sss_irradiance; /* Textures from pool */
  GPUTexture *sss_radius;
  GPUTexture *sss_albedo;
  GPUTexture *sss_blur;
  GPUTexture *sss_stencil;
  /* Volumetrics */
  int volume_current_sample;
  float volume_light_clamp;
  GPUTexture *volume_scatter;
  GPUTexture *volume_transmit;
  /* SSR */
  bool reflection_trace_full;
  bool ssr_was_persp;
  bool ssr_was_valid_double_buffer;
  GPUTexture *ssr_normal_input; /* Textures from pool */
  GPUTexture *ssr_specrough_input;
  GPUTexture *ssr_hit_output;
  GPUTexture *ssr_hit_depth;
  /* Intel devices require a split execution due to shader issue */
  bool use_split_ssr_pass;

  /* Temporal Anti Aliasing */
  int taa_reproject_sample;
  int taa_current_sample;
  int taa_render_sample;
  int taa_total_sample;
  float taa_alpha;
  bool bypass_drawing;
  bool prev_drw_support;
  bool prev_is_navigating;
  float prev_drw_persmat[4][4]; /* Used for checking view validity and reprojection. */
  DRWView *taa_view;
  /* Ambient Occlusion */
  GPUTexture *gtao_horizons;            /* Textures from pool */
  GPUTexture *gtao_horizons_renderpass; /* Texture when rendering render pass */
  GPUTexture *gtao_horizons_debug;
  /* Motion Blur */
  float current_ndc_to_world[4][4];
  float current_world_to_ndc[4][4];
  float current_world_to_view[4][4];
  float past_world_to_ndc[4][4];
  float past_world_to_view[4][4];
  CameraParams past_cam_params;
  CameraParams current_cam_params;
  char motion_blur_step;         /* Which step we are evaluating. */
  int motion_blur_max;           /* Maximum distance in pixels a motion-blurred pixel can cover. */
  float motion_blur_near_far[2]; /* Camera near/far clip distances (positive). */
  bool cam_params_init;
  /* Depth Of Field */
  float dof_jitter_radius;
  float dof_jitter_blades;
  float dof_jitter_focus;
  int dof_jitter_ring_count;
  float4 dof_coc_params;
  float2 dof_bokeh_aniso;
  float dof_bokeh_blades;
  float dof_bokeh_rotation;
  float dof_bokeh_max_size;
  float2 dof_bokeh_aniso_inv;
  float dof_scatter_color_threshold;
  float dof_scatter_coc_threshold;
  float dof_scatter_neighbor_max_color;
  float dof_fx_max_coc;
  float dof_denoise_factor;
  int dof_dilate_slight_focus;
  int dof_dilate_ring_count;
  int dof_dilate_ring_width_multiplier;
  int dof_reduce_steps;
  bool dof_hq_slight_focus;
  eGPUTextureFormat dof_color_format;
  GPUTexture *dof_bg_color_tx; /* All textures from pool... */
  GPUTexture *dof_bg_occlusion_tx;
  GPUTexture *dof_bg_weight_tx;
  GPUTexture *dof_bokeh_gather_lut_tx;
  GPUTexture *dof_bokeh_scatter_lut_tx;
  GPUTexture *dof_bokeh_resolve_lut_tx;
  GPUTexture *dof_coc_dilated_tiles_bg_tx;
  GPUTexture *dof_coc_dilated_tiles_fg_tx;
  GPUTexture *dof_coc_tiles_bg_tx;
  GPUTexture *dof_coc_tiles_fg_tx;
  GPUTexture *dof_downsample_tx;
  GPUTexture *dof_fg_color_tx;
  GPUTexture *dof_fg_occlusion_tx;
  GPUTexture *dof_fg_weight_tx;
  GPUTexture *dof_fg_holefill_color_tx;
  GPUTexture *dof_fg_holefill_weight_tx;
  GPUTexture *dof_half_res_coc_tx;
  GPUTexture *dof_half_res_color_tx;
  GPUTexture *dof_scatter_src_tx;
  GPUTexture *dof_reduce_input_coc_tx; /* Just references to actual textures. */
  GPUTexture *dof_reduce_input_color_tx;
  /* Other */
  float prev_persmat[4][4];
  /* Size used by all full-screen buffers using mipmaps. */
  int hiz_size[2];
  /* Lookdev */
  int sphere_size;
  eDRWLevelOfDetail sphere_lod;
  int anchor[2];
  /* Bloom */
  int bloom_iteration_len;
  float2 source_texel_size;
  float2 blit_texel_size;
  float downsamp_texel_size[MAX_BLOOM_STEP][2];
  float3 bloom_color;
  float bloom_clamp;
  float bloom_sample_scale;
  float4 bloom_curve_threshold;
  float2 unf_source_texel_size;
  GPUTexture *bloom_blit; /* Textures from pool */
  GPUTexture *bloom_downsample[MAX_BLOOM_STEP];
  GPUTexture *bloom_upsample[MAX_BLOOM_STEP - 1];
  GPUTexture *unf_source_buffer; /* pointer copy */
  GPUTexture *unf_base_buffer;   /* pointer copy */
  GPUTexture *source_buffer;     /* latest updated texture */
  GPUFrameBuffer *target_buffer; /* next target to render to */
  GPUTexture *final_tx;          /* Final color to transform to display color space. */
  GPUFrameBuffer *final_fb;      /* Frame-buffer with final_tx as attachment. */
};

/* ***************** COMMON DATA **************** */

/* Common uniform buffer containing all "constant" data over the whole drawing pipeline. */
/* !! CAUTION !!
 * - [i]vec3 need to be padded to [i]vec4 (even in ubo declaration).
 * - Make sure that [i]vec4 start at a multiple of 16 bytes.
 * - Arrays of vec2/vec3 are padded as arrays of vec4.
 * - sizeof(bool) == sizeof(int) in GLSL so use int in C */
struct EEVEE_CommonUniformBuffer {
  float prev_persmat[4][4];               /* mat4 */
  float hiz_uv_scale[2], ssr_uv_scale[2]; /* vec4 */
  /* Ambient Occlusion */
  /* -- 16 byte aligned -- */
  float ao_dist, pad1, ao_factor, pad2;                    /* vec4 */
  float ao_offset, ao_bounce_fac, ao_quality, ao_settings; /* vec4 */
  /* Volumetric */
  /* -- 16 byte aligned -- */
  int vol_tex_size[3], pad3;       /* ivec3 */
  float vol_depth_param[3], pad4;  /* vec3 */
  float vol_inv_tex_size[3], pad5; /* vec3 */
  float vol_jitter[3], pad6;       /* vec3 */
  float vol_coord_scale[4];        /* vec4 */
  /* -- 16 byte aligned -- */
  float vol_history_alpha;  /* float */
  float vol_shadow_steps;   /* float */
  int vol_use_lights;       /* bool */
  int vol_use_soft_shadows; /* bool */
  /* Screen Space Reflections */
  /* -- 16 byte aligned -- */
  float ssr_quality, ssr_thickness, ssr_pixelsize[2]; /* vec4 */
  float ssr_border_fac;                               /* float */
  float ssr_max_roughness;                            /* float */
  float ssr_firefly_fac;                              /* float */
  float ssr_brdf_bias;                                /* float */
  int ssr_toggle;                                     /* bool */
  int ssrefract_toggle;                               /* bool */
  /* SubSurface Scattering */
  float sss_jitter_threshold; /* float */
  int sss_toggle;             /* bool */
  /* Specular */
  int spec_toggle; /* bool */
  /* Lights */
  int la_num_light; /* int */
  /* Probes */
  int prb_num_planar;          /* int */
  int prb_num_render_cube;     /* int */
  int prb_num_render_grid;     /* int */
  int prb_irradiance_vis_size; /* int */
  float prb_irradiance_smooth; /* float */
  float prb_lod_cube_max;      /* float */
  /* Misc */
  int ray_type;                                /* int */
  float ray_depth;                             /* float */
  float alpha_hash_offset;                     /* float */
  float alpha_hash_scale;                      /* float */
  float camera_uv_scale[2], camera_uv_bias[2]; /* vec4 */
  float planar_clip_plane[4];                  /* vec4 */
  int shadow_id_high_bitdepth;
  float pad[3];
};

BLI_STATIC_ASSERT_ALIGN(EEVEE_CommonUniformBuffer, 16)

/* ray_type (keep in sync with rayType) */
#define EEVEE_RAY_CAMERA 0
#define EEVEE_RAY_SHADOW 1
#define EEVEE_RAY_DIFFUSE 2
#define EEVEE_RAY_GLOSSY 3

/* ************** SCENE LAYER DATA ************** */
struct EEVEE_ViewLayerData {
  /* Lights */
  EEVEE_LightsInfo *lights;

  GPUUniformBuf *light_ubo;
  GPUUniformBuf *shadow_ubo;
  GPUUniformBuf *shadow_samples_ubo;

  GPUFrameBuffer *shadow_fb;

  GPUTexture *shadow_cube_pool;
  GPUTexture *shadow_cascade_pool;

  GPUTexture *shadow_cube_id_pool;
  GPUTexture *shadow_cascade_id_pool;

  EEVEE_ShadowCasterBuffer shcasters_buffers[2];

  /* Probes */
  EEVEE_LightProbesInfo *probes;

  GPUUniformBuf *probe_ubo;
  GPUUniformBuf *grid_ubo;
  GPUUniformBuf *planar_ubo;

  /* Material Render passes */
  struct {
    GPUUniformBuf *combined;
    GPUUniformBuf *environment;
    GPUUniformBuf *diff_color;
    GPUUniformBuf *diff_light;
    GPUUniformBuf *spec_color;
    GPUUniformBuf *spec_light;
    GPUUniformBuf *emit;
    GPUUniformBuf *aovs[MAX_AOVS];
  } renderpass_ubo;

  /* Common Uniform Buffer */
  EEVEE_CommonUniformBuffer common_data;
  GPUUniformBuf *common_ubo;

  LightCache *fallback_lightcache;

  BLI_memblock *material_cache;
};

/* ************ OBJECT DATA ************ */

/* These are the structs stored inside Objects.
 * It works even if the object is in multiple layers
 * because we don't get the same "Object *" for each layer. */
struct EEVEE_LightEngineData {
  DrawData dd;

  bool need_update;
};

struct EEVEE_LightProbeEngineData {
  DrawData dd;

  bool need_update;
};

struct EEVEE_ObjectEngineData {
  DrawData dd;

  Object *ob; /* self reference */
  EEVEE_LightProbeVisTest *test_data;
  bool ob_vis, ob_vis_dirty;

  bool need_update;
  bool geom_update;
  uint shadow_caster_id;
};

struct EEVEE_WorldEngineData {
  DrawData dd;
};

struct EEVEE_CryptomatteSample {
  float hash;
  float weight;
};

/* *********************************** */

struct EEVEE_PrivateData {
  DRWShadingGroup *shadow_shgrp;
  DRWShadingGroup *shadow_accum_shgrp;
  DRWCallBuffer *planar_display_shgrp;
  GHash *material_hash;
  float background_alpha; /* TODO: find a better place for this. */
  bool disable_ligthprobes;
  /** Chosen light-cache: can come from Lookdev or the view-layer. */
  LightCache *light_cache;
  /* For planar probes */
  float planar_texel_size[2];
  /* For double buffering */
  bool view_updated;
  uint64_t depsgraph_last_update_ = 0;
  bool valid_double_buffer;
  bool valid_taa_history;
  /* Render Matrices */
  float studiolight_matrix[3][3];
  float overscan, overscan_pixels;
  float camtexcofac[4];
  float size_orig[2];

  /* Cached original camera when rendering for motion blur (see #79637). */
  Object *cam_original_ob;

  /* Mist Settings */
  float3 mist_data;

  /* Color Management */
  bool use_color_render_settings;

  /* Compiling shaders count. This is to track if a shader has finished compiling. */
  int queued_shaders_count;
  int queued_shaders_count_prev;
  /* Optimizing shaders count. */
  int queued_optimise_shaders_count;

  /* LookDev Settings */
  int studiolight_index;
  float studiolight_rot_z;
  float studiolight_intensity;
  int studiolight_cubemap_res;
  float studiolight_glossy_clamp;
  float studiolight_filter_quality;

  /* Render-passes */
  /* Bitmask containing the active render_passes */
  eViewLayerEEVEEPassType render_passes;
  uint aov_hash;
  int num_aovs_used;
  CryptomatteSession *cryptomatte_session;
  bool cryptomatte_accurate_mode;
  EEVEE_CryptomatteSample *cryptomatte_accum_buffer;
  float *cryptomatte_download_buffer;

  /* Uniform references that are referenced inside the `renderpass_pass`. They are updated
   * to reuse the drawing pass and the shading group. */
  int renderpass_type;
  int renderpass_postprocess;
  int renderpass_current_sample;
  GPUTexture *renderpass_input;
  GPUTexture *renderpass_col_input;
  GPUTexture *renderpass_light_input;
  GPUTexture *renderpass_transmittance_input;
  /* Render-pass UBO reference used by material pass. */
  GPUUniformBuf *renderpass_ubo;
  /** For rendering shadows. */
  DRWView *cube_views[6];
  /** For rendering probes. */
  DRWView *bake_views[6];
  /** Same as bake_views but does not generate culling infos. */
  DRWView *world_views[6];
  /** For rendering planar reflections. */
  DRWView *planar_views[MAX_PLANAR];

  int render_timesteps;
  int render_sample_count_per_timestep;
}; /* Transient data */

struct GOOENGINE_Data {

  /* Shadows */
  DRWPass *shadow_pass;
  DRWPass *shadow_accum_pass;

  /* Probes */
  DRWPass *probe_background;
  DRWPass *probe_glossy_compute;
  DRWPass *probe_diffuse_compute;
  DRWPass *probe_visibility_compute;
  DRWPass *probe_grid_fill;
  DRWPass *probe_display;
  DRWPass *probe_planar_downsample_ps;

  /* Effects */
  DRWPass *ao_horizon_search;
  DRWPass *ao_horizon_debug;
  DRWPass *ao_accum_ps;
  PassSimple mist_accum_ps = {"mist_accum_ps"};
  PassSimple bloom_blit = {"bloom_blit"};
  PassSimple bloom_downsample_first = {"bloom_downsample_first"};
  PassSimple bloom_downsample = {"bloom_downsample"};
  PassSimple bloom_upsample = {"bloom_upsample"};
  PassSimple bloom_resolve = {"bloom_resolve"};
  PassSimple bloom_accum_ps = {"bloom_accum_ps"};
  PassSimple dof_setup = {"dof_setup"};
  PassSimple dof_flatten_tiles = {"dof_flatten_tiles"};
  PassSimple dof_dilate_tiles_minmax = {"dof_dilate_tiles_minmax"};
  PassSimple dof_dilate_tiles_minabs = {"dof_dilate_tiles_minabs"};
  PassSimple dof_reduce_copy = {"dof_reduce_copy"};
  PassSimple dof_downsample = {"dof_downsample"};
  PassSimple dof_reduce = {"dof_reduce"};
  PassSimple dof_bokeh = {"dof_bokeh"};
  PassSimple dof_gather_fg = {"dof_gather_fg"};
  PassSimple dof_gather_fg_holefill = {"dof_gather_fg_holefill"};
  PassSimple dof_gather_bg = {"dof_gather_bg"};
  PassSimple dof_scatter_fg = {"dof_scatter_fg"};
  PassSimple dof_scatter_bg = {"dof_scatter_bg"};
  PassSimple dof_filter = {"dof_filter"};
  PassSimple dof_resolve = {"dof_resolve"};
  PassSimple ssr_raytrace = {"ssr_raytrace"};
  PassSimple ssr_resolve = {"ssr_resolve"};
  PassSimple ssr_resolve_probe = {"ssr_resolve_probe"};
  PassSimple ssr_resolve_refl = {"ssr_resolve_refl"};
  PassSimple sss_blur_ps = {"sss_blur_ps"};
  PassSimple sss_resolve_ps = {"sss_resolve_ps"};
  PassSimple sss_translucency_ps = {"sss_translucency_ps"};
  PassSimple taa_resolve = {"taa_resolve"};
  PassSimple color_copy_ps = {"color_copy_ps"};
  PassSimple color_downsample_ps = {"color_downsample_ps"};
  PassSimple color_downsample_cube_ps = {"color_downsample_cube_ps"};

  /* HiZ */
  PassSimple maxz_downlevel_ps = {"maxz_downlevel_ps"};
  PassSimple maxz_copydepth_ps = {"maxz_copydepth_ps"};
  PassSimple maxz_copydepth_layer_ps = {"maxz_copydepth_layer_ps"};

  /* Render-pass Accumulation. */
  DRWPass *material_accum_ps;
  DRWPass *background_accum_ps;
  DRWPass *transparent_accum_ps;
  DRWPass *cryptomatte_ps;

  DRWPass *depth_ps;
  DRWPass *depth_cull_ps;
  DRWPass *depth_clip_ps;
  DRWPass *depth_clip_cull_ps;
  DRWPass *depth_refract_ps;
  DRWPass *depth_refract_cull_ps;
  DRWPass *depth_refract_clip_ps;
  DRWPass *depth_refract_clip_cull_ps;
  DRWPass *material_ps;
  DRWPass *material_cull_ps;
  DRWPass *material_refract_ps;
  DRWPass *material_refract_cull_ps;
  DRWPass *material_sss_ps;
  DRWPass *material_sss_cull_ps;
  DRWPass *transparent_pass;
  DRWPass *background_ps;
  PassSimple update_noise_pass = {"update_noise_pass"};
  PassSimple lookdev_glossy_pass = {"lookdev_glossy_pass"};
  PassSimple lookdev_diffuse_pass = {"lookdev_diffuse_pass"};
  DRWPass *renderpass_pass;

  /* Effects */
  GPUFrameBuffer *gtao_fb;
  GPUFrameBuffer *gtao_debug_fb;
  GPUFrameBuffer *downsample_fb;
  GPUFrameBuffer *maxzbuffer_fb;
  GPUFrameBuffer *bloom_blit_fb;
  GPUFrameBuffer *bloom_down_fb[MAX_BLOOM_STEP];
  GPUFrameBuffer *bloom_accum_fb[MAX_BLOOM_STEP - 1];
  GPUFrameBuffer *bloom_pass_accum_fb;
  GPUFrameBuffer *cryptomatte_fb;
  GPUFrameBuffer *shadow_accum_fb;
  GPUFrameBuffer *ssr_accum_fb;
  GPUFrameBuffer *sss_blur_fb;
  GPUFrameBuffer *sss_blit_fb;
  GPUFrameBuffer *sss_resolve_fb;
  GPUFrameBuffer *sss_clear_fb;
  GPUFrameBuffer *sss_translucency_fb;
  GPUFrameBuffer *sss_accum_fb;
  GPUFrameBuffer *dof_setup_fb;
  GPUFrameBuffer *dof_flatten_tiles_fb;
  GPUFrameBuffer *dof_dilate_tiles_fb;
  GPUFrameBuffer *dof_downsample_fb;
  GPUFrameBuffer *dof_reduce_fb;
  GPUFrameBuffer *dof_reduce_copy_fb;
  GPUFrameBuffer *dof_bokeh_fb;
  GPUFrameBuffer *dof_gather_fg_fb;
  GPUFrameBuffer *dof_filter_fg_fb;
  GPUFrameBuffer *dof_gather_fg_holefill_fb;
  GPUFrameBuffer *dof_gather_bg_fb;
  GPUFrameBuffer *dof_filter_bg_fb;
  GPUFrameBuffer *dof_scatter_fg_fb;
  GPUFrameBuffer *dof_scatter_bg_fb;
  GPUFrameBuffer *screen_tracing_fb;
  GPUFrameBuffer *mist_accum_fb;
  GPUFrameBuffer *material_accum_fb;
  GPUFrameBuffer *renderpass_fb;
  GPUFrameBuffer *ao_accum_fb;

  GPUFrameBuffer *update_noise_fb;

  GPUFrameBuffer *planarref_fb;
  GPUFrameBuffer *planar_downsample_fb;

  GPUFrameBuffer *main_fb;
  GPUFrameBuffer *main_color_fb;
  GPUFrameBuffer *effect_fb;
  GPUFrameBuffer *effect_color_fb;
  GPUFrameBuffer *radiance_filtered_fb;
  GPUFrameBuffer *double_buffer_fb;
  GPUFrameBuffer *double_buffer_color_fb;
  GPUFrameBuffer *double_buffer_depth_fb;
  GPUFrameBuffer *transparent_rpass_fb;
  GPUFrameBuffer *transparent_rpass_accum_fb;
  GPUFrameBuffer *taa_history_fb;
  GPUFrameBuffer *taa_history_color_fb;

  /* Effects */
  GPUTexture *color_post; /* R16_G16_B16 */
  GPUTexture *mist_accum;
  GPUTexture *ao_accum;
  GPUTexture *sss_accum;
  GPUTexture *env_accum;
  GPUTexture *diff_color_accum;
  GPUTexture *diff_light_accum;
  GPUTexture *spec_color_accum;
  GPUTexture *spec_light_accum;
  GPUTexture *aov_surface_accum[MAX_AOVS];
  GPUTexture *emit_accum;
  Texture bloom_accum = {"bloom_accum"};
  GPUTexture *ssr_accum;
  GPUTexture *shadow_accum;
  GPUTexture *transparent_accum;
  GPUTexture *transparent_depth_tmp;
  GPUTexture *transparent_color_tmp;
  GPUTexture *cryptomatte;
  GPUTexture *taa_history;
  /* Could not be pool texture because of mipmapping. */
  GPUTexture *dof_reduced_color;
  GPUTexture *dof_reduced_coc;

  GPUTexture *volume_prop_scattering;
  GPUTexture *volume_prop_extinction;
  GPUTexture *volume_prop_emission;
  GPUTexture *volume_prop_phase;
  GPUTexture *volume_scatter;
  GPUTexture *volume_transmit;
  GPUTexture *volume_scatter_history;
  GPUTexture *volume_transmit_history;
  GPUTexture *volume_scatter_accum;
  GPUTexture *volume_transmittance_accum;

  GPUTexture *lookdev_grid_tx;
  GPUTexture *lookdev_cube_tx;

  GPUTexture *planar_pool;
  GPUTexture *planar_depth;

  GPUTexture *maxzbuffer;
  GPUTexture *filtered_radiance;

  GPUTexture *renderpass;

  GPUTexture *color; /* R16_G16_B16 */
  GPUTexture *color_double_buffer;
  GPUTexture *depth_double_buffer;

  /* Effects */
  EEVEE_EffectsInfo *effects;
  
  View lookdev_view = {"Lookdev.View"};

  EEVEE_PrivateData *g_data;

  LightCache *lookdev_lightcache;
  EEVEE_LightProbe *lookdev_cube_data;
  EEVEE_LightGrid *lookdev_grid_data;
  LightCacheTexture *lookdev_cube_mips;

};

struct Instance : public DrawEngine {

  uint64_t depsgraph_last_update_ = 0;
  bool overlays_enabled_ = false;
  bool shaders_are_ready_ = true;
  bool skip_render_ = false;

  /** Info string displayed at the top of the render / viewport, or the console when baking. */
  std::string info_ = "";

 public:
  ShaderModule &shaders;

  /** Input data. */
  Depsgraph *depsgraph;
  blender::draw::Manager *manager;
  /** Evaluated IDs. */
  Scene *scene;
  ViewLayer *view_layer;
  /** Camera object if rendering through a camera. nullptr otherwise. */
  Object *camera_eval_object;
  Object *camera_orig_object;
  /** Only available when rendering for final render. */
  const RenderLayer *render_layer;
  RenderEngine *render;
  /** Only available when rendering for viewport. */
  const blender::draw::View *drw_view = nullptr;
  const View3D *v3d;
  const RegionView3D *rv3d;

  GOOENGINE_Data *goo_data;

  // void acquire_resources();
  // void release_resources();

 public:
  Instance()
      : shaders(*ShaderModule::module_get()){
    goo_data = new blender::gooengine::GOOENGINE_Data();
  };
  ~Instance(){};

  blender::StringRefNull name_get() final
  {
    return "GOO_ENGINE";
  }

  /* Render & Viewport. */
  /* TODO(fclem): Split for clarity. */
  void init(const int2 &output_res,
            RenderEngine *render,
            Depsgraph *depsgraph,
            Object *camera_object = nullptr,
            const RenderLayer *render_layer = nullptr,
            blender::draw::View *drw_view_ = nullptr,
            const View3D *v3d = nullptr,
            const RegionView3D *rv3d = nullptr);

  void init() final;

  static void update_passes(RenderEngine *engine, Scene *scene, ViewLayer *view_layer);

  /* Render. */

  void render_sync();
  void render_frame(RenderEngine *engine, RenderLayer *render_layer, const char *view_name, const rcti *rect);
  void store_metadata(RenderResult *render_result);
  void draw_viewport();
  void draw_viewport_image_render();

  void begin_sync() final;
  void object_sync(blender::draw::ObjectRef &ob_ref, blender::draw::Manager &manager) final;
  void end_sync() final;

  void draw(blender::draw::Manager &manager) final;
};

/* `eevee_data.cc` */

void EEVEE_view_layer_data_free(void *storage);
EEVEE_ViewLayerData *EEVEE_view_layer_data_ensure();
EEVEE_ObjectEngineData *EEVEE_object_data_get(Object *ob);
EEVEE_ObjectEngineData *EEVEE_object_data_ensure(Object *ob);
EEVEE_LightProbeEngineData *EEVEE_lightprobe_data_get(Object *ob);
EEVEE_LightProbeEngineData *EEVEE_lightprobe_data_ensure(Object *ob);
EEVEE_LightEngineData *EEVEE_light_data_get(Object *ob);
EEVEE_LightEngineData *EEVEE_light_data_ensure(Object *ob);
EEVEE_WorldEngineData *EEVEE_world_data_get(World *wo);
EEVEE_WorldEngineData *EEVEE_world_data_ensure(World *wo);

void eevee_id_update(Instance *inst, ID *id);

/* `eevee_materials.cc` */

GPUTexture *EEVEE_materials_get_util_tex(); /* XXX */
void EEVEE_materials_init(EEVEE_ViewLayerData *sldata,
                          Instance *inst);
void EEVEE_materials_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void GOOENGINE_sync_mesh(Instance *inst,
                                    EEVEE_ViewLayerData *sldata,
                                    const blender::draw::ObjectRef &ob_ref,
                                    bool *cast_shadow);
void GOOENGINE_sync_hair(Instance *inst,
                                        EEVEE_ViewLayerData *sldata,
                                        const blender::draw::ObjectRef &ob_ref,
                                        bool *cast_shadow);
void GOOENGINE_sync_curves(Instance *inst,
                                        EEVEE_ViewLayerData *sldata,
                                        const blender::draw::ObjectRef &ob_ref,
                                        bool *cast_shadow);
void EEVEE_materials_cache_finish(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_materials_free();
void EEVEE_update_noise(Instance *inst, const double offsets[3]);
void EEVEE_material_renderpasses_init(Instance *inst);
void EEVEE_material_output_init(EEVEE_ViewLayerData *sldata, Instance *inst, uint tot_samples);
void EEVEE_material_output_accumulate(EEVEE_ViewLayerData *sldata, Instance *inst);
/**
 * \param ssr_id: Can be null to disable SSR contribution.
 */
void EEVEE_material_bind_resources(DRWShadingGroup *shgrp,
                                   GPUMaterial *gpumat,
                                   EEVEE_ViewLayerData *sldata,
                                   Instance *inst,
                                   const int *ssr_id,
                                   const float *refract_depth,
                                   const float alpha_clip_threshold,
                                   bool use_ssrefraction,
                                   bool use_alpha_blend);
void GOOENGINE_material_bind_resources(PassSimple &pass,
                                       GPUMaterial *gpumat,
                                       EEVEE_ViewLayerData *sldata,
                                       Instance *inst,
                                       const int *ssr_id,
                                       const float *refract_depth,
                                       const float alpha_clip_threshold,
                                       bool use_ssrefraction,
                                       bool use_alpha_blend);
void GOOENGINE_add_material_resources(PassSimple &pass, GPUMaterial *material);

/* `eevee_lights.cc` */

/**
 * Reconstruct local `obmat` from EEVEE_light. (normalized).
 */
void eevee_light_matrix_get(const EEVEE_Light *evli, float r_mat[4][4]);
void EEVEE_lights_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void GOOENGINE_sync_light(EEVEE_ViewLayerData *sldata, Object *ob);
void EEVEE_lights_cache_finish(EEVEE_ViewLayerData *sldata, Instance *inst);

/* `eevee_shadows.cc` */

void eevee_contact_shadow_setup(const Light *la, EEVEE_Shadow *evsh);
void EEVEE_shadows_init(EEVEE_ViewLayerData *sldata);
void EEVEE_shadows_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
/**
 * Make that object update shadow casting lights inside its influence bounding box.
 */
void EEVEE_shadows_caster_register(EEVEE_ViewLayerData *sldata, Object *ob);
void EEVEE_shadows_update(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_shadows_cube_add(EEVEE_LightsInfo *linfo, EEVEE_Light *evli, Object *ob);
/**
 * Return true if sample has changed and light needs to be updated.
 */
bool EEVEE_shadows_cube_setup(EEVEE_LightsInfo *linfo, const EEVEE_Light *evli, int sample_ofs);
void EEVEE_shadows_cascade_add(EEVEE_LightsInfo *linfo, EEVEE_Light *evli, Object *ob);
/**
 * This refresh lights shadow buffers.
 */
void EEVEE_shadows_draw(EEVEE_ViewLayerData *sldata, Instance *inst, DRWView *view);
void EEVEE_shadows_draw_cubemap(EEVEE_ViewLayerData *sldata, Instance *inst, int cube_index);
void EEVEE_shadows_draw_cascades(EEVEE_ViewLayerData *sldata,
                                 Instance *inst,
                                 DRWView *view,
                                 int cascade_index);
void EEVEE_shadow_output_init(EEVEE_ViewLayerData *sldata, Instance *inst, uint tot_samples);
void EEVEE_shadow_output_accumulate(EEVEE_ViewLayerData *sldata, Instance *inst);

/* `eevee_sampling.cc` */

/**
 * Special ball distribution:
 * Point are distributed in a way that when they are orthogonally
 * projected into any plane, the resulting distribution is (close to)
 * a uniform disc distribution.
 */
void EEVEE_sample_ball(int sample_ofs, float radius, float rsample[3]);
void EEVEE_sample_rectangle(int sample_ofs,
                            const float x_axis[3],
                            const float y_axis[3],
                            float size_x,
                            float size_y,
                            float rsample[3]);
void EEVEE_sample_ellipse(int sample_ofs,
                          const float x_axis[3],
                          const float y_axis[3],
                          float size_x,
                          float size_y,
                          float rsample[3]);
void EEVEE_random_rotation_m4(int sample_ofs, float scale, float r_mat[4][4]);

/* `eevee_shaders.cc` */

/**
 * Configure a default node-tree with the given material.
 */
bNodeTree *EEVEE_shader_default_surface_nodetree(Material *ma);
/**
 * Configure a default node-tree with the given world.
 */
bNodeTree *EEVEE_shader_default_world_nodetree(World *wo);
Material *EEVEE_material_default_diffuse_get();
Material *EEVEE_material_default_glossy_get();
Material *EEVEE_material_default_error_get();
World *EEVEE_world_default_get();
/**
 * \note Compilation is not deferred.
 */
GPUMaterial *EEVEE_material_default_get(Scene *scene, Material *ma, int options);
GPUMaterial *EEVEE_material_get(
    Instance *inst, Scene *scene, Material *ma, World *wo, int options);
void EEVEE_shaders_free();

void eevee_shader_material_create_info_amend(GPUMaterial *gpumat,
                                             GPUCodegenOutput *codegen_,
                                             const char *vert,
                                             const char *geom,
                                             const char *frag,
                                             const char *vert_info_name,
                                             const char *geom_info_name,
                                             const char *frag_info_name,
                                             char *defines);
GPUShader *eevee_shaders_sh_create_helper(const char *name,
                                          const char *vert_name,
                                          const char *frag_name,
                                          const char *defines,
                                          bool use_layered_rendering);

/* `eevee_lightprobes.cc` */

bool EEVEE_lightprobes_obj_visibility_cb(bool vis_in, void *user_data);
void EEVEE_lightprobes_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_lightprobes_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void GOOENGINE_sync_probe(EEVEE_ViewLayerData *sldata, Instance *inst, Object *ob);
void EEVEE_lightprobes_cache_finish(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_lightprobes_refresh(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_lightprobes_refresh_planar(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_lightprobes_free();

/**
 * Only initialize the passes useful for rendering the light cache.
 */
void EEVEE_lightbake_cache_init(EEVEE_ViewLayerData *sldata,
                                Instance *inst,
                                GPUTexture *rt_color,
                                GPUTexture *rt_depth);
void EEVEE_lightbake_render_world(EEVEE_ViewLayerData *sldata,
                                  Instance *inst,
                                  GPUFrameBuffer *face_fb[6]);
/**
 * Render the scene to the `probe_rt` texture.
 */
void EEVEE_lightbake_render_scene(EEVEE_ViewLayerData *sldata,
                                  Instance *inst,
                                  GPUFrameBuffer *face_fb[6],
                                  const float pos[3],
                                  float near_clip,
                                  float far_clip);
/**
 * Glossy filter `rt_color` to `light_cache->cube_tx.tex` at index `probe_idx`.
 */
void EEVEE_lightbake_filter_glossy(EEVEE_ViewLayerData *sldata,
                                   Instance *inst,
                                   GPUTexture *rt_color,
                                   GPUFrameBuffer *fb,
                                   int probe_idx,
                                   float intensity,
                                   int maxlevel,
                                   float filter_quality,
                                   float firefly_fac);
/**
 * Diffuse filter `rt_color` to `light_cache->grid_tx.tex` at index `grid_offset`.
 */
void EEVEE_lightbake_filter_diffuse(EEVEE_ViewLayerData *sldata,
                                    Instance *inst,
                                    GPUTexture *rt_color,
                                    GPUFrameBuffer *fb,
                                    int grid_offset,
                                    float intensity);
/**
 * Filter `rt_depth` to `light_cache->grid_tx.tex` at index `grid_offset`.
 */
void EEVEE_lightbake_filter_visibility(EEVEE_ViewLayerData *sldata,
                                       Instance *inst,
                                       GPUTexture *rt_depth,
                                       GPUFrameBuffer *fb,
                                       int grid_offset,
                                       float clipsta,
                                       float clipend,
                                       float vis_range,
                                       float vis_blur,
                                       int vis_size);

void EEVEE_lightprobes_grid_data_from_object(Object *ob, EEVEE_LightGrid *egrid, int *offset);
void EEVEE_lightprobes_cube_data_from_object(Object *ob, EEVEE_LightProbe *eprobe);
void EEVEE_lightprobes_planar_data_from_object(Object *ob,
                                               EEVEE_PlanarReflection *eplanar,
                                               EEVEE_LightProbeVisTest *vis_test);

/* `eevee_depth_of_field.cc` */

int EEVEE_depth_of_field_init(EEVEE_ViewLayerData *sldata, Instance *inst, Object *camera);
void EEVEE_depth_of_field_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_depth_of_field_draw(Instance *inst);
bool EEVEE_depth_of_field_jitter_get(EEVEE_EffectsInfo *effects,
                                     float r_jitter[2],
                                     float *r_focus_distance);
int EEVEE_depth_of_field_sample_count_get(EEVEE_EffectsInfo *effects,
                                          int sample_count,
                                          int *r_ring_count);

/* `eevee_bloom.cc` */

int EEVEE_bloom_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_bloom_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_bloom_draw(Instance *inst);
void EEVEE_bloom_output_init(EEVEE_ViewLayerData *sldata, Instance *inst, uint tot_samples);
void EEVEE_bloom_output_accumulate(EEVEE_ViewLayerData *sldata, Instance *inst);

/* `eevee_cryptomatte.cc` */

void EEVEE_cryptomatte_renderpasses_init(Instance *inst);
void EEVEE_cryptomatte_output_init(EEVEE_ViewLayerData *sldata,
                                   Instance *inst,
                                   int tot_samples);
void EEVEE_cryptomatte_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_cryptomatte_cache_populate(Instance *inst, EEVEE_ViewLayerData *sldata, const blender::draw::ObjectRef &ob_ref);
void EEVEE_cryptomatte_particle_hair_cache_populate(Instance *inst,
                                                    EEVEE_ViewLayerData *sldata,
                                                    const blender::draw::ObjectRef &ob_ref);
void EEVEE_cryptomatte_object_curves_cache_populate(Instance *inst,
                                                    EEVEE_ViewLayerData *sldata,
                                                    const blender::draw::ObjectRef &ob_ref);
void EEVEE_cryptomatte_output_accumulate(EEVEE_ViewLayerData *sldata, Instance *inst);
/**
 * Register the render passes needed for cryptomatte
 * normally this is done in `EEVEE_render_update_passes`, but it has been placed here to keep
 * related code side-by-side for clarity.
 */
void EEVEE_cryptomatte_update_passes(RenderEngine *engine, Scene *scene, ViewLayer *view_layer);
void EEVEE_cryptomatte_render_result(RenderLayer *rl,
                                     const char *viewname,
                                     const rcti *rect,
                                     Instance *inst,
                                     EEVEE_ViewLayerData *sldata);
void EEVEE_cryptomatte_store_metadata(Instance *inst, RenderResult *render_result);
void EEVEE_cryptomatte_free(Instance *inst);

/* `eevee_occlusion.cc` */

int EEVEE_occlusion_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_occlusion_output_init(EEVEE_ViewLayerData *sldata,
                                 Instance *inst,
                                 uint tot_samples);
void EEVEE_occlusion_output_accumulate(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_occlusion_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_occlusion_compute(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_occlusion_draw_debug(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_occlusion_free();

/* `eevee_screen_raytrace.cc` */

int EEVEE_screen_raytrace_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_screen_raytrace_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_refraction_compute(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_reflection_compute(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_reflection_output_init(EEVEE_ViewLayerData *sldata,
                                  Instance *inst,
                                  uint tot_samples);
void EEVEE_reflection_output_accumulate(EEVEE_ViewLayerData *sldata, Instance *inst);

/* `eevee_subsurface.cc` */

void EEVEE_subsurface_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_subsurface_draw_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_subsurface_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_subsurface_output_init(EEVEE_ViewLayerData *sldata,
                                  Instance *inst,
                                  uint tot_samples);
void EEVEE_subsurface_add_pass(EEVEE_ViewLayerData *sldata,
                               Instance *inst,
                               Material *ma,
                               DRWShadingGroup *shgrp,
                               GPUMaterial *gpumat);
void EEVEE_subsurface_data_render(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_subsurface_compute(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_subsurface_output_accumulate(EEVEE_ViewLayerData *sldata, Instance *inst);

/* `eevee_mist.cc` */

void EEVEE_mist_output_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_mist_output_accumulate(EEVEE_ViewLayerData *sldata, Instance *inst);

/* `eevee_renderpasses.cc` */

void EEVEE_renderpasses_init(Instance *inst);
void EEVEE_renderpasses_output_init(EEVEE_ViewLayerData *sldata,
                                    Instance *inst,
                                    uint tot_samples);
void EEVEE_renderpasses_cache_finish(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_renderpasses_output_accumulate(EEVEE_ViewLayerData *sldata,
                                          Instance *inst,
                                          bool post_effect);
void EEVEE_material_transparent_output_init(Instance *inst);
void EEVEE_material_transparent_output_accumulate(Instance *inst);
/**
 * Post-process data to construct a specific render-pass
 *
 * This method will create a shading group to perform the post-processing for the given
 * `renderpass_type`. The post-processing will be done and the result will be stored in the
 * `inst->goo_data->renderpass` texture.
 *
 * Only invoke this function for passes that need post-processing.
 *
 * After invoking this function the active frame-buffer is set to `inst->goo_data->renderpass_fb`.
 */
void EEVEE_renderpasses_postprocess(EEVEE_ViewLayerData *sldata,
                                    Instance *inst,
                                    eViewLayerEEVEEPassType renderpass_type,
                                    int aov_index);
void EEVEE_renderpasses_draw(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_reset_viewport_data(Instance *inst);
void EEVEE_renderpasses_draw_debug(Instance *inst);
bool EEVEE_renderpasses_only_first_sample_pass_active(Instance *inst);
/**
 * Calculate the hash for an AOV. The least significant bit is used to store the AOV
 * type the rest of the bits are used for the name hash.
 */
uint EEVEE_renderpasses_aov_hash(const ViewLayerAOV *aov);

/* `eevee_temporal_sampling.cc` */

void EEVEE_temporal_sampling_reset(Instance *inst);
void EEVEE_temporal_sampling_create_view(Instance *inst);
int EEVEE_temporal_sampling_sample_count_get(const Scene *scene, const Instance *inst);
int EEVEE_temporal_sampling_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_temporal_sampling_offset_calc(const double ht_point[2],
                                         float filter_size,
                                         float r_offset[2]);
void EEVEE_temporal_sampling_matrices_calc(EEVEE_EffectsInfo *effects, const double ht_point[2]);
/**
 * Update the matrices based on the current sample.
 * \note `DRW_MAT_PERS` and `DRW_MAT_VIEW` needs to read the original matrices.
 */
void EEVEE_temporal_sampling_update_matrices(Instance *inst);
void EEVEE_temporal_sampling_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_temporal_sampling_draw(Instance *inst);

/* `eevee_effects.cc` */

void EEVEE_effects_init(EEVEE_ViewLayerData *sldata,
                        Instance *inst,
                        Object *camera,
                        bool minimal);
void EEVEE_effects_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_effects_draw_init(EEVEE_ViewLayerData *sldata, Instance *inst);
/**
 * Simple down-sampling algorithm. Reconstruct mip chain up to mip level.
 */
void EEVEE_effects_downsample_radiance_buffer(Instance *inst, GPUTexture *texture_src);
void EEVEE_effects_radiance_copy(EEVEE_ViewLayerData *sldata, Instance *inst);
void EEVEE_create_minmax_buffer(Instance *inst, GPUTexture *depth_src, int layer);
/**
 * Simple down-sampling algorithm for cube-map. Reconstruct mip chain up to mip level.
 */
void EEVEE_downsample_cube_buffer(Instance *inst, GPUTexture *texture_src, int level);
void EEVEE_draw_effects(EEVEE_ViewLayerData *sldata, Instance *inst);

/* `eevee_render.cc` */

/**
 * Return true if initialized properly.
 */
bool EEVEE_render_init(Instance *inst, RenderEngine *engine, Depsgraph *depsgraph);
void EEVEE_render_view_sync(Instance *inst, RenderEngine *engine, Depsgraph *depsgraph);
void EEVEE_render_modules_init(Instance *inst, RenderEngine *engine, Depsgraph *depsgraph);
void EEVEE_render_cache_init(EEVEE_ViewLayerData *sldata, Instance *inst);
/**
 * Used by light cache. in this case engine is nullptr.
 */
void EEVEE_render_cache(Instance *inst, blender::draw::ObjectRef &ob_ref);
void EEVEE_render_draw(Instance *inst,
                       RenderEngine *engine,
                       RenderLayer *rl,
                       const rcti *rect);
void EEVEE_render_read_result(Instance *inst,
                              RenderEngine *engine,
                              RenderLayer *rl,
                              const rcti *rect);

/** `eevee_lookdev.cc` */
void EEVEE_lookdev_init(Instance *inst);
void EEVEE_lookdev_cache_init(Instance *inst,
                              EEVEE_ViewLayerData *sldata,
                              DRWPass *pass,
                              EEVEE_LightProbesInfo *pinfo,
                              DRWShadingGroup **r_shgrp);
void EEVEE_lookdev_draw(Instance *inst);

/* Shadow Matrix */
static const float texcomat[4][4] = {
    /* From NDC to TexCo */
    {0.5f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.5f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.5f, 0.0f},
    {0.5f, 0.5f, 0.5f, 1.0f},
};

/* Cube-map Matrices */
static const float cubefacemat[6][4][4] = {
    /* Pos X */
    {{0.0f, 0.0f, -1.0f, 0.0f},
     {0.0f, -1.0f, 0.0f, 0.0f},
     {-1.0f, 0.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, 0.0f, 1.0f}},
    /* Neg X */
    {{0.0f, 0.0f, 1.0f, 0.0f},
     {0.0f, -1.0f, 0.0f, 0.0f},
     {1.0f, 0.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, 0.0f, 1.0f}},
    /* Pos Y */
    {{1.0f, 0.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, -1.0f, 0.0f},
     {0.0f, 1.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, 0.0f, 1.0f}},
    /* Neg Y */
    {{1.0f, 0.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, 1.0f, 0.0f},
     {0.0f, -1.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, 0.0f, 1.0f}},
    /* Pos Z */
    {{1.0f, 0.0f, 0.0f, 0.0f},
     {0.0f, -1.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, -1.0f, 0.0f},
     {0.0f, 0.0f, 0.0f, 1.0f}},
    /* Neg Z */
    {{-1.0f, 0.0f, 0.0f, 0.0f},
     {0.0f, -1.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, 1.0f, 0.0f},
     {0.0f, 0.0f, 0.0f, 1.0f}},
};

}  // namespace blender::gooengine
