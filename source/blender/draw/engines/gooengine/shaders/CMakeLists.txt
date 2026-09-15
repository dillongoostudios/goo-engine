# SPDX-FileCopyrightText: 2024 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(INC_GLSL
  .
  ..

  # For variadic macros
  ../../../../blenlib

  ../../../intern
  ../../../intern/shaders

  ../../../../gpu
  ../../../../gpu/intern
  ../../../../gpu/shaders
  ../../../../gpu/shaders/common
  ../../../../gpu/shaders/infos
)

set(SRC_GLSL_VERT
  background_vert.glsl
  cryptomatte_vert.glsl
  effect_dof_scatter_vert.glsl
  lightprobe_cube_display_vert.glsl
  lightprobe_grid_display_vert.glsl
  lightprobe_planar_downsample_vert.glsl
  world_vert.glsl
)

set(SRC_GLSL_FRAG
  bsdf_lut_frag.glsl
  btdf_lut_frag.glsl
  cryptomatte_frag.glsl
  effect_bloom_frag.glsl
  effect_dof_bokeh_frag.glsl
  effect_dof_dilate_tiles_frag.glsl
  effect_dof_downsample_frag.glsl
  effect_dof_filter_frag.glsl
  effect_dof_flatten_tiles_frag.glsl
  effect_dof_gather_frag.glsl
  effect_dof_reduce_frag.glsl
  effect_dof_resolve_frag.glsl
  effect_dof_scatter_frag.glsl
  effect_dof_setup_frag.glsl
  effect_downsample_cube_frag.glsl
  effect_downsample_frag.glsl
  effect_gtao_frag.glsl
  effect_minmaxz_frag.glsl
  effect_mist_frag.glsl
  effect_reflection_resolve_frag.glsl
  effect_reflection_trace_frag.glsl
  effect_subsurface_frag.glsl
  effect_translucency_frag.glsl
  lightprobe_cube_display_frag.glsl
  lightprobe_filter_diffuse_frag.glsl
  lightprobe_filter_glossy_frag.glsl
  lightprobe_filter_visibility_frag.glsl
  lightprobe_grid_display_frag.glsl
  lightprobe_planar_downsample_frag.glsl
  lookdev_world_frag.glsl
  prepass_frag.glsl
  renderpass_accumulate_frag.glsl
  renderpass_postprocess_frag.glsl
  shadow_accum_frag.glsl
  shadow_frag.glsl
  surface_frag.glsl
  update_noise_frag.glsl
)

set(SRC_GLSL_COMP
)

set(SRC_GLSL_LIB
  ambient_occlusion_lib.glsl
  bsdf_common_lib.glsl
  bsdf_sampling_lib.glsl
  closure_eval_diffuse_lib.glsl
  closure_eval_glossy_lib.glsl
  closure_eval_lib.glsl
  closure_eval_refraction_lib.glsl
  closure_eval_surface_lib.glsl
  closure_eval_translucent_lib.glsl
  closure_eval_volume_lib.glsl
  closure_type_lib.glsl
  common_uniforms_lib.glsl
  common_utiltex_lib.glsl
  effect_dof_lib.glsl
  effect_reflection_lib.glsl
  irradiance_lib.glsl
  lights_lib.glsl
  ltc_lib.glsl
  octahedron_lib.glsl
  random_lib.glsl
  raytrace_lib.glsl
  renderpass_lib.glsl
  ssr_lib.glsl
  surface_lib.glsl
)

# Compile shaders with shader code.
if(WITH_GPU_SHADER_CPP_COMPILATION)
  with_shader_cpp_compilation_config()

  compile_sources_as_cpp(gpencil_cpp_shaders_vert "${SRC_GLSL_VERT}" "GPU_VERTEX_SHADER")
  compile_sources_as_cpp(gpencil_cpp_shaders_frag "${SRC_GLSL_FRAG}" "GPU_FRAGMENT_SHADER")
  # compile_sources_as_cpp(gpencil_cpp_shaders_comp "${SRC_GLSL_COMP}" "GPU_COMPUTE_SHADER")
  # Only enable to make sure they compile on their own.
  # Otherwise it creates a warning about `pragma once`.
  # compile_sources_as_cpp(gpencil_cpp_shaders_lib "${SRC_GLSL_LIB}" "GPU_LIBRARY_SHADER")
endif()
