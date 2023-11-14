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
 * The Original Code is Copyright (C) 2021 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup shdnodes
 */

#include "RNA_enum_types.hh"

#include "UI_interface.hh"
#include "UI_resources.hh"

#include "../node_shader_util.hh"

using namespace blender::bke;

namespace blender::nodes {

static void sh_node_sdf_vector_op_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Vector1").hide_value(true);
  b.add_input<decl::Vector>("Vector2").no_muted_links(true);
  b.add_input<decl::Vector>("Vector3").no_muted_links(true);
  b.add_input<decl::Float>("Scale").min(-100000.0f).max(100000.0f).default_value(1.0f);
  b.add_input<decl::Float>("Value1").min(-100000.0f).max(100000.0f).default_value(0.0f);
  b.add_input<decl::Float>("Value2").min(-100000.0f).max(100000.0f).default_value(0.0f);
  b.add_input<decl::Float>("Angle").subtype(PROP_ANGLE);
  b.add_input<decl::Int>("Count1").min(0).max(127).default_value(4);
  b.add_input<decl::Int>("Count2").min(0).max(127).default_value(4);

  b.add_output<decl::Vector>("Vector");
  b.add_output<decl::Vector>("Position");
  b.add_output<decl::Float>("Value");
}

}  // namespace blender::nodes

static const char *node_shader_sdf_vector_op_get_name(int mode)
{
  switch (mode) {
    case SHD_SDF_VEC_OP_SPIN:
      return "node_sdf_vector_op_spin";
    case SHD_SDF_VEC_OP_SWIZZLE:
      return "node_sdf_vector_op_swizzle";
    case SHD_SDF_VEC_OP_EXTRUDE:
      return "node_sdf_vector_op_extrude";
    case SHD_SDF_VEC_OP_TWIST:
      return "node_sdf_vector_op_twist";
    case SHD_SDF_VEC_OP_SWIRL:
      return "node_sdf_vector_op_swirl";
    case SHD_SDF_VEC_OP_RADIAL_SHEAR:
      return "node_sdf_vector_op_radial_shear";
    case SHD_SDF_VEC_OP_PINCH_INFLATE:
      return "node_sdf_vector_op_pinch_inflate";
    case SHD_SDF_VEC_OP_BEND:
      return "node_sdf_vector_op_bend";
    case SHD_SDF_VEC_OP_REPEAT_FINITE:
      return "node_sdf_vector_op_repeat";
    case SHD_SDF_VEC_OP_REPEAT_INF:
      return "node_sdf_vector_op_repeat_inf";
    case SHD_SDF_VEC_OP_REPEAT_INF_MIRROR:
      return "node_sdf_vector_op_repeat_inf_mirror";
    case SHD_SDF_VEC_OP_ROTATE:
      return "node_sdf_vector_op_rotate";
    case SHD_SDF_VEC_OP_REFLECT:
      return "node_sdf_vector_op_reflect";
    case SHD_SDF_VEC_OP_MIRROR:
      return "node_sdf_vector_op_mirror";
    case SHD_SDF_VEC_OP_POLAR:
      return "node_sdf_vector_op_polar";

    case SHD_SDF_VEC_OP_MAP_UV:
      return "node_sdf_vector_op_map_uv";
    case SHD_SDF_VEC_OP_MAP_11:
      return "node_sdf_vector_op_map_11";
    case SHD_SDF_VEC_OP_MAP_05:
      return "node_sdf_vector_op_map_05";
    case SHD_SDF_VEC_OP_ROTATE_UV:
      return "node_sdf_vector_op_uv_rotate";
    case SHD_SDF_VEC_OP_SCALE_UV:
      return "node_sdf_vector_op_uv_scale";
    case SHD_SDF_VEC_OP_RND_UV:
      return "node_sdf_vector_op_random_uv_rotate";
    case SHD_SDF_VEC_OP_RND_UV_FLIP:
      return "node_sdf_vector_op_random_uv_flip";
    case SHD_SDF_VEC_OP_OCTANT:
      return "node_sdf_vector_op_octant";
    case SHD_SDF_VEC_OP_TILESET:
      return "node_sdf_vector_op_tileset";
    case SHD_SDF_VEC_OP_GRID:
      return "node_sdf_vector_op_grid";
  }

  return nullptr;
}

