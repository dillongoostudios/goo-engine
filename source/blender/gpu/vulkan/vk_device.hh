/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gpu
 */

#pragma once

#include <array>
#include <atomic>
#include <mutex>

#include "BLI_task.h"
#include "BLI_threads.h"
#include "BLI_utility_mixins.hh"
#include "BLI_vector.hh"

#include "GPU_vertex_buffer.hh"
#include "GPU_vertex_format.hh"

#include "render_graph/vk_render_graph.hh"
#include "render_graph/vk_resource_state_tracker.hh"
#include "vk_buffer.hh"
#include "vk_common.hh"
#include "vk_debug.hh"
#include "vk_descriptor_pools.hh"
#include "vk_descriptor_set_layouts.hh"
#include "vk_pipeline_pool.hh"
#include "vk_resource_pool.hh"
#include "vk_samplers.hh"

namespace blender::gpu {
class VKBackend;

/* TODO: Split into VKWorkarounds and VKExtensions to remove the negating when an extension isn't
 * supported. */
struct VKWorkarounds {
  /**
   * Some devices don't support pixel formats that are aligned to 24 and 48 bits.
   * In this case we need to use a different texture format.
   *
   * If set to true we should work around this issue by using a different texture format.
   */
  bool not_aligned_pixel_formats = false;

  /**
   * Is the workaround for devices that don't support
   * #VkPhysicalDeviceVulkan12Features::shaderOutputViewportIndex enabled.
   */
  bool shader_output_viewport_index = false;

  /**
   * Is the workaround for devices that don't support
   * #VkPhysicalDeviceVulkan12Features::shaderOutputLayer enabled.
   */
  bool shader_output_layer = false;

  struct {
    /**
     * Is the workaround enabled for devices that don't support using VK_FORMAT_R8G8B8_* as vertex
     * buffer.
     */
    bool r8g8b8 = false;
  } vertex_formats;

  /**
   * Is the workaround for devices that don't support
   * #VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR::fragmentShaderBarycentric enabled.
   * If set to true, the backend would inject a geometry shader to produce barycentric coordinates.
   */
  bool fragment_shader_barycentric = false;

  /**
   * Is the workarounds for devices that don't support VK_KHR_dynamic_rendering enabled.
   */
  bool dynamic_rendering = false;

  /**
   * Is the workarounds for devices that don't support VK_KHR_dynamic_rendering_local_read enabled.
   */
  bool dynamic_rendering_local_read = false;

  /**
   * Is the workarounds for devices that don't support VK_EXT_dynamic_rendering_unused_attachments
   * enabled.
   */
  bool dynamic_rendering_unused_attachments = false;

  /**
   * Is the workarounds for devices that don't support Logic Ops enabled.
   */
  bool logic_ops = false;
};

/**
 * Shared resources between contexts that run in the same thread.
 */
class VKThreadData : public NonCopyable, NonMovable {
  static constexpr uint32_t resource_pools_count = 3;

 public:
  /** Thread ID this instance belongs to. */
  pthread_t thread_id;
  /**
   * Index of the active resource pool. Is in sync with the active swap chain image or cycled when
   * rendering.
   *
   * NOTE: Initialized to `UINT32_MAX` to detect first change.
   */
  uint32_t resource_pool_index = UINT32_MAX;
  std::array<VKResourcePool, resource_pools_count> resource_pools;

  /**
   * The current rendering depth.
   *
   * GPU_rendering_begin can be called multiple times forming a hierarchy. The same resource pool
   * should be used for the whole hierarchy. rendering_depth is increased for every
   * GPU_rendering_begin and decreased when GPU_rendering_end is called. Resources pools are cycled
   * when the rendering_depth set to 0.
   */
  int32_t rendering_depth = 0;

  VKThreadData(VKDevice &device, pthread_t thread_id);
  void deinit(VKDevice &device);

  /**
   * Get the active resource pool.
   */
  VKResourcePool &resource_pool_get()
  {
    if (resource_pool_index >= resource_pools.size()) {
      return resource_pools[0];
    }
    return resource_pools[resource_pool_index];
  }

  /** Activate the next resource pool. */
  void resource_pool_next()
  {
    if (resource_pool_index == UINT32_MAX) {
      resource_pool_index = 1;
    }
    else {
      resource_pool_index = (resource_pool_index + 1) % resource_pools_count;
    }
  }
};

class VKDevice : public NonCopyable {
 private:
  /** Copies of the handles owned by the GHOST context. */
  VkInstance vk_instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice vk_physical_device_ = VK_NULL_HANDLE;
  VkDevice vk_device_ = VK_NULL_HANDLE;
  uint32_t vk_queue_family_ = 0;
  VkQueue vk_queue_ = VK_NULL_HANDLE;
  std::mutex *queue_mutex_ = nullptr;

