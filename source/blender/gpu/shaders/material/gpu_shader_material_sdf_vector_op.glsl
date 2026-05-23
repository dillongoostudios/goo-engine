#include "gpu_shader_material_sdf_util.glsl"
#include "gpu_shader_common_hash.glsl"

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

#define SDF_AXIS_XYZ 0
#define SDF_AXIS_XZY 1
#define SDF_AXIS_YXZ 2
#define SDF_AXIS_YZX 3
#define SDF_AXIS_ZXY 4
#define SDF_AXIS_ZYX 5

/* Swizzle axis for axis aligned functions. */
float3 axis_swizzle(float3 p, float xyz)
{
  int axis = int(xyz);
  if (axis == SDF_AXIS_XYZ) { /* XYZ 123 */
    /* Pass p = p.xyz */
    return p;
  }
  else if (axis == SDF_AXIS_XZY) { /* XZY 132 */
    return p.xzy;
  }
  else if (axis == SDF_AXIS_YXZ) { /* YXZ 213 */
    return p.yxz;
  }
  else if (axis == SDF_AXIS_YZX) { /* YZX 231 */
    return p.yzx;
  }
  else if (axis == SDF_AXIS_ZXY) { /* ZXY 312 */
    return p.zxy;
  }
  else if (axis == SDF_AXIS_ZYX) { /* ZYX 321 */
    return p.zyx;
  }
  else {
    return p;
  }
}

/* Reverse swizzle axis for axis aligned functions. */
float3 axis_unswizzle(float3 p, float xyz)
{
  int axis = int(xyz);
  if (axis == SDF_AXIS_XYZ) { /* XYZ 123 */
    /* Pass p = p.xyz */
    return p;
  }
  else if (axis == SDF_AXIS_XZY) { /* XZY 132 */
    return p.xzy;
  }
  else if (axis == SDF_AXIS_YXZ) { /* YXZ 213 */
    return p.yxz;
  }
  else if (axis == SDF_AXIS_YZX) { /* YZX 312 */
    return p.zxy;
  }
  else if (axis == SDF_AXIS_ZXY) { /* ZXY 231 */
    return p.yzx;
  }
  else if (axis == SDF_AXIS_ZYX) { /* ZYX 321 */
    return p.zyx;
  }
  else {
    return p;
  }
}

/* Splits uv into tiles. */
float2 uv_tileset(float3 puv, float pos, float padding, float scale, float cx, float cy)
{
  float2 uv;

  float stepx = 1.00001f / cx;
  float stepy = 1.00001f / cy;

  float posx = fract(pos);
  float posy = fract(pos * cx);

  float fx = floor(safe_divide(posx, stepx)) * stepx;
  float fy = floor(safe_divide(posy, stepy)) * stepy;

  uv.x = fx + (puv.x) * stepx;
  uv.y = fy + (puv.y) * stepy;

  /* Scale. */
  float hx = stepx * 0.5f;
  float hy = stepy * 0.5f;
  float cfx = hx + fx;
  float cfy = hy + fy;

  uv.x -= cfx;
  uv.x = safe_divide(uv.x, scale);
  uv.x += cfx;

  uv.y -= cfy;
  uv.y = safe_divide(uv.y, scale);
  uv.y += cfy;

  uv.x = clamp(uv.x, fx, fx + stepx);
  uv.y = clamp(uv.y, fy, fy + stepy);

  /* Padding. */
  if (padding > 0.0f) {
    uv = map_value(uv, 0.0f, 1.0f, 0.0f + padding, 1.0f - padding, 0.0f);
  }

  return uv;
}

float2 uv_rotate_90(float2 p, int direction)
{
  float2 uv;
  float2 v = float2(0.5f, 0.5f);
  uv = p - v;
  uv = (direction > 0) ? float2(uv.y, -uv.x) : float2(-uv.y, uv.x);
  return uv + v;
}

/* Returns 1 of 4 rotated uv coordinates. */
float2 uv_coords_rotate(float2 puv, float rnd)
{
  float2 uv;

  if (rnd > 0.75f) {
    uv.x = puv.x;
    uv.y = puv.y;
  }
  else if (rnd > 0.5f) {
    uv = uv_rotate_90(puv, 1);
  }
  else if (rnd > 0.25f) {
    uv.x = 1.0f - puv.x;
    uv.y = 1.0f - puv.y;
  }
  else {
    uv = uv_rotate_90(puv, 0);
  }
  return uv;
}

/* Returns 1 of 4 flipped uv coordinates. */
float2 uv_coords_flip(float2 puv, float rnd)
{
  float2 uv;

  if (rnd > 0.75f) {
    uv.x = puv.x;
    uv.y = puv.y;
  }
  else if (rnd > 0.5f) {
    uv.x = 1.0f - puv.x;
    uv.y = puv.y;
  }
  else if (rnd > 0.25f) {
    uv.x = 1.0f - puv.x;
    uv.y = 1.0f - puv.y;
  }
  else {
    uv.x = puv.x;
    uv.y = 1.0f - puv.y;
  }
  return uv;
}

