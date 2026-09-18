/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gpu
 */

#include "vk_descriptor_set.hh"
#include "vk_index_buffer.hh"
#include "vk_shader.hh"
#include "vk_shader_interface.hh"
#include "vk_state_manager.hh"
#include "vk_storage_buffer.hh"
#include "vk_texture.hh"
#include "vk_vertex_buffer.hh"

namespace blender::gpu {

/** Size of the neutral buffers bound to unbound storage and uniform buffer slots. Only has to
 * satisfy the descriptor; the contents are never expected to be read through. */
static constexpr VkDeviceSize DUMMY_BUFFER_SIZE = 1024;

/**
 * Map a declared sampler type onto the placeholder texture type it needs.
 *
 * Used when a sampler turns out to be unbound at draw time: the placeholder has to match the
 * declared type, otherwise its image view is incompatible with the descriptor and
 * `vkUpdateDescriptorSets` fails validation.
 */
static eGPUTextureType to_dummy_texture_type(const shader::ImageType image_type)
{
  switch (image_type) {
    case shader::ImageType::FLOAT_1D:
    case shader::ImageType::INT_1D:
    case shader::ImageType::UINT_1D:
      return GPU_TEXTURE_1D;
    case shader::ImageType::FLOAT_1D_ARRAY:
    case shader::ImageType::INT_1D_ARRAY:
    case shader::ImageType::UINT_1D_ARRAY:
      return GPU_TEXTURE_1D_ARRAY;
    case shader::ImageType::FLOAT_2D:
    case shader::ImageType::INT_2D:
    case shader::ImageType::UINT_2D:
    case shader::ImageType::UINT_2D_ATOMIC:
    case shader::ImageType::INT_2D_ATOMIC:
    case shader::ImageType::SHADOW_2D:
    case shader::ImageType::DEPTH_2D:
      return GPU_TEXTURE_2D;
    case shader::ImageType::FLOAT_2D_ARRAY:
    case shader::ImageType::INT_2D_ARRAY:
    case shader::ImageType::UINT_2D_ARRAY:
    case shader::ImageType::UINT_2D_ARRAY_ATOMIC:
    case shader::ImageType::INT_2D_ARRAY_ATOMIC:
    case shader::ImageType::SHADOW_2D_ARRAY:
    case shader::ImageType::DEPTH_2D_ARRAY:
      return GPU_TEXTURE_2D_ARRAY;
    case shader::ImageType::FLOAT_3D:
    case shader::ImageType::INT_3D:
    case shader::ImageType::UINT_3D:
    case shader::ImageType::UINT_3D_ATOMIC:
    case shader::ImageType::INT_3D_ATOMIC:
      return GPU_TEXTURE_3D;
    case shader::ImageType::FLOAT_CUBE:
    case shader::ImageType::INT_CUBE:
    case shader::ImageType::UINT_CUBE:
    case shader::ImageType::SHADOW_CUBE:
    case shader::ImageType::DEPTH_CUBE:
      return GPU_TEXTURE_CUBE;
    case shader::ImageType::FLOAT_CUBE_ARRAY:
    case shader::ImageType::INT_CUBE_ARRAY:
    case shader::ImageType::UINT_CUBE_ARRAY:
    case shader::ImageType::SHADOW_CUBE_ARRAY:
    case shader::ImageType::DEPTH_CUBE_ARRAY:
      return GPU_TEXTURE_CUBE_ARRAY;
    default:
      BLI_assert_unreachable();
      return GPU_TEXTURE_2D;
  }
}

/**
 * Format a placeholder sampler reads as. Mirrors `MTLContext::get_dummy_texture()`, which picks
 * the texture format from the sampler format for the same reason.
 */
static eGPUSamplerFormat to_dummy_sampler_format(const shader::ImageType image_type)
{
  switch (image_type) {
    case shader::ImageType::INT_1D:
    case shader::ImageType::INT_1D_ARRAY:
    case shader::ImageType::INT_2D:
    case shader::ImageType::INT_2D_ARRAY:
    case shader::ImageType::INT_3D:
    case shader::ImageType::INT_CUBE:
    case shader::ImageType::INT_CUBE_ARRAY:
    case shader::ImageType::INT_2D_ATOMIC:
    case shader::ImageType::INT_2D_ARRAY_ATOMIC:
    case shader::ImageType::INT_3D_ATOMIC:
      return GPU_SAMPLER_TYPE_INT;
    case shader::ImageType::UINT_1D:
    case shader::ImageType::UINT_1D_ARRAY:
    case shader::ImageType::UINT_2D:
    case shader::ImageType::UINT_2D_ARRAY:
    case shader::ImageType::UINT_3D:
    case shader::ImageType::UINT_CUBE:
    case shader::ImageType::UINT_CUBE_ARRAY:
    case shader::ImageType::UINT_2D_ATOMIC:
    case shader::ImageType::UINT_2D_ARRAY_ATOMIC:
    case shader::ImageType::UINT_3D_ATOMIC:
      return GPU_SAMPLER_TYPE_UINT;
    case shader::ImageType::SHADOW_2D:
    case shader::ImageType::SHADOW_2D_ARRAY:
    case shader::ImageType::SHADOW_CUBE:
    case shader::ImageType::SHADOW_CUBE_ARRAY:
    case shader::ImageType::DEPTH_2D:
    case shader::ImageType::DEPTH_2D_ARRAY:
    case shader::ImageType::DEPTH_CUBE:
    case shader::ImageType::DEPTH_CUBE_ARRAY:
      return GPU_SAMPLER_TYPE_DEPTH;
    default:
      return GPU_SAMPLER_TYPE_FLOAT;
  }
}

/**
 * Whether a declared sampler is a buffer texture.
 *
 * Buffer textures are represented as `VKBindType::SAMPLER` but their descriptor set layout slot
 * is `VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER`, so they cannot be satisfied by the placeholder
 * images used for ordinary samplers.
 */
static bool is_buffer_sampler(const shader::ImageType image_type)
{
  return ELEM(image_type,
              shader::ImageType::FLOAT_BUFFER,
              shader::ImageType::INT_BUFFER,
              shader::ImageType::UINT_BUFFER);
}

void VKDescriptorSetTracker::bind_buffer(VkDescriptorType vk_descriptor_type,
                                         VkBuffer vk_buffer,
                                         VkDeviceSize buffer_offset,
                                         VkDeviceSize size_in_bytes,
                                         VKDescriptorSet::Location location)
{
  vk_descriptor_buffer_infos_.append({vk_buffer, buffer_offset, size_in_bytes});
  vk_write_descriptor_sets_.append({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    nullptr,
                                    vk_descriptor_set,
                                    location,
                                    0,
                                    1,
                                    vk_descriptor_type,
                                    nullptr,
                                    nullptr,
                                    nullptr});
}

void VKDescriptorSetTracker::bind_texel_buffer(VkBufferView vk_buffer_view,
                                               const VKDescriptorSet::Location location)
{
  vk_buffer_views_.append(vk_buffer_view);
  vk_write_descriptor_sets_.append({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    nullptr,
                                    vk_descriptor_set,
                                    location,
                                    0,
                                    1,
                                    VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                                    nullptr,
                                    nullptr,
                                    nullptr});
}

void VKDescriptorSetTracker::bind_image(VkDescriptorType vk_descriptor_type,
                                        VkSampler vk_sampler,
                                        VkImageView vk_image_view,
                                        VkImageLayout vk_image_layout,
                                        VKDescriptorSet::Location location)
{
  vk_descriptor_image_infos_.append({vk_sampler, vk_image_view, vk_image_layout});
  vk_write_descriptor_sets_.append({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    nullptr,
                                    vk_descriptor_set,
                                    location,
                                    0,
                                    1,
                                    vk_descriptor_type,
                                    nullptr,
                                    nullptr,
                                    nullptr});
}

void VKDescriptorSetTracker::bind_image_resource(const VKStateManager &state_manager,
                                                 const VKResourceBinding &resource_binding,
                                                 render_graph::VKResourceAccessInfo &access_info)
{
  VKTexture *texture_ptr = state_manager.images_.get(resource_binding.binding);
  if (texture_ptr == nullptr) {
    /* Image was not bound on this drawing path while the shader still declares it. There is no
     * placeholder storage image that could stand in for it (a write to it would be silently
     * discarded, and its format has to match the declared image type), so the binding stays
     * unwritten and the descriptor is undefined. This is a known gap: unlike the sampler case it
     * has not been observed in practice, because images declared via `ADDITIONAL_INFO` are also
     * bound by the paths that use them. */
    return;
  }
  VKTexture &texture = *texture_ptr;
  const VkImage vk_image = texture.vk_image_handle();
  /* See `bind_texture_resource` for why a texture without an image, or whose view failed to
   * create, is skipped rather than registered. */
  if (vk_image == VK_NULL_HANDLE) {
    return;
  }
  const VKImageView &image_view = texture.image_view_get(resource_binding.arrayed,
                                                        VKImageViewFlags::NO_SWIZZLING);
  if (!image_view.is_valid()) {
    return;
  }
  bind_image(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
             VK_NULL_HANDLE,
             image_view.vk_handle(),
             VK_IMAGE_LAYOUT_GENERAL,
             resource_binding.location);
  /* Update access info. */
  uint32_t layer_base = 0;
  uint32_t layer_count = VK_REMAINING_ARRAY_LAYERS;
  if (resource_binding.arrayed == VKImageViewArrayed::ARRAYED && texture.is_texture_view()) {
    IndexRange layer_range = texture.layer_range();
    layer_base = layer_range.start();
    layer_count = layer_range.size();
  }
  access_info.images.append({vk_image,
                             resource_binding.access_mask,
                             to_vk_image_aspect_flag_bits(texture.device_format_get()),
                             layer_base,
                             layer_count});
}

void VKDescriptorSetTracker::bind_texture_resource(const VKDevice &device,
                                                   const VKStateManager &state_manager,
                                                   const VKResourceBinding &resource_binding,
                                                   render_graph::VKResourceAccessInfo &access_info)
{
  const BindSpaceTextures::Elem &elem = state_manager.textures_.get(resource_binding.binding);
  switch (elem.resource_type) {
    case BindSpaceTextures::Type::VertexBuffer: {
      VKVertexBuffer *vertex_buffer = static_cast<VKVertexBuffer *>(elem.resource);
      vertex_buffer->ensure_updated();
      bind_texel_buffer(vertex_buffer->ensure_and_get_buffer_view(), resource_binding.location);
      access_info.buffers.append({vertex_buffer->vk_handle(), resource_binding.access_mask});
      break;
    }
    case BindSpaceTextures::Type::Texture: {
      VKTexture *texture = static_cast<VKTexture *>(elem.resource);
      if (texture->type_ == GPU_TEXTURE_BUFFER) {
        /* Texture buffers are no textures, but wrap around vertex buffers and need to be
         * bound as texel buffers. */
        /* TODO: Investigate if this can be improved in the API. */
        VKVertexBuffer *vertex_buffer = texture->source_buffer_;
        vertex_buffer->ensure_updated();
        bind_texel_buffer(vertex_buffer->ensure_and_get_buffer_view(), resource_binding.location);
        access_info.buffers.append({vertex_buffer->vk_handle(), resource_binding.access_mask});
      }
      else {
        const VkImage vk_image = texture->vk_image_handle();
        /* A bound texture can reference an image that no longer exists. `VKTexture` handles for
         * texture views forward to their source texture, and the draw manager keeps views alive
         * across `TextureFromPool::release()`, which frees the source texture and clears its
         * `vk_image_`. The view is then still bound while its source has no image left.
         *
         * Registering such a handle would make `VKResourceStateTracker::get_image` look up an
         * image that was never added, and the lookup after it would read out of bounds.
         * `BLI_assert` cannot catch this either, since it is compiled out in release builds.
         * Skip the binding instead: the descriptor stays undefined, which matches how this
         * function already treats a resource that was not bound on the drawing path. */
        if (vk_image == VK_NULL_HANDLE) {
          break;
        }
        /* The image view is created against the image above, so it fails for the same textures.
         * Handing an empty view to the descriptor set crashes the driver, which dereferences it
         * while updating the descriptor. */
        const VKImageView &image_view = texture->image_view_get(resource_binding.arrayed,
                                                               VKImageViewFlags::DEFAULT);
        if (!image_view.is_valid()) {
          break;
        }
        const VKSampler &sampler = device.samplers().get(elem.sampler);
        bind_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   sampler.vk_handle(),
                   image_view.vk_handle(),
                   VK_IMAGE_LAYOUT_GENERAL,
                   resource_binding.location);
        access_info.images.append({vk_image,
                                   resource_binding.access_mask,
                                   to_vk_image_aspect_flag_bits(texture->device_format_get()),
                                   0,
                                   VK_REMAINING_ARRAY_LAYERS});
      }
      break;
    }
    case BindSpaceTextures::Type::Unused: {
      /* The shader declares this sampler but nothing was bound to it on this drawing path.
       *
       * Some backends do not optimize out unused samplers, and several engine code paths
       * intentionally skip optional textures (for example world light-baking renders a material
       * shader whose statically declared sampler list is much larger than what that path binds).
       * Vulkan requires every binding present in the descriptor set layout to be written before
       * the draw, otherwise the descriptor stays undefined, which is undefined behaviour and used
       * to crash while reading the unbound resource. Bind a neutral resource so the layout is
       * always satisfied.
       *
       * Which neutral resource is required depends on the declared sampler type: buffer textures
       * occupy a uniform-texel-buffer slot, while everything else is a combined image sampler
       * whose placeholder has to match the declared dimensionality. */
      const shader::ImageType image_type = resource_binding.image_type;
      if (is_buffer_sampler(image_type)) {
        /* Buffer textures occupy a uniform-texel-buffer slot rather than a combined image
         * sampler, so the placeholder image cannot be used here. The device owns a matching
         * placeholder buffer view. */
        VkBufferView dummy_view = device.dummy_texel_buffer_view_get(
            to_dummy_sampler_format(image_type));
        if (dummy_view != VK_NULL_HANDLE) {
          bind_texel_buffer(dummy_view, resource_binding.location);
        }
        break;
      }
      GPUTexture *dummy_texture = device.dummy_texture_get(to_dummy_texture_type(image_type),
                                                           to_dummy_sampler_format(image_type));
      if (dummy_texture == nullptr) {
        break;
      }
      /* Two unwrap steps are required, as elsewhere in this backend (see `vk_framebuffer.cc` and
       * `vk_context.cc`): `GPUTexture` is only an opaque forward declaration, not a base class, so
       * no single cast reaches `VKTexture`. The first unwrap turns the opaque handle into a
       * `Texture *`, the second reaches the backend type. */
      VKTexture *filler_texture = unwrap(unwrap(dummy_texture));
      const VkImage filler_image = filler_texture->vk_image_handle();
      /* Same reasoning as the bound-texture case above: a placeholder without an image would
       * register a handle that the render graph cannot resolve. The buffer-texture placeholders
       * are the ones that may legitimately have no image of their own, but those take the
       * `is_buffer_sampler` branch and never reach here. */
      if (filler_image == VK_NULL_HANDLE) {
        break;
      }
      const VKSampler &sampler = device.samplers().get(
          ELEM(image_type,
               shader::ImageType::SHADOW_2D,
               shader::ImageType::SHADOW_2D_ARRAY,
               shader::ImageType::SHADOW_CUBE,
               shader::ImageType::SHADOW_CUBE_ARRAY) ?
              GPUSamplerState::compare_sampler() :
              GPUSamplerState::default_sampler());
      /* The placeholder's view fails to create under the same condition as the image is missing,
       * which the check above already covers. Check it anyway so every binding path treats a
       * failed view the same way. */
      const VKImageView &filler_view = filler_texture->image_view_get(resource_binding.arrayed,
                                                                     VKImageViewFlags::DEFAULT);
      if (!filler_view.is_valid()) {
        break;
      }
      bind_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 sampler.vk_handle(),
                 filler_view.vk_handle(),
                 VK_IMAGE_LAYOUT_GENERAL,
                 resource_binding.location);
      access_info.images.append({filler_image,
                                 resource_binding.access_mask,
                                 to_vk_image_aspect_flag_bits(filler_texture->device_format_get()),
                                 0,
                                 VK_REMAINING_ARRAY_LAYERS});
      break;
    }
  }
}

