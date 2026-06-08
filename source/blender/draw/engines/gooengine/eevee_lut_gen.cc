/* SPDX-FileCopyrightText: 2020 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * EEVEE LUT generation:
 *
 * Routine to generate the LUT used by eevee stored in eevee_lut.h
 * These functions are not to be used in the final executable.
 */

#include "DRW_render.hh"
#include "draw_handle.hh"

#include "BLI_fileops.h"
#include "BLI_rand.h"
#include "BLI_string_utils.hh"

#include "gooengine_private.hh"

#define DO_FILE_OUTPUT 0

namespace blender::gooengine {

}  // namespace blender::gooengine