/* Returns 1 of 6 rotated or flipped uv coordinates. */
float2 uv_coords_flip_rotate(float2 puv, float rnd)
{
  float2 uv;

  if (rnd > 0.83333f) {
    uv.x = puv.x;
    uv.y = puv.y;
  }
  else if (rnd > 0.66667f) {
    uv = uv_rotate_90(puv, 1);
  }
  else if (rnd > 0.50000f) {
    uv = uv_rotate_90(puv, 0);
  }
  else if (rnd > 0.33333f) {
    uv.x = 1.0f - puv.x;
    uv.y = puv.y;
  }
  else if (rnd > 0.16667f) {
    uv.x = 1.0f - puv.x;
    uv.y = 1.0f - puv.y;
  }
  else {
    uv.x = puv.x;
    uv.y = 1.0f - puv.y;
  }
  return uv;
}

float2 uv_random_coords_flip_rotate(float3 pos, float2 puv)
{
  float rnd = hash_vec3_to_float(pos);
  return uv_coords_flip_rotate(puv, rnd);
}

float2 uv_random_coords_flip(float3 pos, float2 puv)
{
  float rnd = hash_vec3_to_float(pos);
  return uv_coords_flip(puv, rnd);
}

float2 uv_random_coords_rotate(float3 pos, float2 puv)
{
  float rnd = hash_vec3_to_float(pos);
  return uv_coords_rotate(puv, rnd);
}

/* Node functions. */

void node_sdf_vector_op_extrude(float3 p,
                                float3 p2,
                                float3 p3,
                                float scale,
                                float v,
                                float v2,
                                float angle,
                                float n,
                                float n2,
                                float axis,
                                out float3 vout,
                                out float3 pos,
                                out float d)
{
  p = axis_swizzle(p, axis);
  d = sdf_op_extrude(p, p2);
  p = axis_unswizzle(p, axis);
  vout = p;
}

void node_sdf_vector_op_spin(float3 p,
                             float3 p2,
                             float3 p3,
                             float scale,
                             float v,
                             float v2,
                             float angle,
                             float n,
                             float n2,
                             float axis,
                             out float3 vout,
                             out float3 pos,
                             out float d)
{
  p = axis_swizzle(p, axis);
  p = sdf_op_spin(p, v);
  p = axis_unswizzle(p, axis);
  vout = p;
}

void node_sdf_vector_op_twist(float3 p,
                              float3 p2,
                              float3 p3,
                              float scale,
                              float v,
                              float v2,
                              float angle,
                              float n,
                              float n2,
                              float axis,
                              out float3 vout,
                              out float3 pos,
                              out float d)
{
  p = axis_swizzle(p, axis);
  p = sdf_op_twist(p, v, angle);
  p = axis_unswizzle(p, axis);
  vout = p;
}

void node_sdf_vector_op_bend(float3 p,
                             float3 p2,
                             float3 p3,
                             float scale,
                             float v,
                             float v2,
                             float angle,
                             float n,
                             float n2,
                             float axis,
                             out float3 vout,
                             out float3 pos,
                             out float d)
{
  p = axis_swizzle(p, axis);
  p = sdf_op_bend(p, angle);
  p = axis_unswizzle(p, axis);
  vout = p;
}

void node_sdf_vector_op_repeat_inf_mirror(float3 p,
                                          float3 p2,
                                          float3 p3,
                                          float scale,
                                          float v,
                                          float v2,
                                          float angle,
                                          float n,
                                          float n2,
                                          float axis,
                                          out float3 vout,
                                          out float3 pos,
                                          out float d)
{
  p_mod_mirror3(p, p2);
  vout = p;
}

void node_sdf_vector_op_repeat_inf(float3 p,
                                   float3 p2,
                                   float3 p3,
                                   float scale,
                                   float v,
                                   float v2,
                                   float angle,
                                   float n,
                                   float n2,
                                   float axis,
                                   out float3 vout,
                                   out float3 pos,
                                   out float d)
{
  vout = sdf_safe_mod(p + 0.5f * p2, p2) - 0.5f * p2;
}

void node_sdf_vector_op_repeat(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               out float3 vout,
                               out float3 pos,
                               out float d)
{
  vout = p - clamp(floor(p / p2 + 0.5f), -p3, p3) * p2;
}

void node_sdf_vector_op_rotate(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               out float3 vout,
                               out float3 pos,
                               out float d)
{
  p = axis_swizzle(p, axis);
  p.xy = cos(angle) * p.xy + sin(angle) * float2(p.y, -p.x);
  p = axis_unswizzle(p, axis);
  vout = p;
}

