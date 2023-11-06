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
#include "RNA_access.hh"
#include "BLI_string.h"

#include "../node_shader_util.hh"
#include "node_util.hh"

using namespace blender::bke;

namespace blender::nodes {

static void sh_node_sdf_op_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Distance").min(-100000.0f).max(100000.0f).default_value(0.5f);
  b.add_input<decl::Float>("Distance", "Distance_001")
      .min(-100000)
      .max(100000)
      .default_value(0.5f);
  b.add_input<decl::Float>("Value").min(-100000.0f).max(100000.0f).default_value(0.5f);
  b.add_input<decl::Float>("Value", "Value_001")
      .min(-100000.0f)
      .max(100000.0f)
      .default_value(0.5f);
  b.add_input<decl::Int>("Count").min(-100000).max(100000).default_value(1);

  b.add_output<decl::Float>("Distance");
}

}  // namespace blender::nodes

static const char *node_shader_sdf_op_get_name(int mode)
{
  switch (mode) {
    case SHD_SDF_OP_DILATE:
      return "node_sdf_op_dilate";
    case SHD_SDF_OP_ONION:
      return "node_sdf_op_onion";
    case SHD_SDF_OP_ANNULAR:
      return "node_sdf_op_annular";
    case SHD_SDF_OP_BLEND:
      return "node_sdf_op_blend";
    case SHD_SDF_OP_INVERT:
      return "node_sdf_op_invert";
    case SHD_SDF_OP_FLATTEN:
      return "node_sdf_op_flatten";
    case SHD_SDF_OP_MASK:
      return "node_sdf_op_mask";
    case SHD_SDF_OP_PULSE:
      return "node_sdf_op_pulse";

    case SHD_SDF_OP_PIPE:
      return "node_sdf_op_pipe";
    case SHD_SDF_OP_ENGRAVE:
      return "node_sdf_op_engrave";
    case SHD_SDF_OP_GROOVE:
      return "node_sdf_op_groove";
    case SHD_SDF_OP_TONGUE:
      return "node_sdf_op_tongue";
    case SHD_SDF_OP_UNION:
      return "node_sdf_op_union";
    case SHD_SDF_OP_INTERSECT:
      return "node_sdf_op_intersect";
    case SHD_SDF_OP_DIFF:
      return "node_sdf_op_diff";
    case SHD_SDF_OP_UNION_SMOOTH:
      return "node_sdf_op_union_smooth";
    case SHD_SDF_OP_INTERSECT_SMOOTH:
      return "node_sdf_op_intersect_smooth";
    case SHD_SDF_OP_DIFF_SMOOTH:
      return "node_sdf_op_diff_smooth";
    case SHD_SDF_OP_UNION_STAIRS:
      return "node_sdf_op_union_stairs";
    case SHD_SDF_OP_INTERSECT_STAIRS:
      return "node_sdf_op_intersect_stairs";
    case SHD_SDF_OP_DIFF_STAIRS:
      return "node_sdf_op_diff_stairs";
    case SHD_SDF_OP_UNION_CHAMFER:
      return "node_sdf_op_union_chamfer";
    case SHD_SDF_OP_INTERSECT_CHAMFER:
      return "node_sdf_op_intersect_chamfer";
    case SHD_SDF_OP_DIFF_CHAMFER:
      return "node_sdf_op_diff_chamfer";
    case SHD_SDF_OP_UNION_COLUMNS:
      return "node_sdf_op_union_columns";
    case SHD_SDF_OP_INTERSECT_COLUMNS:
      return "node_sdf_op_intersect_columns";
    case SHD_SDF_OP_DIFF_COLUMNS:
      return "node_sdf_op_diff_columns";
    case SHD_SDF_OP_UNION_ROUND:
      return "node_sdf_op_union_round";
    case SHD_SDF_OP_INTERSECT_ROUND:
      return "node_sdf_op_intersect_round";
    case SHD_SDF_OP_DIFF_ROUND:
      return "node_sdf_op_diff_round";
    case SHD_SDF_OP_DIVIDE:
      return "node_sdf_op_divide";
    case SHD_SDF_OP_EXCLUSION:
      return "node_sdf_op_exclusion";
  }

  return nullptr;
}