void VKDescriptorSetTracker::bind_input_attachment_resource(
    const VKDevice &device,
    const VKStateManager &state_manager,
    const VKResourceBinding &resource_binding,
    render_graph::VKResourceAccessInfo &access_info)
{
  const bool supports_local_read = !device.workarounds_get().dynamic_rendering_local_read;
  if (supports_local_read) {
    VKTexture *texture = state_manager.images_.get(resource_binding.binding);
    if (texture == nullptr) {
      /* See `bind_image_resource` for why the binding is skipped. */
      return;
    }
    const VkImage vk_image = texture->vk_image_handle();
    /* See `bind_texture_resource` for why a texture without an image, or whose view failed to
     * create, is skipped rather than registered. */
    if (vk_image == VK_NULL_HANDLE) {
      return;
    }
    const VKImageView &image_view = texture->image_view_get(resource_binding.arrayed,
                                                          VKImageViewFlags::NO_SWIZZLING);
    if (!image_view.is_valid()) {
      return;
    }
    bind_image(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
               VK_NULL_HANDLE,
               image_view.vk_handle(),
               VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR,
               resource_binding.location);
    access_info.images.append({vk_image,
                               resource_binding.access_mask,
                               to_vk_image_aspect_flag_bits(texture->device_format_get()),
                               0,
                               VK_REMAINING_ARRAY_LAYERS});
  }
  else {
    bool supports_dynamic_rendering = !device.workarounds_get().dynamic_rendering;
    const BindSpaceTextures::Elem &elem = state_manager.textures_.get(resource_binding.binding);
    if (elem.resource_type != BindSpaceTextures::Type::Texture || elem.resource == nullptr) {
      /* The input attachment was not bound on this drawing path. Nothing sensible can be bound
       * in its place, so skip it rather than dereferencing a null resource. */
      return;
    }
    VKTexture *texture = static_cast<VKTexture *>(elem.resource);
    const VkImage vk_image = texture->vk_image_handle();
    /* See `bind_texture_resource` for why a texture without an image, or whose view failed to
     * create, is skipped rather than registered. */
    if (vk_image == VK_NULL_HANDLE) {
      return;
    }
    if (supports_dynamic_rendering) {
      const VKImageView &image_view = texture->image_view_get(resource_binding.arrayed,
                                                            VKImageViewFlags::DEFAULT);
      if (!image_view.is_valid()) {
        return;
      }
      const VKSampler &sampler = device.samplers().get(elem.sampler);
      bind_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 sampler.vk_handle(),
                 image_view.vk_handle(),
                 VK_IMAGE_LAYOUT_GENERAL,
                 resource_binding.location);
      access_info.images.append({vk_image,
                                 resource_binding.access_mask,
                                 to_vk_image_aspect_flag_bits(texture->device_format_get()),
                                 0,
                                 VK_REMAINING_ARRAY_LAYERS});
    }
    else {
      /* Fallback to render-passes / sub-passes. */
      const VKImageView &image_view = texture->image_view_get(resource_binding.arrayed,
                                                            VKImageViewFlags::NO_SWIZZLING);
      if (!image_view.is_valid()) {
        return;
      }
      bind_image(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                 VK_NULL_HANDLE,
                 image_view.vk_handle(),
                 VK_IMAGE_LAYOUT_GENERAL,
                 resource_binding.location);
      access_info.images.append({vk_image,
                                 resource_binding.access_mask,
                                 to_vk_image_aspect_flag_bits(texture->device_format_get()),
                                 0,
                                 VK_REMAINING_ARRAY_LAYERS});
    }
  }
}

