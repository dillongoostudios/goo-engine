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
#include "BKE_texture.h"

#include "../node_shader_util.hh"

using namespace blender::bke;

namespace blender::nodes {

static void sh_node_sdf_primitive_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Vector").hide_value(true).no_muted_links(true);
  b.add_input<decl::Float>("Size").min(-100000.0f).max(100000.0f).default_value(1.0f);
  b.add_input<decl::Float>("Radius").min(-100000.0f).max(100000.0f).default_value(0.5f);
  b.add_input<decl::Float>("Value1").min(-100000.0f).max(100000.0f).default_value(1.0f);
  b.add_input<decl::Float>("Value2").min(-100000.0f).max(100000.0f).default_value(1.0f);
  b.add_input<decl::Float>("Value3").min(-100000.0f).max(100000.0f).default_value(1.0f);
  b.add_input<decl::Float>("Value4").min(-100000.0f).max(100000.0f).default_value(0.5f);
  b.add_input<decl::Vector>("Point").default_value({0.5f, 0.5f, 0.5f}).no_muted_links(true);
  b.add_input<decl::Vector>("Point", "Point_001")
      .default_value({0.5f, 0.5f, 0.5f})
      .no_muted_links(true);
  b.add_input<decl::Vector>("Point", "Point_002")
      .default_value({0.5f, 0.5f, 0.5f})
      .no_muted_links(true);
  b.add_input<decl::Vector>("Point", "Point_003")
      .default_value({0.5f, 0.5f, 0.5f})
      .no_muted_links(true);
  b.add_input<decl::Float>("Angle").subtype(PROP_ANGLE);
  b.add_input<decl::Float>("Roundness")
      .min(0.0f)
      .max(1.0f)
      .default_value(0.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Linewidth").min(-100000.0f).max(100000.0f).default_value(0.0f);

  b.add_output<decl::Float>("Distance");
}

}  // namespace blender::nodes

static void node_shader_init_sdf_primitive(bNodeTree * /* ntree */, bNode *node)
{
  NodeSdfPrimitive *sdf = (NodeSdfPrimitive *)MEM_callocN(sizeof(NodeSdfPrimitive), __func__);
  BKE_texture_mapping_default(&sdf->base.tex_mapping, TEXMAP_TYPE_POINT);
  BKE_texture_colormapping_default(&sdf->base.color_mapping);
  sdf->mode = SHD_SDF_3D_SPHERE;
  sdf->invert = 0;
  node->storage = sdf;
}

static void node_shader_buts_sdf_primitive(uiLayout *layout, bContext * /* C */, PointerRNA *ptr)
{
  uiItemR(layout, ptr, "mode", UI_ITEM_NONE, "", ICON_NONE);
  uiItemR(layout, ptr, "invert", UI_ITEM_NONE, NULL, ICON_NONE);
}


