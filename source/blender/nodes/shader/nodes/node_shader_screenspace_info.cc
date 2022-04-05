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

static bNodeSocketTemplate sh_node_screenspace_in[] = {
    {SOCK_VECTOR, N_("View Position"), 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, PROP_NONE, SOCK_HIDE_VALUE},
    {-1, ""},
};

static bNodeSocketTemplate sh_node_screenspace_out[] = {
    {SOCK_RGBA, N_("Scene Color")},
    {SOCK_FLOAT, N_("Scene Depth")},
    {-1, ""},
};

static int node_shader_gpu_screenspace_info(GPUMaterial *mat,
                                        bNode *node,
                                        bNodeExecData *UNUSED(execdata),
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
  static bNodeType ntype;

  sh_node_type_base(&ntype, SH_NODE_SCREENSPACE_INFO, "Screenspace Info", NODE_CLASS_INPUT);
  node_type_socket_templates(&ntype, sh_node_screenspace_in, sh_node_screenspace_out);
  node_type_size_preset(&ntype, NODE_SIZE_MIDDLE);
  node_type_init(&ntype, NULL);
  node_type_storage(&ntype, "", NULL, NULL);
  node_type_gpu(&ntype, node_shader_gpu_screenspace_info);

  nodeRegisterType(&ntype);
}