void VKDescriptorSetTracker::bind_storage_buffer_resource(
    const VKDevice &device,
    const VKStateManager &state_manager,
    const VKResourceBinding &resource_binding,
    render_graph::VKResourceAccessInfo &access_info)
{
  const BindSpaceStorageBuffers::Elem &elem = state_manager.storage_buffers_.get(
      resource_binding.binding);
  VkBuffer vk_buffer = VK_NULL_HANDLE;
  VkDeviceSize vk_device_size = 0;
  switch (elem.resource_type) {
    case BindSpaceStorageBuffers::Type::IndexBuffer: {
      VKIndexBuffer *index_buffer = static_cast<VKIndexBuffer *>(elem.resource);
      index_buffer->ensure_updated();
      vk_buffer = index_buffer->vk_handle();
      vk_device_size = index_buffer->size_get();
      break;
    }
    case BindSpaceStorageBuffers::Type::VertexBuffer: {
      VKVertexBuffer *vertex_buffer = static_cast<VKVertexBuffer *>(elem.resource);
      vertex_buffer->ensure_updated();
      vk_buffer = vertex_buffer->vk_handle();
      vk_device_size = vertex_buffer->size_used_get();
      break;
    }
    case BindSpaceStorageBuffers::Type::UniformBuffer: {
      VKUniformBuffer *uniform_buffer = static_cast<VKUniformBuffer *>(elem.resource);
      uniform_buffer->ensure_updated();
      vk_buffer = uniform_buffer->vk_handle();
      vk_device_size = uniform_buffer->size_in_bytes();
      break;
    }
    case BindSpaceStorageBuffers::Type::StorageBuffer: {
      VKStorageBuffer *storage_buffer = static_cast<VKStorageBuffer *>(elem.resource);
      storage_buffer->ensure_allocated();
      vk_buffer = storage_buffer->vk_handle();
      vk_device_size = storage_buffer->size_in_bytes();
      break;
    }
    case BindSpaceStorageBuffers::Type::Buffer: {
      VKBuffer *buffer = static_cast<VKBuffer *>(elem.resource);
      vk_buffer = buffer->vk_handle();
      vk_device_size = buffer->size_in_bytes();
      break;
    }
    case BindSpaceStorageBuffers::Type::Unused: {
      /* The storage buffer was not bound on this drawing path while the shader still declares it.
       * Vulkan requires the slot to be written, so bind a neutral buffer rather than leaving the
       * descriptor undefined. */
      VkBuffer dummy_buffer = device.dummy_buffer_get(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      if (dummy_buffer == VK_NULL_HANDLE) {
        return;
      }
      bind_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                  dummy_buffer,
                  0,
                  DUMMY_BUFFER_SIZE,
                  resource_binding.location);
      access_info.buffers.append({dummy_buffer, resource_binding.access_mask});
      return;
    }
  }

  bind_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              vk_buffer,
              elem.offset,
              vk_device_size - elem.offset,
              resource_binding.location);
  access_info.buffers.append({vk_buffer, resource_binding.access_mask});
}

