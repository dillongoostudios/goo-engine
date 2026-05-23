/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw
 */

/* Private functions / structs of the draw manager */

#pragma once

#include "DRW_engine.hh"
#include "DRW_render.hh"

#include "BLI_assert.h"
#include "BLI_linklist.h"
#include "BLI_memblock.h"

#include "BLI_task.h"
#include "BLI_threads.h"

#include "GPU_batch.hh"
#include "GPU_context.hh"
#include "GPU_drawlist.hh"
#include "GPU_framebuffer.hh"
#include "GPU_uniform_buffer.hh"
#include "GPU_shader.hh"
#include "GPU_viewport.hh"

#include "draw_instance_data.hh"
#include "draw_shader_shared.hh"

struct DRWDebugModule;
struct DRWTexturePool;
struct DRWUniformChunk;
struct DRWViewData;
struct DRWTextStore;
struct DupliObject;
struct Object;
struct Mesh;
namespace blender::draw {
struct CurvesModule;
struct VolumeModule;
struct PointCloudModule;
struct DRW_Attributes;
struct DRW_MeshCDMask;
class CurveRefinePass;
class View;
}  // namespace blender::draw
struct GPUMaterial;
struct GSet;

/** Use draw-call batching using instanced rendering. */
#define USE_BATCHING 1

// #define DRW_DEBUG_CULLING

/**
 * Data structure containing all drawcalls organized by passes and materials.
 * DRWPass > DRWShadingGroup > DRWCall > DRWCallState
 *                           > DRWUniform
 */

struct DRWCullingState {
  uint32_t mask;
  /* Culling: Using Bounding Sphere for now for faster culling.
   * Not ideal for planes. Could be extended. */
  BoundSphere bsphere;
  /* Grrr only used by EEVEE. */
  void *user_data;
};

/* Minimum max UBO size is 64KiB. We take the largest
 * UBO struct and alloc the max number.
 * `((1 << 16) / sizeof(DRWObjectMatrix)) = 512`
 * Keep in sync with `common_view_lib.glsl`. */
#define DRW_RESOURCE_CHUNK_LEN 512

/**
 * Identifier used to sort similar drawcalls together.
 * Also used to reference elements inside memory blocks.
 *
 * From MSB to LSB
 * 1 bit for negative scale.
 * 22 bits for chunk id.
 * 9 bits for resource id inside the chunk. (can go up to 511)
 * |-|----------------------|---------|
 *
 * Use manual bit-shift and mask instead of bit-fields to avoid
 * compiler dependent behavior that would mess the ordering of
 * the members thus changing the sorting order.
 */
typedef uint32_t DRWResourceHandle;

BLI_INLINE uint32_t DRW_handle_negative_scale_get(const DRWResourceHandle *handle)
{
  return (*handle & 0x80000000) != 0;
}

BLI_INLINE uint32_t DRW_handle_chunk_get(const DRWResourceHandle *handle)
{
  return (*handle & 0x7FFFFFFF) >> 9;
}

BLI_INLINE uint32_t DRW_handle_id_get(const DRWResourceHandle *handle)
{
  return (*handle & 0x000001FF);
}

BLI_INLINE void DRW_handle_increment(DRWResourceHandle *handle)
{
  *handle += 1;
}

BLI_INLINE void DRW_handle_negative_scale_enable(DRWResourceHandle *handle)
{
  *handle |= 0x80000000;
}

BLI_INLINE void *DRW_memblock_elem_from_handle(BLI_memblock *memblock,
                                               const DRWResourceHandle *handle)
{
  int elem = DRW_handle_id_get(handle);
  int chunk = DRW_handle_chunk_get(handle);
  return BLI_memblock_elem_get(memblock, chunk, elem);
}

struct DRWObjectMatrix {
  float model[4][4];
  float modelinverse[4][4];
};

struct DRWObjectInfos {
  float orcotexfac[2][4];
  float ob_color[4];
  float ob_index;
  float pad; /*UNUSED*/
  float ob_random;
  float ob_flag; /* Sign is negative scaling. */
  float pad1; /*UNUSED*/
  float pad2; /*UNUSED*/
  float pad3; /*UNUSED*/
  float pad4; /*UNUSED*/
};

BLI_STATIC_ASSERT_ALIGN(DRWObjectMatrix, 16)
BLI_STATIC_ASSERT_ALIGN(DRWObjectInfos, 16)

