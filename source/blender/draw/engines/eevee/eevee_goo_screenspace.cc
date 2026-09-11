/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <cstdio>

#include "BKE_global.hh"
#include "GPU_state.hh"
#include "eevee_goo_screenspace.hh"
#include "eevee_instance.hh"

namespace blender::eevee {

GooScreenSpaceModule::GooScreenSpaceModule(Instance &inst) : inst_(inst)
{
  const float4 black(0.0f);
  const float far_depth = 0.0f; /* Hardware depth is reversed. */
  dummy_color_tx_.ensure_2d(
      gpu::TextureFormat::SFLOAT_16_16_16_16, int2(1), GPU_TEXTURE_USAGE_SHADER_READ, black);
  dummy_depth_tx_.ensure_2d(
      gpu::TextureFormat::SFLOAT_32, int2(1), GPU_TEXTURE_USAGE_SHADER_READ, &far_depth);
  dummy_data_.push_update();
  data_.push_update();
  color_ref_ = dummy_color_tx_;
  depth_ref_ = dummy_depth_tx_;
}

bool GooScreenSpaceModule::uses_material(const blender::Material *material,
                                         const GPUMaterial *gpumat)
{
  return (material->blend_flag & MA_BL_SS_REFRACTION) &&
         GPU_material_flag_get(
             gpumat, GPU_MATFLAG_GOO_SCREENSPACE_COLOR | GPU_MATFLAG_GOO_SCREENSPACE_DEPTH);
}

void GooScreenSpaceModule::begin_sync()
{
  deferred_color_ = forward_color_ = depth_ = false;
}

void GooScreenSpaceModule::register_material(const blender::Material *material,
                                             const GPUMaterial *gpumat,
                                             eMaterialPipeline pipeline)
{
  if (!ELEM(pipeline, MAT_PIPE_DEFERRED, MAT_PIPE_FORWARD) || !inst_.raytracing.use_raytracing() ||
      !uses_material(material, gpumat))
  {
    return;
  }
  const bool color = GPU_material_flag_get(gpumat, GPU_MATFLAG_GOO_SCREENSPACE_COLOR);
  deferred_color_ |= color && pipeline == MAT_PIPE_DEFERRED;
  forward_color_ |= color && pipeline == MAT_PIPE_FORWARD;
  depth_ |= GPU_material_flag_get(gpumat, GPU_MATFLAG_GOO_SCREENSPACE_DEPTH);
}

void GooScreenSpaceModule::end_sync()
{
  if (deferred_color_) {
    inst_.pipelines.background.sync_scene_capture();
  }
  if (!deferred_color_ && !forward_color_) {
    color_ref_ = dummy_color_tx_;
    color_tx_.free();
  }
  if (!depth_) {
    depth_ref_ = dummy_depth_tx_;
    depth_tx_.free();
  }
}

void GooScreenSpaceModule::begin_view()
{
  color_copies_ = depth_copies_ = world_draws_ = 0;
  color_ref_ = dummy_color_tx_;
  depth_ref_ = dummy_depth_tx_;
  /* With no consumers, do not upload a per-sample UBO; invalidate once on disable. */
  if (data_.color_valid || data_.depth_valid) {
    data_ = GooScreenSpaceData{};
    data_.push_update();
  }
}

void GooScreenSpaceModule::copy_color(int2 extent)
{
  /* No dependency on physical closures, feedback allocation, history or denoisers. */
  const int mip_count = min_ii(6, 1 + int(log2_floor_u(uint(max_ii(extent.x, extent.y)))));
  color_tx_.ensure_2d(RenderBuffers::color_format,
                      extent,
                      GPU_TEXTURE_USAGE_ATTACHMENT | GPU_TEXTURE_USAGE_SHADER_READ,
                      nullptr,
                      mip_count);
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE | GPU_BARRIER_FRAMEBUFFER);
  GPU_texture_copy(color_tx_, inst_.render_buffers.combined_tx);
  color_ref_ = color_tx_;
  data_.extent_inv = 1.0f / float2(extent);
  data_.color_valid = 1;
  color_copies_++;
}

void GooScreenSpaceModule::capture_opaque(draw::View &view, int2 extent)
{
  if (depth_) {
    depth_tx_.ensure_2d(RenderBuffers::depth_format,
                        extent,
                        GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE | GPU_BARRIER_FRAMEBUFFER);
    GPU_texture_copy(depth_tx_, inst_.render_buffers.depth_tx);
    depth_ref_ = depth_tx_;
    data_.depth_valid = 1;
    data_.extent_inv = 1.0f / float2(extent);
    depth_copies_++;
  }
  if (deferred_color_) {
    copy_color(extent);
    /* World has not yet been drawn into combined. Fill only the private snapshot's far pixels;
     * the capture variant cannot write Environment/AOV or other public render passes. */
    capture_fb_.ensure(GPU_ATTACHMENT_TEXTURE(inst_.render_buffers.depth_tx),
                       GPU_ATTACHMENT_TEXTURE(color_tx_));
    inst_.pipelines.background.render_scene_capture(view, capture_fb_);
    world_draws_++;
    GPU_texture_update_mipmap_chain(color_tx_);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);
  }
  if (depth_ || deferred_color_) {
    data_.push_update();
  }
}

void GooScreenSpaceModule::capture_forward(int2 extent)
{
  if (!forward_color_) {
    return;
  }
  /* After opaque refraction and world, before volume resolve and all BLENDED surfaces. */
  copy_color(extent);
  GPU_texture_update_mipmap_chain(color_tx_);
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);
  data_.push_update();
}

void GooScreenSpaceModule::end_view()
{
  if (G.debug_value == 734) {
    printf("GOO_SCREENSPACE_DIAG color_copies=%d depth_copies=%d world_draws=%d\n",
           color_copies_,
           depth_copies_,
           world_draws_);
  }
}
}  // namespace blender::eevee