void VKDescriptorSetTracker::bind_uniform_buffer_resource(
    const VKDevice &device,
    const VKStateManager &state_manager,
    const VKResourceBinding &resource_binding,
    render_graph::VKResourceAccessInfo &access_info)
{
  VKUniformBuffer *uniform_buffer_ptr = state_manager.uniform_buffers_.get(
      resource_binding.binding);
  if (uniform_buffer_ptr == nullptr) {
    /* The uniform buffer was not bound on this drawing path while the shader still declares it.
     * Vulkan requires the slot to be written, so bind a neutral buffer rather than leaving the
     * descriptor undefined. */
    VkBuffer dummy_buffer = device.dummy_buffer_get(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    if (dummy_buffer == VK_NULL_HANDLE) {
      return;
    }
    bind_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                dummy_buffer,
                0,
                DUMMY_BUFFER_SIZE,
                resource_binding.location);
    access_info.buffers.append({dummy_buffer, resource_binding.access_mask});
    return;
  }
  VKUniformBuffer &uniform_buffer = *uniform_buffer_ptr;
  uniform_buffer.ensure_updated();
  bind_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              uniform_buffer.vk_handle(),
              0,
              uniform_buffer.size_in_bytes(),
              resource_binding.location);
  access_info.buffers.append({uniform_buffer.vk_handle(), resource_binding.access_mask});
}