typedef enum {
  /* Draw Commands */
  DRW_CMD_DRAW = 0, /* Only sortable type. Must be 0. */
  DRW_CMD_DRAW_RANGE = 1,
  DRW_CMD_DRAW_INSTANCE = 2,
  DRW_CMD_DRAW_INSTANCE_RANGE = 3,
  DRW_CMD_DRAW_PROCEDURAL = 4,
  DRW_CMD_DRAW_INDIRECT = 5,

  /* Compute Commands. */
  DRW_CMD_COMPUTE = 8,
  DRW_CMD_COMPUTE_REF = 9,
  DRW_CMD_COMPUTE_INDIRECT = 10,

  /* Other Commands */
  DRW_CMD_BARRIER = 11,
  DRW_CMD_CLEAR = 12,
  DRW_CMD_DRWSTATE = 13,
  DRW_CMD_STENCIL = 14,
  DRW_CMD_SELECTID = 15,
  /* Needs to fit in 4bits */
} eDRWCommandType;

#define DRW_MAX_DRAW_CMD_TYPE DRW_CMD_DRAW_INDIRECT

struct DRWCommandDraw {
  blender::gpu::Batch *batch;
  DRWResourceHandle handle;
};

/* Assume DRWResourceHandle to be 0. */
struct DRWCommandDrawRange {
  blender::gpu::Batch *batch;
  DRWResourceHandle handle;
  uint vert_first;
  uint vert_count;
};

struct DRWCommandDrawInstance {
  blender::gpu::Batch *batch;
  DRWResourceHandle handle;
  uint inst_count;
  uint use_attrs; /* bool */
};

struct DRWCommandDrawInstanceRange {
  blender::gpu::Batch *batch;
  DRWResourceHandle handle;
  uint inst_first;
  uint inst_count;
};

struct DRWCommandDrawIndirect {
  blender::gpu::Batch *batch;
  DRWResourceHandle handle;
  GPUStorageBuf *indirect_buf;
};

struct DRWCommandCompute {
  int groups_x_len;
  int groups_y_len;
  int groups_z_len;
};

struct DRWCommandComputeRef {
  int *groups_ref;
};

struct DRWCommandComputeIndirect {
  GPUStorageBuf *indirect_buf;
};

struct DRWCommandBarrier {
  eGPUBarrier type;
};

struct DRWCommandDrawProcedural {
  blender::gpu::Batch *batch;
  DRWResourceHandle handle;
  uint vert_count;
};

struct DRWCommandSetMutableState {
  /** State changes (or'd or and'd with the pass's state) */
  DRWState enable;
  DRWState disable;
};

struct DRWCommandSetStencil {
  uint write_mask;
  uint comp_mask;
  uint ref;
};

struct DRWCommandSetSelectID {
  blender::gpu::VertBuf *select_buf;
  uint select_id;
};

struct DRWCommandClear {
  eGPUFrameBufferBits clear_channels;
  uchar r, g, b, a; /* [0..1] for each channels. Normalized. */
  float depth;      /* [0..1] for depth. Normalized. */
  uchar stencil;    /* Stencil value [0..255] */
};

union DRWCommand {
  DRWCommandDraw draw;
  DRWCommandDrawRange range;
  DRWCommandDrawInstance instance;
  DRWCommandDrawInstanceRange instance_range;
  DRWCommandDrawProcedural procedural;
  DRWCommandDrawIndirect draw_indirect;
  DRWCommandCompute compute;
  DRWCommandComputeRef compute_ref;
  DRWCommandComputeIndirect compute_indirect;
  DRWCommandBarrier barrier;
  DRWCommandSetMutableState state;
  DRWCommandSetStencil stencil;
  DRWCommandSetSelectID select_id;
  DRWCommandClear clear;
};

/** Used for aggregating calls into #blender::gpu::VertBuf's. */
struct DRWCallBuffer {
  blender::gpu::VertBuf *buf;
  blender::gpu::VertBuf *buf_select;
  int count;
};

/** Used by #DRWUniform.type */
/* TODO(@jbakker): rename to DRW_RESOURCE/DRWResourceType. */
typedef enum {
  DRW_UNIFORM_INT = 0,
  DRW_UNIFORM_INT_COPY,
  DRW_UNIFORM_FLOAT,
  DRW_UNIFORM_FLOAT_COPY,
  DRW_UNIFORM_TEXTURE,
  DRW_UNIFORM_TEXTURE_REF,
  DRW_UNIFORM_IMAGE,
  DRW_UNIFORM_IMAGE_REF,
  DRW_UNIFORM_BLOCK,
  DRW_UNIFORM_BLOCK_REF,
  DRW_UNIFORM_STORAGE_BLOCK,
  DRW_UNIFORM_STORAGE_BLOCK_REF,
  DRW_UNIFORM_VERTEX_BUFFER_AS_TEXTURE,
  DRW_UNIFORM_VERTEX_BUFFER_AS_TEXTURE_REF,
  DRW_UNIFORM_VERTEX_BUFFER_AS_STORAGE,
  DRW_UNIFORM_VERTEX_BUFFER_AS_STORAGE_REF,
  /** Per drawcall uniforms/UBO */
  DRW_UNIFORM_BLOCK_OBMATS,
  DRW_UNIFORM_BLOCK_OBINFOS,
  DRW_UNIFORM_BLOCK_OBATTRS,
  DRW_UNIFORM_BLOCK_VLATTRS,
  DRW_UNIFORM_RESOURCE_CHUNK,
  DRW_UNIFORM_RESOURCE_ID,
  /** Legacy / Fallback */
  DRW_UNIFORM_BASE_INSTANCE,
  DRW_UNIFORM_MODEL_MATRIX,
  DRW_UNIFORM_MODEL_MATRIX_INVERSE,
  /* WARNING: set DRWUniform->type
   * bit length accordingly. */
} DRWUniformType;

