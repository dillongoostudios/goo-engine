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

namespace blender::nodes::node_shader_sdf_noise_cc {
static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>(N_("Position")).hide_value();
  b.add_input<decl::Float>(N_("Distance"));
  b.add_input<decl::Float>(N_("Detail"))
      .default_value(4.f)
      .min(0.f)
      .max(12.f);
  b.add_input<decl::Float>(N_("Roughness"))
      .default_value(0.5f)
      .min(0.f)
      .max(1.f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>(N_("Detail Inflation"))
      .default_value(0.1f)
      .min(0.f)
      .max(1.f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>(N_("Detail Blend"))
      .default_value(0.3f)
      .min(0.f)
      .max(1.f)
      .subtype(PROP_FACTOR);

  b.add_output<decl::Float>(N_("Distance"));
}
}

static int node_shader_gpu_sdf_noise(GPUMaterial *mat,
                                    bNode *node,
                                    bNodeExecData* /* execdata */,
                                    GPUNodeStack *in,
                                    GPUNodeStack *out)
{
 node_shader_gpu_default_tex_coord(mat, node, &in[0].link);

 return GPU_stack_link(mat, node, "node_sdf_noise", in, out);
}

/* node type definition */
void register_node_type_sh_sdf_noise(void)
{
 namespace file_ns = blender::nodes::node_shader_sdf_noise_cc;

 static bNodeType ntype;

 sh_node_type_base(&ntype, SH_NODE_SDF_NOISE, "SDF Noise", NODE_CLASS_TEXTURE);
 ntype.declare = file_ns::node_declare;
 ntype.gpu_fn = node_shader_gpu_sdf_noise;

 nodeRegisterType(&ntype);
}
