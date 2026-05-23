/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw
 */

/* This is the Render Functions used by Realtime engines to draw with OpenGL */

#pragma once

#include <functional>

#include "BLI_math_vector_types.hh"
#include "DNA_object_enums.h"
#include "DNA_object_types.h"

#include "BLI_math_matrix.h"
#include "BLI_string.h"

#include "BKE_context.hh"
#include "BKE_layer.hh"
#include "BKE_material.hh"

#include "DNA_light_types.h"
#include "DNA_material_types.h"
#include "DNA_world_types.h"

#include "GPU_framebuffer.hh"
#include "GPU_material.hh"
#include "GPU_primitive.hh"
#include "GPU_storage_buffer.hh"
#include "GPU_uniform_buffer.hh"
#include "GPU_drawlist.hh"

#include "draw_cache.hh"
#include "draw_common_c.hh"

#include "draw_instance_data.hh"
#include "draw_state.hh"
// #include "draw_view_data.hh"

// #include "RE_engine.h"

/* Uncomment to track unused resource bindings. */
// #define DRW_UNUSED_RESOURCE_TRACKING

#ifdef DRW_UNUSED_RESOURCE_TRACKING
#  define DRW_DEBUG_FILE_LINE_ARGS , const char *file, int line
#else
#  define DRW_DEBUG_FILE_LINE_ARGS
#endif

namespace blender::gpu {
class Batch;
}
struct ARegion;
struct bContext;
struct Depsgraph;
struct DefaultFramebufferList;
struct DefaultTextureList;
struct DupliObject;
struct GPUMaterial;
struct GPUShader;
struct GPUTexture;
struct GPUUniformBuf;
struct Mesh;
struct Object;
struct ParticleSystem;
struct RenderEngineType;
struct bContext;
struct rcti;
struct RegionView3D;
struct RenderEngine;
struct RenderEngineType;
struct RenderLayer;
struct RenderResult;
struct SpaceLink;
struct TaskGraph;
struct View3D;
struct ViewLayer;
struct DRWContext;
struct World;
struct DRWData;
struct DRWViewData;
struct GPUViewport;
struct GPUFrameBuffer;
struct DRWTextStore;
struct GSet;
struct GPUViewport;
namespace blender::draw {
class TextureFromPool;
struct ObjectRef;
class Manager;
}  // namespace blender::draw

typedef struct DRWCallBuffer DRWCallBuffer;
typedef struct DRWPass DRWPass;
typedef struct DRWShaderLibrary DRWShaderLibrary;
typedef struct DRWShadingGroup DRWShadingGroup;
typedef struct DRWUniform DRWUniform;
typedef struct DRWView DRWView;

/* TODO: Put it somewhere else? */
struct BoundSphere {
  float center[3], radius;
};

struct DrawEngine {
  static constexpr int GPU_INFO_SIZE = 512; /* IMA_MAX_RENDER_TEXT_SIZE */

  char info[GPU_INFO_SIZE] = {'\0'};
  DRWTextStore *text_draw_cache = nullptr;

  bool used = false;

  virtual ~DrawEngine() = default;

  virtual blender::StringRefNull name_get() = 0;

  /* Functions called for viewport. */

  /* Init engine. Run first and for every redraw. */
  virtual void init() = 0;
  /* Scene synchronization. Command buffers building. */
  virtual void begin_sync() = 0;
  virtual void object_sync(blender::draw::ObjectRef &ob_ref, blender::draw::Manager &manager) = 0;
  virtual void end_sync() = 0;
  /* Command Submission. */
  virtual void draw(blender::draw::Manager &manager) = 0;

  /* Called when closing blender.
   * Cleanup all lazily initialized static members that have GPU resources.
   * Implemented on a case by case basis and called directly. */
  //  static void exit(){};

  struct Pointer {
    DrawEngine *instance = nullptr;

    ~Pointer()
    {
      free_instance();
    }

    void free_instance()
    {
      delete instance;
      instance = nullptr;
    }

    void set_used(bool used)
    {
      if (used) {
        if (instance == nullptr) {
          instance = create_instance();
        }
        instance->used = true;
      }
      else if (instance) {
        instance->used = false;
      }
    }

    virtual DrawEngine *create_instance() = 0;
  };
};

/* Textures */
enum DRWTextureFlag {
  DRW_TEX_FILTER = (1 << 0),
  DRW_TEX_WRAP = (1 << 1),
  DRW_TEX_COMPARE = (1 << 2),
  DRW_TEX_MIPMAP = (1 << 3),
};

/**
 * Textures from `DRW_texture_pool_query_*` have the options
 * #DRW_TEX_FILTER for color float textures, and no options
 * for depth textures and integer textures.
 */
GPUTexture *DRW_texture_pool_query_2d(int w,
                                      int h,
                                      eGPUTextureFormat format,
                                      int id);

