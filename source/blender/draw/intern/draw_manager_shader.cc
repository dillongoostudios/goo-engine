/* SPDX-FileCopyrightText: 2016 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw
 */

#include "DNA_material_types.h"
#include "DNA_world_types.h"

#include "BLI_listbase.h"
#include "BLI_dynstr.h"
#include "BLI_string_utils.hh"
#include "BLI_threads.h"
#include "BLI_time.h"

#include "DEG_depsgraph_query.hh"

#include "GPU_capabilities.hh"
#include "GPU_material.hh"
#include "GPU_state.hh"

#include "WM_api.hh"

#include "draw_context_private.hh"

#include "CLG_log.h"

static CLG_LogRef LOG = {"draw.manager.shader"};

#include <atomic>
#include <condition_variable>
#include <mutex>

extern "C" char datatoc_gpu_shader_depth_only_frag_glsl[];
extern "C" char datatoc_common_fullscreen_vert_glsl[];

using namespace blender;

/* -------------------------------------------------------------------- */
/** \name Deferred Compilation (DRW_deferred)
 *
 * Since compiling shader can take a long time, we do it in a non blocking
 * manner in another thread.
 *
 * \{ */

struct DRWShaderCompiler {
  /** Default compilation queue. */
  Vector<GPUMaterial *> queue;
  /** Optimization queue. */
  Vector<GPUMaterial *> optimize_queue;

  std::mutex queue_mutex;
  std::condition_variable queue_cv;

  void *system_gpu_context;
  GPUContext *blender_gpu_context;

  std::atomic<bool> stop;
};

/** NOTE: While the `BLI_threads` API requires a List,
 * we only create a single thread at application startup and delete it at exit. */
static ListBase &compilation_threadpool()
{
  static ListBase compilation_threadpool_ = {};
  return compilation_threadpool_;
}

static DRWShaderCompiler &compiler_data()
{
  static DRWShaderCompiler compiler_data_ = {};
  return compiler_data_;
}

static void *drw_deferred_shader_compilation_exec(void * /*unused*/)
{
  using namespace blender;

  void *system_gpu_context = compiler_data().system_gpu_context;
  GPUContext *blender_gpu_context = compiler_data().blender_gpu_context;
  BLI_assert(system_gpu_context != nullptr);
  BLI_assert(blender_gpu_context != nullptr);
  GPU_render_begin();
  WM_system_gpu_context_activate(system_gpu_context);
  GPU_context_active_set(blender_gpu_context);

  const bool use_parallel_compilation = GPU_use_parallel_compilation();
  Vector<GPUMaterial *> async_mats;

  while (true) {
    if (compiler_data().stop) {
      break;
    }

    compiler_data().queue_mutex.lock();
    /* Pop last because it will be less likely to lock the main thread
     * if all GPUMaterials are to be freed (see DRW_deferred_shader_remove()). */
    GPUMaterial *mat = compiler_data().queue.is_empty() ? nullptr :
                                                          compiler_data().queue.pop_last();
    if (mat) {
      /* Avoid another thread freeing the material mid compilation. */
      GPU_material_acquire(mat);
    }
    compiler_data().queue_mutex.unlock();

    if (mat) {
      /* We have a new material that must be compiled,
       * we either compile it directly or add it to the async compilation list. */
      if (use_parallel_compilation) {
        GPU_material_async_compile(mat);
        async_mats.append(mat);
      }
      else {
        GPU_material_compile(mat);
        GPU_material_release(mat);
      }
    }
    else if (!async_mats.is_empty()) {
      /* (only if use_parallel_compilation == true)
       * Keep querying the requested materials until all of them are ready. */
      async_mats.remove_if([](GPUMaterial *mat) {
        if (GPU_material_async_try_finalize(mat)) {
          GPU_material_release(mat);
          return true;
        }
        return false;
      });
    }
    else {
      /* Check for Material Optimization job once there are no more
       * shaders to compile. */
      compiler_data().queue_mutex.lock();
      /* Pop last because it will be less likely to lock the main thread
       * if all GPUMaterials are to be freed (see DRW_deferred_shader_remove()). */
      GPUMaterial *optimize_mat = compiler_data().optimize_queue.is_empty() ?
                                      nullptr :
                                      compiler_data().optimize_queue.pop_last();
      if (optimize_mat) {
        /* Avoid another thread freeing the material during optimization. */
        GPU_material_acquire(optimize_mat);
      }
      compiler_data().queue_mutex.unlock();

      if (optimize_mat) {
        /* Compile optimized material shader. */
        GPU_material_optimize(optimize_mat);
        GPU_material_release(optimize_mat);
      }
      else {
        /* No more materials to optimize, or shaders to compile. */
        std::unique_lock lock(compiler_data().queue_mutex);
        compiler_data().queue_cv.wait(lock);
      }
    }

    if (GPU_type_matches_ex(GPU_DEVICE_ANY, GPU_OS_ANY, GPU_DRIVER_ANY, GPU_BACKEND_OPENGL)) {
      GPU_flush();
    }
  }

  /* We have to wait until all the requested batches are ready,
   * even if compiler_data().stop is true. */
  while (!async_mats.is_empty()) {
    async_mats.remove_if([](GPUMaterial *mat) {
      if (GPU_material_async_try_finalize(mat)) {
        GPU_material_release(mat);
        return true;
      }
      return false;
    });
  }

  GPU_context_active_set(nullptr);
  WM_system_gpu_context_release(system_gpu_context);
  GPU_render_end();

  return nullptr;
}

