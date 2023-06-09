/*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software Foundation,
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
* The Original Code is Copyright (C) 2005 Blender Foundation.
* All rights reserved.
*/

#include "../node_shader_util.hh"

/* **************** OUTPUT ******************** */

namespace blender::nodes::node_shader_curvature_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>(N_("Samples")).max(64.f)
      .default_value(8.f);
  b.add_input<decl::Float>(N_("Sample Radius"))
      .default_value(1.f);
  b.add_input<decl::Float>(N_("Thickness"))
      .default_value(1.f);
  b.add_input<decl::Vector>(N_("Scale"))
      .default_value(float3(1.f, 1.f, 0.f));

  b.add_output<decl::Float>(N_("Scene Curvature"));
  b.add_output<decl::Float>(N_("Scene Rim"));
}

}

static int node_shader_gpu_curvature(GPUMaterial *mat,
                                    bNode *node,
                                    bNodeExecData * /* execdata */,
                                    GPUNodeStack *in,
                                    GPUNodeStack *out)
{

 // Set this to not break things.
 GPU_material_flag_set(mat, GPU_MATFLAG_DIFFUSE);

 return GPU_stack_link(mat, node, "node_screenspace_curvature", in, out);
}

/* node type definition */
void register_node_type_sh_curvature(void)
{
 namespace file_ns = blender::nodes::node_shader_curvature_cc;

 static bNodeType ntype;

 sh_node_type_base(&ntype, SH_NODE_CURVATURE, "Curvature", NODE_CLASS_INPUT);
 ntype.declare = file_ns::node_declare;
 ntype.gpu_fn = node_shader_gpu_curvature;

 nodeRegisterType(&ntype);
}
