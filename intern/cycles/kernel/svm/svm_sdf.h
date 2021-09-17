/*
 * Copyright 2011-2021 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
ee the License for the specific language governing permissions and
 * limitations under the License.
 */

CCL_NAMESPACE_BEGIN

/* Sdf Shader Functions */

ccl_device float svm_sdf_primitive(float3 co,
                                   float size,
                                   float radius,
                                   float v1,
                                   float v2,
                                   float v3,
                                   float v4,
                                   float3 p1,
                                   float3 p2,
                                   float3 p3,
                                   float3 p4,
                                   float angle,
                                   float round,
                                   float linewidth,
                                   bool invert,
                                   NodeSdfMode mode)
{
  if (size == 0.0f) {
    return len(co);
  }

  float dist = 0.0f;
  float dimf = 0.0f;
  float2 dim2 = zero_float2();
  float3 dim3 = zero_float3();
  co /= size;

  switch (mode) {
    case NODE_SDF_3D_SPHERE:
      dist = sdf_3d_sphere(co, radius);
      break;
    case NODE_SDF_3D_BOX:
      dim3 = sdf_dimension(v1, v2, v3, &round);
      dist = sdf_3d_box(co, dim3) - round;
      break;
    case NODE_SDF_3D_TORUS:
      dist = sdf_3d_torus(co, make_float2(radius, v1));
      break;
    case NODE_SDF_3D_CONE:
      dim2 = sdf_dimension(radius, v3, &round);
      dist = sdf_3d_upright_cone(co, dim2.x, dim2.y, round) - round;
      break;
    case NODE_SDF_3D_POINT_CONE:
      dim2 = sdf_dimension(radius, v1, &round);
      dist = sdf_3d_point_cone(co, p1, p2, dim2.x, dim2.y) - round * 2.0f;
      break;
    case NODE_SDF_3D_PLANE:
      dist = dot(co, normalize(p1)) + v1;
      break;
    case NODE_SDF_3D_SOLID_ANGLE:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_3d_solid_angle(co, angle, dimf) - round * 2.0f;
      break;
    case NODE_SDF_3D_PYRAMID:
      /* Sanitize when inputs are out of bounds. */
      dim2 = sdf_dimension(v1, v3, &round);
      if (dim2.y == 0.0f) {
        dist = sdf_3d_box(co, make_float3(dim2.x, dim2.x, 0.0f)) - round * 2.0f;
      }
      else if (dim2.x == 0.0f) {
        co.z -= dim2.y * 0.5f;
        dist = sdf_3d_box(co, make_float3(0.0f, 0.0f, dim2.y)) - round * 2.0f;
      }
      else {
        dist = sdf_3d_pyramid(co, dim2.x, dim2.y) - round * 2.0f;
      }
      break;
    case NODE_SDF_3D_POINT_CYLINDER:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_3d_cylinder(co, p1, p2, dimf) - round;
      break;
    case NODE_SDF_3D_CAPSULE:
      dist = sdf_3d_capsule(co, p1, p2, radius);
      break;
    case NODE_SDF_3D_OCTAHEDRON:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_3d_octahedron(co, dimf) - round * 2.0f;
      break;
    case NODE_SDF_3D_HEX_PRISM:
      dim2 = sdf_dimension(v1, v3, &round);
      dist = sdf_3d_hex_prism(co, dim2) - round;
      break;
    case NODE_SDF_3D_HEX_PRISM_INCIRCLE:
      dim2 = sdf_dimension(v1, v3, &round);
      dist = sdf_3d_hex_prism_incircle(co, dim2) - round;
      break;
    case NODE_SDF_3D_CYLINDER:
      dim2 = sdf_dimension(radius, v3, &round);
      dist = sdf_capped_cylinder(co, dim2.x, dim2.y - round * 2.0f) - round * 2.0f;
      break;
    case NODE_SDF_3D_CIRCLE:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_3d_circle(co, dimf) - round * 2.0f;
      break;
    case NODE_SDF_3D_DISC:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_3d_disc(co, dimf) - round * 2.0f;
      break;
    case NODE_SDF_2D_CIRCLE:
      dist = len(float3_to_float2(co)) - radius;
      break;
    case NODE_SDF_2D_RECTANGLE:
      dim2 = sdf_dimension(v1, v2, &round);
      dist = sdf_2d_rectangle(float3_to_float2(co), dim2.x, dim2.y) - round;
      break;
    case NODE_SDF_2D_RHOMBUS:
      round = min(round, 0.9999f);
      dim2 = sdf_dimension(v1, v2, &round);
      dist = sdf_2d_rhombus(float3_to_float2(co), dim2) - round;
      break;
    case NODE_SDF_2D_TRIANGLE:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_2d_triangle(float3_to_float2(co), dimf) - round * 2.0f;
      break;
    case NODE_SDF_2D_LINE:
      dist = sdf_2d_line(float3_to_float2(co), float3_to_float2(p1), float3_to_float2(p2));
      break;
    case NODE_SDF_2D_STAR:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_2d_star(float3_to_float2(co), dimf, v1, v2, v3) - round * 2.0f;
      break;
    case NODE_SDF_2D_HEXAGON:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_2d_hexagon(float3_to_float2(co), dimf) - round * 2.0f;
      break;
    case NODE_SDF_2D_CORNER:
      dist = sdf_2d_corner(float3_to_float2(co));
      break;
    case NODE_SDF_2D_PIE:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_2d_pie(float3_to_float2(co), dimf, angle) - round * 2.0f;
      break;
    case NODE_SDF_2D_ARC:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_2d_arc(float3_to_float2(co), angle, dimf) - round * 2.0f;
      break;
    case NODE_SDF_2D_BEZIER:
      dist = sdf_2d_bezier(
          float3_to_float2(co), float3_to_float2(p1), float3_to_float2(p2), float3_to_float2(p3));
      break;
    case NODE_SDF_2D_UNEVEN_CAPSULE:
      dim2 = sdf_dimension(radius, v1, &round);
      dist = sdf_2d_uneven_capsule(
          float3_to_float2(co), float3_to_float2(p1), float3_to_float2(p2), dim2.x, dim2.y);
      break;
    case NODE_SDF_2D_POINT_TRIANGLE:
      dist = sdf_2d_point_triangle(
          float3_to_float2(co), float3_to_float2(p1), float3_to_float2(p2), float3_to_float2(p3));
      break;
    case NODE_SDF_2D_TRAPEZOID:
      dim3 = sdf_dimension(v1, v2, v3, &round);
      dist = sdf_2d_trapezoid(float3_to_float2(co), dim3.x, dim3.y, dim3.z) - round;
      break;
    case NODE_SDF_2D_VESICA:
      dim2 = sdf_dimension(radius, v1, &round);
      dist = sdf_2d_vesica(float3_to_float2(co), dim2.x, dim2.y) - round;
      break;
    case NODE_SDF_2D_CROSS:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_2d_blobby_cross(float3_to_float2(co), dimf) - round;
      break;
    case NODE_SDF_2D_ROUNDX:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_2d_rounded_x(float3_to_float2(co), dimf) - round * 2.0f;
      break;
    case NODE_SDF_2D_HORSESHOE:
      dim2 = sdf_dimension(radius, v1, &round);
      dist = sdf_2d_horseshoe(float3_to_float2(co), dim2.x, angle, dim2.y, linewidth) -
             round * 2.0f;
      break;
    case NODE_SDF_2D_PARABOLA:
      dist = sdf_2d_parabola(float3_to_float2(co), v1);
      break;
    case NODE_SDF_2D_ELLIPSE:
      dist = sdf_2d_ellipse(float3_to_float2(co), make_float2(v1, v2));
      break;
    case NODE_SDF_2D_ISOSCELES:
      dim2 = sdf_dimension(v1, v3, &round);
      dist = sdf_2d_isosceles(float3_to_float2(co), dim2) - round;
      break;
    case NODE_SDF_2D_ROUND_JOINT:
      dist = sdf_2d_round_joint(float3_to_float2(co), v3, angle, linewidth);
      break;
    case NODE_SDF_2D_FLAT_JOINT:
      dist = sdf_2d_flat_joint(float3_to_float2(co), v3, angle, linewidth);
      break;
    case NODE_SDF_2D_PENTAGON:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_2d_pentagon(float3_to_float2(co), dimf) - round * 2.0f;
      break;
    case NODE_SDF_2D_PARABOLA_SEGMENT:
      dist = sdf_2d_parabola_segment(float3_to_float2(co), v1, v2);
      break;
    case NODE_SDF_2D_MOON:
      dim2 = sdf_dimension(radius, v1, &round);
      dist = sdf_2d_moon(float3_to_float2(co), v2, dim2.x, dim2.y) - round * 2.0f;
      break;
    case NODE_SDF_2D_QUAD:
      dist = sdf_2d_quad(float3_to_float2(co),
                         float3_to_float2(p1),
                         float3_to_float2(p2),
                         float3_to_float2(p3),
                         float3_to_float2(p4));
      break;
    case NODE_SDF_2D_HEART:
      dimf = sdf_dimension(radius, &round);
      dist = sdf_2d_heart(float3_to_float2(co), dimf) - round * 2.0f;
      break;
  }
  dist = sdf_alteration(size, dist, linewidth, invert);

  return dist;
}