void VKDescriptorSetTracker::bind_push_constants(VKPushConstants &push_constants,
                                                 render_graph::VKResourceAccessInfo &access_info)
{
  if (push_constants.layout_get().storage_type_get() !=
      VKPushConstants::StorageType::UNIFORM_BUFFER)
  {
    return;
  }
  push_constants.update_uniform_buffer();
  const VKUniformBuffer &uniform_buffer = *push_constants.uniform_buffer_get().get();
  bind_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              uniform_buffer.vk_handle(),
              0,
              uniform_buffer.size_in_bytes(),
              push_constants.layout_get().descriptor_set_location_get());
  access_info.buffers.append({uniform_buffer.vk_handle(), VK_ACCESS_UNIFORM_READ_BIT});
}

void VKDescriptorSetTracker::bind_shader_resources(const VKDevice &device,
                                                   const VKStateManager &state_manager,
                                                   VKShader &shader,
                                                   render_graph::VKResourceAccessInfo &access_info)
{
  const VKShaderInterface &shader_interface = shader.interface_get();
  for (const VKResourceBinding &resource_binding : shader_interface.resource_bindings_get()) {
    if (resource_binding.binding == -1) {
      continue;
    }

    switch (resource_binding.bind_type) {
      case VKBindType::UNIFORM_BUFFER:
        bind_uniform_buffer_resource(device, state_manager, resource_binding, access_info);
        break;

      case VKBindType::STORAGE_BUFFER:
        bind_storage_buffer_resource(device, state_manager, resource_binding, access_info);
        break;

      case VKBindType::SAMPLER:
        bind_texture_resource(device, state_manager, resource_binding, access_info);
        break;

      case VKBindType::IMAGE:
        bind_image_resource(state_manager, resource_binding, access_info);
        break;

      case VKBindType::INPUT_ATTACHMENT:
        bind_input_attachment_resource(device, state_manager, resource_binding, access_info);
        break;
    }
  }

  /* Bind uniform push constants to descriptor set. */
  bind_push_constants(shader.push_constants, access_info);
}

