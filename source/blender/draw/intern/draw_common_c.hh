/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw
 */

#pragma once

struct DRWShadingGroup;
struct DRWView;
struct FluidModifierData;
struct GPUMaterial;
struct GPUTexture;
struct GPUUniformBuf;

namespace blender::gpu {
class VertBuf;
}
struct ModifierData;
struct Object;
struct ParticleSystem;
struct RegionView3D;
struct ViewLayer;
struct Scene;
struct DRWData;
namespace blender::draw {
class Manager;
struct CurvesModule;
struct PointCloudModule;
struct VolumeModule;
struct ObjectRef;
}  // namespace blender::draw

/* draw_hair.cc */
 
namespace blender::draw {
/**
 * This creates a shading group with display hairs.
 * The draw call is already added by this function, just add additional uniforms.
 */
DRWShadingGroup *DRW_shgroup_hair_create_sub(const blender::draw::ObjectRef &ob_ref,
                                             ParticleSystem *psys,
                                             ModifierData *md,
                                             DRWShadingGroup *shgrp,
                                             GPUMaterial *gpu_material);
}  // namespace blender::draw

/**
 * \note Only valid after #DRW_curves_update().
 */
blender::gpu::VertBuf *DRW_hair_pos_buffer_get(Object *object,
                                               ParticleSystem *psys,
                                               ModifierData *md);
void DRW_hair_duplimat_get(const blender::draw::ObjectRef &ob_ref,
                           ParticleSystem *psys,
                           ModifierData *md,
                           float (*dupli_mat)[4]);

/* draw_curves.cc */

namespace blender::draw {

/**
 * \note Only valid after #DRW_curves_update().
 */
gpu::VertBuf *DRW_curves_pos_buffer_get(Object *object);

DRWShadingGroup *DRW_shgroup_curves_create_sub(Object *object,
                                               DRWShadingGroup *shgrp,
                                               GPUMaterial *gpu_material);

/* If drw_data is nullptr, DST global is accessed to get it. */
void DRW_curves_init(DRWData *drw_data = nullptr);
void DRW_curves_module_free(draw::CurvesModule *module);
void DRW_curves_update(draw::Manager &manager);

/* draw_pointcloud.cc */

/* If drw_data is nullptr, DST global is accessed to get it. */
void DRW_pointcloud_init(DRWData *drw_data = nullptr);
void DRW_pointcloud_module_free(draw::PointCloudModule *module);

/* draw_volume.cc */

/**
 * Add attributes bindings of volume grids to an existing shading group.
 * No draw call is added so the caller can decide how to use the data.
 * \return nullptr if there is nothing to draw.
 */
DRWShadingGroup *DRW_shgroup_volume_create_sub(Scene *scene,
                                               Object *ob,
                                               DRWShadingGroup *shgrp,
                                               GPUMaterial *gpu_material);

/* If drw_data is nullptr, DST global is accessed to get it. */
void DRW_volume_init(DRWData *drw_data = nullptr);
void DRW_volume_module_free(draw::VolumeModule *module);

}  // namespace blender::draw

/* `draw_fluid.cc` */

/* Fluid simulation. */
void DRW_smoke_ensure(FluidModifierData *fmd, int highres);
void DRW_smoke_ensure_coba_field(FluidModifierData *fmd);
void DRW_smoke_ensure_velocity(FluidModifierData *fmd);
void DRW_fluid_ensure_flags(FluidModifierData *fmd);
void DRW_fluid_ensure_range_field(FluidModifierData *fmd);

void DRW_smoke_free(FluidModifierData *fmd);

void DRW_smoke_init(DRWData *drw_data);
void DRW_smoke_exit(DRWData *drw_data);

/* `draw_common.cc` */

struct DRW_Global {
  GPUUniformBuf *view_ubo;
  GPUUniformBuf *clipping_ubo;
};
extern DRW_Global G_draw;