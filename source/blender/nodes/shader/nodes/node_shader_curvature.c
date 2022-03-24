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

#include "../node_shader_util.h"

/* **************** OUTPUT ******************** */

static bNodeSocketTemplate sh_node_curvature_in[] = {
    {SOCK_FLOAT, N_("Samples"), 8.0f, 0.0f, 0.0f, 0.0f, 0.0f, 64.0f, PROP_NONE},
    {SOCK_FLOAT, N_("Sample Radius"), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1000.0f, PROP_NONE},
    {SOCK_FLOAT, N_("Thickness"), 1.0f, 0.0f, 0.0f, 0.0f, 0.01f, 1000.0f, PROP_NONE},
    {-1, ""},
};

static bNodeSocketTemplate sh_node_curvature_out[] = {
    {SOCK_FLOAT, N_("Scene Curvature")},
    {SOCK_FLOAT, N_("Scene Rim")},
    {-1, ""},
};

static int node_shader_gpu_curvature(GPUMaterial *mat,
                                        bNode *node,
                                        bNodeExecData *UNUSED(execdata),
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
  static bNodeType ntype;

  sh_node_type_base(&ntype, SH_NODE_CURVATURE, "Curvature", NODE_CLASS_INPUT, 0);
  node_type_socket_templates(&ntype, sh_node_curvature_in, sh_node_curvature_out);
  node_type_size_preset(&ntype, NODE_SIZE_MIDDLE);
  node_type_init(&ntype, NULL);
  node_type_storage(&ntype, "", NULL, NULL);
  node_type_gpu(&ntype, node_shader_gpu_curvature);

  nodeRegisterType(&ntype);
}