static int node_shader_gpu_sdf_op(GPUMaterial *mat,
                                  bNode *node,
                                  bNodeExecData * /* execdata */,
                                  GPUNodeStack *in,
                                  GPUNodeStack *out)
{
  NodeSdfOp *sdf = (NodeSdfOp *)node->storage;

  const char *name = node_shader_sdf_op_get_name(sdf->operation);

  if (name != nullptr) {
    float invert = (sdf->invert) ? 1.0f : -1.0f;
    return GPU_stack_link(mat, node, name, in, out, GPU_constant(&invert));
  }
  else {
    return 0;
  }
}

static void node_shader_label_sdf_op(const struct bNodeTree * /* ntree */,
                                     const struct bNode *node,
                                     char *label,
                                     int maxlen)
{
  NodeSdfOp &node_storage = *(NodeSdfOp *)node->storage;
  const char *name;
  bool enum_label = RNA_enum_name(rna_enum_node_sdf_op_items, node_storage.operation, &name);
  if (!enum_label) {
    name = "Unknown SDF Op";
  }
  BLI_strncpy(label, IFACE_(name), maxlen);
}

static void node_shader_update_sdf_op(bNodeTree *ntree, bNode *node)
{
  NodeSdfOp *sdf = (NodeSdfOp *)node->storage;

  bNodeSocket *sockInputA = (bNodeSocket *)BLI_findlink(&node->inputs, 0);
  bNodeSocket *sockInputB = (bNodeSocket *)BLI_findlink(&node->inputs, 1);
  bNodeSocket *sockValue = (bNodeSocket *)BLI_findlink(&node->inputs, 2);
  bNodeSocket *sockValue2 = (bNodeSocket *)BLI_findlink(&node->inputs, 3);
  bNodeSocket *sockCount = (bNodeSocket *)BLI_findlink(&node->inputs, 4);

  bNodeSocket *sockDistanceOut = (bNodeSocket *)BLI_findlink(&node->outputs, 0);

  /* Distance */
  nodeSetSocketAvailability(ntree, sockInputA, true);

  nodeSetSocketAvailability(ntree, sockInputB,
                            !ELEM(sdf->operation,
                                  SHD_SDF_OP_MASK,
                                  SHD_SDF_OP_INVERT,
                                  SHD_SDF_OP_DILATE,
                                  SHD_SDF_OP_ONION,
                                  SHD_SDF_OP_ANNULAR,
                                  SHD_SDF_OP_PULSE,
                                  SHD_SDF_OP_FLATTEN));

  /* Values */
  nodeSetSocketAvailability(ntree, sockValue,
                            !ELEM(sdf->operation,
                                  SHD_SDF_OP_UNION,
                                  SHD_SDF_OP_INTERSECT,
                                  SHD_SDF_OP_DIFF,
                                  SHD_SDF_OP_INVERT));

  nodeSetSocketAvailability(ntree, sockValue2,
                            ELEM(sdf->operation,
                                 SHD_SDF_OP_DIVIDE,
                                 SHD_SDF_OP_FLATTEN,
                                 SHD_SDF_OP_PULSE,
                                 SHD_SDF_OP_EXCLUSION,
                                 SHD_SDF_OP_TONGUE,
                                 SHD_SDF_OP_GROOVE,
                                 SHD_SDF_OP_UNION_COLUMNS,
                                 SHD_SDF_OP_INTERSECT_COLUMNS,
                                 SHD_SDF_OP_DIFF_COLUMNS,
                                 SHD_SDF_OP_UNION_STAIRS,
                                 SHD_SDF_OP_INTERSECT_STAIRS,
                                 SHD_SDF_OP_DIFF_STAIRS));

  nodeSetSocketAvailability(ntree, sockCount, ELEM(sdf->operation, SHD_SDF_OP_ONION));

  node_sock_label_clear(sockValue);
  node_sock_label_clear(sockValue2);
  node_sock_label_clear(sockDistanceOut);
  node_sock_label(sockInputA, "Distance");
  node_sock_label(sockInputB, "Distance");
  node_sock_label(sockDistanceOut, "Distance");

  switch (sdf->operation) {
    case SHD_SDF_OP_UNION_SMOOTH:
    case SHD_SDF_OP_INTERSECT_SMOOTH:
    case SHD_SDF_OP_DIFF_SMOOTH:
      node_sock_label(sockValue, "Smooth");
      break;
    case SHD_SDF_OP_BLEND:
      node_sock_label(sockValue, "Factor");
      break;
    case SHD_SDF_OP_MASK:
      node_sock_label(sockValue, "Feather");
      node_sock_label(sockValue2, "Expand");
      node_sock_label(sockDistanceOut, "Value");
      break;
    case SHD_SDF_OP_FLATTEN:
      node_sock_label(sockValue, "Min");
      node_sock_label(sockValue2, "Max");
      node_sock_label(sockDistanceOut, "Value");
      break;
    case SHD_SDF_OP_PULSE:
      node_sock_label(sockInputA, "Value");
      node_sock_label(sockValue, "Center");
      node_sock_label(sockValue2, "Width");
      node_sock_label(sockDistanceOut, "Value");
      break;
    case SHD_SDF_OP_DIVIDE:
    case SHD_SDF_OP_EXCLUSION:
      node_sock_label(sockValue, "Gap");
      node_sock_label(sockValue2, "Gap");
      break;
    case SHD_SDF_OP_UNION_COLUMNS:
    case SHD_SDF_OP_INTERSECT_COLUMNS:
    case SHD_SDF_OP_DIFF_COLUMNS:
    case SHD_SDF_OP_UNION_STAIRS:
    case SHD_SDF_OP_INTERSECT_STAIRS:
    case SHD_SDF_OP_DIFF_STAIRS:
      node_sock_label(sockValue2, "Count");
      break;
    case SHD_SDF_OP_TONGUE:
    case SHD_SDF_OP_GROOVE:
      node_sock_label(sockValue, "Size");
      node_sock_label(sockValue2, "Gap");
      break;
    case SHD_SDF_OP_PIPE:
      node_sock_label(sockValue, "Size");
      break;
  }
}