static int node_shader_gpu_sdf_vector_op(GPUMaterial *mat,
                                         bNode *node,
                                         bNodeExecData* /* execdata */,
                                         GPUNodeStack *in,
                                         GPUNodeStack *out)
{
  NodeSdfVectorOp *sdf = (NodeSdfVectorOp *)node->storage;

  const char *name = node_shader_sdf_vector_op_get_name(sdf->operation);

  if (name != nullptr) {
    float axis = sdf->axis;
    return GPU_stack_link(mat, node, name, in, out, GPU_constant(&axis));
  }
  else {
    return 0;
  }
}

static void node_shader_label_sdf_vector_op(const struct bNodeTree * /* ntree */,
                                            const struct bNode *node,
                                            char *label,
                                            int maxlen)
{
  NodeSdfVectorOp &node_storage = *(NodeSdfVectorOp *)node->storage;
  const char *name;
  bool enum_label = RNA_enum_name(
      rna_enum_node_sdf_vector_op_items, node_storage.operation, &name);
  if (!enum_label) {
    name = "Unknown SDF Vector Op";
  }
  BLI_strncpy(label, IFACE_(name), maxlen);
}

static void node_shader_update_sdf_vector_op(bNodeTree *ntree, bNode *node)
{
  NodeSdfVectorOp *sdf = (NodeSdfVectorOp *)node->storage;

  bNodeSocket *sockVector1 = (bNodeSocket *)BLI_findlink(&node->inputs, 0);
  bNodeSocket *sockVector2 = (bNodeSocket *)BLI_findlink(&node->inputs, 1);
  bNodeSocket *sockVector3 = (bNodeSocket *)BLI_findlink(&node->inputs, 2);
  bNodeSocket *sockScale = (bNodeSocket *)BLI_findlink(&node->inputs, 3);
  bNodeSocket *sockValue = (bNodeSocket *)BLI_findlink(&node->inputs, 4);
  bNodeSocket *sockValue2 = (bNodeSocket *)BLI_findlink(&node->inputs, 5);
  bNodeSocket *sockAngle = (bNodeSocket *)BLI_findlink(&node->inputs, 6);
  bNodeSocket *sockCount = (bNodeSocket *)BLI_findlink(&node->inputs, 7);
  bNodeSocket *sockCount2 = (bNodeSocket *)BLI_findlink(&node->inputs, 8);

  bNodeSocket *sockVectorOut = (bNodeSocket *)BLI_findlink(&node->outputs, 0);
  bNodeSocket *sockPositionOut = (bNodeSocket *)BLI_findlink(&node->outputs, 1);
  bNodeSocket *sockValueOut = (bNodeSocket *)BLI_findlink(&node->outputs, 2);

  /* Out sockets. */
  nodeSetSocketAvailability(ntree, sockValueOut,
                            ELEM(sdf->operation,
                                 SHD_SDF_VEC_OP_MIRROR,
                                 SHD_SDF_VEC_OP_REFLECT,
                                 SHD_SDF_VEC_OP_POLAR,
                                 SHD_SDF_VEC_OP_EXTRUDE));

  nodeSetSocketAvailability(ntree, sockPositionOut,
                            ELEM(sdf->operation, SHD_SDF_VEC_OP_MIRROR, SHD_SDF_VEC_OP_GRID));

  /* In sockets. */
  nodeSetSocketAvailability(ntree, sockVector2,
                            ELEM(sdf->operation,
                                 SHD_SDF_VEC_OP_SWIRL,
                                 SHD_SDF_VEC_OP_RADIAL_SHEAR,
                                 SHD_SDF_VEC_OP_PINCH_INFLATE,
                                 SHD_SDF_VEC_OP_ROTATE_UV,
                                 SHD_SDF_VEC_OP_GRID,
                                 SHD_SDF_VEC_OP_RND_UV,
                                 SHD_SDF_VEC_OP_RND_UV_FLIP,
                                 SHD_SDF_VEC_OP_MIRROR,
                                 SHD_SDF_VEC_OP_EXTRUDE,
                                 SHD_SDF_VEC_OP_REFLECT,
                                 SHD_SDF_VEC_OP_REPEAT_FINITE,
                                 SHD_SDF_VEC_OP_REPEAT_INF,
                                 SHD_SDF_VEC_OP_REPEAT_INF_MIRROR));

  nodeSetSocketAvailability(ntree, sockVector3,
                            ELEM(sdf->operation,
                                 SHD_SDF_VEC_OP_SWIRL,
                                 SHD_SDF_VEC_OP_RADIAL_SHEAR,
                                 SHD_SDF_VEC_OP_REPEAT_FINITE));

  /* Values */
  nodeSetSocketAvailability(ntree, sockScale,
                            ELEM(sdf->operation, SHD_SDF_VEC_OP_SCALE_UV, SHD_SDF_VEC_OP_TILESET));

  nodeSetSocketAvailability(ntree, sockValue,
                            !ELEM(sdf->operation,
                                  SHD_SDF_VEC_OP_MAP_11,
                                  SHD_SDF_VEC_OP_MAP_05,
                                  SHD_SDF_VEC_OP_MAP_UV,
                                  SHD_SDF_VEC_OP_RND_UV_FLIP,
                                  SHD_SDF_VEC_OP_ROTATE_UV,
                                  SHD_SDF_VEC_OP_RND_UV,
                                  SHD_SDF_VEC_OP_SCALE_UV,
                                  SHD_SDF_VEC_OP_EXTRUDE,
                                  SHD_SDF_VEC_OP_GRID,
                                  SHD_SDF_VEC_OP_MIRROR,
                                  SHD_SDF_VEC_OP_ROTATE,
                                  SHD_SDF_VEC_OP_SWIZZLE,
                                  SHD_SDF_VEC_OP_BEND,
                                  SHD_SDF_VEC_OP_REPEAT_FINITE,
                                  SHD_SDF_VEC_OP_REPEAT_INF,
                                  SHD_SDF_VEC_OP_REPEAT_INF_MIRROR));

  nodeSetSocketAvailability(ntree,
                            sockValue2, ELEM(sdf->operation, SHD_SDF_VEC_OP_TILESET, SHD_SDF_VEC_OP_PINCH_INFLATE));

  nodeSetSocketAvailability(ntree, sockAngle,
                            ELEM(sdf->operation,
                                 SHD_SDF_VEC_OP_BEND,
                                 SHD_SDF_VEC_OP_TWIST,
                                 SHD_SDF_VEC_OP_ROTATE,
                                 SHD_SDF_VEC_OP_ROTATE_UV));
  nodeSetSocketAvailability(ntree, sockCount, ELEM(sdf->operation, SHD_SDF_VEC_OP_TILESET));
  nodeSetSocketAvailability(ntree, sockCount2, ELEM(sdf->operation, SHD_SDF_VEC_OP_TILESET));

  node_sock_label_clear(sockValue);
  node_sock_label_clear(sockValue2);
  node_sock_label_clear(sockValueOut);
  node_sock_label_clear(sockVector1);
  node_sock_label_clear(sockVector2);
  node_sock_label_clear(sockVectorOut);
  node_sock_label(sockVector1, "Vector");
  node_sock_label(sockVector2, "Vector");
  sockVector2->flag &= ~SOCK_HIDE_VALUE;

  switch (sdf->operation) {
    case SHD_SDF_VEC_OP_GRID:
      node_sock_label(sockVector2, "Scale");
      node_sock_label(sockVectorOut, "UVW");
      break;
    case SHD_SDF_VEC_OP_ROTATE_UV:
      node_sock_label(sockVector2, "Center");
      node_sock_label(sockVector1, "UV");
      node_sock_label(sockVectorOut, "UV");
      break;
    case SHD_SDF_VEC_OP_MAP_UV:
      node_sock_label(sockVectorOut, "UVW");
      break;
    case SHD_SDF_VEC_OP_MAP_11:
    case SHD_SDF_VEC_OP_MAP_05:
      node_sock_label(sockVector1, "UV");
      node_sock_label(sockVectorOut, "Vector");
      break;
    case SHD_SDF_VEC_OP_TILESET:
      node_sock_label(sockValue, "Index");
      node_sock_label(sockValue2, "Padding");
      node_sock_label(sockVector1, "UV");
      node_sock_label(sockVectorOut, "UV");
      sockVector2->flag |= SOCK_HIDE_VALUE;
      break;
    case SHD_SDF_VEC_OP_RND_UV:
    case SHD_SDF_VEC_OP_RND_UV_FLIP:
      node_sock_label(sockValue, "Padding");
      node_sock_label(sockVector1, "UV");
      node_sock_label(sockVector2, "Position");
      node_sock_label(sockVectorOut, "UV");
      sockVector2->flag |= SOCK_HIDE_VALUE;
      break;
    case SHD_SDF_VEC_OP_REPEAT_FINITE:
      node_sock_label(sockVector2, "Spacing");
      node_sock_label(sockVector3, "Count");
      break;
    case SHD_SDF_VEC_OP_REPEAT_INF_MIRROR:
    case SHD_SDF_VEC_OP_REPEAT_INF:
    case SHD_SDF_VEC_OP_MIRROR:
      node_sock_label(sockVector2, "Spacing");
      break;
    case SHD_SDF_VEC_OP_REFLECT:
      node_sock_label(sockValue, "Offset");
      node_sock_label(sockVector2, "Normal");
      node_sock_label(sockValueOut, "Mask");
      break;
    case SHD_SDF_VEC_OP_POLAR:
      node_sock_label(sockValue, "Count");
      node_sock_label(sockValueOut, "Mask");
      break;
    case SHD_SDF_VEC_OP_EXTRUDE:
      node_sock_label(sockValueOut, "Internal Distance");
      break;
    case SHD_SDF_VEC_OP_TWIST:
      node_sock_label(sockValue, "Twist");
      break;
    case SHD_SDF_VEC_OP_SWIRL:
    case SHD_SDF_VEC_OP_RADIAL_SHEAR:
    case SHD_SDF_VEC_OP_PINCH_INFLATE:
      node_sock_label(sockValue, "Strength");
      node_sock_label(sockValue2, "Radius");
      node_sock_label(sockVector2, "Center");
      node_sock_label(sockVector3, "Offset");
      break;
    case SHD_SDF_VEC_OP_SPIN:
      node_sock_label(sockValue, "Offset");
      break;
  }
}

