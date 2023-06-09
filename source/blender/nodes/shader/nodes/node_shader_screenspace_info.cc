/* SPDX-License-Identifier: GPL-2.0-or-later
* Copyright 2005 Blender Foundation. All rights reserved. */

#include "../node_shader_util.hh"

/* **************** OUTPUT ******************** */

namespace blender::nodes::node_shader_screenspace_info_cc {

static void node_declare(NodeDeclarationBuilder &b) {
  b.add_input<decl::Vector>(N_("View Position")).hide_value();

  b.add_output<decl::Color>(N_("Scene Color"));
  b.add_output<decl::Float>(N_("Scene Depth"));
}

}

static int node_shader_gpu_screenspace_info(GPUMaterial *mat,
                                           bNode *node,
                                           bNodeExecData * /* execdata */,
                                           GPUNodeStack *in,
                                           GPUNodeStack *out)
{
 GPU_material_flag_set(mat, GPU_MATFLAG_REFRACT);

 if (!in[0].link) {
   GPU_link(mat, "view_position_get", &in[0].link);
 }

 return GPU_stack_link(mat, node, "node_screenspace_info", in, out);
}

/* node type definition */
void register_node_type_sh_screenspace_info(void)
{
 namespace file_ns = blender::nodes::node_shader_screenspace_info_cc;
 static bNodeType ntype;

 sh_node_type_base(&ntype, SH_NODE_SCREENSPACE_INFO, "Screenspace Info", NODE_CLASS_INPUT);
 ntype.declare = file_ns::node_declare;
 ntype.gpu_fn = node_shader_gpu_screenspace_info;

 nodeRegisterType(&ntype);
}