static const char *node_shader_sdf_primitive_get_name(int mode)
{
  switch (mode) {
    case SHD_SDF_3D_SPHERE:
      return "node_sdf_prim_3d_sphere";
    case SHD_SDF_3D_BOX:
      return "node_sdf_prim_3d_box";
    case SHD_SDF_3D_HEX_PRISM:
      return "node_sdf_prim_3d_hex_prism";
    case SHD_SDF_3D_HEX_PRISM_INCIRCLE:
      return "node_sdf_prim_3d_hex_prism_incircle";
    case SHD_SDF_3D_TORUS:
      return "node_sdf_prim_3d_torus";
    case SHD_SDF_3D_CONE:
      return "node_sdf_prim_3d_cone";
    case SHD_SDF_3D_POINT_CONE:
      return "node_sdf_prim_3d_point_cone";
    case SHD_SDF_3D_CYLINDER:
      return "node_sdf_prim_3d_cylinder";
    case SHD_SDF_3D_POINT_CYLINDER:
      return "node_sdf_prim_3d_point_cylinder";
    case SHD_SDF_3D_CAPSULE:
      return "node_sdf_prim_3d_capsule";
    case SHD_SDF_3D_OCTAHEDRON:
      return "node_sdf_prim_3d_octahedron";
    case SHD_SDF_3D_PLANE:
      return "node_sdf_prim_3d_plane";
    case SHD_SDF_3D_SOLID_ANGLE:
      return "node_sdf_prim_3d_solid_angle";
    case SHD_SDF_3D_PYRAMID:
      return "node_sdf_prim_3d_pyramid";
    case SHD_SDF_3D_DISC:
      return "node_sdf_prim_3d_disc";
    case SHD_SDF_3D_CIRCLE:
      return "node_sdf_prim_3d_circle";

    case SHD_SDF_2D_CIRCLE:
      return "node_sdf_prim_2d_circle";
    case SHD_SDF_2D_RECTANGLE:
      return "node_sdf_prim_2d_rectangle";
    case SHD_SDF_2D_LINE:
      return "node_sdf_prim_2d_line";
    case SHD_SDF_2D_RHOMBUS:
      return "node_sdf_prim_2d_rhombus";
    case SHD_SDF_2D_STAR:
      return "node_sdf_prim_2d_star";
    case SHD_SDF_2D_TRIANGLE:
      return "node_sdf_prim_2d_triangle";
    case SHD_SDF_2D_HEXAGON:
      return "node_sdf_prim_2d_hexagon";
    case SHD_SDF_2D_PIE:
      return "node_sdf_prim_2d_pie";
    case SHD_SDF_2D_ARC:
      return "node_sdf_prim_2d_arc";
    case SHD_SDF_2D_BEZIER:
      return "node_sdf_prim_2d_bezier";
    case SHD_SDF_2D_UNEVEN_CAPSULE:
      return "node_sdf_prim_2d_uneven_capsule";
    case SHD_SDF_2D_POINT_TRIANGLE:
      return "node_sdf_prim_2d_point_triangle";
    case SHD_SDF_2D_TRAPEZOID:
      return "node_sdf_prim_2d_trapezoid";
    case SHD_SDF_2D_MOON:
      return "node_sdf_prim_2d_moon";
    case SHD_SDF_2D_VESICA:
      return "node_sdf_prim_2d_vesica";
    case SHD_SDF_2D_CROSS:
      return "node_sdf_prim_2d_cross";
    case SHD_SDF_2D_ROUNDX:
      return "node_sdf_prim_2d_rounded_x";
    case SHD_SDF_2D_HORSESHOE:
      return "node_sdf_prim_2d_horseshoe";
    case SHD_SDF_2D_PARABOLA:
      return "node_sdf_prim_2d_parabola";
    case SHD_SDF_2D_PARABOLA_SEGMENT:
      return "node_sdf_prim_2d_parabola_segment";
    case SHD_SDF_2D_ELLIPSE:
      return "node_sdf_prim_2d_ellipse";
    case SHD_SDF_2D_ISOSCELES:
      return "node_sdf_prim_2d_isosceles";
    case SHD_SDF_2D_ROUND_JOINT:
      return "node_sdf_prim_2d_round_joint";
    case SHD_SDF_2D_FLAT_JOINT:
      return "node_sdf_prim_2d_flat_joint";
    case SHD_SDF_2D_PENTAGON:
      return "node_sdf_prim_2d_pentagon";
    case SHD_SDF_2D_QUAD:
      return "node_sdf_prim_2d_quad";
    case SHD_SDF_2D_HEART:
      return "node_sdf_prim_2d_heart";
    case SHD_SDF_2D_CORNER:
      return "node_sdf_prim_2d_corner";
  }

  return nullptr;
}

static int node_shader_gpu_sdf_primitive(GPUMaterial *mat,
                                         bNode *node,
                                         bNodeExecData * /* execdata */,
                                         GPUNodeStack *in,
                                         GPUNodeStack *out)
{
  node_shader_gpu_default_tex_coord(mat, node, &in[0].link);
  node_shader_gpu_tex_mapping(mat, node, in, out);
  NodeSdfPrimitive *sdf = (NodeSdfPrimitive *)node->storage;

  const char *name = node_shader_sdf_primitive_get_name(sdf->mode);

  if (name != nullptr) {
    float invert = (sdf->invert) ? 1.0f : -1.0f;
    return GPU_stack_link(mat, node, name, in, out, GPU_constant(&invert));
  }
  else {
    return 0;
  }
}

static void node_shader_label_sdf_primitive(const struct bNodeTree */* ntree */,
                                            const struct bNode *node,
                                            char *label,
                                            int maxlen)
{
  NodeSdfPrimitive &node_storage = *(NodeSdfPrimitive *)node->storage;
  const char *name;
  bool enum_label = RNA_enum_name(rna_enum_node_sdf_primitive_items, node_storage.mode, &name);

  if (!enum_label) {
    name = "Unknown SDF Primitive";
  }
  BLI_strncpy(label, IFACE_(name), maxlen);
}

