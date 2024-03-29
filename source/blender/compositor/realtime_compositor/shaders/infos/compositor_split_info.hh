/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(compositor_split_shared)
    .local_group_size(16, 16)
    .push_constant(Type::FLOAT, "split_ratio")
    .sampler(0, ImageType::FLOAT_2D, "first_image_tx")
    .sampler(1, ImageType::FLOAT_2D, "second_image_tx")
    .image(0, GPU_RGBA16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .compute_source("compositor_split.glsl");

GPU_SHADER_CREATE_INFO(compositor_split_horizontal)
    .additional_info("compositor_split_shared")
    .define("SPLIT_HORIZONTAL")
    .do_static_compilation(true);

GPU_SHADER_CREATE_INFO(compositor_split_vertical)
    .additional_info("compositor_split_shared")
    .define("SPLIT_VERTICAL")
    .do_static_compilation(true);