void DRW_shader_init()
{
  if (GPU_use_main_context_workaround()) {
    /* Deferred compilation is not supported. */
    return;
  }
  static bool initialized = false;
  if (initialized) {
    BLI_assert_unreachable();
    return;
  }
  initialized = true;

  compiler_data().stop = false;

  compiler_data().system_gpu_context = WM_system_gpu_context_create();
  compiler_data().blender_gpu_context = GPU_context_create(nullptr,
                                                           compiler_data().system_gpu_context);

  /* Some part of the code assumes no context is left bound. */
  GPU_context_active_set(nullptr);
  WM_system_gpu_context_release(compiler_data().system_gpu_context);

  BLI_threadpool_init(&compilation_threadpool(), drw_deferred_shader_compilation_exec, 1);
  BLI_threadpool_insert(&compilation_threadpool(), nullptr);
}

void DRW_shader_exit()
{
  if (GPU_use_main_context_workaround()) {
    /* Deferred compilation is not supported. */
    return;
  }

  compiler_data().stop = true;
  compiler_data().queue_cv.notify_one();
  BLI_threadpool_end(&compilation_threadpool());

  /* Revert the queued state for the materials that has not been compiled.
   * Note that this is not strictly needed since this function is called at program exit. */
  {
    std::scoped_lock queue_lock(compiler_data().queue_mutex);

    while (!compiler_data().queue.is_empty()) {
      GPU_material_status_set(compiler_data().queue.pop_last(), GPU_MAT_CREATED);
    }
    while (!compiler_data().optimize_queue.is_empty()) {
      GPU_material_optimization_status_set(compiler_data().optimize_queue.pop_last(),
                                           GPU_MAT_OPTIMIZATION_READY);
    }
  }

  WM_system_gpu_context_activate(compiler_data().system_gpu_context);
  GPU_context_active_set(compiler_data().blender_gpu_context);
  GPU_context_discard(compiler_data().blender_gpu_context);
  WM_system_gpu_context_dispose(compiler_data().system_gpu_context);
}

/**
 * Append either shader compilation or optimization job to deferred queue.
 * We keep two separate queue's to ensure core compilations always complete before optimization.
 */
static void drw_deferred_queue_append(GPUMaterial *mat, bool is_optimization_job)
{
  std::scoped_lock queue_lock(compiler_data().queue_mutex);

  /* Add to either compilation or optimization queue. */
  if (is_optimization_job) {
    BLI_assert(GPU_material_optimization_status(mat) != GPU_MAT_OPTIMIZATION_QUEUED);
    GPU_material_optimization_status_set(mat, GPU_MAT_OPTIMIZATION_QUEUED);
    compiler_data().optimize_queue.append(mat);
  }
  else {
    GPU_material_status_set(mat, GPU_MAT_QUEUED);
    compiler_data().queue.append(mat);
  }

  compiler_data().queue_cv.notify_one();
}

