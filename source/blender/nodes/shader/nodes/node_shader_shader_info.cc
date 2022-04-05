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

static bNodeSocketTemplate sh_node_shader_info_in[] = {
    {SOCK_VECTOR, N_("WorldPosition"), 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, PROP_NONE, SOCK_HIDE_VALUE},
    {SOCK_VECTOR, N_("Normal"), 0.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, PROP_NONE, SOCK_HIDE_VALUE},
    {-1, ""},
};

static bNodeSocketTemplate sh_node_shader_info_out[] = {
    {SOCK_RGBA, N_("Diffuse Shading")},
    {SOCK_FLOAT, N_("Cast Shadows")},
    {SOCK_FLOAT, N_("Self Shadows")},
    {SOCK_RGBA, N_("Ambient Lighting")},
    {-1, ""},
};

static int node_shader_gpu_shader_info(GPUMaterial *mat,
                                        bNode *node,
                                        bNodeExecData *UNUSED(execdata),
                                        GPUNodeStack *in,
                                        GPUNodeStack *out)
{
  if (!in[0].link) {
    GPU_link(mat, "world_position_get", &in[0].link);
  }
  if (!in[1].link) {
    GPU_link(mat, "world_normals_get", &in[1].link);
  }

  // Set this to ensure shadowmap eval.
  GPU_material_flag_set(mat, GPU_MATFLAG_DIFFUSE);

  return GPU_stack_link(mat, node, "node_shader_info", in, out);
}

/* node type definition */
void register_node_type_sh_shader_info(void)
{
  static bNodeType ntype;

  sh_node_type_base(&ntype, SH_NODE_SHADER_INFO, "Shader Info", NODE_CLASS_INPUT);
  node_type_socket_templates(&ntype, sh_node_shader_info_in, sh_node_shader_info_out);
  node_type_size_preset(&ntype, NODE_SIZE_MIDDLE);
  node_type_init(&ntype, NULL);
  node_type_storage(&ntype, "", NULL, NULL);
  node_type_gpu(&ntype, node_shader_gpu_shader_info);

  nodeRegisterType(&ntype);
}