ccl_device float svm_sdf_op(
    float a, float b, float v, float v2, int n, bool invert, NodeSdfOperation operation)
{
  switch (operation) {
    case NODE_SDF_OP_DILATE:
      return sdf_op_dilate(a, v);
    case NODE_SDF_OP_ONION:
      return sdf_op_onion(a, v, n);
    case NODE_SDF_OP_BLEND:
      return sdf_op_blend(a, b, v);
    case NODE_SDF_OP_ANNULAR:
      return fabsf(a) - v * 0.5f;
    case NODE_SDF_OP_FLATTEN:
      return sdf_op_flatten(v, v2, a);
    case NODE_SDF_OP_INVERT:
      return -a;
    case NODE_SDF_OP_MASK:
      return sdf_op_mask(a, v, v2, invert);
    case NODE_SDF_OP_DIVIDE:
      return sdf_op_divide(a, b, v, v2);
    case NODE_SDF_OP_EXCLUSION:
      return sdf_op_exclusion(a, b, v, v2);
    case NODE_SDF_OP_PULSE:
      return cubic_pulse(v, v2, a);
    case NODE_SDF_OP_PIPE:
      return sdf_op_pipe(a, b, v);
    case NODE_SDF_OP_ENGRAVE:
      return sdf_op_engrave(a, b, v);
    case NODE_SDF_OP_GROOVE:
      return sdf_op_groove(a, b, v, v2);
    case NODE_SDF_OP_TONGUE:
      return sdf_op_tongue(a, b, v, v2);
    case NODE_SDF_OP_UNION:
      return sdf_op_union(a, b);
    case NODE_SDF_OP_INTERSECT:
      return sdf_op_intersect(a, b);
    case NODE_SDF_OP_DIFF:
      return sdf_op_diff(a, b);
    case NODE_SDF_OP_UNION_SMOOTH:
      return sdf_op_union_smooth(a, b, v);
    case NODE_SDF_OP_INTERSECT_SMOOTH:
      return sdf_op_intersect_smooth(a, b, v);
    case NODE_SDF_OP_DIFF_SMOOTH:
      return sdf_op_diff_smooth(a, b, v);
    case NODE_SDF_OP_UNION_CHAMFER:
      return sdf_op_union_chamfer(a, b, v);
    case NODE_SDF_OP_INTERSECT_CHAMFER:
      return sdf_op_intersect_chamfer(a, b, v);
    case NODE_SDF_OP_DIFF_CHAMFER:
      return sdf_op_diff_chamfer(a, b, v);
    case NODE_SDF_OP_UNION_ROUND:
      return sdf_op_union_round(a, b, v);
    case NODE_SDF_OP_INTERSECT_ROUND:
      return sdf_op_intersect_round(a, b, v);
    case NODE_SDF_OP_DIFF_ROUND:
      return sdf_op_diff_round(a, b, v);
    case NODE_SDF_OP_UNION_COLUMNS:
      return sdf_op_union_columns(a, b, v, v2);
    case NODE_SDF_OP_INTERSECT_COLUMNS:
      return sdf_op_intersect_columns(a, b, v, v2);
    case NODE_SDF_OP_DIFF_COLUMNS:
      return sdf_op_diff_columns(a, b, v, v2);
    case NODE_SDF_OP_UNION_STAIRS:
      return sdf_op_union_stairs(a, b, v, v2);
    case NODE_SDF_OP_INTERSECT_STAIRS:
      return sdf_op_intersect_stairs(a, b, v, v2);
    case NODE_SDF_OP_DIFF_STAIRS:
      return sdf_op_diff_stairs(a, b, v, v2);
    default:
      return 0.0f;
  }
}