static void drw_deferred_shader_add(GPUMaterial *mat, bool deferred)
{
  if (ELEM(GPU_material_status(mat), GPU_MAT_SUCCESS, GPU_MAT_FAILED)) {
    return;
  }

  if (GPU_use_main_context_workaround()) {
    deferred = false;
  }

  if (!deferred) {
    DRW_deferred_shader_remove(mat);
    /* Shaders could already be compiling. Have to wait for compilation to finish. */
    while (GPU_material_status(mat) == GPU_MAT_QUEUED) {
      BLI_time_sleep_ms(20);
    }
    if (GPU_material_status(mat) == GPU_MAT_CREATED) {
      GPU_material_compile(mat);
    }
    return;
  }

  /* Don't add material to the queue twice. */
  if (GPU_material_status(mat) == GPU_MAT_QUEUED) {
    return;
  }

  /* Add deferred shader compilation to queue. */
  drw_deferred_queue_append(mat, false);
}

static void drw_register_shader_vlattrs(GPUMaterial *mat)
{
  const ListBase *attrs = GPU_material_layer_attributes(mat);

  if (!attrs) {
    return;
  }

  GHash *hash = drw_get().data->vlattrs_name_cache;
  ListBase *list = &drw_get().data->vlattrs_name_list;

  LISTBASE_FOREACH (GPULayerAttr *, attr, attrs) {
    GPULayerAttr **p_val;

    /* Add to the table and list if newly seen. */
    if (!BLI_ghash_ensure_p(hash, POINTER_FROM_UINT(attr->hash_code), (void ***)&p_val)) {
      drw_get().data->vlattrs_ubo_ready = false;

      GPULayerAttr *new_link = *p_val = static_cast<GPULayerAttr *>(MEM_dupallocN(attr));

      /* Insert into the list ensuring sorted order. */
      GPULayerAttr *link = static_cast<GPULayerAttr *>(list->first);

      while (link && link->hash_code <= attr->hash_code) {
        link = link->next;
      }

      new_link->prev = new_link->next = nullptr;
      BLI_insertlinkbefore(list, link, new_link);
    }

    /* Reset the unused frames counter. */
    (*p_val)->users = 0;
  }
}

void DRW_deferred_shader_remove(GPUMaterial *mat)
{
  if (GPU_use_main_context_workaround()) {
    /* Deferred compilation is not supported. */
    return;
  }

  std::scoped_lock queue_lock(compiler_data().queue_mutex);

  /* Search for compilation job in queue. */
  if (compiler_data().queue.contains(mat)) {
    compiler_data().queue.remove_first_occurrence_and_reorder(mat);
    GPU_material_status_set(mat, GPU_MAT_CREATED);
  }

  /* Search for optimization job in queue. */
  if (compiler_data().optimize_queue.contains(mat)) {
    compiler_data().optimize_queue.remove_first_occurrence_and_reorder(mat);
    GPU_material_optimization_status_set(mat, GPU_MAT_OPTIMIZATION_READY);
  }
}

void DRW_deferred_shader_optimize_remove(GPUMaterial *mat)
{
  if (GPU_use_main_context_workaround()) {
    /* Deferred compilation is not supported. */
    return;
  }

  std::scoped_lock queue_lock(compiler_data().queue_mutex);

  /* Search for optimization job in queue. */
  if (compiler_data().optimize_queue.contains(mat)) {
    compiler_data().optimize_queue.remove_first_occurrence_and_reorder(mat);
    GPU_material_optimization_status_set(mat, GPU_MAT_OPTIMIZATION_READY);
  }
}

/** \} */

/* -------------------------------------------------------------------- */

/** \{ */

