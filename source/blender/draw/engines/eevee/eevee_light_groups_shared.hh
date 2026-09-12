/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "GPU_shader_shared_utils.hh"
#ifndef GPU_SHADER
namespace blender::eevee {
#endif
/** Receiver material masks. Not object identities and never serialized as a table index. */
struct [[host_shared]] GooMaterialLightGroups {
  int4 lighting;
  int4 shadows;
};
#ifndef GPU_SHADER
}  // namespace blender::eevee
#endif
