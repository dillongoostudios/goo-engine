/* SPDX-FileCopyrightText: 2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gpu
 */

#include "MEM_guardedalloc.h"

#include "vk_data_conversion.hh"
#include "vk_shader.hh"
#include "vk_shader_interface.hh"
#include "vk_staging_buffer.hh"
#include "vk_state_manager.hh"
#include "vk_vertex_buffer.hh"

namespace blender::gpu {

VKVertexBuffer::~VKVertexBuffer()
{
  release_data();
}

void VKVertexBuffer::bind_as_ssbo(uint binding)
{
  VKContext &context = *VKContext::get();
  VKStateManager &state_manager = context.state_manager_get();
  state_manager.storage_buffer_bind(BindSpaceStorageBuffers::Type::VertexBuffer, this, binding);
}

void VKVertexBuffer::bind_as_texture(uint binding)
{
  VKContext &context = *VKContext::get();
  VKStateManager &state_manager = context.state_manager_get();
  state_manager.texel_buffer_bind(*this, binding);
}

void VKVertexBuffer::ensure_updated()
{
  /* The dummy resources are shared device-level vertex buffers, which can be reached from
   * several threads at once. `upload_data()` mutates `buffer_`, `data_uploaded_` and the dirty
   * flags without any synchronization of its own, so the upload has to be serialized per buffer
   * here. This mutex is independent from the view mutex: `upload_data()` never touches the
   * buffer view, and neither lock is taken while holding the other. */
  std::scoped_lock lock(upload_mutex_);
  upload_data();
}

VkBufferView VKVertexBuffer::ensure_and_get_buffer_view()
{
  /* Callers can reach this from several threads at once, so the creation has to be serialized.
   * The handle is re-checked under the lock because `release_data()` can have discarded an
   * earlier view, which is also why this is not a `std::call_once`: a cleared vertex buffer
   * reacquires its data, and then needs a view again.
   * Ensuring and reading the view share one lock scope: with separate calls, `release_data()`
   * could acquire the mutex in between and the caller would be handed a null view. */
  std::scoped_lock lock(vk_buffer_view_mutex_);
  if (vk_buffer_view_ == VK_NULL_HANDLE) {
    VkBufferViewCreateInfo buffer_view_info = {};
    eGPUTextureFormat texture_format = to_texture_format(&format);

    buffer_view_info.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    buffer_view_info.buffer = buffer_.vk_handle();
    buffer_view_info.format = to_vk_format(texture_format);
    buffer_view_info.range = buffer_.size_in_bytes();

    const VKDevice &device = VKBackend::get().device;
    vkCreateBufferView(device.vk_handle(), &buffer_view_info, nullptr, &vk_buffer_view_);
    debug::object_label(vk_buffer_view_, "VertexBufferView");
  }
  return vk_buffer_view_;
}

void VKVertexBuffer::wrap_handle(uint64_t /*handle*/)
{
  NOT_YET_IMPLEMENTED
}

void VKVertexBuffer::update_sub(uint /*start*/, uint /*len*/, const void * /*data*/)
{
  NOT_YET_IMPLEMENTED
}

void VKVertexBuffer::read(void *data) const
{
  VKContext &context = *VKContext::get();
  if (buffer_.is_mapped()) {
    buffer_.read(context, data);
    return;
  }

  VKStagingBuffer staging_buffer(buffer_, VKStagingBuffer::Direction::DeviceToHost);
  staging_buffer.copy_from_device(context);
  staging_buffer.host_buffer_get().read(context, data);
}

void VKVertexBuffer::acquire_data()
{
  if (usage_ == GPU_USAGE_DEVICE_ONLY) {
    return;
  }

  /* Discard previous data if any. */
  /* TODO: Use mapped memory. */
  MEM_SAFE_FREE(data_);
  data_ = (uchar *)MEM_mallocN(sizeof(uchar) * this->size_alloc_get(), __func__);
}

void VKVertexBuffer::resize_data()
{
  if (usage_ == GPU_USAGE_DEVICE_ONLY) {
    return;
  }

  data_ = (uchar *)MEM_reallocN(data_, sizeof(uchar) * this->size_alloc_get());
}

void VKVertexBuffer::release_data()
{
  /* Taking the lock here is what makes the discard safe against `ensure_and_get_buffer_view()`
   * running on another thread: without it the view could be created after this discarded it, and
   * the `vk_buffer_view_` just written would be leaked. */
  {
    std::scoped_lock lock(vk_buffer_view_mutex_);
    if (vk_buffer_view_ != VK_NULL_HANDLE) {
      VKDiscardPool::discard_pool_get().discard_buffer_view(vk_buffer_view_);
      vk_buffer_view_ = VK_NULL_HANDLE;
    }
  }

  /* Freeing `data_` under the upload lock: `upload_data()` reads and converts `data_` and can be
   * running on another thread (the shared dummy buffers reach it concurrently), while this frees
   * the memory it works on. */
  {
    std::scoped_lock lock(upload_mutex_);
    MEM_SAFE_FREE(data_);
  }
}

void VKVertexBuffer::upload_data_direct(const VKBuffer &host_buffer)
{
  device_format_ensure();
  if (vertex_format_converter.needs_conversion()) {
    if (G.debug & G_DEBUG_GPU) {
      std::cout << "PERFORMANCE: Vertex buffer requires conversion.\n";
    }
    vertex_format_converter.convert(host_buffer.mapped_memory_get(), data_, vertex_len);
    host_buffer.flush();
  }
  else {
    host_buffer.update_immediately(data_);
  }
}

void VKVertexBuffer::upload_data_via_staging_buffer(VKContext &context)
{
  VKStagingBuffer staging_buffer(buffer_, VKStagingBuffer::Direction::HostToDevice);
  upload_data_direct(staging_buffer.host_buffer_get());
  staging_buffer.copy_to_device(context);
}

void VKVertexBuffer::upload_data()
{
  if (!buffer_.is_allocated()) {
    allocate();
  }
  if (!ELEM(usage_, GPU_USAGE_STATIC, GPU_USAGE_STREAM, GPU_USAGE_DYNAMIC)) {
    return;
  }

  if (flag & GPU_VERTBUF_DATA_DIRTY) {
    device_format_ensure();
    if (buffer_.is_mapped() && !data_uploaded_) {
      upload_data_direct(buffer_);
    }
    else {
      VKContext &context = *VKContext::get();
      upload_data_via_staging_buffer(context);
    }
    if (usage_ == GPU_USAGE_STATIC) {
      MEM_SAFE_FREE(data_);
    }
    data_uploaded_ = true;

    flag &= ~GPU_VERTBUF_DATA_DIRTY;
    flag |= GPU_VERTBUF_DATA_UPLOADED;
  }
}

void VKVertexBuffer::duplicate_data(VertBuf * /*dst*/)
{
  NOT_YET_IMPLEMENTED
}

void VKVertexBuffer::device_format_ensure()
{
  if (!vertex_format_converter.is_initialized()) {
    const VKWorkarounds &workarounds = VKBackend::get().device.workarounds_get();
    vertex_format_converter.init(&format, workarounds);
  }
}

const GPUVertFormat &VKVertexBuffer::device_format_get() const
{
  return vertex_format_converter.device_format_get();
}

void VKVertexBuffer::allocate()
{
  VkBufferUsageFlags vk_buffer_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                       VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  buffer_.create(size_alloc_get(),
                 vk_buffer_usage,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                 VmaAllocationCreateFlags(0));
  debug::object_label(buffer_.vk_handle(), "VertexBuffer");
}

}  // namespace blender::gpu