GPUMaterial *DRW_shader_from_world(World *wo,
                                   bNodeTree *ntree,
                                   eGPUMaterialEngine engine,
                                   const uint64_t shader_id,
                                   const bool is_volume_shader,
                                   bool deferred,
                                   GPUCodegenCallbackFn callback,
                                   void *thunk)
{
  Scene *scene = DEG_get_original(drw_get().scene);
  GPUMaterial *mat = GPU_material_from_nodetree(scene,
                                                nullptr,
                                                ntree,
                                                &wo->gpumaterial,
                                                wo->id.name,
                                                engine,
                                                shader_id,
                                                is_volume_shader,
                                                false,
                                                callback,
                                                thunk);

  drw_register_shader_vlattrs(mat);

  if (DRW_context_get()->is_image_render()) {
    /* Do not deferred if doing render. */
    deferred = false;
  }

  drw_deferred_shader_add(mat, deferred);
  DRW_shader_queue_optimize_material(mat);
  return mat;
}

GPUMaterial *DRW_shader_from_material(Material *ma,
                                      bNodeTree *ntree,
                                      eGPUMaterialEngine engine,
                                      const uint64_t shader_id,
                                      const bool is_volume_shader,
                                      bool deferred,
                                      GPUCodegenCallbackFn callback,
                                      void *thunk,
                                      GPUMaterialPassReplacementCallbackFn pass_replacement_cb)
{
  Scene *scene = DEG_get_original(drw_get().scene);
  GPUMaterial *mat = GPU_material_from_nodetree(scene,
                                                ma,
                                                ntree,
                                                &ma->gpumaterial,
                                                ma->id.name,
                                                engine,
                                                shader_id,
                                                is_volume_shader,
                                                false,
                                                callback,
                                                thunk,
                                                 pass_replacement_cb);
  const DRWContext *draw_ctx = DRW_context_get();
  if (draw_ctx->is_image_render()) {
    /* Do not deferred if doing render. */
    deferred = false;
  }

  drw_register_shader_vlattrs(mat);

  drw_deferred_shader_add(mat, deferred);
  DRW_shader_queue_optimize_material(mat);
  return mat;
}

void DRW_shader_queue_optimize_material(GPUMaterial *mat)
{
  /* Do not perform deferred optimization if performing render.
   * De-queue any queued optimization jobs. */
  if (DRW_context_get()->is_image_render()) {
    if (GPU_material_optimization_status(mat) == GPU_MAT_OPTIMIZATION_QUEUED) {
      /* Remove from pending optimization job queue. */
      DRW_deferred_shader_optimize_remove(mat);
      /* If optimization job had already started, wait for it to complete. */
      while (GPU_material_optimization_status(mat) == GPU_MAT_OPTIMIZATION_QUEUED) {
        BLI_time_sleep_ms(20);
      }
    }
    return;
  }

  /* We do not need to perform optimization on the material if it is already compiled or in the
   * optimization queue. If optimization is not required, the status will be flagged as
   * `GPU_MAT_OPTIMIZATION_SKIP`.
   * We can also skip cases which have already been queued up. */
  if (ELEM(GPU_material_optimization_status(mat),
           GPU_MAT_OPTIMIZATION_SKIP,
           GPU_MAT_OPTIMIZATION_SUCCESS,
           GPU_MAT_OPTIMIZATION_QUEUED))
  {
    return;
  }

  /* Only queue optimization once the original shader has been successfully compiled. */
  if (GPU_material_status(mat) != GPU_MAT_SUCCESS) {
    return;
  }

  /* Defer optimization until sufficient time has passed beyond creation. This avoids excessive
   * recompilation for shaders which are being actively modified. */
  if (!GPU_material_optimization_ready(mat)) {
    return;
  }

  /* Add deferred shader compilation to queue. */
  drw_deferred_queue_append(mat, true);
}

/** \} */

/* -------------------------------------------------------------------- */

/** \{ */

