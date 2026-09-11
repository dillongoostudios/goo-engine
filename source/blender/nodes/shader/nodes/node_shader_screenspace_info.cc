/* SPDX-FileCopyrightText: 2021 Blender Authors
 * SPDX-FileCopyrightText: 2025 Goo Engine Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * Screenspace Info node (ported from Goo Engine, SH_NODE_SCREENSPACE_INFO).
 * Raytraced Transmission enables current-sample scene-buffer reads for opaque,
 * hybrid and forward surfaces. The engine supplies immutable snapshots, separate
 * from native ray-tracing history and Shader-to-RGB resources.
 */

#include "node_util.hh"
#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_screenspace_info_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("View Position"_ustr).hide_value();
  b.add_output<decl::Color>("Scene Color"_ustr);
  b.add_output<decl::Float>("Scene Depth"_ustr);
}

static int node_shader_gpu_screenspace_info(GPUMaterial *mat,
                                            bNode *node,
                                            bNodeExecData * /*execdata*/,
                                            GPUNodeStack *in,
                                            GPUNodeStack *out)
{
  if (out[0].hasoutput) {
    GPU_material_flag_set(mat, GPU_MATFLAG_GOO_SCREENSPACE_COLOR);
  }
  if (out[1].hasoutput) {
    GPU_material_flag_set(mat, GPU_MATFLAG_GOO_SCREENSPACE_DEPTH);
  }
  /* Default the View Position input to the fragment's own view position (Goo's view_position_get),
   * so an unlinked node samples its own pixel; linked positions sample elsewhere (e.g. DepthRim). */
  if (!in[0].link) {
    GPU_link(mat, "view_position_get", &in[0].link);
  }
  return GPU_stack_link(mat, node, "node_screenspace_info", in, out);
}

}  // namespace nodes::node_shader_screenspace_info_cc

void register_node_type_sh_screenspace_info()
{
  namespace file_ns = nodes::node_shader_screenspace_info_cc;

  static bke::bNodeType ntype;

  common_node_type_base(&ntype, "ShaderNodeScreenspaceInfo"_ustr, SH_NODE_SCREENSPACE_INFO);
  ntype.ui_name = "Screenspace Info";
  ntype.ui_description = "Sample the internal scene color and depth buffers";
  ntype.enum_name_legacy = "SCREENSPACEINFO";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_shader_gpu_screenspace_info;

  bke::node_register_type(ntype);
}

}  // namespace blender
