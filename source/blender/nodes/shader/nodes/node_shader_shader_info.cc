/* SPDX-License-Identifier: GPL-2.0-or-later
* Copyright 2005 Blender Foundation. All rights reserved. */

#include "../node_shader_util.hh"

#include "UI_interface.hh"
#include "UI_resources.hh"
namespace blender::nodes::node_shader_shader_info_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
 b.add_input<decl::Vector>(N_("WorldPosition")).hide_value();
 b.add_input<decl::Vector>(N_("Normal")).hide_value();

 b.add_output<decl::Color>(N_("Diffuse Shading"));
 b.add_output<decl::Float>(N_("Cast Shadows"));
 b.add_output<decl::Float>(N_("Self Shadows"));
 b.add_output<decl::Color>(N_("Ambient Lighting"));
}

static void node_shader_init_shader_info(bNodeTree* /*ntree*/, bNode *node)
{
 NodeShaderInfo *shinfo = MEM_cnew<NodeShaderInfo>("NodeShaderInfo");
 shinfo->light_group_bits[3] = 1;
 shinfo->light_group_shadow_bits[3] = 1;
 shinfo->use_own_light_groups = 0;
 node->storage = shinfo;
}

static void node_shader_buts_shader_info(struct uiLayout *layout, struct bContext* /* C */, PointerRNA *ptr)
{
 uiItemR(layout,
         ptr,
         "use_own_light_groups",
         UI_ITEM_R_SPLIT_EMPTY_NAME,
         IFACE_("Light Groups"),
         ICON_NONE);

#if 0
 /* Show group bits for debugging */
 if (RNA_boolean_get(ptr, "use_own_light_groups")) {
   uiItemR(
       layout, ptr, "light_group_bits", UI_ITEM_R_SPLIT_EMPTY_NAME, IFACE_("bits"), ICON_NONE);
   uiItemR(layout,
           ptr,
           "light_group_shadow_bits",
           UI_ITEM_R_SPLIT_EMPTY_NAME,
           IFACE_("shadow bits"),
           ICON_NONE);
 }
#endif
}

static int node_shader_gpu_shader_info(GPUMaterial *mat,
                                      bNode *node,
                                      bNodeExecData* /* execdata */,
                                      GPUNodeStack *in,
                                      GPUNodeStack *out)
{
 // Set this to ensure shadowmap eval.
 GPU_material_flag_set(mat, GPU_MATFLAG_DIFFUSE);

 if (!in[0].link) {
   GPU_link(mat, "world_position_get", &in[0].link);
 }
 if (!in[1].link) {
   GPU_link(mat, "world_normals_get", &in[1].link);
 }

 auto* info = (NodeShaderInfo*) node->storage;
 if (info->use_own_light_groups) {
   // HACK: GPU_uniform supports floats only, use floatBitsToInt in shader to reinterpret.
   return GPU_stack_link(mat, node, "node_shader_info_light_groups", in, out, GPU_uniform((float*)info->light_group_bits), GPU_uniform((float*)info->light_group_shadow_bits));
 } else {
   return GPU_stack_link(mat, node, "node_shader_info", in, out);
 }
}

}  // namespace blender::nodes::node_shader_shader_info_cc

/* node type definition */
void register_node_type_sh_shader_info(void)
{
 namespace file_ns = blender::nodes::node_shader_shader_info_cc;

 static bNodeType ntype;

 sh_node_type_base(&ntype, SH_NODE_SHADER_INFO, "Shader Info", NODE_CLASS_INPUT);

 ntype.declare = file_ns::node_declare;
 ntype.draw_buttons = file_ns::node_shader_buts_shader_info;
 ntype.initfunc = file_ns::node_shader_init_shader_info;

 node_type_storage(
     &ntype, "NodeShaderInfo", node_free_standard_storage, node_copy_standard_storage);

 ntype.gpu_fn = file_ns::node_shader_gpu_shader_info;

 nodeRegisterType(&ntype);
}