ccl_device void svm_sdf_vector_op(float3 p,
                                  float3 p2,
                                  float3 p3,
                                  float scale,
                                  float v,
                                  float v2,
                                  float angle,
                                  int n,
                                  int n2,
                                  NodeSdfVectorOperation operation,
                                  NodeSdfVectorAxis axis,
                                  float3 *vout,
                                  float3 *pos,
                                  float *d)
{

  switch (operation) {
    case NODE_SDF_VEC_OP_EXTRUDE: {
      p = axis_swizzle(p, axis);
      *d = sdf_op_extrude(&p, p2);
      p = axis_unswizzle(p, axis);
      *vout = p;
      break;
    }
    case NODE_SDF_VEC_OP_REPEAT_INF_MIRROR: {
      pModMirror3(&p, p2);
      *vout = p;
      break;
    }
    case NODE_SDF_VEC_OP_REPEAT_INF: {
      *vout = safe_mod(p + 0.5f * p2, p2) - 0.5f * p2;
      break;
    }
    case NODE_SDF_VEC_OP_REPEAT_FINITE: {
      *vout = p - clamp(floor(p / p2 + 0.5f), -p3, p3) * p2;
      break;
    }
    case NODE_SDF_VEC_OP_TWIST: {
      p = axis_swizzle(p, axis);
      p = sdf_op_twist(p, v, angle);
      p = axis_unswizzle(p, axis);
      *vout = p;
      break;
    }
    case NODE_SDF_VEC_OP_BEND: {
      p = axis_swizzle(p, axis);
      p = sdf_op_bend(p, angle);
      p = axis_unswizzle(p, axis);
      *vout = p;
      break;
    }
    case NODE_SDF_VEC_OP_SWIZZLE: {
      p = axis_swizzle(p, axis);
      *vout = p;
      break;
    }
    case NODE_SDF_VEC_OP_ROTATE: {
      p = axis_swizzle(p, axis);
      float2 r = float3_to_float2(p);
      r = cos(angle) * r + sin(angle) * make_float2(r.y, -r.x);
      p = make_float3(r.x, r.y, p.z);
      p = axis_unswizzle(p, axis);
      *vout = p;
      break;
    }
    case NODE_SDF_VEC_OP_REFLECT: {
      *d = sdf_op_reflect(&p, normalize(p2), v);
      *vout = p;
      break;
    }
    case NODE_SDF_VEC_OP_MIRROR: {
      p = axis_swizzle(p, axis);
      float2 pxy = float3_to_float2(p);
      float2 p2xy = float3_to_float2(p2);
      *pos = float2_to_float3(pModGrid2(&pxy, p2xy));
      p = make_float3(pxy.x, pxy.y, p.z);
      *pos = float2_to_float3(p2xy);
      p = axis_unswizzle(p, axis);
      *pos = axis_unswizzle(p2, axis);
      *vout = p;
      break;
    }
    case NODE_SDF_VEC_OP_POLAR: {
      p = axis_swizzle(p, axis);
      *d = sdf_op_polar(&p, v);
      p = axis_unswizzle(p, axis);
      *vout = p;
    } break;
    case NODE_SDF_VEC_OP_MAP_UV: {
      *vout = map_value(p, -1.0f, 1.0f, 0.0f, 1.0f, 0.0f);
      break;
    }
    case NODE_SDF_VEC_OP_MAP_11: {
      *vout = map_value(p, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f);
      break;
    }
    case NODE_SDF_VEC_OP_MAP_05: {
      *vout = map_value(p, 0.0f, 1.0f, -0.5f, 0.5f, 0.0f);
      break;
    }
    case NODE_SDF_VEC_OP_ROTATE_UV: {
      float2 pxy = float3_to_float2(p);
      float2 center = float3_to_float2(p2);
      pxy = pxy - center;
      pxy = cosf(angle) * pxy + sinf(angle) * make_float2(pxy.y, -pxy.x);
      pxy = pxy + center;
      *vout = float2_to_float3(pxy);
      break;
    }
    case NODE_SDF_VEC_OP_RND_UV: {
      *vout = float2_to_float3(uv_random_coords_rotate(p2, float3_to_float2(p)));
      break;
    }

    case NODE_SDF_VEC_OP_RND_UV_FLIP: {
      *vout = float2_to_float3(uv_random_coords_flip_rotate(p2, float3_to_float2(p)));
      break;
    }
    case NODE_SDF_VEC_OP_OCTANT: {
      p = axis_swizzle(p, axis);
      pMirror(&p.x, v);
      pMirror(&p.y, v);
      if (p.y > p.x) {
        p = make_float3(p.y, p.x, p.z);
      }
      *vout = axis_unswizzle(p, axis);
      break;
    }
    case NODE_SDF_VEC_OP_TILESET: {
      float2 uv = uv_tileset(p, v, v2, scale, n, n2);
      *vout = make_float3(uv.x, uv.y, 0.0f);
      break;
    }
    case NODE_SDF_VEC_OP_SPIN: {
      p = axis_swizzle(p, axis);
      p = sdf_op_spin(p, v);
      p = axis_unswizzle(p, axis);
      *vout = p;
    } break;
    case NODE_SDF_VEC_OP_GRID: {
      p = axis_swizzle(p, axis);
      /* Prevent precision issues on unit coordinates. */
      p = (p + 0.000001f) * 0.999999f;
      p = p * p2;
      float3 pf = floor(p);
      p = p - pf;
      *vout = axis_unswizzle(p, axis);
      *pos = (pf + zero_float3()) / p2;
      *pos = axis_unswizzle(*pos, axis);
      break;
    }
    case NODE_SDF_VEC_OP_SCALE_UV: {
      float2 center = make_float2(0.5f, 0.5f);
      float2 pxy = float3_to_float2(p);
      pxy = pxy - center;
      pxy *= v2;
      pxy += center;
      *vout = make_float3(pxy.x, pxy.y, p.z);
      break;
    }
    case NODE_SDF_VEC_OP_SWIRL: {
      p = axis_swizzle(p, axis);
      float2 pxy = float3_to_float2(p);
      float2 p2xy = float3_to_float2(p2);
      float2 p3xy = float3_to_float2(p3);
      pxy = sdf_op_swirl(pxy, p2xy, v, p3xy);
      p = make_float3(pxy.x, pxy.y, p.z);
      p = axis_unswizzle(p, axis);
      *vout = p;
      break;
    }
    case NODE_SDF_VEC_OP_RADIAL_SHEAR: {
      p = axis_swizzle(p, axis);
      float2 pxy = float3_to_float2(p);
      float2 p2xy = float3_to_float2(p2);
      float2 p3xy = float3_to_float2(p3);
      pxy = sdf_op_radial_shear(pxy, p2xy, v, p3xy);
      p = make_float3(pxy.x, pxy.y, p.z);
      p = axis_unswizzle(p, axis);
      *vout = p;
      break;
    }
    case NODE_SDF_VEC_OP_PINCH_INFLATE: {
      p = axis_swizzle(p, axis);
      p = sdf_op_pinch_inflate(p, p2, v, v2);
      p = axis_unswizzle(p, axis);
      *vout = p;
      break;
    }
  }
}