static void node_shader_update_sdf_primitive(bNodeTree *ntree, bNode *node)
{
  NodeSdfPrimitive *sdf = (NodeSdfPrimitive *)node->storage;

  bNodeSocket *sockRadius = (bNodeSocket *)BLI_findlink(&node->inputs, 2);
  bNodeSocket *sockValue1 = (bNodeSocket *)BLI_findlink(&node->inputs, 3);
  bNodeSocket *sockValue2 = (bNodeSocket *)BLI_findlink(&node->inputs, 4);
  bNodeSocket *sockValue3 = (bNodeSocket *)BLI_findlink(&node->inputs, 5);
  bNodeSocket *sockValue4 = (bNodeSocket *)BLI_findlink(&node->inputs, 6);
  bNodeSocket *sockPoint1 = (bNodeSocket *)BLI_findlink(&node->inputs, 7);
  bNodeSocket *sockPoint2 = (bNodeSocket *)BLI_findlink(&node->inputs, 8);
  bNodeSocket *sockPoint3 = (bNodeSocket *)BLI_findlink(&node->inputs, 9);
  bNodeSocket *sockPoint4 = (bNodeSocket *)BLI_findlink(&node->inputs, 10);
  bNodeSocket *sockAngle1 = (bNodeSocket *)BLI_findlink(&node->inputs, 11);
  bNodeSocket *sockRound = (bNodeSocket *)BLI_findlink(&node->inputs, 12);
  bNodeSocket *sockLinewidth = (bNodeSocket *)BLI_findlink(&node->inputs, 13);

  nodeSetSocketAvailability(ntree, sockRound,
                            ELEM(sdf->mode,
                                 SHD_SDF_3D_BOX,
                                 SHD_SDF_3D_CONE,
                                 SHD_SDF_3D_CYLINDER,
                                 SHD_SDF_3D_POINT_CYLINDER,
                                 SHD_SDF_3D_OCTAHEDRON,
                                 SHD_SDF_3D_PYRAMID,
                                 SHD_SDF_3D_HEX_PRISM,
                                 SHD_SDF_3D_HEX_PRISM_INCIRCLE,
                                 SHD_SDF_3D_DISC,
                                 SHD_SDF_3D_CIRCLE,
                                 SHD_SDF_3D_SOLID_ANGLE) ||
                                ELEM(sdf->mode,
                                     SHD_SDF_2D_TRIANGLE,
                                     SHD_SDF_2D_RECTANGLE,
                                     SHD_SDF_2D_PENTAGON,
                                     SHD_SDF_2D_HEART,
                                     SHD_SDF_2D_HEXAGON,
                                     SHD_SDF_2D_ISOSCELES,
                                     SHD_SDF_2D_ARC,
                                     SHD_SDF_2D_CROSS,
                                     SHD_SDF_2D_HEXAGON,
                                     SHD_SDF_2D_PENTAGON,
                                     SHD_SDF_2D_PIE,
                                     SHD_SDF_2D_ROUNDX,
                                     SHD_SDF_2D_STAR,
                                     SHD_SDF_2D_TRIANGLE,
                                     SHD_SDF_2D_UNEVEN_CAPSULE,
                                     SHD_SDF_2D_HORSESHOE));
  nodeSetSocketAvailability(ntree, sockLinewidth, true);

  /* Points */
  nodeSetSocketAvailability(ntree, sockPoint1,
                            ELEM(sdf->mode,
                                 SHD_SDF_3D_PLANE,
                                 SHD_SDF_3D_POINT_CONE,
                                 SHD_SDF_3D_POINT_CYLINDER,
                                 SHD_SDF_2D_LINE,
                                 SHD_SDF_2D_UNEVEN_CAPSULE,
                                 SHD_SDF_3D_CAPSULE,
                                 SHD_SDF_2D_BEZIER,
                                 SHD_SDF_2D_POINT_TRIANGLE,
                                 SHD_SDF_2D_QUAD));
  nodeSetSocketAvailability(ntree, sockPoint2,
                            ELEM(sdf->mode,
                                 SHD_SDF_3D_POINT_CONE,
                                 SHD_SDF_3D_POINT_CYLINDER,
                                 SHD_SDF_2D_LINE,
                                 SHD_SDF_2D_UNEVEN_CAPSULE,
                                 SHD_SDF_3D_CAPSULE,
                                 SHD_SDF_2D_BEZIER,
                                 SHD_SDF_2D_POINT_TRIANGLE,
                                 SHD_SDF_2D_QUAD));
  nodeSetSocketAvailability(ntree,
                            sockPoint3, ELEM(sdf->mode, SHD_SDF_2D_BEZIER, SHD_SDF_2D_POINT_TRIANGLE, SHD_SDF_2D_QUAD));
  nodeSetSocketAvailability(ntree, sockPoint4, ELEM(sdf->mode, SHD_SDF_2D_QUAD));

  /* Radius */
  nodeSetSocketAvailability(ntree, sockRadius,
                            ELEM(sdf->mode,
                                 SHD_SDF_2D_CIRCLE,
                                 SHD_SDF_2D_ARC,
                                 SHD_SDF_2D_CROSS,
                                 SHD_SDF_2D_HEXAGON,
                                 SHD_SDF_2D_PENTAGON,
                                 SHD_SDF_2D_HEART,
                                 SHD_SDF_2D_PIE,
                                 SHD_SDF_2D_ROUNDX,
                                 SHD_SDF_2D_STAR,
                                 SHD_SDF_2D_TRIANGLE,
                                 SHD_SDF_2D_UNEVEN_CAPSULE,
                                 SHD_SDF_2D_VESICA,
                                 SHD_SDF_2D_MOON,
                                 SHD_SDF_2D_HORSESHOE) ||
                                ELEM(sdf->mode,
                                     SHD_SDF_3D_CONE,
                                     SHD_SDF_3D_DISC,
                                     SHD_SDF_3D_CIRCLE,
                                     SHD_SDF_3D_SOLID_ANGLE,
                                     SHD_SDF_3D_TORUS,
                                     SHD_SDF_3D_CAPSULE,
                                     SHD_SDF_3D_OCTAHEDRON,
                                     SHD_SDF_3D_CYLINDER,
                                     SHD_SDF_3D_POINT_CYLINDER,
                                     SHD_SDF_3D_SPHERE,
                                     SHD_SDF_3D_POINT_CONE));

  /* Values */
  nodeSetSocketAvailability(ntree, sockValue1,
                            ELEM(sdf->mode,
                                 SHD_SDF_3D_PYRAMID,
                                 SHD_SDF_3D_PLANE,
                                 SHD_SDF_3D_POINT_CONE,
                                 SHD_SDF_3D_BOX,
                                 SHD_SDF_3D_TORUS,
                                 SHD_SDF_3D_HEX_PRISM,
                                 SHD_SDF_3D_HEX_PRISM_INCIRCLE) ||
                                ELEM(sdf->mode,
                                     SHD_SDF_2D_FLAT_JOINT,
                                     SHD_SDF_2D_ROUND_JOINT,
                                     SHD_SDF_2D_MOON,
                                     SHD_SDF_2D_VESICA,
                                     SHD_SDF_2D_PARABOLA,
                                     SHD_SDF_2D_PARABOLA_SEGMENT,
                                     SHD_SDF_2D_ISOSCELES,
                                     SHD_SDF_2D_HORSESHOE,
                                     SHD_SDF_2D_RECTANGLE,
                                     SHD_SDF_2D_RHOMBUS,
                                     SHD_SDF_2D_TRAPEZOID,
                                     SHD_SDF_2D_STAR,
                                     SHD_SDF_2D_UNEVEN_CAPSULE,
                                     SHD_SDF_2D_ELLIPSE));
  nodeSetSocketAvailability(ntree, sockValue2,
                            ELEM(sdf->mode,
                                 SHD_SDF_2D_PARABOLA_SEGMENT,
                                 SHD_SDF_2D_ELLIPSE,
                                 SHD_SDF_2D_MOON,
                                 SHD_SDF_2D_STAR,
                                 SHD_SDF_2D_TRAPEZOID,
                                 SHD_SDF_2D_RECTANGLE,
                                 SHD_SDF_2D_RHOMBUS) ||
                                ELEM(sdf->mode, SHD_SDF_3D_BOX));
  nodeSetSocketAvailability(ntree, sockValue3,
                            ELEM(sdf->mode,
                                 SHD_SDF_2D_ISOSCELES,
                                 SHD_SDF_2D_TRAPEZOID,
                                 SHD_SDF_3D_CONE,
                                 SHD_SDF_3D_CYLINDER,
                                 SHD_SDF_2D_STAR,
                                 SHD_SDF_3D_PYRAMID,
                                 SHD_SDF_3D_HEX_PRISM,
                                 SHD_SDF_3D_HEX_PRISM_INCIRCLE,
                                 SHD_SDF_3D_BOX));
  nodeSetSocketAvailability(ntree, sockValue4, false);

  /* Angles */
  nodeSetSocketAvailability(ntree, sockAngle1,
                            ELEM(sdf->mode,
                                 SHD_SDF_3D_SOLID_ANGLE,
                                 SHD_SDF_2D_FLAT_JOINT,
                                 SHD_SDF_2D_ROUND_JOINT,
                                 SHD_SDF_2D_PIE,
                                 SHD_SDF_2D_ARC,
                                 SHD_SDF_2D_HORSESHOE));

  /* Labels */
  node_sock_label_clear(sockRadius);
  node_sock_label_clear(sockValue1);
  node_sock_label_clear(sockValue2);
  node_sock_label_clear(sockValue3);
  node_sock_label_clear(sockValue4);
  node_sock_label_clear(sockPoint1);
  node_sock_label_clear(sockPoint2);
  node_sock_label_clear(sockPoint3);

  switch (sdf->mode) {
    case SHD_SDF_2D_ROUND_JOINT:
    case SHD_SDF_2D_FLAT_JOINT:
      node_sock_label(sockValue3, "Length");
      break;
    case SHD_SDF_2D_RECTANGLE:
    case SHD_SDF_2D_RHOMBUS:
    case SHD_SDF_2D_ELLIPSE:
    case SHD_SDF_2D_ISOSCELES:
    case SHD_SDF_2D_PARABOLA_SEGMENT:
      node_sock_label(sockValue1, "Width");
      node_sock_label(sockValue2, "Depth");
      break;
    case SHD_SDF_3D_BOX:
      node_sock_label(sockValue1, "Width");
      node_sock_label(sockValue2, "Depth");
      node_sock_label(sockValue3, "Height");
      break;
    case SHD_SDF_2D_HORSESHOE:
      node_sock_label(sockValue1, "Overshoot");
      break;
    case SHD_SDF_2D_STAR:
      node_sock_label(sockValue1, "Sides");
      node_sock_label(sockValue2, "Inset");
      node_sock_label(sockValue3, "Inradius");
      break;
    case SHD_SDF_2D_PARABOLA:
      node_sock_label(sockValue1, "K");
      break;
    case SHD_SDF_2D_BEZIER:
      node_sock_label(sockPoint3, "Control Point");
      break;
    case SHD_SDF_2D_TRAPEZOID:
      node_sock_label(sockValue1, "Width A");
      node_sock_label(sockValue2, "Depth");
      node_sock_label(sockValue3, "Width B");
      break;
    case SHD_SDF_3D_POINT_CONE:
    case SHD_SDF_2D_UNEVEN_CAPSULE:
      node_sock_label(sockRadius, "Radius A");
      node_sock_label(sockValue1, "Radius B");
      node_sock_label(sockPoint1, "Point A");
      node_sock_label(sockPoint2, "Point B");
      break;
    case SHD_SDF_3D_POINT_CYLINDER:
    case SHD_SDF_3D_CAPSULE:
      node_sock_label(sockRadius, "Radius");
      node_sock_label(sockPoint1, "Point A");
      node_sock_label(sockPoint2, "Point B");
      break;
    case SHD_SDF_3D_TORUS:
      node_sock_label(sockRadius, "Radius A");
      node_sock_label(sockValue1, "Radius B");
      break;
    case SHD_SDF_2D_MOON:
      node_sock_label(sockRadius, "Radius A");
      node_sock_label(sockValue1, "Radius B");
      node_sock_label(sockValue2, "Distance");
      break;
    case SHD_SDF_2D_VESICA:
      node_sock_label(sockValue1, "Distance");
      break;
    case SHD_SDF_3D_PLANE:
      node_sock_label(sockValue1, "Distance");
      node_sock_label(sockPoint1, "Normal");
      break;
    default:
      node_sock_label(sockValue1, "Width");
      node_sock_label(sockValue2, "Depth");
      node_sock_label(sockValue3, "Height");
      break;
  }
}

/* node type definition */
void register_node_type_sh_sdf_primitive(void)
{
  static bNodeType ntype;

  sh_node_type_base(&ntype, SH_NODE_SDF_PRIMITIVE, "Sdf Primitive", NODE_CLASS_TEXTURE);
  ntype.declare = blender::nodes::sh_node_sdf_primitive_declare;
  node_type_storage(
      &ntype, "NodeSdfPrimitive", node_free_standard_storage, node_copy_standard_storage);
  ntype.gpu_fn = node_shader_gpu_sdf_primitive;
  ntype.initfunc = node_shader_init_sdf_primitive;
  ntype.labelfunc = node_shader_label_sdf_primitive;
  ntype.updatefunc = node_shader_update_sdf_primitive;
  ntype.draw_buttons = node_shader_buts_sdf_primitive;

  nodeRegisterType(&ntype);
}