  /**
   * Guards the lazy placeholder caches (`dummy_textures_`, `dummy_buffer_textures_` and the two
   * dummy buffers).
   *
   * Those caches live on the device but are filled from the drawing paths, which several threads
   * can reach at the same time (each thread has its own `VKThreadData`, but the caches are shared).
   * Without this, two threads could both miss the same entry and create it concurrently, leaking
   * one of the two resources and racing on the pointer store.
   *
   * Deliberately separate from `VKResourceStateTracker::mutex`: creating a `VKBuffer` registers it
   * through `resources.add_buffer()`, which takes that mutex, so reusing it here would self
   * deadlock. This mutex is only ever held around the creation of a cache entry, never around
   * anything that reaches the resource state tracker.
   *
   * Held only while an entry is being created; lookups stay lock free.
   */
  mutable std::mutex *dummy_resources_mutex_ = nullptr;

  /**
   * Lifetime of the device.
   *
   * Used for de-initialization of the command builder thread.
   */
  enum Lifetime {
    UNINITIALIZED,
    RUNNING,
    DEINITIALIZING,
    DESTROYED,
  };
  Lifetime lifetime = Lifetime::UNINITIALIZED;
  /**
   * Task pool for render graph submission.
   *
   * Multiple threads in Blender can build a render graph. Building the command buffer for a render
   * graph is faster when doing it in serial. Submission pool ensures that only one task is
   * building at a time (background_serial).
   */
  TaskPool *submission_pool_ = nullptr;
  /**
   * All created render graphs.
   */
  Vector<render_graph::VKRenderGraph *> render_graphs_;
  ThreadQueue *submitted_render_graphs_ = nullptr;
  ThreadQueue *unused_render_graphs_ = nullptr;
  VkSemaphore vk_timeline_semaphore_ = VK_NULL_HANDLE;
  std::atomic<uint_least64_t> timeline_value_ = 0;

  VKSamplers samplers_;
  VKDescriptorSetLayouts descriptor_set_layouts_;

  /**
   * Available Contexts for this device.
   *
   * Device keeps track of each contexts. When buffers/images are freed they need to be removed
   * from all contexts state managers.
   *
   * The contexts inside this list aren't owned by the VKDevice. Caller of `GPU_context_create`
   * holds the ownership.
   */
  Vector<std::reference_wrapper<VKContext>> contexts_;

  /** Allocator used for texture and buffers and other resources. */
  VmaAllocator mem_allocator_ = VK_NULL_HANDLE;

  /** Limits of the device linked to this context. */
  VkPhysicalDeviceProperties vk_physical_device_properties_ = {};
  VkPhysicalDeviceDriverProperties vk_physical_device_driver_properties_ = {};
  VkPhysicalDeviceMemoryProperties vk_physical_device_memory_properties_ = {};
  /** Features support. */
  VkPhysicalDeviceFeatures vk_physical_device_features_ = {};
  VkPhysicalDeviceVulkan11Features vk_physical_device_vulkan_11_features_ = {};
  VkPhysicalDeviceVulkan12Features vk_physical_device_vulkan_12_features_ = {};
  Array<VkExtensionProperties> device_extensions_;

  /** Functions of vk_ext_debugutils for this device/instance. */
  debug::VKDebuggingTools debugging_tools_;

  /* Workarounds */
  VKWorkarounds workarounds_;

  std::string glsl_patch_;
  Vector<VKThreadData *> thread_data_;

 public:
  render_graph::VKResourceStateTracker resources;
  VKDiscardPool orphaned_data;
  VKPipelinePool pipelines;
  /** Buffer to bind to unbound resource locations. */
  VKBuffer dummy_buffer;

  /**
   * Number of distinct texture types the dummy texture cache distinguishes. `eGPUTextureType` is a
   * bit mask whose values are sparse, so the cache is indexed by a dense slot derived from this
   * count rather than by the raw enum value. Kept in sync with `DUMMY_TEXTURE_TYPES` in
   * `vk_device.cc` by a `static_assert`.
   */
  static constexpr size_t DUMMY_TEXTURE_TYPE_COUNT = 7;

  /**
   * Texture to bind to unbound sampler locations, created on first request and cached per type
   * and sampler format.
   *
   * A shader can declare a sampler that is never bound on the current drawing path (some
   * backends do not optimize out unused samplers, and several engine paths intentionally skip
   * optional textures). Vulkan requires every binding declared in the descriptor set layout to
   * be written before the draw, so a neutral texture has to be supplied instead -- and it has to
   * match the declared sampler type, otherwise the image view is incompatible with the
   * descriptor and `vkUpdateDescriptorSets` fails validation. Declared samplers cover 1D, 2D,
   * 3D, cube and arrayed variants, hence one placeholder per combination, as in
   * `MTLContext::get_dummy_texture()`.
   *
   * Creation is deferred to the first request: allocating a texture uploads its initial pixels
   * through the render graph, which requires a context that does not exist yet while
   * `VKDevice::init()` is running. Use this instead of touching the cache directly.
   */
  GPUTexture *dummy_texture_get(eGPUTextureType type, eGPUSamplerFormat sampler_format) const;

