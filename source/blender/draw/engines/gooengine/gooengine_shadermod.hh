/* SPDX-FileCopyrightText: 2021 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gooengine
 *
 * Shader module that manage shader libraries, deferred compilation,
 * and static shader usage.
 */

#pragma once

#include <array>
#include <string>

#include "BLI_mutex.hh"
#include "BLI_string_ref.hh"

#include "DRW_render.hh"
#include "GPU_material.hh"
#include "GPU_shader.hh"

// #include "eevee_material.hh"
// #include "eevee_sync.hh"

namespace blender::gooengine {

using StaticShader = gpu::StaticShader;

/* Keep alphabetical order and clean prefix. */
enum eShaderType {
  AMBIENT_OCCLUSION = 0,
  AMBIENT_OCCLUSION_DEBUG,

  BLOOM_BLIT,
  BLOOM_BLIT_HQ,
  BLOOM_DOWNSAMPLE,
  BLOOM_DOWNSAMPLE_HQ,
  BLOOM_RESOLVE,
  BLOOM_RESOLVE_HQ,
  BLOOM_UPSAMPLE,
  BLOOM_UPSAMPLE_HQ,

  CRYPTOMATTE_HAIR,
  CRYPTOMATTE_MESH,

  DEPTH_OF_FIELD_BOKEH,
  DEPTH_OF_FIELD_DILATE_TILES_MINABS,
  DEPTH_OF_FIELD_DILATE_TILES_MINMAX,
  DEPTH_OF_FIELD_DOWNSAMPLE,
  DEPTH_OF_FIELD_FILTER,
  DEPTH_OF_FIELD_FLATTEN_TILES,
  DEPTH_OF_FIELD_GATHER_BG,
  DEPTH_OF_FIELD_GATHER_BG_BOKEH,
  DEPTH_OF_FIELD_GATHER_FG,
  DEPTH_OF_FIELD_GATHER_FG_BOKEH,
  DEPTH_OF_FIELD_GATHER_HOLEFILL,
  DEPTH_OF_FIELD_GATHER_HOLEFILL_BOKEH,
  DEPTH_OF_FIELD_REDUCE,
  DEPTH_OF_FIELD_REDUCE_COPY,
  DEPTH_OF_FIELD_RESOLVE_HQ,
  DEPTH_OF_FIELD_RESOLVE_HQ_BOKEH,
  DEPTH_OF_FIELD_RESOLVE_LQ,
  DEPTH_OF_FIELD_RESOLVE_LQ_BOKEH,
  DEPTH_OF_FIELD_SCATTER_BG,
  DEPTH_OF_FIELD_SCATTER_BG_BOKEH,
  DEPTH_OF_FIELD_SCATTER_FG,
  DEPTH_OF_FIELD_SCATTER_FG_BOKEH,
  DEPTH_OF_FIELD_SETUP,

  EFFECT_COLOR_COPY,
  EFFECT_DOWNSAMPLE,
  EFFECT_DOWNSAMPLE_CUBE,
  EFFECT_MAXZ_COPYDEPTH,
  EFFECT_MAXZ_COPYDEPTH_LAYER,
  EFFECT_MAXZ_DOWNDEPTH,
  EFFECT_MAXZ_DOWNDEPTH_LAYER,
  EFFECT_MAXZ_DOWNLEVEL,
  EFFECT_MINZ_COPYDEPTH,
  EFFECT_MINZ_DOWNDEPTH,
  EFFECT_MINZ_DOWNDEPTH_LAYER,
  EFFECT_MINZ_DOWNLEVEL,

  EFFECT_MIST,
  EFFECT_MOTION_BLUR_BASE,
  EFFECT_MOTION_BLUR_HAIR,
  EFFECT_MOTION_BLUR_OBJECT,
  EFFECT_MOTION_BLUR_VELOCITY_TILES,
  EFFECT_MOTION_BLUR_VELOCITY_TILES_EXPAND,

  EFFECT_REFLECTION_RESOLVE,
  EFFECT_REFLECTION_RESOLVE_PROBE,
  EFFECT_REFLECTION_RESOLVE_REFL,
  EFFECT_REFLECTION_TRACE,
  