GPUShader *DRW_shader_create_from_info_name(const char *info_name)
{
  return GPU_shader_create_from_info_name(info_name);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Shader Library
 *
 * Simple include system for glsl files.
 *
 * Usage: Create a DRWShaderLibrary and add the library in the right order.
 * You can have nested dependencies but each new library needs to have all its dependencies already
 * added to the DRWShaderLibrary.
 * Finally you can use DRW_shader_library_create_shader_string to get a shader string that also
 * contains the needed libraries for this shader.
 * \{ */

/* 64 because we use a 64bit bitmap. */
#define MAX_LIB 64
#define MAX_LIB_NAME 64
#define MAX_LIB_DEPS 8

struct DRWShaderLibrary {
  const char *libs[MAX_LIB];
  char libs_name[MAX_LIB][MAX_LIB_NAME];
  uint64_t libs_deps[MAX_LIB];
};

DRWShaderLibrary *DRW_shader_library_create()
{
  return static_cast<DRWShaderLibrary *>(
      MEM_callocN(sizeof(DRWShaderLibrary), "DRWShaderLibrary"));
}

void DRW_shader_library_free(DRWShaderLibrary *lib)
{
  MEM_SAFE_FREE(lib);
}

static int drw_shader_library_search(const DRWShaderLibrary *lib, const char *name)
{
  for (int i = 0; i < MAX_LIB; i++) {
    if (lib->libs[i]) {
      if (!strncmp(lib->libs_name[i], name, strlen(lib->libs_name[i]))) {
        return i;
      }
    }
    else {
      break;
    }
  }
  return -1;
}

/* Return bitmap of dependencies. */
static uint64_t drw_shader_dependencies_get(const DRWShaderLibrary *lib,
                                            const char *pragma_str,
                                            const char *lib_code,
                                            const char * /*lib_name*/)
{
  /* Search dependencies. */
  uint pragma_len = strlen(pragma_str);
  uint64_t deps = 0;
  const char *haystack = lib_code;
  while ((haystack = strstr(haystack, pragma_str))) {
    haystack += pragma_len;
    int dep = drw_shader_library_search(lib, haystack);
    if (dep == -1) {
      char dbg_name[MAX_NAME];
      int i = 0;
      while ((*haystack != '\n') && (i < (sizeof(dbg_name) - 2))) {
        dbg_name[i] = *haystack;
        haystack++;
        i++;
      }
      dbg_name[i] = '\0';

      CLOG_INFO(&LOG,
                0,
                "Dependency '%s' not found\n"
                "This might be due to bad lib ordering or overriding a builtin shader.\n",
                dbg_name);
    }
    else {
      deps |= 1llu << uint64_t(dep);
    }
  }
  return deps;
}

void DRW_shader_library_add_file(DRWShaderLibrary *lib, const char *lib_code, const char *lib_name)
{
  int index = -1;
  for (int i = 0; i < MAX_LIB; i++) {
    if (lib->libs[i] == nullptr) {
      index = i;
      break;
    }
  }

  if (index > -1) {
    lib->libs[index] = lib_code;
    STRNCPY(lib->libs_name[index], lib_name);
    lib->libs_deps[index] = drw_shader_dependencies_get(
        lib, "3349401894139530110 ", lib_code, lib_name);
  }
  else {
    printf("Error: Too many libraries. Cannot add %s.\n", lib_name);
    BLI_assert(0);
  }
}

char *DRW_shader_library_create_shader_string(const DRWShaderLibrary *lib, const char *shader_code)
{
  uint64_t deps = drw_shader_dependencies_get(lib, "3349401894139530110 ", shader_code, "shader code");

  DynStr *ds = BLI_dynstr_new();
  /* Add all dependencies recursively. */
  for (int i = MAX_LIB - 1; i > -1; i--) {
    if (lib->libs[i] && (deps & (1llu << uint64_t(i)))) {
      deps |= lib->libs_deps[i];
    }
  }
  /* Concatenate all needed libs into one string. */
  for (int i = 0; i < MAX_LIB && deps != 0llu; i++, deps >>= 1llu) {
    if (deps & 1llu) {
      BLI_dynstr_append(ds, lib->libs[i]);
    }
  }

  BLI_dynstr_append(ds, shader_code);

  char *str = BLI_dynstr_get_cstring(ds);
  BLI_dynstr_free(ds);

  return str;
}

/** \} */