  /**
   * Buffer view to bind to unbound buffer-texture samplers.
   *
   * Buffer textures are declared as samplers but occupy a uniform-texel-buffer descriptor slot,
   * which the placeholder images returned by `dummy_texture_get()` cannot fill. This returns the
   * view of a placeholder vertex buffer instead. Returns `VK_NULL_HANDLE` if the buffer could not
   * be created.
   */
  VkBufferView dummy_texel_buffer_view_get(eGPUSamplerFormat sampler_format) const;

  /**
   * Buffer to bind to unbound storage or uniform buffer locations, created on first request and
   * cached per usage.
   *
   * Same reasoning as `dummy_texture_get()`: a shader declares its resource interface at compile
   * time, so a buffer that is not bound on the current drawing path still occupies a slot in the
   * descriptor set layout. Vulkan requires every such slot to be written before the draw, so a
   * neutral buffer has to be supplied. Returns `VK_NULL_HANDLE` if the buffer could not be
   * created.
   */
  VkBuffer dummy_buffer_get(VkDescriptorType vk_descriptor_type) const;

  /**
   * This struct contains the functions pointer to extension provided functions.
   */
  struct {
    /* Extension: VK_KHR_dynamic_rendering */
    PFN_vkCmdBeginRendering vkCmdBeginRendering = nullptr;
    PFN_vkCmdEndRendering vkCmdEndRendering = nullptr;

    /* Extension: VK_EXT_debug_utils */
    PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabel = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabel = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = nullptr;
    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessenger = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessenger = nullptr;
  } functions;

  const char *extension_name_get(int index) const
  {
    return device_extensions_[index].extensionName;
  }

  VkPhysicalDevice physical_device_get() const
  {
    return vk_physical_device_;
  }

  const VkPhysicalDeviceProperties &physical_device_properties_get() const
  {
    return vk_physical_device_properties_;
  }

  const VkPhysicalDeviceFeatures &physical_device_features_get() const
  {
    return vk_physical_device_features_;
  }

  const VkPhysicalDeviceVulkan11Features &physical_device_vulkan_11_features_get() const
  {
    return vk_physical_device_vulkan_11_features_;
  }

  const VkPhysicalDeviceVulkan12Features &physical_device_vulkan_12_features_get() const
  {
    return vk_physical_device_vulkan_12_features_;
  }

  VkInstance instance_get() const
  {
    return vk_instance_;
  };

  VkDevice vk_handle() const
  {
    return vk_device_;
  }

  VkQueue queue_get() const
  {
    return vk_queue_;
  }
  std::mutex &queue_mutex_get()
  {
    return *queue_mutex_;
  }

  const uint32_t queue_family_get() const
  {
    return vk_queue_family_;
  }

  VmaAllocator mem_allocator_get() const
  {
    return mem_allocator_;
  }

  VKDescriptorSetLayouts &descriptor_set_layouts_get()
  {
    return descriptor_set_layouts_;
  }

  debug::VKDebuggingTools &debugging_tools_get()
  {
    return debugging_tools_;
  }

  const debug::VKDebuggingTools &debugging_tools_get() const
  {
    return debugging_tools_;
  }

  const VKSamplers &samplers() const
  {
    return samplers_;
  }

  bool is_initialized() const;
  void init(void *ghost_context);
  void reinit();
  void deinit();

  eGPUDeviceType device_type() const;
  eGPUDriverType driver_type() const;
  std::string vendor_name() const;
  std::string driver_version() const;

  /**
   * Check if a specific extension is supported by the device.
   *
   * This should be called from vk_backend to set the correct capabilities and workarounds needed
   * for this device.
   */
  bool supports_extension(const char *extension_name) const;

  const VKWorkarounds &workarounds_get() const
  {
    return workarounds_;
  }

  const char *glsl_patch_get() const;
  void init_glsl_patch();

  /* -------------------------------------------------------------------- */
  /** \name Render graph
   * \{ */
  static void submission_runner(TaskPool *__restrict pool, void *task_data);
  render_graph::VKRenderGraph *render_graph_new();

  TimelineValue render_graph_submit(render_graph::VKRenderGraph *render_graph,
                                    VKDiscardPool &context_discard_pool,
                                    bool submit_to_device,
                                    bool wait_for_completion);
  void wait_for_timeline(TimelineValue timeline);