  GGX_LUT,
  GGX_REFRACTION_LUT,

  PROBE_CUBE_DISPLAY,
  PROBE_FILTER_DIFFUSE,
  PROBE_FILTER_GLOSSY,
  PROBE_FILTER_VISIBILITY,
  PROBE_GRID_DISPLAY,
  PROBE_GRID_FILL,
  PROBE_PLANAR_DISPLAY,
  PROBE_PLANAR_DOWNSAMPLE,
  
  RENDERPASSES_ACCUMULATE,
  RENDERPASSES_POST_PROCESS,

  SHADOW,
  SHADOW_ACCUM,

  STUDIOLIGHT_BACKGROUND,
  STUDIOLIGHT_PROBE,
  
  SUBSURFACE_FIRST_PASS,
  SUBSURFACE_SECOND_PASS,
  SUBSURFACE_TRANSLUCENCY,

  TAA_RESOLVE_REPROJECTION,
  TAA_RESOLVE_RESOLVE_BASIC,
  
  VELOCITY_RESOLVE,

  VOLUMES_ACCUM,
  VOLUMES_CLEAR,
  VOLUMES_INTEGRATION,
  VOLUMES_INTEGRATION_OPTI,
  VOLUMES_RESOLVE,
  VOLUMES_RESOLVE_ACCUM,
  VOLUMES_SCATTER,
  VOLUMES_SCATTER_WITH_LIGHTS,

  UPDATE_NOISE,

  MAX_SHADER_TYPE,
};

enum eMaterialPipeline {
  MAT_PIPE_DEFERRED = 0,
  MAT_PIPE_FORWARD,
  /* These all map to the depth shader. */
  MAT_PIPE_PREPASS_DEFERRED,
  MAT_PIPE_PREPASS_DEFERRED_VELOCITY,
  MAT_PIPE_PREPASS_OVERLAP,
  MAT_PIPE_PREPASS_FORWARD,
  MAT_PIPE_PREPASS_FORWARD_VELOCITY,
  MAT_PIPE_PREPASS_PLANAR,

  MAT_PIPE_VOLUME_MATERIAL,
  MAT_PIPE_VOLUME_OCCUPANCY,
  MAT_PIPE_SHADOW,
  MAT_PIPE_CAPTURE,
};

enum eMaterialGeometry {
  /* These maps directly to object types. */
  MAT_GEOM_MESH = 0,
  MAT_GEOM_POINT_CLOUD,
  MAT_GEOM_CURVES,
  MAT_GEOM_GPENCIL,
  MAT_GEOM_VOLUME,

  /* These maps to special shader. */
  MAT_GEOM_WORLD,
};

/**
 * Shader module. shared between instances.
 */
class ShaderModule {
 private:
  std::array<StaticShader, MAX_SHADER_TYPE> shaders_;
  BatchHandle compilation_handle_ = 0;
  Mutex mutex_;

  static gpu::StaticShaderCache<ShaderModule> &get_static_cache()
  {
    /** Shared shader module across all engine instances. */
    static gpu::StaticShaderCache<ShaderModule> static_cache;
    return static_cache;
  }

 public:
  ShaderModule();
  ~ShaderModule();

  bool static_shaders_are_ready(bool block_until_ready);

  GPUShader *static_shader_get(eShaderType shader_type);
//   GPUMaterial *material_default_shader_get(eMaterialPipeline pipeline_type,
//                                            eMaterialGeometry geometry_type);
//   GPUMaterial *material_shader_get(::Material *blender_mat,
//                                    bNodeTree *nodetree,
//                                    eMaterialPipeline pipeline_type,
//                                    eMaterialGeometry geometry_type,
//                                    bool deferred_compilation);
//   GPUMaterial *world_shader_get(::World *blender_world,
//                                 bNodeTree *nodetree,
//                                 eMaterialPipeline pipeline_type);

//   void material_create_info_amend(GPUMaterial *mat, GPUCodegenOutput *codegen);

  /** Only to be used by Instance constructor. */
  static ShaderModule *module_get();
  static void module_free();

 private:
  const char *static_shader_create_info_name_get(eShaderType shader_type);
};

}  // namespace blender::gooengine
