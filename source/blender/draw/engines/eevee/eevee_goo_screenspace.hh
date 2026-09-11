/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "DRW_gpu_wrapper.hh"
#include "draw_pass.hh"
#include "eevee_defines.hh"
#include "eevee_goo_screenspace_shared.hh"
#include "eevee_material.hh"

namespace blender::eevee {
class Instance;

/** Current-sample camera snapshots, never native ray-tracing history or an active color target. */
class GooScreenSpaceModule {
 private:
  Instance &inst_;
  bool deferred_color_ = false;
  bool forward_color_ = false;
  bool depth_ = false;
  draw::Texture color_tx_{"Goo.SceneColor"};
  draw::Texture depth_tx_{"Goo.SceneDepth"};
  draw::Texture dummy_color_tx_{"Goo.EmptyColor"};
  draw::Texture dummy_depth_tx_{"Goo.EmptyDepth"};
  draw::Framebuffer capture_fb_{"Goo.SceneBackground"};
  draw::UniformBuffer<GooScreenSpaceData> data_{"Goo.ScreenSpace"};
  draw::UniformBuffer<GooScreenSpaceData> dummy_data_{"Goo.ScreenSpace.Disabled"};
  gpu::Texture *color_ref_ = nullptr;
  gpu::Texture *depth_ref_ = nullptr;
  int color_copies_ = 0;
  int depth_copies_ = 0;
  int world_draws_ = 0;

  void copy_color(int2 extent);

 public:
  GooScreenSpaceModule(Instance &inst);
  static bool uses_material(const blender::Material *material, const GPUMaterial *gpumat);
  void begin_sync();
  void register_material(const blender::Material *material,
                         const GPUMaterial *gpumat,
                         eMaterialPipeline pipeline);
  void end_sync();
  void begin_view();
  void capture_opaque(draw::View &view, int2 extent);
  void capture_forward(int2 extent);
  void end_view();

  template<typename Pass> void bind(Pass &pass, bool camera = true)
  {
    pass.bind_ubo(GOO_SCREENSPACE_BUF_SLOT, camera ? &data_ : &dummy_data_);
    pass.bind_texture(GOO_SCENE_COLOR_TEX_SLOT,
                      camera ? &color_ref_ : &dummy_color_tx_,
                      {GPU_SAMPLER_FILTERING_LINEAR | GPU_SAMPLER_FILTERING_MIPMAP});
    pass.bind_texture(GOO_SCENE_DEPTH_TEX_SLOT, camera ? &depth_ref_ : &dummy_depth_tx_);
  }
};
}  // namespace blender::eevee
