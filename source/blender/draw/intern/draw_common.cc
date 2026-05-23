/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw
 */

#include "DNA_userdef_types.h"
#include "DNA_view3d_types.h"

#include "DRW_render.hh"

#include "GPU_texture.hh"
#include "GPU_uniform_buffer.hh"

#include "UI_resources.hh"

#include "BLI_index_range.hh"
#include "BLI_math_color.h"
#include "BLI_math_vector.h"

#include "BKE_colorband.hh"

#include "draw_common_c.hh"

/**
 * Colors & Constant.
 */

DRW_Global G_draw{};