void VKDescriptorSetTracker::update_descriptor_set(VKContext &context,
                                                   render_graph::VKResourceAccessInfo &access_info)
{
  VKShader &shader = *unwrap(context.shader);
  VKStateManager &state_manager = context.state_manager_get();

  /* Can we reuse previous descriptor set. */
  if (!state_manager.is_dirty &&
      !assign_if_different(vk_descriptor_set_layout_, shader.vk_descriptor_set_layout_get()) &&
      shader.push_constants.layout_get().storage_type_get() !=
          VKPushConstants::StorageType::UNIFORM_BUFFER)
  {
    return;
  }
  state_manager.is_dirty = false;

  /* Allocate a new descriptor set. */
  VkDescriptorSetLayout vk_descriptor_set_layout = shader.vk_descriptor_set_layout_get();
  vk_descriptor_set = context.descriptor_pools_get().allocate(vk_descriptor_set_layout);
  BLI_assert(vk_descriptor_set != VK_NULL_HANDLE);
  debug::object_label(vk_descriptor_set, shader.name_get());
  const VKDevice &device = VKBackend::get().device;
  bind_shader_resources(device, state_manager, shader, access_info);
}

void VKDescriptorSetTracker::upload_descriptor_sets()
{
  if (vk_write_descriptor_sets_.is_empty()) {
    return;
  }

  /* Finalize pointers that could have changed due to reallocations. */
  int buffer_index = 0;
  int buffer_view_index = 0;
  int image_index = 0;
  for (VkWriteDescriptorSet &vk_write_descriptor_set : vk_write_descriptor_sets_) {
    switch (vk_write_descriptor_set.descriptorType) {
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        vk_write_descriptor_set.pImageInfo = &vk_descriptor_image_infos_[image_index++];
        break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        vk_write_descriptor_set.pTexelBufferView = &vk_buffer_views_[buffer_view_index++];
        break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        vk_write_descriptor_set.pBufferInfo = &vk_descriptor_buffer_infos_[buffer_index++];
        break;

      default:
        BLI_assert_unreachable();
        break;
    }
  }

#if 0
  /* Uncomment this for rebalancing VKDescriptorPools::POOL_SIZE_* */
  {
    int storage_buffer_count = 0;
    int storage_image_count = 0;
    int combined_image_sampler_count = 0;
    int uniform_buffer_count = 0;
    int uniform_texel_buffer_count = 0;
    int input_attachment_count = 0;
    Set<VkDescriptorSet> descriptor_set_count;

    for (VkWriteDescriptorSet &vk_write_descriptor_set : vk_write_descriptor_sets_) {
      descriptor_set_count.add(vk_write_descriptor_set.dstSet);
      switch (vk_write_descriptor_set.descriptorType) {
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
          combined_image_sampler_count += 1;
          break;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
          storage_image_count += 1;
          break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
          uniform_texel_buffer_count += 1;
          break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
          uniform_buffer_count += 1;
          break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
          storage_buffer_count += 1;
          break;
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
          input_attachment_count += 1;
          break;
        default:
          BLI_assert_unreachable();
      }
    }
    std::cout << __func__ << ": "
              << "descriptor_set=" << descriptor_set_count.size()
              << ", combined_image_sampler=" << combined_image_sampler_count
              << ", storage_image=" << storage_image_count
              << ", uniform_texel_buffer=" << uniform_texel_buffer_count
              << ", uniform_buffer=" << uniform_buffer_count
              << ", storage_buffer=" << storage_buffer_count
              << ", input_attachment=" << input_attachment_count << "\n";
  }
#endif

  /* Update the descriptor set on the device. */
  const VKDevice &device = VKBackend::get().device;
  vkUpdateDescriptorSets(device.vk_handle(),
                         vk_write_descriptor_sets_.size(),
                         vk_write_descriptor_sets_.data(),
                         0,
                         nullptr);

  vk_descriptor_image_infos_.clear();
  vk_descriptor_buffer_infos_.clear();
  vk_buffer_views_.clear();
  vk_write_descriptor_sets_.clear();
}

}  // namespace blender::gpu