void node_sdf_vector_op_reflect(float3 p,
                                float3 p2,
                                float3 p3,
                                float scale,
                                float v,
                                float v2,
                                float angle,
                                float n,
                                float n2,
                                float axis,
                                out float3 vout,
                                out float3 pos,
                                out float d)
{
  d = sdf_op_reflect(p, normalize(p2), v);
  vout = p;
}

void node_sdf_vector_op_mirror(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               out float3 vout,
                               out float3 pos,
                               out float d)
{
  p = axis_swizzle(p, axis);
  // pos.xy = sdf_op_mirror(p, p2);
  pos.xy = p_mod_grid2(p.xy, p2.xy);
  pos.z = 0.0f;
  p = axis_unswizzle(p, axis);
  pos = axis_unswizzle(pos, axis);
  vout = p;
}

void node_sdf_vector_op_polar(float3 p,
                              float3 p2,
                              float3 p3,
                              float scale,
                              float v,
                              float v2,
                              float angle,
                              float n,
                              float n2,
                              float axis,
                              out float3 vout,
                              out float3 pos,
                              out float d)
{
  p = axis_swizzle(p, axis);
  d = sdf_op_polar(p.xy, v);
  p = axis_unswizzle(p, axis);
  vout = p;
}

void node_sdf_vector_op_mirror_alt(float3 p,
                                   float3 p2,
                                   float3 p3,
                                   float scale,
                                   float v,
                                   float v2,
                                   float angle,
                                   float n,
                                   float n2,
                                   float axis,
                                   out float3 vout,
                                   out float3 pos,
                                   out float d)
{
  float t = dot(p, p2) + v;
  if (t < 0.0f) {
    vout = p - (2.0f * t) * v2;
  }
  d = sign(t);
}

void node_sdf_vector_op_swizzle(float3 p,
                                float3 p2,
                                float3 p3,
                                float scale,
                                float v,
                                float v2,
                                float angle,
                                float n,
                                float n2,
                                float axis,
                                out float3 vout,
                                out float3 pos,
                                out float d)
{
  vout = axis_swizzle(p, axis);
}

void node_sdf_vector_op_grid(float3 p,
                             float3 p2,
                             float3 p3,
                             float scale,
                             float v,
                             float v2,
                             float angle,
                             float n,
                             float n2,
                             float axis,
                             out float3 vout,
                             out float3 pos,
                             out float d)
{
  p = axis_swizzle(p, axis);

  /* Prevent precision issues on unit coordinates. */
  p = (p + 0.000001f) * 0.999999f;

  p = p * p2;
  float3 pf = floor(p);
  p = p - pf;

  vout = axis_unswizzle(p, axis);

  pos = safe_divide(pf + float3(0, 0, 0), p2);
  pos = axis_unswizzle(pos, axis);
}

void node_sdf_vector_op_random_uv_rotate(float3 p,
                                         float3 p2,
                                         float3 p3,
                                         float scale,
                                         float v,
                                         float v2,
                                         float angle,
                                         float n,
                                         float n2,
                                         float axis,
                                         out float3 vout,
                                         out float3 pos,
                                         out float d)
{
  vout.xy = uv_random_coords_rotate(p2, p.xy);
  vout.z = 0.0f;
}

void node_sdf_vector_op_random_uv_flip(float3 p,
                                       float3 p2,
                                       float3 p3,
                                       float scale,
                                       float v,
                                       float v2,
                                       float angle,
                                       float n,
                                       float n2,
                                       float axis,
                                       out float3 vout,
                                       out float3 pos,
                                       out float d)
{
  vout.xy = uv_random_coords_flip_rotate(p2, p.xy);
  vout.z = 0.0f;
}

void node_sdf_vector_op_tileset(float3 p,
                                float3 p2,
                                float3 p3,
                                float scale,
                                float v,
                                float v2,
                                float angle,
                                float n,
                                float n2,
                                float axis,
                                out float3 vout,
                                out float3 pos,
                                out float d)
{
  vout.xy = uv_tileset(p, v, v2, scale, n, n2);
  vout.z = 0.0f;
}

void node_sdf_vector_op_octant(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               out float3 vout,
                               out float3 pos,
                               out float d)
{
  p = axis_swizzle(p, axis);
  float size = v;
  float3 s = sgn(p);
  p_mirror(p.x, size);
  p_mirror(p.y, size);
  if (p.y > p.x) {
    p.xy = p.yx;
  }
  vout = axis_unswizzle(p, axis);

  // return s;
}

void node_sdf_vector_op_map_uv(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               out float3 vout,
                               out float3 pos,
                               out float d)
{
  vout = map_value(p, -1.0f, 1.0f, 0.0f, 1.0f, 0.0f);
}