static void node_shader_init_sdf_op(bNodeTree * /* ntree */, bNode *node)
{
  NodeSdfOp *sdf = (NodeSdfOp *)MEM_callocN(sizeof(NodeSdfOp), __func__);
  sdf->operation = SHD_SDF_OP_UNION;
  sdf->invert = 0;
  node->storage = sdf;
}

static void node_shader_buts_sdf_op(uiLayout *layout, bContext * /* C */, PointerRNA *ptr)
{
  uiItemR(layout, ptr, "operation", UI_ITEM_NONE, "", ICON_NONE);
  int type = RNA_enum_get(ptr, "operation");
  if (ELEM(type, SHD_SDF_OP_MASK)) {
    uiItemR(layout, ptr, "invert", UI_ITEM_NONE, NULL, ICON_NONE);
  }
}

/* node type definition */
void register_node_type_sh_sdf_op(void)
{
  static bNodeType ntype;

  sh_node_type_base(&ntype, SH_NODE_SDF_OP, "Sdf Operator", NODE_CLASS_CONVERTER);
  ntype.declare = blender::nodes::sh_node_sdf_op_declare;
  node_type_storage(&ntype, "NodeSdfOp", node_free_standard_storage, node_copy_standard_storage);
  ntype.gpu_fn = node_shader_gpu_sdf_op;
  ntype.initfunc = node_shader_init_sdf_op;
  ntype.labelfunc = node_shader_label_sdf_op;
  ntype.updatefunc = node_shader_update_sdf_op;
  ntype.draw_buttons = node_shader_buts_sdf_op;
  nodeRegisterType(&ntype);
}