/* Nodes */

ccl_device void svm_node_sdf_primitive(
    KernelGlobals *kg, ShaderData *sd, float *stack, uint4 node, int *offset)
{
  uint4 node_inout = read_node(kg, offset);
  uint4 node_points = read_node(kg, offset);
  uint4 node_defaults = read_node(kg, offset);
  uint4 node_defaults2 = read_node(kg, offset);
  uint4 node_defaults3 = read_node(kg, offset);

  /* Input and Output Sockets */
  uint size_in_stack_offset, radius_in_stack_offset, round_in_stack_offset;
  uint value1_stack_offset, value2_stack_offset, value3_stack_offset, value4_stack_offset;
  uint mode, invert, angle_in_stack_offset, linewidth_in_stack_offset;

  svm_unpack_node_uchar3(
      node.y, &size_in_stack_offset, &radius_in_stack_offset, &round_in_stack_offset);
  svm_unpack_node_uchar4(node.z,
                         &value1_stack_offset,
                         &value2_stack_offset,
                         &value3_stack_offset,
                         &value4_stack_offset);
  svm_unpack_node_uchar4(
      node.w, &mode, &invert, &angle_in_stack_offset, &linewidth_in_stack_offset);

  uint vector_in = node_inout.x;
  uint distance_out = node_inout.y;

  float size = stack_load_float_default(stack, size_in_stack_offset, node_defaults.x);
  float radius = stack_load_float_default(stack, radius_in_stack_offset, node_defaults.y);

  float v1 = stack_load_float_default(stack, value1_stack_offset, node_defaults2.x);
  float v2 = stack_load_float_default(stack, value2_stack_offset, node_defaults2.y);
  float v3 = stack_load_float_default(stack, value3_stack_offset, node_defaults2.z);
  float v4 = stack_load_float_default(stack, value4_stack_offset, node_defaults2.w);

  float angle = stack_load_float_default(stack, angle_in_stack_offset, node_defaults3.x);
  float round = stack_load_float_default(stack, round_in_stack_offset, node_defaults3.y);
  float linewidth = stack_load_float_default(stack, linewidth_in_stack_offset, node_defaults3.y);

  float3 vector = stack_load_float3(stack, vector_in);
  float3 p1 = stack_load_float3(stack, node_points.x);
  float3 p2 = stack_load_float3(stack, node_points.y);
  float3 p3 = stack_load_float3(stack, node_points.z);
  float3 p4 = stack_load_float3(stack, node_points.w);

  if (stack_valid(distance_out)) {
    float distance;
    if (size == 0.0f) {
      distance = len(vector);
    }
    else {
      distance = svm_sdf_primitive(vector,
                                   size,
                                   radius,
                                   v1,
                                   v2,
                                   v3,
                                   v4,
                                   p1,
                                   p2,
                                   p3,
                                   p4,
                                   angle,
                                   round,
                                   linewidth,
                                   invert,
                                   (NodeSdfMode)mode);
    }
    stack_store_float(stack, distance_out, distance);
  }
}