struct DRWUniform {
  union {
    /* For reference or array/vector types. */
    const void *pvalue;
    /* DRW_UNIFORM_TEXTURE */
    struct {
      union {
        GPUTexture *texture;
        GPUTexture **texture_ref;
      };
      GPUSamplerState sampler_state;
    };
    /* DRW_UNIFORM_BLOCK */
    union {
      GPUUniformBuf *block;
      GPUUniformBuf **block_ref;
    };
    /* DRW_UNIFORM_STORAGE_BLOCK */
    union {
      GPUStorageBuf *ssbo;
      GPUStorageBuf **ssbo_ref;
    };
    /* DRW_UNIFORM_VERTEX_BUFFER_AS_STORAGE */
    union {
      blender::gpu::VertBuf *vertbuf;
      blender::gpu::VertBuf **vertbuf_ref;
    };
    /* DRW_UNIFORM_FLOAT_COPY */
    float fvalue[4];
    /* DRW_UNIFORM_INT_COPY */
    int ivalue[4];
    /* DRW_UNIFORM_BLOCK_OBATTRS */
    const struct GPUUniformAttrList *uniform_attrs;
  };
  int location;      /* Uniform location or binding point for textures and UBO's. */
  uint8_t type;      /* #DRWUniformType */
  uint8_t length;    /* Length of vector types. */
  uint8_t arraysize; /* Array size of scalar/vector types. */
};

struct DRWShadingGroup {
  DRWShadingGroup *next;

  GPUShader *shader;         /* Shader to bind */
  DRWUniformChunk *uniforms; /* Uniforms pointers */

  struct {
    /* Chunks of draw calls. */
    struct DRWCommandChunk *first, *last;
  } cmd;

  union {
    /* This struct is used during cache populate. */
    struct {
      int objectinfo;                /* Equal to 1 if the shader needs obinfos. */
      DRWResourceHandle pass_handle; /* Memblock key to parent pass. */

      /* Set of uniform attributes used by this shader. */
      const struct GPUUniformAttrList *uniform_attrs;
    };
    /* This struct is used after cache populate if using the Z sorting.
     * It will not conflict with the above struct. */
    struct {
      float distance;      /* Distance from camera. */
      uint original_index; /* Original position inside the shgroup list. */
    } z_sorting;
  };
};

#define MAX_PASS_NAME 32

struct DRWPass {
  /* Linked list */
  struct {
    DRWShadingGroup *first;
    DRWShadingGroup *last;
  } shgroups;

  /* Draw the shgroups of this pass instead.
   * This avoid duplicating drawcalls/shgroups
   * for similar passes. */
  DRWPass *original;
  /* Link list of additional passes to render. */
  DRWPass *next;

  DRWResourceHandle handle;
  DRWState state;
  char name[MAX_PASS_NAME];
};

#define MAX_CULLED_VIEWS 32

struct DRWView {
  /**
   * These float4x4 (as well as the ViewMatrices) have alignment requirements in C++
   * (see math::MatBase) that isn't fulfilled in C. So they need to be manually aligned.
   * Since the DRWView are allocated using BLI_memblock, the chunks are given to be 16 bytes
   * aligned (equal to the alignment of float4x4). We then assert that the DRWView itself is 16
   * bytes aligned.
   */
  float4x4 persmat;
  float4x4 persinv;
  ViewMatrices storage;

  /** Parent view if this is a sub view. nullptr otherwise. */
  DRWView *parent;

  float4 clip_planes[6];

  /** Number of active clip planes. */
  int clip_planes_len;
  /** Does culling result needs to be updated. */
  bool is_dirty;
  /** Does facing needs to be reversed? */
  bool is_inverted;
  /** Culling */
  uint32_t culling_mask;
  BoundBox frustum_corners;
  BoundSphere frustum_bsphere;
  float frustum_planes[6][4];
  /** Custom visibility function. */
  DRWCallVisibilityFn *visibility_fn;
  void *user_data;
};
/* Needed to assert that alignment is the same in C++ and C. */
BLI_STATIC_ASSERT_ALIGN(DRWView, 16);

/** \} */

