/* SPDX-FileCopyrightText: 2005 Blender Foundation
*
* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../node_shader_util.hh"

namespace blender::nodes::node_shader_set_depth_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
 b.add_input<decl::Shader>(N_("Shader"));
 b.add_input<decl::Float>(N_("View Depth"))
     .hide_value(true);
 b.add_output<decl::Shader>(N_("Shader"));
}

static int node_shader_gpu_add_shader(GPUMaterial *mat,
                                     bNode *node,
                                     bNodeExecData * /* execdata */,
                                     GPUNodeStack *in,
                                     GPUNodeStack *out)
{
 if (!in[1].link) {
   GPU_link(mat, "view_z_get", &in[1].link);
 }

 GPU_material_flag_set(mat, GPU_MATFLAG_SET_DEPTH);

 return GPU_stack_link(mat, node, "node_set_depth", in, out);
}

} // blender::nodes::node_shader_set_depth_cc

/* node type definition */
void register_node_type_sh_set_depth()
{
  namespace file_ns = blender::nodes::node_shader_set_depth_cc;
  static blender::bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeSetDepth", SH_NODE_SET_DEPTH);
  ntype.ui_name = "Set Depth";
  ntype.ui_description =
      "Pixel depth offset";
  ntype.enum_name_legacy = "SET_DEPTH";
  ntype.nclass = NODE_CLASS_SHADER;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_shader_gpu_add_shader;

  blender::bke::node_register_type(&ntype);
}