GPUTexture *DRW_texture_create_2d(
    int w, int h, eGPUTextureFormat format, DRWTextureFlag flags, const float *fpixels);
GPUTexture *DRW_texture_create_2d_array(
    int w, int h, int d, eGPUTextureFormat format, DRWTextureFlag flags, const float *fpixels);

void DRW_texture_ensure_fullscreen_2d(GPUTexture **tex,
                                      eGPUTextureFormat format,
                                      DRWTextureFlag flags);
void DRW_texture_ensure_2d(
    GPUTexture **tex, int w, int h, eGPUTextureFormat format, DRWTextureFlag flags);

/* Explicit parameter variants. */
GPUTexture *DRW_texture_pool_query_2d_ex(
    int w, int h, eGPUTextureFormat format, eGPUTextureUsage usage, int id);

GPUTexture *DRW_texture_create_2d_ex(int w,
                                     int h,
                                     eGPUTextureFormat format,
                                     eGPUTextureUsage usage_flags,
                                     DRWTextureFlag flags,
                                     const float *fpixels);
GPUTexture *DRW_texture_create_2d_array_ex(int w,
                                           int h,
                                           int d,
                                           eGPUTextureFormat format,
                                           eGPUTextureUsage usage_flags,
                                           DRWTextureFlag flags,
                                           const float *fpixels);
GPUTexture *DRW_texture_create_3d_ex(int w,
                                     int h,
                                     int d,
                                     eGPUTextureFormat format,
                                     eGPUTextureUsage usage_flags,
                                     DRWTextureFlag flags,
                                     const float *fpixels);
GPUTexture *DRW_texture_create_cube_ex(int w,
                                       eGPUTextureFormat format,
                                       eGPUTextureUsage usage_flags,
                                       DRWTextureFlag flags,
                                       const float *fpixels);
GPUTexture *DRW_texture_create_cube_array_ex(int w,
                                             int d,
                                             eGPUTextureFormat format,
                                             eGPUTextureUsage usage_flags,
                                             DRWTextureFlag flags,
                                             const float *fpixels);

void DRW_texture_ensure_fullscreen_2d_ex(GPUTexture **tex,
                                         eGPUTextureFormat format,
                                         eGPUTextureUsage usage,
                                         DRWTextureFlag flags);
void DRW_texture_ensure_2d_ex(GPUTexture **tex,
                              int w,
                              int h,
                              eGPUTextureFormat format,
                              eGPUTextureUsage usage,
                              DRWTextureFlag flags);
void DRW_texture_free(GPUTexture *tex);
#define DRW_TEXTURE_FREE_SAFE(tex) \
  do { \
    if (tex != nullptr) { \
      DRW_texture_free(tex); \
      tex = nullptr; \
    } \
  } while (0)

/* Shaders */
GPUShader *DRW_shader_create_from_info_name(const char *info_name);
/** IMPORTANT: Modify the currently bound context. */
void DRW_shader_init();
void DRW_shader_exit();

GPUMaterial *DRW_shader_from_world(World *wo,
                                   bNodeTree *ntree,
                                   eGPUMaterialEngine engine,
                                   const uint64_t shader_id,
                                   const bool is_volume_shader,
                                   bool deferred,
                                   GPUCodegenCallbackFn callback,
                                   void *thunk);
GPUMaterial *DRW_shader_from_material(
    Material *ma,
    bNodeTree *ntree,
    eGPUMaterialEngine engine,
    const uint64_t shader_id,
    const bool is_volume_shader,
    bool deferred,
    GPUCodegenCallbackFn callback,
    void *thunk,
    GPUMaterialPassReplacementCallbackFn pass_replacement_cb = nullptr);
void DRW_shader_queue_optimize_material(GPUMaterial *mat);

DRWShaderLibrary *DRW_shader_library_create();

/**
 * \warning Each library must be added after all its dependencies.
 */
void DRW_shader_library_add_file(DRWShaderLibrary *lib,
                                 const char *lib_code,
                                 const char *lib_name);