/* -------------------------------------------------------------------- */
/** \name Data Chunks
 *
 * In order to keep a cache friendly data structure,
 * we allocate most of our little data into chunks of multiple item.
 * Iteration, allocation and memory usage are better.
 * We lose a bit of memory by allocating more than what we need
 * but it's counterbalanced by not needing the linked-list pointers
 * for each item.
 * \{ */

struct DRWUniformChunk {
  DRWUniformChunk *next; /* single-linked list */
  uint32_t uniform_len;
  uint32_t uniform_used;
  DRWUniform uniforms[10];
};

struct DRWCommandChunk {
  DRWCommandChunk *next;
  uint32_t command_len;
  uint32_t command_used;
  /* 4bits for each command. */
  uint64_t command_type[6];
  /* -- 64 bytes aligned -- */
  DRWCommand commands[96];
  /* -- 64 bytes aligned -- */
};

struct DRWCommandSmallChunk {
  DRWCommandChunk *next;
  uint32_t command_len;
  uint32_t command_used;
  /* 4bits for each command. */
  /* TODO: reduce size of command_type. */
  uint64_t command_type[6];
  DRWCommand commands[6];
};

/* Only true for 64-bit platforms. */
#ifdef __LP64__
BLI_STATIC_ASSERT_ALIGN(DRWCommandChunk, 16);
#endif

/* -------------------------------------------------------------------- */
/** \name Memory Pools
 * \{ */

/** Contains memory pools information. */
struct DRWData {
  /** Instance data. */
  DRWInstanceDataList *idatalist;
  /** Memory-pools for draw-calls. */
  BLI_memblock *commands;
  BLI_memblock *commands_small;
  BLI_memblock *callbuffers;
  BLI_memblock *obmats;
  BLI_memblock *obinfos;
  BLI_memblock *cullstates;
  BLI_memblock *shgroups;
  BLI_memblock *uniforms;
  BLI_memblock *views;
  BLI_memblock *passes;
  BLI_memblock *images;
  GPUUniformBuf **matrices_ubo;
  GPUUniformBuf **obinfos_ubo;
  GHash *obattrs_ubo_pool;
  GHash *vlattrs_name_cache;
  ListBase vlattrs_name_list;
  LayerAttribute *vlattrs_buf;
  GPUUniformBuf *vlattrs_ubo;
  bool vlattrs_ubo_ready;
  uint ubo_len;
  /** List of smoke textures to free after drawing. */
  ListBase smoke_textures;
  /**
   * Texture pool to reuse temp texture across engines.
   * TODO(@fclem): The pool could be shared even between view-ports.
   */
  DRWTexturePool *texture_pool;
  /** Per stereo view data. Contains engine data and default frame-buffers. */
  DRWViewData *view_data[2];
  /** Module storage. */
  blender::draw::CurvesModule *curves_module;
  blender::draw::VolumeModule *volume_module;
  blender::draw::PointCloudModule *pointcloud_module;
  /** Default view that feeds every engine. */
  blender::draw::View *default_view;

  /* Ensure modules are created. */
  void modules_init();
  /* Callbacks after one draw to clear transient data. */
  void modules_exit();
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Functions
 * \{ */

/* Get thread local draw context. */
DRWContext &drw_get();

eDRWCommandType command_type_get(const uint64_t *command_type_bits, int index);

void drw_state_set(DRWState state);

void drw_batch_cache_validate(Object *ob);
void drw_batch_cache_generate_requested(Object *ob, TaskGraph &task_graph);

/**
 * \warning Only evaluated mesh data is handled by this delayed generation.
 */
void drw_batch_cache_generate_requested_delayed(Object *ob);
void drw_batch_cache_generate_requested_evaluated_mesh_or_curve(Object *ob, TaskGraph &task_graph);

void drw_resource_buffer_finish(DRWData *data);

 /* Procedural Drawing */
blender::gpu::Batch *drw_cache_procedural_triangles_get();

void drw_uniform_attrs_pool_update(GHash *table,
                                   const GPUUniformAttrList *key,
                                   DRWResourceHandle *handle,
                                   const Object *ob,
                                   const Object *dupli_parent,
                                   const DupliObject *dupli_source);

GPUUniformBuf *drw_ensure_layer_attribute_buffer();

namespace blender::draw {

void DRW_mesh_get_attributes(const Object &object,
                             const Mesh &mesh,
                             Span<const GPUMaterial *> materials,
                             DRW_Attributes *r_attrs,
                             DRW_MeshCDMask *r_cd_needed);

void GOO_mesh_get_attributes(const Object &object,
                             const Mesh &mesh,
                             const GPUMaterial *const *gpumat_array,
                             int gpumat_array_len,
                             DRW_Attributes *r_attrs,
                             DRW_MeshCDMask *r_cd_needed);

}  // namespace blender::draw

/** \} */
