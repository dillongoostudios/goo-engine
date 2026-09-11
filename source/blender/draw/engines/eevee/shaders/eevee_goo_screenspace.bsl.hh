/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "eevee_goo_screenspace_shared.hh"
namespace eevee {
struct GooScreenSpace {
  [[uniform(GOO_SCREENSPACE_BUF_SLOT)]] const GooScreenSpaceData &data;
  [[sampler(GOO_SCENE_COLOR_TEX_SLOT)]] sampler2D color_tx;
  [[sampler(GOO_SCENE_DEPTH_TEX_SLOT)]] sampler2D depth_tx;
};
}  // namespace eevee