#define DRW_SHADER_LIB_ADD(lib, lib_name) \
  DRW_shader_library_add_file(lib, datatoc_##lib_name##_glsl, STRINGIFY(lib_name) ".glsl")

#define DRW_SHADER_LIB_ADD_SHARED(lib, lib_name) \
  DRW_shader_library_add_file(lib, datatoc_##lib_name##_hh, STRINGIFY(lib_name) ".hh")

/**
 * \return an allocN'ed string containing the shader code with its dependencies prepended.
 * Caller must free the string with #MEM_freeN after use.
 */
char *DRW_shader_library_create_shader_string(const DRWShaderLibrary *lib,
                                              const char *shader_code);

void DRW_shader_library_free(DRWShaderLibrary *lib);
#define DRW_SHADER_LIB_FREE_SAFE(lib) \
  do { \
    if (lib != nullptr) { \
      DRW_shader_library_free(lib); \
      lib = nullptr; \
    } \
  } while (0)

/* Batches */

enum eDRWAttrType {
  DRW_ATTR_INT,
  DRW_ATTR_FLOAT,
};
struct DRWInstanceAttrFormat {
  char name[32];
  eDRWAttrType type;
  int components;
};

GPUVertFormat *DRW_shgroup_instance_format_array(const DRWInstanceAttrFormat attrs[],
                                                 int arraysize);
#define DRW_shgroup_instance_format(format, ...) \
  do { \
    if (format == nullptr) { \
      DRWInstanceAttrFormat drw_format[] = __VA_ARGS__; \
      format = DRW_shgroup_instance_format_array( \
          drw_format, (sizeof(drw_format) / sizeof(DRWInstanceAttrFormat))); \
    } \
  } while (0)

DRWShadingGroup *DRW_shgroup_create(GPUShader *shader, DRWPass *pass);
DRWShadingGroup *DRW_shgroup_create_sub(DRWShadingGroup *shgroup);
DRWShadingGroup *DRW_shgroup_material_create(GPUMaterial *material, DRWPass *pass);

void DRW_shgroup_add_material_resources(DRWShadingGroup *grp, GPUMaterial *material);

/**
 * Return final visibility.
 */
typedef bool(DRWCallVisibilityFn)(bool vis_in, void *user_data);

void DRW_shgroup_call_ex(DRWShadingGroup *shgroup,
                         const Object *ob,
                         const float (*obmat)[4],
                         blender::gpu::Batch *geom,
                         bool bypass_culling,
                         void *user_data);

/**
 * If ob is nullptr, unit model-matrix is assumed and culling is bypassed.
 */
#define DRW_shgroup_call(shgroup, geom, ob) \
  DRW_shgroup_call_ex(shgroup, ob, nullptr, geom, false, nullptr)

/* TODO(fclem): remove this when we have #DRWView */
/* user_data is used by #DRWCallVisibilityFn defined in #DRWView. */
#define DRW_shgroup_call_with_callback(shgroup, geom, ob, user_data) \
  DRW_shgroup_call_ex(shgroup, ob, nullptr, geom, false, user_data)

/**
 * Same as #DRW_shgroup_call but bypass culling even if ob is not nullptr.
 */
#define DRW_shgroup_call_no_cull(shgroup, geom, ob) \
  DRW_shgroup_call_ex(shgroup, ob, nullptr, geom, true, nullptr)

/**
 * \note No need for a barrier. \a indirect_buf is internally synchronized.
 */
void DRW_shgroup_call_procedural_triangles(DRWShadingGroup *sh, const Object *ob, uint tri_count);
/**
 * \warning Only use with Shaders that have `IN_PLACE_INSTANCES` defined.
 * TODO: Should be removed.
 */
void DRW_shgroup_call_instances(DRWShadingGroup *shgroup,
                                const Object *ob,
                                blender::gpu::Batch *geom,
                                uint count);

void DRW_shgroup_call_sculpt_with_materials(DRWShadingGroup **shgroups,
                                            GPUMaterial **gpumats,
                                            int num_shgroups,
                                            const Object *ob);

DRWCallBuffer *DRW_shgroup_call_buffer_instance(DRWShadingGroup *shgroup,
                                                GPUVertFormat *format,
                                                blender::gpu::Batch *geom);

void DRW_buffer_add_entry_array(DRWCallBuffer *callbuf, const void *attr[], uint attr_len);

#define DRW_buffer_add_entry(buffer, ...) \
  do { \
    const void *array[] = {__VA_ARGS__}; \
    DRW_buffer_add_entry_array(buffer, array, (sizeof(array) / sizeof(*array))); \
  } while (0)

/**
 * State is added to #Pass.state while drawing.
 * Use to temporarily enable draw options.
 */
void DRW_shgroup_state_enable(DRWShadingGroup *shgroup, DRWState state);
void DRW_shgroup_state_disable(DRWShadingGroup *shgroup, DRWState state);

/**
 * TODO: remove this function. Obsolete version. mask is actually reference value.
 */
void DRW_shgroup_stencil_mask(DRWShadingGroup *shgroup, uint mask);

void DRW_shgroup_uniform_texture_ex(DRWShadingGroup *shgroup,
                                    const char *name,
                                    const GPUTexture *tex,
                                    GPUSamplerState sampler_state);
void DRW_shgroup_uniform_texture_ref_ex(DRWShadingGroup *shgroup,
                                        const char *name,
                                        GPUTexture **tex,
                                        GPUSamplerState sampler_state);
void DRW_shgroup_uniform_texture(DRWShadingGroup *shgroup,
                                 const char *name,
                                 const GPUTexture *tex);
void DRW_shgroup_uniform_texture_ref(DRWShadingGroup *shgroup, const char *name, GPUTexture **tex);
void DRW_shgroup_uniform_block_ex(DRWShadingGroup *shgroup,
                                  const char *name,
                                  const GPUUniformBuf *ubo DRW_DEBUG_FILE_LINE_ARGS);
void DRW_shgroup_uniform_block_ref_ex(DRWShadingGroup *shgroup,
                                      const char *name,
                                      GPUUniformBuf **ubo DRW_DEBUG_FILE_LINE_ARGS);
void DRW_shgroup_storage_block_ex(DRWShadingGroup *shgroup,
                                  const char *name,
                                  const GPUStorageBuf *ssbo DRW_DEBUG_FILE_LINE_ARGS);
void DRW_shgroup_storage_block_ref_ex(DRWShadingGroup *shgroup,
                                      const char *name,
                                      GPUStorageBuf **ssbo DRW_DEBUG_FILE_LINE_ARGS);
void DRW_shgroup_uniform_float(DRWShadingGroup *shgroup,
                               const char *name,
                               const float *value,
                               int arraysize);
void DRW_shgroup_uniform_vec2(DRWShadingGroup *shgroup,
                              const char *name,
                              const float *value,
                              int arraysize);
void DRW_shgroup_uniform_vec3(DRWShadingGroup *shgroup,
                              const char *name,
                              const float *value,
                              int arraysize);
void DRW_shgroup_uniform_vec4(DRWShadingGroup *shgroup,
                              const char *name,
                              const float *value,
                              int arraysize);
void DRW_shgroup_uniform_bool(DRWShadingGroup *shgroup,
                              const char *name,
                              const int *value,
                              int arraysize);
void DRW_shgroup_uniform_int(DRWShadingGroup *shgroup,
                             const char *name,
                             const int *value,
                             int arraysize);
void DRW_shgroup_uniform_ivec2(DRWShadingGroup *shgroup,
                               const char *name,
                               const int *value,
                               int arraysize);
void DRW_shgroup_uniform_ivec3(DRWShadingGroup *shgroup,
                               const char *name,
                               const int *value,
                               int arraysize);
void DRW_shgroup_uniform_ivec4(DRWShadingGroup *shgroup,
                               const char *name,
                               const int *value,
                               int arraysize);
void DRW_shgroup_uniform_mat3(DRWShadingGroup *shgroup, const char *name, const float (*value)[3]);
void DRW_shgroup_uniform_mat4(DRWShadingGroup *shgroup, const char *name, const float (*value)[4]);
void DRW_shgroup_uniform_image(DRWShadingGroup *shgroup, const char *name, const GPUTexture *tex);
void DRW_shgroup_uniform_image_ref(DRWShadingGroup *shgroup, const char *name, GPUTexture **tex);

/* Store value instead of referencing it. */

void DRW_shgroup_uniform_int_copy(DRWShadingGroup *shgroup, const char *name, int value);
void DRW_shgroup_uniform_ivec2_copy(DRWShadingGroup *shgroup, const char *name, const int *value);
void DRW_shgroup_uniform_ivec3_copy(DRWShadingGroup *shgroup, const char *name, const int *value);
void DRW_shgroup_uniform_ivec4_copy(DRWShadingGroup *shgroup, const char *name, const int *value);
void DRW_shgroup_uniform_bool_copy(DRWShadingGroup *shgroup, const char *name, bool value);
void DRW_shgroup_uniform_float_copy(DRWShadingGroup *shgroup, const char *name, float value);
void DRW_shgroup_uniform_vec2_copy(DRWShadingGroup *shgroup, const char *name, const float *value);
void DRW_shgroup_uniform_vec3_copy(DRWShadingGroup *shgroup, const char *name, const float *value);
void DRW_shgroup_uniform_vec4_copy(DRWShadingGroup *shgroup, const char *name, const float *value);
void DRW_shgroup_uniform_mat4_copy(DRWShadingGroup *shgroup,
                                   const char *name,
                                   const float (*value)[4]);
void DRW_shgroup_vertex_buffer_ex(DRWShadingGroup *shgroup,
                                  const char *name,
                                  blender::gpu::VertBuf *vertex_buffer DRW_DEBUG_FILE_LINE_ARGS);
void DRW_shgroup_vertex_buffer_ref_ex(DRWShadingGroup *shgroup,
                                      const char *name,
                                      blender::gpu::VertBuf **vertex_buffer
                                          DRW_DEBUG_FILE_LINE_ARGS);
void DRW_shgroup_buffer_texture(DRWShadingGroup *shgroup,
                                const char *name,
                                blender::gpu::VertBuf *vertex_buffer);

#ifdef DRW_UNUSED_RESOURCE_TRACKING
#  define DRW_shgroup_vertex_buffer(shgroup, name, vert) \
    DRW_shgroup_vertex_buffer_ex(shgroup, name, vert, __FILE__, __LINE__)
#  define DRW_shgroup_vertex_buffer_ref(shgroup, name, vert) \
    DRW_shgroup_vertex_buffer_ref_ex(shgroup, name, vert, __FILE__, __LINE__)
#  define DRW_shgroup_uniform_block(shgroup, name, ubo) \
    DRW_shgroup_uniform_block_ex(shgroup, name, ubo, __FILE__, __LINE__)
#  define DRW_shgroup_uniform_block_ref(shgroup, name, ubo) \
    DRW_shgroup_uniform_block_ref_ex(shgroup, name, ubo, __FILE__, __LINE__)
#  define DRW_shgroup_storage_block(shgroup, name, ssbo) \
    DRW_shgroup_storage_block_ex(shgroup, name, ssbo, __FILE__, __LINE__)
#  define DRW_shgroup_storage_block_ref(shgroup, name, ssbo) \
    DRW_shgroup_storage_block_ref_ex(shgroup, name, ssbo, __FILE__, __LINE__)
#else
#  define DRW_shgroup_vertex_buffer(shgroup, name, vert) \
    DRW_shgroup_vertex_buffer_ex(shgroup, name, vert)
#  define DRW_shgroup_vertex_buffer_ref(shgroup, name, vert) \
    DRW_shgroup_vertex_buffer_ref_ex(shgroup, name, vert)
#  define DRW_shgroup_uniform_block(shgroup, name, ubo) \
    DRW_shgroup_uniform_block_ex(shgroup, name, ubo)
#  define DRW_shgroup_uniform_block_ref(shgroup, name, ubo) \
    DRW_shgroup_uniform_block_ref_ex(shgroup, name, ubo)
#  define DRW_shgroup_storage_block(shgroup, name, ssbo) \
    DRW_shgroup_storage_block_ex(shgroup, name, ssbo)
#  define DRW_shgroup_storage_block_ref(shgroup, name, ssbo) \
    DRW_shgroup_storage_block_ref_ex(shgroup, name, ssbo)
#endif

bool DRW_shgroup_is_empty(DRWShadingGroup *shgroup);

/* Passes. */

DRWPass *DRW_pass_create(const char *name, DRWState state);
/**
 * Create an instance of the original pass that will execute the same drawcalls but with its own
 * #DRWState.
 */
DRWPass *DRW_pass_create_instance(const char *name, DRWPass *original, DRWState state);
/**
 * Link two passes so that they are both rendered if the first one is being drawn.
 */
void DRW_pass_link(DRWPass *first, DRWPass *second);
/**
 * Sort Shading groups by decreasing Z of their first draw call.
 * This is useful for order dependent effect such as alpha-blending.
 */
void DRW_pass_sort_shgroup_z(DRWPass *pass);

bool DRW_pass_is_empty(DRWPass *pass);

#define DRW_PASS_CREATE(pass, state) (pass = DRW_pass_create(#pass, state))
#define DRW_PASS_INSTANCE_CREATE(pass, original, state) \
  (pass = DRW_pass_create_instance(#pass, (original), state))

/* Views. */

/**
 * Create a view with culling.
 */
DRWView *DRW_view_create(const float viewmat[4][4],
                         const float winmat[4][4],
                         const float (*culling_viewmat)[4],
                         const float (*culling_winmat)[4],
                         DRWCallVisibilityFn *visibility_fn);
/**
 * Create a view with culling done by another view.
 */
DRWView *DRW_view_create_sub(const DRWView *parent_view,
                             const float viewmat[4][4],
                             const float winmat[4][4]);

/**
 * Update matrices of a view created with #DRW_view_create.
 */
void DRW_view_update(DRWView *view,
                     const float viewmat[4][4],
                     const float winmat[4][4],
                     const float (*culling_viewmat)[4],
                     const float (*culling_winmat)[4]);
/**
 * Update matrices of a view created with #DRW_view_create_sub.
 */
void DRW_view_update_sub(DRWView *view, const float viewmat[4][4], const float winmat[4][4]);

/**
 * \return default view if it is a viewport render.
 */
const DRWView *DRW_view_default_get();
/**
 * MUST only be called once per render and only in render mode. Sets default view.
 */
void DRW_view_default_set(const DRWView *view);
/**
 * \warning Only use in render AND only if you are going to set view_default again.
 */
void DRW_view_reset();
/**
 * Set active view for rendering.
 */
void DRW_view_set_active(const DRWView *view);

/**
 * This only works if DRWPasses have been tagged with DRW_STATE_CLIP_PLANES,
 * and if the shaders have support for it (see usage of gl_ClipDistance).
 * \note planes must be in world space.
 */
void DRW_view_clip_planes_set(DRWView *view, float (*planes)[4], int plane_len);

/* For all getters, if view is nullptr, default view is assumed. */

void DRW_view_winmat_get(const DRWView *view, float mat[4][4], bool inverse);
void DRW_view_viewmat_get(const DRWView *view, float mat[4][4], bool inverse);
void DRW_view_persmat_get(const DRWView *view, float mat[4][4], bool inverse);

/**
 * \return world space frustum sides as planes.
 * See #draw_frustum_culling_planes_calc() for the plane order.
 */
std::array<blender::float4, 6> DRW_view_frustum_planes_get(const DRWView *view);

/**
 * These are in view-space, so negative if in perspective.
 * Extract near and far clip distance from the projection matrix.
 */
float DRW_view_near_distance_get(const DRWView *view);
float DRW_view_far_distance_get(const DRWView *view);
bool DRW_view_is_persp_get(const DRWView *view);

/* Culling, return true if object is inside view frustum. */

/**
 * \return True if the given BoundSphere intersect the current view frustum.
 * bsphere must be in world space.
 */
bool DRW_culling_sphere_test(const DRWView *view, const BoundSphere *bsphere);
/**
 * \return True if the given BoundBox intersect the current view frustum.
 * bbox must be in world space.
 */
bool DRW_culling_box_test(const DRWView *view, const BoundBox *bbox);

/* Viewport. */

/* Returns a TextureFromPool stored in the given view data for the pass identified by the given
 * pass name. Engines should call this function for each of the passes needed by the viewport
 * compositor in every redraw, then it should allocate the texture and write the pass data to it.
 * The texture should cover the entire viewport. */
blender::draw::TextureFromPool &DRW_viewport_pass_texture_get(const char *pass_name);

void DRW_viewport_request_redraw();

void DRW_render_to_image(
    RenderEngine *engine,
    Depsgraph *depsgraph,
    std::function<void(RenderEngine *, RenderLayer *, const rcti)> render_view_cb,
    std::function<void(RenderResult *)> store_metadata_cb);

void DRW_render_object_iter(
    RenderEngine *engine,
    Depsgraph *depsgraph,
    std::function<void(blender::draw::ObjectRef &, RenderEngine *, Depsgraph *)>);

/**
 * Must run after all instance datas have been added.
 */
void DRW_render_instance_buffer_finish();
/**
 * \warning Changing frame might free the #ViewLayerEngineData.
 */
void DRW_render_set_time(RenderEngine *engine, Depsgraph *depsgraph, int frame, float subframe);
/**
 * \warning only use for custom pipeline. 99% of the time, you don't want to use this.
 */
void DRW_render_viewport_size_set(const int size[2]);

/**
 * Assume a valid GL context is bound (and that the gl_context_mutex has been acquired).
 * This function only setup DST and execute the given function.
 * \warning similar to DRW_render_to_image you cannot use default lists (`dfbl` & `dtxl`).
 */
void DRW_custom_pipeline(Depsgraph *depsgraph,
                         void (*callback)(void *user_data),
                         void *user_data);
/**
 * Assume a valid GL context is bound (and that the gl_context_mutex has been acquired).
 * This function only setup DST and execute the given function.
 * \warning similar to DRW_render_to_image you cannot use default lists (`dfbl` & `dtxl`).
 */
void DRW_custom_pipeline_begin(DRWContext &draw_ctx, Depsgraph *depsgraph);
void DRW_custom_pipeline_end(DRWContext &draw_ctx);

/**
 * Used when the render engine want to redo another cache populate inside the same render frame.
 * Assumes it is called between `DRW_custom_pipeline_begin/end()`.
 */
void DRW_cache_restart();

/* ViewLayers */

void **DRW_view_layer_engine_data_ensure_ex(ViewLayer *view_layer,
                                            void (*callback)(void *storage));
void **DRW_view_layer_engine_data_ensure(void (*callback)(void *storage));

/* DrawData */

DrawData *DRW_drawdata_get(ID *id);
DrawData *DRW_drawdata_ensure(ID *id,
                              size_t size,
                              DrawDataInitCb init_cb,
                              DrawDataFreeCb free_cb);
/* Settings. */

bool DRW_object_is_renderable(const Object *ob);
/**
 * Does `ob` needs to be rendered in edit mode.
 *
 * When using duplicate linked meshes, objects that are not in edit-mode will be drawn as
 * it is in edit mode, when another object with the same mesh is in edit mode.
 * This will not be the case when one of the objects are influenced by modifiers.
 */
bool DRW_object_is_in_edit_mode(const Object *ob);
/**
 * Return whether this object is visible depending if
 * we are rendering or drawing in the viewport.
 */
int DRW_object_visibility_in_active_context(const Object *ob);
bool DRW_object_use_hide_faces(const Object *ob);

bool DRW_object_is_visible_psys_in_active_context(const Object *object,
                                                  const ParticleSystem *psys);

Object *DRW_object_get_dupli_parent(const Object *ob);
DupliObject *DRW_object_get_dupli(const Object *ob);

/* Draw commands */

void DRW_draw_pass(DRWPass *pass);

/**
 * Reset state to not interfere with other UI draw-call.
 */
void DRW_state_reset_ex(DRWState state);
void DRW_state_reset();

/* Selection. */

void DRW_select_load_id(uint id);

/**
 * Convenient accessor for object data, that also automatically returns
 * the base or tessellated mesh depending if GPU subdivision is enabled.
 */
template<typename T> T &DRW_object_get_data_for_drawing(const Object &object)
{
  return *static_cast<T *>(object.data);
}

template<> Mesh &DRW_object_get_data_for_drawing(const Object &object);

/* Draw State. */

/* -------------------------------------------------------------------- */
/** \name Draw Context
 * \{ */

#define MAX_CLIP_PLANES 6 /* GL_MAX_CLIP_PLANES is at least 6 */
#define DRW_DRAWLIST_LEN 256

typedef uint32_t DRWResourceHandle;

struct DRWContext {
 private:
  /** Render State: No persistent data between draw calls. */
  static thread_local DRWContext *g_context;

  /* TODO(fclem): Private? */
 public:
  /* TODO: clean up this struct a bit. */
  /* Cache generation */
  DRWData *data = nullptr;
  /** Active view data structure for one of the 2 stereo view. */
  DRWViewData *view_data_active = nullptr;

  /** Dupli object that corresponds to the current object. */
  DupliObject *dupli_source = nullptr;
  /** Object that created the dupli-list the current object is part of. */
  Object *dupli_parent = nullptr;

  /* State of the object being evaluated if already allocated. */
  DRWResourceHandle ob_handle;
  /** True if current drw_get().ob_state has its matching DRWObjectInfos init. */
  bool ob_state_obinfo_init;
  /** Handle of current object resource in object resource arrays (DRWObjectMatrices/Infos). */
  DRWResourceHandle resource_handle;
  /** Handle of next DRWPass to be allocated. */
  DRWResourceHandle pass_handle;

  /** TODO(@fclem): try to remove usage of this. */
  DRWInstanceData *object_instance_data[MAX_INSTANCE_DATA_SIZE];

  /* Rendering state */
  GPUShader *shader;
  blender::gpu::Batch *batch;

  /* Managed by `DRW_state_set`, `DRW_state_reset` */
  DRWState state;
  DRWState state_lock;

  /* Optional associated viewport. Can be nullptr. */
  GPUViewport *viewport = nullptr;
  /* Size of the viewport or the final render frame. */
  blender::float2 size = {0, 0};
  blender::float2 inv_size = {0, 0};

  /* Returns the viewport's default framebuffer. */
  GPUFrameBuffer *default_framebuffer();
  /* Returns the viewport's default framebuffer list. Not all of them might be available. */
  DefaultFramebufferList *viewport_framebuffer_list_get() const;
  /* Returns the viewport's default texture list. Not all of them might be available. */
  DefaultTextureList *viewport_texture_list_get() const;

  const enum Mode {
    /* Render for display of 2D or 3D area. Runs on main thread. */
    VIEWPORT = 0,

    /* These viewport modes will render without some overlays (i.e. no text). */

    /* Render for a 3D viewport in XR. Runs on main thread. */
    VIEWPORT_XR,
    /* Render for a 3D viewport offscreen render (python). Runs on main thread. */
    VIEWPORT_OFFSCREEN,
    /* Render for a 3D viewport image render (render preview). Runs on main thread. */
    VIEWPORT_RENDER,

    /* Render for object mode selection. Runs on main thread. */
    SELECT_OBJECT,
    /* Render for object material selection. Runs on main thread. */
    SELECT_OBJECT_MATERIAL,
    /* Render for edit mesh selection. Runs on main thread. */
    SELECT_EDIT_MESH,

    /* Render for depth picking (auto-depth). Runs on main thread. */
    DEPTH,

    /* Render for F12 final render. Can run in any thread. */
    RENDER,
    /* Used by custom pipeline. Can run in any thread. */
    CUSTOM,
  } mode;

  struct {
    bool draw_background = false;
    bool draw_text = false;
  } options;

  /* Convenience pointer to text_store owned by the viewport */
  DRWTextStore **text_store_p = nullptr;

  bool buffer_finish_called; /* Avoid bad usage of DRW_render_instance_buffer_finish */

  DRWView *view_default = nullptr;
  DRWView *view_active = nullptr;
  DRWView *view_previous = nullptr;
  uint primary_view_num;

  uint select_id;

  /* Contains list of objects that needs to be extracted from other objects. */
  GSet *delayed_extraction = nullptr;

  GPUDrawList *draw_list = nullptr;

  /* TODO(fclem): Public. */

  /* Current rendering context. Avoid too many lookups while drawing. */

  /* Evaluated Depsgraph. */
  Depsgraph *depsgraph = nullptr;
  /* Evaluated Scene. */
  Scene *scene = nullptr;
  /* Evaluated ViewLayer. */
  ViewLayer *view_layer = nullptr;

  /** Last resort (some functions take this as an arg so we can't easily avoid).
   * May be nullptr when used for selection or depth buffer. */
  const bContext *evil_C = nullptr;
  /* Can be nullptr depending on context. */
  ARegion *region = nullptr;
  /* Can be nullptr depending on context. */
  SpaceLink *space_data = nullptr;
  /* Can be nullptr depending on context. */
  RegionView3D *rv3d = nullptr;
  /* Can be nullptr depending on context. */
  View3D *v3d = nullptr;
  /* Use 'object_edit' for edit-mode */
  Object *obact = nullptr;
  Object *object_pose = nullptr;
  Object *object_edit = nullptr;

  eObjectMode object_mode = OB_MODE_OBJECT;

 public:
  /**
   * If `viewport` is not specified, `DRWData` will be considered temporary and discarded on exit.
   * If `C` is nullptr, it means that the context is **not** associated with any UI or operator.
   * If `region` is nullptr, it will be sourced from the context `C` or left as nullptr otherwise.
   * If `v3d` is nullptr, it will be sourced from the context `C` or left as nullptr otherwise.
   */
  DRWContext(Mode mode,
             Depsgraph *depsgraph,
             GPUViewport *viewport,
             const bContext *C = nullptr,
             ARegion *region = nullptr,
             View3D *v3d = nullptr);
  DRWContext(Mode mode,
             Depsgraph *depsgraph,
             const blender::int2 size = {1, 1},
             const bContext *C = nullptr,
             ARegion *region = nullptr,
             View3D *v3d = nullptr);

  ~DRWContext();

  /**
   * Acquire `data` and `view_data_active`.
   * Needs to be called before enabling any draw engine.
   * IMPORTANT: This can be called multiple times before release_data.
   * IMPORTANT: This must be called with an active GPUContext.
   */
  void acquire_data();

  /**
   * Make sure to release acquired DRWData. If created on the fly, make sure to destroy them.
   * IMPORTANT: This needs to be called with the same active GPUContext `acquire_data()` was called
   * with.
   */
  void release_data();

  /**
   * Enable engines from context. Not needed for Mode::RENDER and Mode::CUSTOM.
   *
   * `render_engine_type` specify the engine to use in OB_MATERIAL or OB_RENDER modes.
   * `gpencil_engine_needed` should be set to true if the grease pencil engine is needed.
   */
  void enable_engines(bool gpencil_engine_needed = false,
                      RenderEngineType *render_engine_type = nullptr);

  /* Free unused engine data. */
  void engines_data_validate();

  using iter_callback_t =
      std::function<void(struct DupliCacheManager &, struct ExtractionGraph &)>;

  /* Run the sync phase with data extraction. iter_callback defines which object to sync. */
  void sync(iter_callback_t iter_callback);
  /* Run enabled engine init and sync callbacks. iter_callback defines which object to sync. */
  void engines_init_and_sync(iter_callback_t iter_callback);
  /* Run enabled engine init and draw scene callbacks. */
  void engines_draw_scene();

  static DRWContext &get_active()
  {
    return *g_context;
  }

  blender::float2 viewport_size_get() const
  {
    return size;
  }

  /* Return true if any DRWContext is active on this thread. */
  static bool is_active()
  {
    return g_context != nullptr;
  }

  bool is_select() const
  {
    return ELEM(mode, SELECT_OBJECT, SELECT_OBJECT_MATERIAL, SELECT_EDIT_MESH);
  }
  bool is_material_select() const
  {
    return ELEM(mode, SELECT_OBJECT_MATERIAL);
  }
  bool is_depth() const
  {
    return ELEM(mode, DEPTH);
  }
  bool is_image_render() const
  {
    return ELEM(mode, VIEWPORT_RENDER, RENDER);
  }
  bool is_scene_render() const
  {
    return ELEM(mode, RENDER);
  }
  bool is_viewport_image_render() const
  {
    return ELEM(mode, VIEWPORT_RENDER);
  }

  /** True if current viewport is drawn during playback. */
  bool is_playback() const;
  /** True if current viewport is drawn during navigation operator. */
  bool is_navigating() const;
  /** True if current viewport is drawn during painting operator. */
  bool is_painting() const;
  /** True if current viewport is drawn during transforming operator. */
  bool is_transforming() const;
  /** True if viewport compositor is enabled when drawing with this context. */
  bool is_viewport_compositor_enabled() const;
};

/** \} */

const DRWContext *DRW_context_get();

bool DRW_state_draw_support();
/**
 * Whether we should render the background
 */
bool DRW_state_draw_background();