void node_sdf_vector_op_map_11(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               out float3 vout,
                               out float3 pos,
                               out float d)
{
  vout = map_value(p, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f);
}

void node_sdf_vector_op_map_05(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               out float3 vout,
                               out float3 pos,
                               out float d)
{
  vout = map_value(p, 0.0f, 1.0f, -0.5f, 0.5f, 0.0f);
}

void node_sdf_vector_op_uv_rotate(float3 p,
                                  float3 p2,
                                  float3 p3,
                                  float scale,
                                  float v,
                                  float v2,
                                  float angle,
                                  float n,
                                  float n2,
                                  float axis,
                                  out float3 vout,
                                  out float3 pos,
                                  out float d)
{
  float2 center = p2.xy;
  p.xy -= center;
  p.xy = cos(angle) * p.xy + sin(angle) * float2(p.y, -p.x);
  p.xy += center;
  vout = p;
  vout.z = 0.0f;
}

void node_sdf_vector_op_uv_scale(float3 p,
                                 float3 p2,
                                 float3 p3,
                                 float scale,
                                 float v,
                                 float v2,
                                 float angle,
                                 float n,
                                 float n2,
                                 float axis,
                                 out float3 vout,
                                 out float3 pos,
                                 out float d)
{
  float2 center = float2(0.5f, 0.5f);
  p.xy -= center;
  p.xy *= scale;
  p.xy += center;
  vout = p;
  // vout.z = 0.0f;
}

float2 sdf_op_radial_shear(float2 uv, float2 center, float strength, float2 offset)
{
  float2 pos = uv - center;
  float pos2 = dot(pos.xy, pos.xy);
  float pos_offset = pos2 * strength;
  return float2(pos.y, -pos.x) * pos_offset + offset;
}

float2 sdf_op_swirl(float2 uv, float2 center, float strength, float2 offset)
{
  float2 pos = uv - center;
  float angle = strength * length(pos);
  float x = cos(angle) * pos.x - sin(angle) * pos.y;
  float y = sin(angle) * pos.x + cos(angle) * pos.y;
  return float2(x + center.x + offset.x, y + center.y + offset.y);
}

void node_sdf_vector_op_swirl(float3 p,
                              float3 p2,
                              float3 p3,
                              float scale,
                              float v,
                              float v2,
                              float angle,
                              float n,
                              float n2,
                              float axis,
                              out float3 vout,
                              out float3 pos,
                              out float d)
{
  p = axis_swizzle(p, axis);
  p.xy = sdf_op_swirl(p.xy, p2.xy, v, p3.xy);
  p = axis_unswizzle(p, axis);
  vout = p;
}

void node_sdf_vector_op_radial_shear(float3 p,
                                     float3 p2,
                                     float3 p3,
                                     float scale,
                                     float v,
                                     float v2,
                                     float angle,
                                     float n,
                                     float n2,
                                     float axis,
                                     out float3 vout,
                                     out float3 pos,
                                     out float d)
{
  p = axis_swizzle(p, axis);
  p.xy = sdf_op_radial_shear(p.xy, p2.xy, v, p3.xy);
  p = axis_unswizzle(p, axis);
  vout = p;
}

float3 sdf_op_pinch_inflate(float3 uv, float3 center, float strength, float radius)
{

  uv -= center;

  float distance = length(uv);

  if (distance < radius) {
    float percent = distance / radius;
    if (strength > 0.0f) {
      uv *= mix(1.0f, smoothstep(0.0f, radius / distance, percent), strength * 0.75f);
    }
    else {
      uv *= mix(1.0f, pow(percent, 1.0f + strength * 0.75f) * radius / distance, 1.0f - percent);
    }
  }

  uv += center;

  return uv;
}

void node_sdf_vector_op_pinch_inflate(float3 p,
                                      float3 p2,
                                      float3 p3,
                                      float scale,
                                      float v,
                                      float v2,
                                      float angle,
                                      float n,
                                      float n2,
                                      float axis,
                                      out float3 vout,
                                      out float3 pos,
                                      out float d)
{
  p = axis_swizzle(p, axis);
  p = sdf_op_pinch_inflate(p, p2, v, v2);
  p = axis_unswizzle(p, axis);
  vout = p;
}

void node_sdf_vector_op_test(float3 p,
                             float3 p2,
                             float3 p3,
                             float scale,
                             float v,
                             float v2,
                             float angle,
                             float n,
                             float n2,
                             float axis,
                             out float3 vout,
                             out float3 pos,
                             out float d)
{
  p = axis_swizzle(p, axis);
  p = sdf_op_pinch_inflate(p, p2, v, v2);
  p = axis_unswizzle(p, axis);
  vout = p;
}
