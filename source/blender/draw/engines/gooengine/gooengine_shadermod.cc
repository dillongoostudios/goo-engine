/* SPDX-FileCopyrightText: 2021 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gooengine
 *
 * Shader module that manage shader libraries, deferred compilation,
 * and static shader usage.
 */

#include "GPU_capabilities.hh"

#include "gpu_shader_create_info.hh"

#include "gooengine_shadermod.hh"
#include "eevee_private.hh"

#include "BLI_assert.h"

namespace blender::gooengine {

/* -------------------------------------------------------------------- */
/** \name Module
 *
 * \{ */

ShaderModule *ShaderModule::module_get()
{
  return &get_static_cache().get();
}

void ShaderModule::module_free()
{
  get_static_cache().release();
}

ShaderModule::ShaderModule()
{
  Vector<const GPUShaderCreateInfo *> infos;
  infos.reserve(MAX_SHADER_TYPE);

  for (auto i : IndexRange(MAX_SHADER_TYPE)) {
    const char *name = static_shader_create_info_name_get(eShaderType(i));
    const GPUShaderCreateInfo *create_info = GPU_shader_create_info_get(name);

    if (GPU_use_parallel_compilation()) {
      infos.append(create_info);
    }
    else {
      shaders_[i] = {name};
    }

#ifndef NDEBUG
    if (name == nullptr) {
      std::cerr << "EEVEE: Missing case for eShaderType(" << i
                << ") in static_shader_create_info_name_get().";
      BLI_assert(0);
    }
    BLI_assert_msg(create_info != nullptr, "EEVEE: Missing create info for static shader.");
#endif
  }

  if (GPU_use_parallel_compilation()) {
    compilation_handle_ = GPU_shader_batch_create_from_infos(infos);
  }
}

ShaderModule::~ShaderModule()
{
  /* Finish compilation to avoid asserts on exit at GLShaderCompiler destructor. */

  if (compilation_handle_) {
    static_shaders_are_ready(true);
  }

}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Static shaders
 *
 * \{ */

bool ShaderModule::static_shaders_are_ready(bool block_until_ready)
{
  if (!GPU_use_parallel_compilation()) {
    return true;
  }

  std::lock_guard lock(mutex_);

  if (compilation_handle_) {
    if (GPU_shader_batch_is_ready(compilation_handle_) || block_until_ready) {
      Vector<GPUShader *> shaders = GPU_shader_batch_finalize(compilation_handle_);
      for (int i : IndexRange(MAX_SHADER_TYPE)) {
        shaders_[i].set(shaders[i]);
      }
    }
  }

  return compilation_handle_ == 0;
}

const char *ShaderModule::static_shader_create_info_name_get(eShaderType shader_type)
{
  switch (shader_type) {
    case AMBIENT_OCCLUSION:
      return "eevee_legacy_ambient_occlusion";
    case AMBIENT_OCCLUSION_DEBUG:
      return "eevee_legacy_ambient_occlusion_debug";
      
    case BLOOM_BLIT:
      return "eevee_legacy_bloom_blit";
    case BLOOM_BLIT_HQ:
      return "eevee_legacy_bloom_blit_hq";
    case BLOOM_DOWNSAMPLE:
      return "eevee_legacy_bloom_downsample";
    case BLOOM_DOWNSAMPLE_HQ:
      return "eevee_legacy_bloom_downsample_hq";
    case BLOOM_RESOLVE:
      return "eevee_legacy_bloom_resolve";
    case BLOOM_RESOLVE_HQ:
      return "eevee_legacy_bloom_resolve_hq";
    case BLOOM_UPSAMPLE:
      return "eevee_legacy_bloom_upsample";
    case BLOOM_UPSAMPLE_HQ:
      return "eevee_legacy_bloom_upsample_hq";

    case CRYPTOMATTE_HAIR:
      return "eevee_legacy_cryptomatte_hair";
    case CRYPTOMATTE_MESH:
      return "eevee_legacy_cryptomatte_mesh";

    case DEPTH_OF_FIELD_BOKEH:
      return "eevee_legacy_depth_of_field_bokeh";
    case DEPTH_OF_FIELD_DILATE_TILES_MINABS:
      return "eevee_legacy_depth_of_field_dilate_tiles_MINMAX";
    case DEPTH_OF_FIELD_DILATE_TILES_MINMAX:
      return "eevee_legacy_depth_of_field_dilate_tiles_MINABS";
    case DEPTH_OF_FIELD_DOWNSAMPLE:
      return "eevee_legacy_depth_of_field_downsample";
    case DEPTH_OF_FIELD_FILTER:
      return "eevee_legacy_depth_of_field_filter";
    case DEPTH_OF_FIELD_FLATTEN_TILES:
      return "eevee_legacy_depth_of_field_flatten_tiles";
    case DEPTH_OF_FIELD_GATHER_BG:
      return "eevee_legacy_depth_of_field_gather_BACKGROUND";
    case DEPTH_OF_FIELD_GATHER_BG_BOKEH:
      return "eevee_legacy_depth_of_field_gather_BACKGROUND_BOKEH";
    case DEPTH_OF_FIELD_GATHER_FG:
      return "eevee_legacy_depth_of_field_gather_FOREGROUND";
    case DEPTH_OF_FIELD_GATHER_FG_BOKEH:
      return "eevee_legacy_depth_of_field_gather_FOREGROUND_BOKEH";
    case DEPTH_OF_FIELD_GATHER_HOLEFILL:
      return "eevee_legacy_depth_of_field_gather_HOLEFILL";
    case DEPTH_OF_FIELD_GATHER_HOLEFILL_BOKEH:
      return "eevee_legacy_depth_of_field_gather_HOLEFILL_BOKEH";
    case DEPTH_OF_FIELD_REDUCE:
      return "eevee_legacy_depth_of_field_reduce_REDUCE_PASS";
    case DEPTH_OF_FIELD_REDUCE_COPY:
      return "eevee_legacy_depth_of_field_reduce_COPY_PASS";
    case DEPTH_OF_FIELD_RESOLVE_HQ:
      return "eevee_legacy_depth_of_field_resolve_HQ";
    case DEPTH_OF_FIELD_RESOLVE_HQ_BOKEH:
      return "eevee_legacy_depth_of_field_resolve_HQ_BOKEH";
    case DEPTH_OF_FIELD_RESOLVE_LQ:
      return "eevee_legacy_depth_of_field_resolve_LQ";
    case DEPTH_OF_FIELD_RESOLVE_LQ_BOKEH:
      return "eevee_legacy_depth_of_field_resolve_LQ_BOKEH";
    case DEPTH_OF_FIELD_SCATTER_BG:
      return "eevee_legacy_depth_of_field_scatter_BACKGROUND";
    case DEPTH_OF_FIELD_SCATTER_BG_BOKEH:
      return "eevee_legacy_depth_of_field_scatter_BACKGROUND_BOKEH";
    case DEPTH_OF_FIELD_SCATTER_FG:
      return "eevee_legacy_depth_of_field_scatter_FOREGROUND";
    case DEPTH_OF_FIELD_SCATTER_FG_BOKEH:
      return "eevee_legacy_depth_of_field_scatter_FOREGROUND_BOKEH";
    case DEPTH_OF_FIELD_SETUP:
      return "eevee_legacy_depth_of_field_setup";

    case EFFECT_COLOR_COPY:
      return "eevee_legacy_color_copy";
    case EFFECT_DOWNSAMPLE:
      return "eevee_legacy_downsample";
    case EFFECT_DOWNSAMPLE_CUBE:
      return "eevee_legacy_effect_downsample_cube";
    case EFFECT_MAXZ_COPYDEPTH:
      return "eevee_legacy_maxz_copydepth";
    case EFFECT_MAXZ_COPYDEPTH_LAYER:
      return "eevee_legacy_maxz_copydepth_layer";
    case EFFECT_MAXZ_DOWNDEPTH:
      return "eevee_legacy_maxz_downdepth";
    case EFFECT_MAXZ_DOWNDEPTH_LAYER:
      return "eevee_legacy_maxz_downdepth_layer";
    case EFFECT_MAXZ_DOWNLEVEL:
      return "eevee_legacy_maxz_downlevel";
    case EFFECT_MINZ_COPYDEPTH:
      return "eevee_legacy_minz_copydepth";
    case EFFECT_MINZ_DOWNDEPTH:
      return "eevee_legacy_minz_downdepth";
    case EFFECT_MINZ_DOWNDEPTH_LAYER:
      return "eevee_legacy_minz_downdepth_layer";
    case EFFECT_MINZ_DOWNLEVEL:
      return "eevee_legacy_minz_downlevel";
    case EFFECT_MIST:
      return "eevee_legacy_effect_mist_FIRST_PASS";
    case EFFECT_MOTION_BLUR_BASE:
      return "eevee_legacy_effect_motion_blur";
    case EFFECT_MOTION_BLUR_HAIR:
      return "eevee_legacy_effect_motion_blur_object_hair";
    case EFFECT_MOTION_BLUR_OBJECT:
      return "eevee_legacy_effect_motion_blur_object";
    case EFFECT_MOTION_BLUR_VELOCITY_TILES:
      return "eevee_legacy_effect_motion_blur_velocity_tiles_GATHER";
    case EFFECT_MOTION_BLUR_VELOCITY_TILES_EXPAND:
      return "eevee_legacy_effect_motion_blur_velocity_tiles_EXPANSION";
    case EFFECT_REFLECTION_RESOLVE:
      return "eevee_legacy_effect_reflection_resolve";
    case EFFECT_REFLECTION_RESOLVE_PROBE:
      return "eevee_legacy_effect_reflection_resolve_probe";
    case EFFECT_REFLECTION_RESOLVE_REFL:
      return "eevee_legacy_effect_reflection_resolve_ssr";
    case EFFECT_REFLECTION_TRACE:
      return "eevee_legacy_effect_reflection_trace";

    case GGX_LUT:
      return "eevee_legacy_ggx_lut_bsdf";
    case GGX_REFRACTION_LUT:
      return "eevee_legacy_ggx_lut_btdf";

    case PROBE_CUBE_DISPLAY:
      return "eevee_legacy_lightprobe_cube_display";
    case PROBE_FILTER_DIFFUSE:
#if defined(IRRADIANCE_SH_L2)
      return "eevee_legacy_probe_filter_diffuse_sh_l2";
#elif defined(IRRADIANCE_HL2)
      return "eevee_legacy_probe_filter_diffuse_hl2";
#endif
      return "eevee_depth_of_field_scatter";
    case PROBE_FILTER_GLOSSY:
      return "eevee_legacy_probe_filter_glossy";
    case PROBE_FILTER_VISIBILITY:
      return "eevee_legacy_probe_filter_visibility";
    case PROBE_GRID_DISPLAY:
#if defined(IRRADIANCE_SH_L2)
      return "eevee_legacy_lightprobe_grid_display_common_sh_l2";
#elif defined(IRRADIANCE_HL2)
      return "eevee_legacy_lightprobe_grid_display_common_hl2";
#endif
    case PROBE_GRID_FILL:
#if defined(IRRADIANCE_SH_L2)
      return "eevee_legacy_probe_grid_fill_sh_l2";
#elif defined(IRRADIANCE_HL2)
      return "eevee_legacy_probe_grid_fill_hl2";
#endif
    case PROBE_PLANAR_DISPLAY:
      return "eevee_legacy_probe_planar_display";
    case PROBE_PLANAR_DOWNSAMPLE:
      return "eevee_legacy_lightprobe_planar_downsample";
    case RENDERPASSES_ACCUMULATE:
      return "eevee_legacy_renderpass_accumulate";
    case RENDERPASSES_POST_PROCESS:
      return "eevee_legacy_post_process";

    case SHADOW:
      return "eevee_legacy_shader_shadow";
    case SHADOW_ACCUM:
      return "eevee_legacy_shader_shadow_accum";

    case STUDIOLIGHT_BACKGROUND:
      return "eevee_legacy_studiolight_background";
    case STUDIOLIGHT_PROBE:
      return "eevee_legacy_studiolight_probe";

    case SUBSURFACE_FIRST_PASS:
      return "eevee_legacy_shader_effect_subsurface_common_FIRST_PASS";
    case SUBSURFACE_SECOND_PASS:
      return "eevee_legacy_shader_effect_subsurface_common_SECOND_PASS";
    case SUBSURFACE_TRANSLUCENCY:
      return "eevee_legacy_shader_effect_subsurface_translucency";

    case TAA_RESOLVE_REPROJECTION:
      return "eevee_legacy_taa_resolve_reprojection";
    case TAA_RESOLVE_RESOLVE_BASIC:
      return "eevee_legacy_taa_resolve_basic";

    case VELOCITY_RESOLVE:
      return "eevee_legacy_velocity_resolve";
    case VOLUMES_ACCUM:
      return "eevee_legacy_volumes_accum";
    case VOLUMES_CLEAR:
      return "eevee_legacy_volumes_clear";
    case VOLUMES_INTEGRATION:
      return "eevee_legacy_volumes_integration";
    case VOLUMES_INTEGRATION_OPTI:
      return "eevee_legacy_volumes_integration_OPTI";
    case VOLUMES_RESOLVE:
      return "eevee_legacy_volumes_resolve";
    case VOLUMES_RESOLVE_ACCUM:
      return "eevee_legacy_volumes_resolve_accum";
    case VOLUMES_SCATTER:
      return "eevee_legacy_volumes_scatter";
    case VOLUMES_SCATTER_WITH_LIGHTS:
      return "eevee_legacy_volumes_scatter_with_lights";

    case UPDATE_NOISE:
      return "eevee_legacy_update_noise";

    /* To avoid compiler warning about missing case. */
    case MAX_SHADER_TYPE:
      return "";
  }
  return "";
}

GPUShader *ShaderModule::static_shader_get(eShaderType shader_type)
{
  BLI_assert(compilation_handle_ == 0);
  if (GPU_use_parallel_compilation() && shaders_[shader_type].get() == nullptr) {
    const char *shader_name = static_shader_create_info_name_get(shader_type);
    fprintf(stderr, "EEVEE: error: Could not compile static shader \"%s\"\n", shader_name);
    BLI_assert(0);
  }

  return shaders_[shader_type].get();
}

// /** \} */

// /* -------------------------------------------------------------------- */
// /** \name GPU Materials
//  *
//  * \{ */

// static inline bool geometry_type_has_surface(eMaterialGeometry geometry_type)
// {
//   return geometry_type < MAT_GEOM_VOLUME;
// }

// static inline void material_type_from_shader_uuid(uint64_t shader_uuid,
//                                                   eMaterialPipeline &pipeline_type,
//                                                   eMaterialGeometry &geometry_type,
//                                                   bool &transparent_shadows)
// {
//   const uint64_t geometry_mask = ((1u << 4u) - 1u);
//   const uint64_t pipeline_mask = ((1u << 4u) - 1u);
//   geometry_type = static_cast<eMaterialGeometry>(shader_uuid & geometry_mask);
//   pipeline_type = static_cast<eMaterialPipeline>((shader_uuid >> 4u) & pipeline_mask);
//   transparent_shadows = (shader_uuid >> 10u) & 1u;
// }

// static inline uint64_t shader_uuid_from_material_type(
//     eMaterialPipeline pipeline_type,
//     eMaterialGeometry geometry_type,
//     char blend_flags = 0)
// {
//   BLI_assert(geometry_type < (1 << 4));
//   BLI_assert(pipeline_type < (1 << 4));
//   uint64_t transparent_shadows = blend_flags & MA_BL_TRANSPARENT_SHADOW ? 1 : 0;

//   uint64_t uuid;
//   uuid = geometry_type;
//   uuid |= pipeline_type << 4;
//   uuid |= transparent_shadows << 10;
//   return uuid;
// }

// void ShaderModule::material_create_info_amend(GPUMaterial *gpumat, GPUCodegenOutput *codegen_)
// {
//   using namespace blender::gpu::shader;

//   uint64_t shader_uuid = GPU_material_uuid_get(gpumat);

//   eMaterialPipeline pipeline_type;
//   eMaterialGeometry geometry_type;
//   bool transparent_shadows;
//   material_type_from_shader_uuid(
//       shader_uuid, pipeline_type, geometry_type, transparent_shadows);

//   GPUCodegenOutput &codegen = *codegen_;
//   ShaderCreateInfo &info = *reinterpret_cast<ShaderCreateInfo *>(codegen.create_info);

//   /* WORKAROUND: Replace by new ob info. */
//   int64_t ob_info_index = info.additional_infos_.first_index_of_try("draw_object_infos");
//   if (ob_info_index != -1) {
//     info.additional_infos_[ob_info_index] = "draw_object_infos_new";
//   }

//   /* WORKAROUND: Add new ob attr buffer. */
//   if (GPU_material_uniform_attributes(gpumat) != nullptr) {
//     info.additional_info("draw_object_attribute_new");

//     /* Search and remove the old object attribute UBO which would creating bind point collision. */
//     for (auto &resource_info : info.batch_resources_) {
//       if (resource_info.bind_type == ShaderCreateInfo::Resource::BindType::UNIFORM_BUFFER &&
//           resource_info.uniformbuf.name == GPU_ATTRIBUTE_UBO_BLOCK_NAME "[512]")
//       {
//         info.batch_resources_.remove_first_occurrence_and_reorder(resource_info);
//         break;
//       }
//     }
//     /* Remove references to the UBO. */
//     info.define("UNI_ATTR(a)", "vec4(0.0)");
//   }

//   /* First indices are reserved by the engine.
//    * Put material samplers in reverse order, starting from the last slot. */
//   int sampler_slot = GPU_max_textures_frag() - 1;
//   for (auto &resource : info.batch_resources_) {
//     if (resource.bind_type == ShaderCreateInfo::Resource::BindType::SAMPLER) {
//       resource.slot = sampler_slot--;
//     }
//   }

//   if (GPU_material_flag_get(gpumat, GPU_MATFLAG_AO) &&
//       ELEM(pipeline_type, MAT_PIPE_FORWARD, MAT_PIPE_DEFERRED) &&
//       geometry_type_has_surface(geometry_type))
//   {
//     info.define("MAT_AMBIENT_OCCLUSION");
//   }

//   if (GPU_material_flag_get(gpumat, GPU_MATFLAG_TRANSPARENT)) {
//     if (pipeline_type != MAT_PIPE_SHADOW || transparent_shadows) {
//       info.define("MAT_TRANSPARENT");
//     }
//     /* Transparent material do not have any velocity specific pipeline. */
//     if (pipeline_type == MAT_PIPE_PREPASS_FORWARD_VELOCITY) {
//       pipeline_type = MAT_PIPE_PREPASS_FORWARD;
//     }
//   }

//   bool supports_render_passes = (pipeline_type == MAT_PIPE_DEFERRED);
//   /* Opaque forward do support AOVs and render pass if not using transparency. */
//   if (!GPU_material_flag_get(gpumat, GPU_MATFLAG_TRANSPARENT) &&
//       (pipeline_type == MAT_PIPE_FORWARD))
//   {
//     supports_render_passes = true;
//   }

//   if (supports_render_passes) {
//     info.additional_info("eevee_render_pass_out");
//     info.additional_info("eevee_cryptomatte_out");
//   }

//   int lit_closure_count = 0;
//   if (GPU_material_flag_get(gpumat, GPU_MATFLAG_DIFFUSE)) {
//     info.define("MAT_DIFFUSE");
//     lit_closure_count++;
//   }
//   if (GPU_material_flag_get(gpumat, GPU_MATFLAG_GLOSSY)) {
//     info.define("MAT_REFLECTION");
//     lit_closure_count++;
//   }
//   if (GPU_material_flag_get(gpumat, GPU_MATFLAG_TRANSLUCENT)) {
//     info.define("MAT_TRANSLUCENT");
//     lit_closure_count++;
//   }
//   if (GPU_material_flag_get(gpumat, GPU_MATFLAG_SUBSURFACE)) {
//     info.define("MAT_SUBSURFACE");
//     lit_closure_count++;
//   }
//   if (GPU_material_flag_get(gpumat, GPU_MATFLAG_REFRACT)) {
//     info.define("MAT_REFRACTION");
//     /* TODO(fclem): Support refracted lights. */
//   }

//   if (GPU_material_flag_get(gpumat, GPU_MATFLAG_TRANSLUCENT | GPU_MATFLAG_SUBSURFACE)) {
//     info.define("SHADOW_SUBSURFACE");
//   }

//   if ((pipeline_type == MAT_PIPE_FORWARD) ||
//       GPU_material_flag_get(gpumat, GPU_MATFLAG_SHADER_TO_RGBA))
//   {
//     switch (lit_closure_count) {
//       case 0:
//         /* Define nothing. This will in turn define SKIP_LIGHT_EVAL. */
//         break;
//       /* These need to be separated since the strings need to be static. */
//       case 1:
//         info.define("LIGHT_CLOSURE_EVAL_COUNT", "1");
//         break;
//       case 2:
//         info.define("LIGHT_CLOSURE_EVAL_COUNT", "2");
//         break;
//       case 3:
//         info.define("LIGHT_CLOSURE_EVAL_COUNT", "3");
//         break;
//       case 4:
//         info.define("LIGHT_CLOSURE_EVAL_COUNT", "4");
//         break;
//       default:
//         BLI_assert_unreachable();
//         break;
//     }
//   }

//   if (GPU_material_flag_get(gpumat, GPU_MATFLAG_BARYCENTRIC)) {
//     switch (geometry_type) {
//       case MAT_GEOM_MESH:
//         /* Support using gpu builtin barycentrics. */
//         info.define("USE_BARYCENTRICS");
//         info.builtins(BuiltinBits::BARYCENTRIC_COORD);
//         break;
//       case MAT_GEOM_CURVES:
//         /* Support using one vec2 attribute. See #hair_get_barycentric(). */
//         info.define("USE_BARYCENTRICS");
//         break;
//       default:
//         /* No support */
//         break;
//     }
//   }

//   std::stringstream global_vars;
//   switch (geometry_type) {
//     case MAT_GEOM_MESH:
//       /** Noop. */
//       break;
//     case MAT_GEOM_POINT_CLOUD:
//     case MAT_GEOM_CURVES:
//       /** Hair attributes come from sampler buffer. Transfer attributes to sampler. */
//       for (auto &input : info.vertex_inputs_) {
//         if (input.name == "orco") {
//           /** NOTE: Orco is generated from strand position for now. */
//           global_vars << input.type << " " << input.name << ";\n";
//         }
//         else {
//           info.sampler(sampler_slot--, ImageType::FLOAT_BUFFER, input.name, Frequency::BATCH);
//         }
//       }
//       info.vertex_inputs_.clear();
//       break;
//     case MAT_GEOM_WORLD:
//       /**
//        * Only orco layer is supported by world and it is procedurally generated. These are here to
//        * make the attribs_load function calls valid.
//        */
//       ATTR_FALLTHROUGH;
//     case MAT_GEOM_GPENCIL:
//       /**
//        * Only one uv and one color attribute layer are supported by gpencil objects and they are
//        * already declared in another createInfo. These are here to make the attribs_load
//        * function calls valid.
//        */
//       for (auto &input : info.vertex_inputs_) {
//         global_vars << input.type << " " << input.name << ";\n";
//       }
//       info.vertex_inputs_.clear();
//       break;
//     case MAT_GEOM_VOLUME:
//     case MAT_GEOM_VOLUME_OBJECT:
//     case MAT_GEOM_VOLUME_WORLD:
//       /** Volume grid attributes come from 3D textures. Transfer attributes to samplers. */
//       for (auto &input : info.vertex_inputs_) {
//         info.sampler(sampler_slot--, ImageType::FLOAT_3D, input.name, Frequency::BATCH);
//       }
//       info.vertex_inputs_.clear();
//       break;
//   }

//   const bool do_vertex_attrib_load = !ELEM(geometry_type,
//                                            MAT_GEOM_WORLD,
//                                            MAT_GEOM_VOLUME_WORLD,
//                                            MAT_GEOM_VOLUME_OBJECT,
//                                            MAT_GEOM_VOLUME);

//   if (!do_vertex_attrib_load && !info.vertex_out_interfaces_.is_empty()) {
//     /* Codegen outputs only one interface. */
//     const StageInterfaceInfo &iface = *info.vertex_out_interfaces_.first();
//     /* Globals the attrib_load() can write to when it is in the fragment shader. */
//     global_vars << "struct " << iface.name << " {\n";
//     for (const auto &inout : iface.inouts) {
//       global_vars << "  " << inout.type << " " << inout.name << ";\n";
//     }
//     global_vars << "};\n";
//     global_vars << iface.name << " " << iface.instance_name << ";\n";

//     info.vertex_out_interfaces_.clear();
//   }

//   std::stringstream attr_load;
//   attr_load << "void attrib_load()\n";
//   attr_load << "{\n";
//   attr_load << ((codegen.attr_load) ? codegen.attr_load : "");
//   attr_load << "}\n\n";

//   std::stringstream vert_gen, frag_gen, comp_gen;

//   bool is_compute = pipeline_type == MAT_PIPE_VOLUME_MATERIAL;

//   if (do_vertex_attrib_load) {
//     vert_gen << global_vars.str() << attr_load.str();
//   }
//   else if (!is_compute) {
//     frag_gen << global_vars.str() << attr_load.str();
//   }
//   else {
//     comp_gen << global_vars.str() << attr_load.str();
//   }

//   if (!is_compute && pipeline_type != MAT_PIPE_VOLUME_OCCUPANCY) {
//     frag_gen << ((codegen.material_functions) ? codegen.material_functions : "\n");

//     frag_gen << "Closure nodetree_surface()\n";
//     frag_gen << "{\n";
//     frag_gen << "  closure_weights_reset();\n";
//     frag_gen << ((codegen.surface) ? codegen.surface : "return Closure(0);\n");
//     frag_gen << "}\n\n";

//     info.fragment_source_generated = frag_gen.str();
//   }

//   if (is_compute) {
//     comp_gen << ((codegen.material_functions) ? codegen.material_functions : "\n");

//     comp_gen << "Closure nodetree_volume()\n";
//     comp_gen << "{\n";
//     comp_gen << "  closure_weights_reset();\n";
//     comp_gen << ((codegen.volume) ? codegen.volume : "return Closure(0);\n");
//     comp_gen << "}\n\n";

//     info.compute_source_generated = comp_gen.str();
//   }

//   /* Geometry Info. */
//   switch (geometry_type) {
//     case MAT_GEOM_WORLD:
//       info.additional_info("eevee_geom_world");
//       break;
//     case MAT_GEOM_VOLUME_WORLD:
//       info.additional_info("eevee_volume_world");
//       break;
//     case MAT_GEOM_VOLUME_OBJECT:
//       info.additional_info("eevee_volume_object");
//       break;
//     case MAT_GEOM_GPENCIL:
//       info.additional_info("eevee_geom_gpencil");
//       break;
//     case MAT_GEOM_CURVES:
//       info.additional_info("eevee_geom_curves");
//       break;
//     case MAT_GEOM_MESH:
//       info.additional_info("eevee_geom_mesh");
//       break;
//     case MAT_GEOM_POINT_CLOUD:
//       info.additional_info("eevee_geom_point_cloud");
//       break;
//     case MAT_GEOM_VOLUME:
//       info.additional_info("eevee_geom_volume");
//       break;
//   }
//   /* Pipeline Info. */
//   switch (geometry_type) {
//     case MAT_GEOM_WORLD:
//       info.additional_info("eevee_surf_world");
//       break;
//     case MAT_GEOM_VOLUME_OBJECT:
//     case MAT_GEOM_VOLUME_WORLD:
//       break;
//     default:
//       switch (pipeline_type) {
//         case MAT_PIPE_PREPASS_FORWARD_VELOCITY:
//         case MAT_PIPE_PREPASS_DEFERRED_VELOCITY:
//           info.additional_info("eevee_surf_depth", "eevee_velocity_geom");
//           break;
//         case MAT_PIPE_PREPASS_OVERLAP:
//         case MAT_PIPE_PREPASS_FORWARD:
//         case MAT_PIPE_PREPASS_DEFERRED:
//           info.additional_info("eevee_surf_depth");
//           break;
//         case MAT_PIPE_PREPASS_PLANAR:
//           info.additional_info("eevee_surf_depth", "eevee_clip_plane");
//           break;
//         case MAT_PIPE_SHADOW:
//           /* Determine surface shadow shader depending on used update technique. */
//           switch (ShadowModule::shadow_technique) {
//             case ShadowTechnique::ATOMIC_RASTER: {
//               info.additional_info("eevee_surf_shadow_atomic");
//             } break;
//             case ShadowTechnique::TILE_COPY: {
//               info.additional_info("eevee_surf_shadow_tbdr");
//             } break;
//             default: {
//               BLI_assert_unreachable();
//             } break;
//           }
//           break;
//         case MAT_PIPE_VOLUME_OCCUPANCY:
//           info.additional_info("eevee_surf_occupancy");
//           break;
//         case MAT_PIPE_CAPTURE:
//           info.additional_info("eevee_surf_capture");
//           break;
//         case MAT_PIPE_DEFERRED:
//           if (GPU_material_flag_get(gpumat, GPU_MATFLAG_SHADER_TO_RGBA)) {
//             info.additional_info("eevee_surf_deferred_hybrid");
//           }
//           else {
//             info.additional_info("eevee_surf_deferred");
//           }
//           break;
//         case MAT_PIPE_FORWARD:
//           info.additional_info("eevee_surf_forward");
//           break;
//         default:
//           BLI_assert_unreachable();
//           break;
//       }
//       break;
//   }
// }

// /* WATCH: This can be called from another thread! Needs to not touch the shader module in any
//  * thread unsafe manner. */
// static void codegen_callback(void *thunk, GPUMaterial *mat, GPUCodegenOutput *codegen)
// {
//   reinterpret_cast<ShaderModule *>(thunk)->material_create_info_amend(mat, codegen);
// }

// GPUMaterial *ShaderModule::material_shader_get(::Material *blender_mat,
//                                                bNodeTree *nodetree,
//                                                eMaterialPipeline pipeline_type,
//                                                eMaterialGeometry geometry_type,
//                                                bool deferred_compilation)
// {
//   bool is_volume = ELEM(pipeline_type, MAT_PIPE_VOLUME_MATERIAL, MAT_PIPE_VOLUME_OCCUPANCY);

//   uint64_t shader_uuid = shader_uuid_from_material_type(
//       pipeline_type, geometry_type, blender_mat->blend_flag);

//   return DRW_shader_from_material(blender_mat,
//                                   nodetree,
//                                   GPU_MAT_EEVEE,
//                                   shader_uuid,
//                                   is_volume,
//                                   deferred_compilation,
//                                   codegen_callback,
//                                   this);
// }

// GPUMaterial *ShaderModule::world_shader_get(::World *blender_world,
//                                             bNodeTree *nodetree,
//                                             eMaterialPipeline pipeline_type)
// {
//   const bool is_volume = (options & VAR_MAT_VOLUME) != 0;
//   const bool is_default = (options & VAR_DEFAULT) != 0;
//   bool is_volume = (pipeline_type == MAT_PIPE_VOLUME_MATERIAL);
//   bool defer_compilation = is_volume;

//   eMaterialGeometry geometry_type = is_volume ? MAT_GEOM_VOLUME_WORLD : MAT_GEOM_WORLD;

//   uint64_t shader_uuid = shader_uuid_from_material_type(pipeline_type, geometry_type);

//   return DRW_shader_from_world(blender_world,
//                                nodetree,
//                                GPU_MAT_EEVEE,
//                                shader_uuid,
//                                is_volume,
//                                defer_compilation,
//                                codegen_callback,
//                                this);
// }

// /** \} */

}  // namespace blender::gooengine
