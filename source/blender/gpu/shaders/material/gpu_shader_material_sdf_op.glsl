#pragma BLENDER_REQUIRE(gpu_shader_material_sdf_util.glsl)

/**
 * SDF Functions based on these sources, inc comments:
 *
 * The MIT License
 * Copyright © 2018-2021 Inigo Quilez
 * - https://www.iquilezles.org/www/articles/distfunctions2d/distfunctions2d.htm
 *
 * The MIT License
 * Copyright (c) 2011-2021 Mercury Demogroup
 * - http://mercury.sexy/hg_sdf/
 *
 */

void node_sdf_op_dilate(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = a - v;
}

void node_sdf_op_onion(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_onion(a, v, int(n));
}

void node_sdf_op_annular(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = abs(a) - v * 0.5;
}

void node_sdf_op_blend(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = mix(a, b, v);
}

void node_sdf_op_flatten(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_flatten(v, v2, a);
}

void node_sdf_op_pulse(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = cubic_pulse(v, v2, a);
}

void node_sdf_op_mask(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  a -= v2;
  if (v == 0.0) {
    dist = (a > 0.0) ? 0.0 : 1.0;
  }
  else {
    dist = map_value(a, -v, 0.0, 0.0, 1.0, 0.0);
    if (v > 0.0) {
      dist = 1.0 - dist;
    }
    dist = clamp(dist, 0.0, 1.0);
  }
  if (invert > 0.0) {
    dist = 1.0 - dist;
  };
}

void node_sdf_op_invert(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = -a;
}

void node_sdf_op_pipe(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_pipe(a, b, v);
}

void node_sdf_op_engrave(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_engrave(a, b, v);
}

void node_sdf_op_groove(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_groove(a, b, v, v2);
}

void node_sdf_op_tongue(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_tongue(a, b, v, v2);
}

void node_sdf_op_union(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_union(a, b);
}

void node_sdf_op_intersect(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_intersect(a, b);
}

void node_sdf_op_diff(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_diff(a, b);
}

/* Smooth. */
void node_sdf_op_union_smooth(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_union_smooth(a, b, v);
}

void node_sdf_op_intersect_smooth(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_intersect_smooth(a, b, v);
}

void node_sdf_op_diff_smooth(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_diff_smooth(a, b, v);
}

/* Stairs. */
void node_sdf_op_union_stairs(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_union_stairs(a, b, v, v2);
}

void node_sdf_op_intersect_stairs(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_intersect_stairs(a, b, v, v2);
}

void node_sdf_op_diff_stairs(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_diff_stairs(a, b, v, v2);
}

/* Chamfer. */
void node_sdf_op_union_chamfer(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_union_chamfer(a, b, v);
}

void node_sdf_op_intersect_chamfer(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_intersect_chamfer(a, b, v);
}

void node_sdf_op_diff_chamfer(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_diff_chamfer(a, b, v);
}

/* Columns. */
void node_sdf_op_union_columns(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_union_columns(a, b, v, v2);
}

void node_sdf_op_intersect_columns(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_intersect_columns(a, b, v, v2);
}

void node_sdf_op_diff_columns(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_diff_columns(a, b, v, v2);
}

void node_sdf_op_union_round(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_union_round(a, b, v);
}

void node_sdf_op_intersect_round(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_intersect_round(a, b, v);
}

void node_sdf_op_diff_round(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_diff_round(a, b, v);
}

void node_sdf_op_divide(float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_divide(a, b, v, v2);
}

void node_sdf_op_exclusion(
    float a, float b, float v, float v2, float n, float invert, out float dist)
{
  dist = sdf_op_exclusion(a, b, v, v2);
}