  /**
   * Retrieve the last finished submission timeline.
   */
  TimelineValue submission_finished_timeline_get() const
  {
    BLI_assert(vk_timeline_semaphore_ != VK_NULL_HANDLE);
    TimelineValue current_timeline;
    vkGetSemaphoreCounterValue(vk_device_, vk_timeline_semaphore_, &current_timeline);
    return current_timeline;
  }

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \name Resource management
   * \{ */

  /**
   * Get or create current thread data.
   */
  VKThreadData &current_thread_data();

#if 0
  /**
   * Get the discard pool for the current thread.
   *
   * When the active thread has a context a discard pool associated to the thread is returned.
   * When there is no context the orphan discard pool is returned.
   *
   * A thread with a context can have multiple discard pools. One for each swap-chain image.
   * A thread without a context is most likely a discarded resource triggered during dependency
   * graph update. A dependency graph update from the viewport during playback or editing;
   * or a dependency graph update when rendering.
   * These can happen from a different thread which will don't have a context at all.
   * \param thread_safe: Caller thread already owns the resources mutex and is safe to run this
   * function without trying to reacquire resources mutex making a deadlock.
   */
  VKDiscardPool &discard_pool_for_current_thread(bool thread_safe = false);
#endif

  void context_register(VKContext &context);
  void context_unregister(VKContext &context);
  Span<std::reference_wrapper<VKContext>> contexts_get() const;

  void memory_statistics_get(int *r_total_mem_kb, int *r_free_mem_kb) const;
  static void debug_print(std::ostream &os, const VKDiscardPool &discard_pool);
  void debug_print();

  /** \} */

 private:
  void init_physical_device_properties();
  void init_physical_device_memory_properties();
  void init_physical_device_features();
  void init_physical_device_extensions();
  void init_debug_callbacks();
  void init_memory_allocator();
  void init_submission_pool();
  void deinit_submission_pool();
  /**
   * Initialize the functions struct with extension specific function pointer.
   */
  void init_functions();

  /**
   * Initialize a dummy buffer that can be bound for missing attributes.
   */
  void init_dummy_buffer();

  /**
   * Get (creating on first use) the placeholder vertex buffer for a buffer-texture slot. Shared
   * by `dummy_texture_get()` and `dummy_texel_buffer_view_get()`, which wrap the same buffer in
   * different ways. Takes `dummy_resources_mutex_` for the duration of the call.
   */
  VertBuf *dummy_buffer_texture_ensure_locked(eGPUSamplerFormat sampler_format) const;

  /**
   * Same as `dummy_buffer_texture_ensure_locked()`, but the caller must already own
   * `dummy_resources_mutex_`. Exists so that `dummy_texture_get()` can create its buffer-texture
   * placeholder from inside the critical section without re-entering it.
   */
  VertBuf *dummy_buffer_texture_ensure_unlocked(eGPUSamplerFormat sampler_format) const;

  /**
   * Cached results of `dummy_texture_get()`, owned by this device. Indexed by sampler format and
   * texture type, mirroring `MTLContext::dummy_textures_`. `mutable` because the cache is filled
   * on first use, which happens through the `const VKDevice &` handed to the drawing paths; it
   * does not change the observable state of the device.
   *
   * Atomic because the drawing paths that fill it can run on several threads, while the cache is
   * shared. Entries are published with a release store and read with an acquire load, so a non-null
   * pointer is always a fully initialized resource; guarding only the store with a mutex would
   * leave the unsynchronized reads racing.
   *
   * `eGPUTextureType` is a bit mask, so the second dimension is not indexed by the raw enum value
   * but by the dense slot assigned in `vk_device.cc`; see `DUMMY_TEXTURE_TYPES`. The count has to
   * stay in sync with that table, which a `static_assert` in the implementation enforces.
   */
  using DummyTextureArray = std::array<std::atomic<GPUTexture *>,
                                       size_t(GPU_SAMPLER_TYPE_MAX) * DUMMY_TEXTURE_TYPE_COUNT>;
  mutable DummyTextureArray dummy_textures_ = {};

  /**
   * Vertex buffers backing the buffer-type placeholders, one per sampler format. They are kept
   * alive for the device lifetime because `GPU_texture_create_from_vertbuf` wraps the buffer
   * rather than copying it. Only ever touched while `dummy_resources_mutex_` is held.
   */
  mutable std::array<blender::gpu::VertBuf *, size_t(GPU_SAMPLER_TYPE_MAX)> dummy_buffer_textures_ =
      {};

  /**
   * Cached storage and uniform placeholder buffers. Atomic for the same reason as the texture
   * cache.
   */
  mutable std::atomic<VKBuffer *> dummy_storage_buffer_ = nullptr;
  mutable std::atomic<VKBuffer *> dummy_uniform_buffer_ = nullptr;

  /* During initialization the backend requires access to update the workarounds. */
  friend VKBackend;
};

}  // namespace blender::gpu
