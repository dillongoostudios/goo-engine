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

static bNodeSocketTemplate sh_node_sdf_noise_in[] = {
        {SOCK_VECTOR, N_("Position"), 0.0f, 0.0f, 0.0f, 0.0f, -10000.0f, 10000.0f, PROP_NONE, SOCK_HIDE_VALUE},
        {SOCK_FLOAT, N_("Distance"), 0.0f, 0.0f, 0.0f, 0.0f, -10000.0f, 10000.0f},
        {SOCK_FLOAT, N_("Detail"), 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 12.0f},
        {SOCK_FLOAT, N_("Roughness"), 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_FACTOR},
        {SOCK_FLOAT, N_("Octave Inflation"), 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_FACTOR},
        {SOCK_FLOAT, N_("Octave Smooth"), 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_FACTOR},
        {-1, ""},
};

static bNodeSocketTemplate sh_node_sdf_noise_out[] = {
        {SOCK_FLOAT, N_("Distance")},
        {-1, ""},
};

static int node_shader_gpu_sdf_noise(GPUMaterial *mat,
                                            bNode *node,
                                            bNodeExecData *UNUSED(execdata),
                                            GPUNodeStack *in,
                                            GPUNodeStack *out)
{
    node_shader_gpu_default_tex_coord(mat, node, &in[0].link);

    return GPU_stack_link(mat, node, "node_sdf_noise", in, out);
}

/* node type definition */
void register_node_type_sh_sdf_noise(void)
{
    static bNodeType ntype;

    sh_node_type_base(&ntype, SH_NODE_SDF_NOISE, "SDF Noise", NODE_CLASS_TEXTURE);
    node_type_socket_templates(&ntype, sh_node_sdf_noise_in, sh_node_sdf_noise_out);
    node_type_size_preset(&ntype, NODE_SIZE_MIDDLE);
    node_type_init(&ntype, NULL);
    node_type_storage(&ntype, "", NULL, NULL);
    node_type_gpu(&ntype, node_shader_gpu_sdf_noise);

    nodeRegisterType(&ntype);
}