static void node_shader_init_sdf_vector_op(bNodeTree* /* ntree */, bNode *node)
{
  NodeSdfVectorOp *sdf = (NodeSdfVectorOp *)MEM_callocN(sizeof(NodeSdfVectorOp), __func__);
  sdf->operation = SHD_SDF_VEC_OP_GRID;
  sdf->axis = SHD_SDF_AXIS_XYZ;
  node->storage = sdf;
}

static void node_shader_buts_sdf_vector_op(uiLayout *layout, bContext* /* C */, PointerRNA *ptr)
{
  uiItemR(layout, ptr, "operation", UI_ITEM_NONE, "", ICON_NONE);
  int type = RNA_enum_get(ptr, "operation");
  if (ELEM(type,
           SHD_SDF_VEC_OP_ROTATE_UV,
           SHD_SDF_VEC_OP_OCTANT,
           SHD_SDF_VEC_OP_GRID,
           SHD_SDF_VEC_OP_TWIST,
           SHD_SDF_VEC_OP_SWIRL,
           SHD_SDF_VEC_OP_RADIAL_SHEAR,
           SHD_SDF_VEC_OP_MIRROR,
           SHD_SDF_VEC_OP_SWIZZLE,
           SHD_SDF_VEC_OP_ROTATE,
           SHD_SDF_VEC_OP_POLAR,
           SHD_SDF_VEC_OP_BEND,
           SHD_SDF_VEC_OP_SPIN,
           SHD_SDF_VEC_OP_EXTRUDE)) {
    uiItemR(layout, ptr, "axis", UI_ITEM_NONE, "", ICON_NONE);
  }
}

/* node type definition */
void register_node_type_sh_sdf_vector_op(void)
{
  static bNodeType ntype;
  sh_node_type_base(&ntype, SH_NODE_SDF_VECTOR_OP, "Sdf Vector Operator", NODE_CLASS_OP_VECTOR);
  ntype.declare = blender::nodes::sh_node_sdf_vector_op_declare;
  node_type_storage(
      &ntype, "NodeSdfVectorOp", node_free_standard_storage, node_copy_standard_storage);
  ntype.gpu_fn = node_shader_gpu_sdf_vector_op;
  ntype.initfunc = node_shader_init_sdf_vector_op;
  ntype.labelfunc = node_shader_label_sdf_vector_op;
  ntype.updatefunc = node_shader_update_sdf_vector_op;
  ntype.draw_buttons = node_shader_buts_sdf_vector_op;
  nodeRegisterType(&ntype);
}