ccl_device void svm_node_sdf_op(
    KernelGlobals *kg, ShaderData *sd, float *stack, uint4 node, int *offset)
{
  uint4 node_defaults = read_node(kg, offset);
  uint4 node_defaults2 = read_node(kg, offset);

  /* Input and Output Sockets */
  uint value1_stack_offset, value2_stack_offset, value3_stack_offset, value4_stack_offset;

  svm_unpack_node_uchar4(node.z,
                         &value1_stack_offset,
                         &value2_stack_offset,
                         &value3_stack_offset,
                         &value4_stack_offset);

  uint value_out = node.w;

  float value1 = stack_load_float_default(stack, value1_stack_offset, node_defaults2.x);
  float value2 = stack_load_float_default(stack, value2_stack_offset, node_defaults2.y);
  float value3 = stack_load_float_default(stack, value3_stack_offset, node_defaults2.z);
  float value4 = stack_load_float_default(stack, value4_stack_offset, node_defaults2.w);
  int count = stack_load_int_default(stack, node_defaults.x, node_defaults.y);
  bool invert = (bool)node_defaults.z;

  if (stack_valid(value_out)) {
    float dist = svm_sdf_op(
        value1, value2, value3, value4, count, invert, (NodeSdfOperation)node.y);
    stack_store_float(stack, value_out, dist);
  }
}

ccl_device void svm_node_sdf_vector_op(
    KernelGlobals *kg, ShaderData *sd, float *stack, uint4 node, int *offset)
{
  uint4 node_vec_in = read_node(kg, offset);
  uint4 node_out = read_node(kg, offset);
  uint4 node_defaults = read_node(kg, offset);
  uint4 node_defaults2 = read_node(kg, offset);

  /* Input and Output Sockets */
  uint scale_in_stack_offset, value1_in_stack_offset, value2_in_stack_offset,
      angle_in_stack_offset;

  svm_unpack_node_uchar4(node.w,
                         &scale_in_stack_offset,
                         &value1_in_stack_offset,
                         &value2_in_stack_offset,
                         &angle_in_stack_offset);

  uint vector_out = node_out.x;
  uint position_out = node_out.y;
  uint value_out = node_out.z;

  float3 p1 = stack_load_float3(stack, node_vec_in.x);
  float3 p2 = stack_load_float3(stack, node_vec_in.y);
  float3 p3 = stack_load_float3(stack, node_vec_in.z);

  float scale = stack_load_float_default(stack, scale_in_stack_offset, node_defaults.x);
  float v1 = stack_load_float_default(stack, value1_in_stack_offset, node_defaults.y);
  float v2 = stack_load_float_default(stack, value2_in_stack_offset, node_defaults.z);
  float angle = stack_load_float_default(stack, angle_in_stack_offset, node_defaults.w);

  int n1 = stack_load_int_default(stack, node_defaults2.x, node_defaults2.z);
  int n2 = stack_load_int_default(stack, node_defaults2.y, node_defaults2.w);

  float3 vout = zero_float3();
  float3 pos = zero_float3();
  float value = 0.0f;

  svm_sdf_vector_op(p1,
                    p2,
                    p3,
                    scale,
                    v1,
                    v2,
                    angle,
                    n1,
                    n2,
                    (NodeSdfVectorOperation)node.y,
                    (NodeSdfVectorAxis)node.z,
                    &vout,
                    &pos,
                    &value);

  if (stack_valid(vector_out)) {
    stack_store_float3(stack, vector_out, vout);
  }
  if (stack_valid(position_out)) {
    stack_store_float3(stack, position_out, pos);
  }
  if (stack_valid(value_out)) {
    stack_store_float(stack, value_out, value);
  }
}

CCL_NAMESPACE_END
