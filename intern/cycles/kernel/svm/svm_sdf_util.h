/*
 * Copyright 2011-2021 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

CCL_NAMESPACE_BEGIN

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

#define M_SQRT3_F 1.73205080756887729352f   /* sqrt(3) */
#define M_SQRT3_2_F 0.86602540378443864676f /* sqrt(3)/2 */
#define M_SQRT1_3_F 0.57735026918962576450f /* sqrt(1/3) */
#define M_SQRT1_2_F 0.70710678118654752440f /* sqrt(1/2) */
#define M_PHI_F 4.97213595499957939281f     /* (sqrt(5)*0.5 + 0.5) */

ccl_device_inline float ndot(float2 a, float2 b)
{
  return a.x * b.x - a.y * b.y;
}

ccl_device_inline float dot2(float2 v)
{
  return dot(v, v);
}

ccl_device_inline float cross2(float2 a, float2 b)
{
  return a.x * b.y - a.y * b.x;
}

ccl_device_inline float vmax(float2 v)
{
  return max(v.x, v.y);
}

ccl_device_inline float vmax(float3 v)
{
  return max(max(v.x, v.y), v.z);
}

ccl_device_inline float vmax(float4 v)
{
  return max(max(v.x, v.y), max(v.z, v.w));
}

ccl_device_inline float vmin(float2 v)
{
  return min(v.x, v.y);
}

ccl_device_inline float vmin(float3 v)
{
  return min(min(v.x, v.y), v.z);
}

ccl_device_inline float vmin(float4 v)
{
  return min(min(v.x, v.y), min(v.z, v.w));
}

ccl_device_inline float sgn(float v)
{
  return (v < 0.0f) ? -1.0f : 1.0f;
}

ccl_device_inline float2 sgn(float2 v)
{
  return make_float2((v.x < 0.0f) ? -1.0f : 1.0f, (v.y < 0.0f) ? -1.0f : 1.0f);
}

ccl_device_inline float3 sgn(float3 v)
{
  return make_float3(
      (v.x < 0.0f) ? -1.0f : 1.0f, (v.y < 0.0f) ? -1.0f : 1.0f, (v.z < 0.0f) ? -1.0f : 1.0f);
}

/* Swizzle axis for axis aligned functions */
ccl_device_inline float3 axis_swizzle(float3 p, NodeSdfVectorAxis axis)
{
  if (axis == NODE_SDF_AXIS_XYZ) { /* XYZ 123 */
    /* Pass p = p.xyz */
    return p;
  }
  else if (axis == NODE_SDF_AXIS_XZY) { /* XZY 132 */
    return make_float3(p.x, p.z, p.y);
  }
  else if (axis == NODE_SDF_AXIS_YXZ) { /* YXZ 213 */
    return make_float3(p.y, p.x, p.z);
  }
  else if (axis == NODE_SDF_AXIS_YZX) { /* YZX 231 */
    return make_float3(p.y, p.z, p.x);
  }
  else if (axis == NODE_SDF_AXIS_ZXY) { /* ZXY 312 */
    return make_float3(p.z, p.x, p.y);
  }
  else if (axis == NODE_SDF_AXIS_ZYX) { /* ZYX 321 */
    return make_float3(p.z, p.y, p.x);
  }
  else {
    return p;
  }
}

/* Reverse swizzle axis for axis aligned functions */
ccl_device_inline float3 axis_unswizzle(float3 p, NodeSdfVectorAxis axis)
{
  if (axis == NODE_SDF_AXIS_XYZ) { /* XYZ 123 */
    /* Pass p = p.xyz */
    return p;
  }
  else if (axis == NODE_SDF_AXIS_XZY) { /* XZY 132 */
    return make_float3(p.x, p.z, p.y);
  }
  else if (axis == NODE_SDF_AXIS_YXZ) { /* YXZ 213 */
    return make_float3(p.y, p.x, p.z);
  }
  else if (axis == NODE_SDF_AXIS_YZX) { /* YZX 312 */
    return make_float3(p.z, p.x, p.y);
  }
  else if (axis == NODE_SDF_AXIS_ZXY) { /* ZXY 231 */
    return make_float3(p.y, p.z, p.x);
  }
  else if (axis == NODE_SDF_AXIS_ZYX) { /* ZYX 321 */
    return make_float3(p.z, p.y, p.x);
  }
  else {
    return p;
  }
}

ccl_device_inline float2 pR45(float2 p)
{
  return (p + make_float2(p.y, -p.x)) * M_SQRT1_2_F;
}

ccl_device_inline float3 safe_mod(float3 a, float b)
{
  a.x = safe_mod(a.x, b);
  a.y = safe_mod(a.y, b);
  a.z = safe_mod(a.z, b);
  return a;
}

ccl_device_inline float3 safe_mod(float3 a, float3 b)
{
  a.x = safe_mod(a.x, b.x);
  a.y = safe_mod(a.y, b.y);
  a.z = safe_mod(a.z, b.z);
  return a;
}

ccl_device_inline float2 safe_mod(float2 a, float2 b)
{
  a.x = safe_mod(a.x, b.x);
  a.y = safe_mod(a.y, b.y);
  return a;
}

ccl_device_inline float pMod1(float *p, float size)
{
  float halfsize = size * 0.5f;
  float c = floorf((*p + halfsize) / size);
  *p = safe_mod(*p + halfsize, size) - halfsize;
  return c;
}

ccl_device_inline float pMirror(float *p, float dist)
{
  float s = sgn(*p);
  *p = fabsf(*p) - dist;
  return s;
}

ccl_device_inline float3 pModMirror3(float3 *p, float3 size)
{
  float3 halfsize = size * 0.5f;
  float3 c = floor(safe_divide_float3_float3((*p + halfsize) , size));
  *p = safe_mod(*p + halfsize, size) - halfsize;
  *p = *p * (safe_mod(c, make_float3(2.0f, 2.0f, 2.0f)) * 2.0f - one_float3());
  return c;
}

ccl_device_inline float2 pModGrid2(float2 *p, float2 size)
{
  float2 c = floor(safe_divide_float2_float2((*p + size * 0.5f), size));
  *p = safe_mod(*p + size * 0.5f, size) - size * 0.5f;
  *p = *p * (safe_mod(c, make_float2(2.0f, 2.0f)) * 2.0f - one_float2());
  *p = *p - size / 2.0f;
  if (p->x > p->y) {
    *p = make_float2(p->y, p->x);
  }
  c = floor(c / 2.0f);
  return c;
}

ccl_device_inline float2 cosin(float a)
{
  return make_float2(cosf(a), sinf(a));
}

ccl_device_inline float2 sincos(float a)
{
  return make_float2(sinf(a), cosf(a));
}

ccl_device_inline float3 rotate_2d(float3 p, float angle)
{
  float2 pxy = cosf(angle) * make_float2(p.x, p.y) + sinf(angle) * make_float2(p.y, -p.x);
  return make_float3(pxy.x, pxy.y, p.z);
}

ccl_device_inline float2 sdf_op_radial_shear(float2 uv,
                                             float2 center,
                                             float strength,
                                             float2 offset)
{
  float2 pos = uv - center;
  float pos2 = dot(pos, pos);
  float pos_offset = pos2 * strength;
  return make_float2(pos.y, -pos.x) * pos_offset + offset;
}

ccl_device_inline float2 sdf_op_swirl(float2 uv, float2 center, float strength, float2 offset)
{
  float2 pos = uv - center;
  float angle = strength * len(pos);
  float c = cosf(angle);
  float s = sinf(angle);
  float x = c * pos.x - s * pos.y;
  float y = s * pos.x + c * pos.y;
  return make_float2(x + center.x + offset.x, y + center.y + offset.y);
}

ccl_device_inline float3 sdf_op_pinch_inflate(float3 uv,
                                              float3 center,
                                              float strength,
                                              float radius)
{
  uv -= center;

  float distance = len(uv);

  if (distance < radius) {
    float percent = distance / radius;
    if (strength > 0.0f) {
      uv *= mix(1.0f, smoothstep(0.0f, radius / distance, percent), strength * 0.75f);
    }
    else {
      uv *= mix(
          1.0f, safe_powf(percent, 1.0f + strength * 0.75f) * radius / distance, 1.0f - percent);
    }
  }

  uv += center;

  return uv;
}

/* Utility functions. */

ccl_device_inline float sdf_dimension(float w, float *r)
{
  float roundness = *r;
  float sw = compatible_signf(w);
  w = fabsf(w);
  roundness = mix(0.0f, w, clamp(roundness, 0.0f, 1.0f));
  float dimension = max(w - roundness, 0.0f);
  *r = roundness * 0.5f;
  return dimension * sw;
}

ccl_device_inline float2 sdf_dimension(float w, float d, float *r)
{
  float roundness = *r;
  float sw = compatible_signf(w);
  float sd = compatible_signf(d);
  w = fabsf(w);
  d = fabsf(d);
  roundness = mix(0.0f, min(w, d), clamp(roundness, 0.0f, 1.0f));
  float2 dimension = make_float2(max(w - roundness, 0.0f), max(d - roundness, 0.0f));
  *r = roundness * 0.5f;
  return dimension * make_float2(sw, sd);
}

ccl_device_inline float3 sdf_dimension(float w, float d, float h, float *r)
{
  float roundness = *r;
  float sw = compatible_signf(w);
  float sd = compatible_signf(d);
  float sh = compatible_signf(h);
  w = fabsf(w);
  d = fabsf(d);
  h = fabsf(h);
  roundness = mix(0.0f, min(w, min(d, h)), clamp(roundness, 0.0f, 1.0f));
  float3 dimension = make_float3(
      max(w - roundness, 0.0f), max(d - roundness, 0.0f), max(h - roundness, 0.0f));
  *r = roundness * 0.5f;
  return dimension * make_float3(sw, sd, sh);
}

ccl_device_inline float sdf_alteration(float size, float dist, float lw, float invert)
{
  if (lw != 0.0f) {
    dist = fabsf(dist) - lw * 0.5f;
  }

  dist *= size;

  return (invert > 0.0f) ? -dist : dist;
}

ccl_device_inline float map_value(
    float value, float from_min, float from_max, float to_min, float to_max, float d)
{
  return (from_max != from_min) ?
             to_min + ((value - from_min) / (from_max - from_min)) * (to_max - to_min) :
             d;
}

ccl_device_inline float2
map_value(float2 p, float from_min, float from_max, float to_min, float to_max, float d)
{
  p.x = map_value(p.x, from_min, from_max, to_min, to_max, d);
  p.y = map_value(p.y, from_min, from_max, to_min, to_max, d);
  return p;
}

ccl_device_inline float3
map_value(float3 p, float from_min, float from_max, float to_min, float to_max, float d)
{
  p.x = map_value(p.x, from_min, from_max, to_min, to_max, d);
  p.y = map_value(p.y, from_min, from_max, to_min, to_max, d);
  p.z = map_value(p.z, from_min, from_max, to_min, to_max, d);
  return p;
}

// Equivalent of smoothstep(c-w,c,x)-smoothstep(c,c+w,x)
// t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0);
// return t * t * (3.0 - 2.0 * t);
ccl_device_inline float cubic_pulse(float center, float width, float x)
{
  x = fabsf(x - center);
  float inv = signf(width);
  width *= 0.5f;
  width = fabsf(width);
  if (x > width) {
    return inv >= 0.0f ? 0.0f : 1.0f;
  }
  else {
    x /= width;
    x = 1.0f - x * x * (3.0f - 2.0f * x);
    return inv > 0.0f ? x : 1.0f - x;
  }
}

/* Splits uv into tiles. */
ccl_device_inline float2
uv_tilesetx(float3 puv, float pos, float padding, float scale, float cx, float cy)
{
  float2 uv;

  float stepx = 1.0f / cx;
  float stepy = 1.0f / cy;

  float posx = fractf(pos);
  float posy = fractf(pos * cx);

  float fx = floorf(safe_divide(posx, stepx)) * stepx;
  float fy = floorf(safe_divide(posy, stepy)) * stepy;

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

/* Splits uv into tiles. */
ccl_device_inline float2
uv_tileset(float3 puv, float pos, float padding, float scale, float cx, float cy)
{
  float2 uv;

  float stepx = 1.00001f / cx;
  float stepy = 1.00001f / cy;

  float posx = fractf(pos);
  float posy = fractf(pos * cx);

  float fx = floorf(safe_divide(posx, stepx)) * stepx;
  float fy = floorf(safe_divide(posy, stepy)) * stepy;

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

ccl_device_inline float2 uv_rotate_90(float2 p, int direction)
{
  float2 uv;
  float2 v = make_float2(0.5f, 0.5f);
  uv = p - v;
  uv = (direction > 0) ? make_float2(uv.y, -uv.x) : make_float2(-uv.y, uv.x);
  return uv + v;
}

/* Returns 1 of 4 rotated uv coordinates. */
ccl_device_inline float2 uv_coords_rotate(float2 puv, float rnd)
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
ccl_device_inline float2 uv_coords_flip(float2 puv, float rnd)
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
ccl_device_inline float2 uv_coords_flip_rotate(float2 puv, float rnd)
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

ccl_device_inline float2 uv_random_coords_flip_rotate(float3 pos, float2 puv)
{
  float rnd = hash_float3_to_float(pos);
  return uv_coords_flip_rotate(puv, rnd);
}

ccl_device_inline float2 uv_random_coords_flip(float3 pos, float2 puv)
{
  float rnd = hash_float3_to_float(pos);
  return uv_coords_flip(puv, rnd);
}

ccl_device_inline float2 uv_random_coords_rotate(float3 pos, float2 puv)
{
  float rnd = hash_float3_to_float(pos);
  return uv_coords_rotate(puv, rnd);
}

ccl_device_inline float sdf_op_diff(float a, float b)
{
  return max(a, -b);
}

ccl_device_inline float sdf_op_intersect(float a, float b)
{
  return max(a, b);
}

// The "Columns" flavour makes n-1 circular columns at a 45 degree angle:
ccl_device_inline float sdf_op_union_columns(float a, float b, float r, float n)
{
  n += 1.0f;
  if ((a < r) && (b < r) && (n > 0.0f)) {
    float2 p = make_float2(a, b);
    float columnradius = r * M_SQRT2_F / ((n - 1.0f) * 2.0f + M_SQRT2_F);
    p = pR45(p);
    p.x -= M_SQRT2_F / 2.0f * r;
    p.x += columnradius * M_SQRT2_F;
    if (safe_mod(n, 2.0f) == 1.0f) {
      p.y += columnradius;
    }
    // At this point, we have turned 45 degrees and moved at a point on the
    // diagonal that we want to place the columns on.
    // Now, repeat the domain along this direction and place a circle.
    pMod1(&p.y, columnradius * 2.0f);
    float result = len(p) - columnradius;
    result = min(result, p.x);
    result = min(result, a);
    return min(result, b);
  }
  else {
    return min(a, b);
  }
}

ccl_device_inline float sdf_op_diff_columns(float a, float b, float r, float n)
{
  a = -a;
  float m = min(a, b);
  // avoid the expensive computation where not needed (produces discontinuity though)
  if ((a < r) && (b < r)) {
    float2 p = make_float2(a, b);
    float columnradius = r * M_SQRT2_F / n / 2.0f;
    columnradius = r * M_SQRT2_F / ((n - 1.0f) * 2.0f + M_SQRT2_F);

    p = pR45(p);
    p.y += columnradius;
    p.x -= M_SQRT2_F / 2.0f * r;
    p.x += -columnradius * M_SQRT2_F / 2.0f;

    if (safe_mod(n, 2.0f) == 1.0f) {
      p.y += columnradius;
    }
    pMod1(&p.y, columnradius * 2.0f);

    float result = -len(p) + columnradius;
    result = max(result, p.x);
    result = min(result, a);
    return -min(result, b);
  }
  else {
    return -m;
  }
}

/* The "Round" variant uses a quarter-circle to join the two objects smoothly. */
ccl_device_inline float sdf_op_union_round(float a, float b, float r)
{
  float2 u = max(make_float2(r - a, r - b), zero_float2());
  return max(r, min(a, b)) - len(u);
}

ccl_device_inline float sdf_op_intersect_round(float a, float b, float r)
{
  float2 u = max(make_float2(r + a, r + b), zero_float2());
  return min(-r, max(a, b)) + len(u);
}

ccl_device_inline float sdf_op_diff_round(float a, float b, float r)
{
  return sdf_op_intersect_round(a, -b, r);
}

ccl_device_inline float sdf_op_intersect_columns(float a, float b, float r, float n)
{
  return sdf_op_diff_columns(a, -b, r, n);
}

ccl_device_inline float sdf_op_union_stairs(float a, float b, float r, float n)
{
  float s = r / n;
  float u = b - r;
  return min(min(a, b), 0.5f * (u + a + fabsf((safe_mod(u - a + s, 2.0f * s)) - s)));
}

ccl_device_inline float sdf_op_intersect_stairs(float a, float b, float r, float n)
{
  return -sdf_op_union_stairs(-a, -b, r, n);
}

ccl_device_inline float sdf_op_diff_stairs(float a, float b, float r, float n)
{
  return -sdf_op_union_stairs(-a, b, r, n);
}

ccl_device_inline float sdf_op_divide(float a, float b, float gap, float gap2)
{
  float di = max(a, -b);
  float da = max(a, -(b - gap));
  float db = max(b, -(di - gap2));
  return min(da, db);
}

ccl_device_inline float sdf_op_extrude(float3 *p, float3 h)
{
  float3 q = fabs(*p) - h;
  float3 b = sgn(*p) * max(q, zero_float3());
  *p = b;
  return -min(max(q.x, max(q.y, q.z)), 0.0f);
}

ccl_device_inline float3 mat2_float3(float ma, float mb, float mc, float md, float3 p)
{
  float3 r;

  r.x = ma * p.x + mc * p.y;
  r.y = mb * p.x + md * p.y;
  r.z = p.z;

  return r;
}

ccl_device_inline float2 mat2_float2(float ma, float mb, float mc, float md, float2 p)
{
  float2 r;

  r.x = ma * p.x + mc * p.y;
  r.y = mb * p.x + md * p.y;
  
  return r;
}

ccl_device_inline float3 sdf_op_bend(float3 p, float k)
{
  float c = cosf(k * p.x);
  float s = sinf(k * p.x);
  return mat2_float3(c, -s, s, c, p);
}

ccl_device_inline float sdf_op_exclusion(float a, float b, float gap, float gap2)
{
  return max(min(a, b) - gap2, -(max(a, b)) - gap);
}

ccl_device_inline float sdf_op_mask(float a, float v, float v2, float invert)
{
  float dist;
  a -= v2;
  if (v == 0.0f) {
    dist = (a > 0.0f) ? 0.0f : 1.0f;
  }
  else {
    dist = map_value(a, -v, 0.0f, 0.0f, 1.0f, 0.0f);
    if (v > 0.0f) {
      dist = 1.0f - dist;
    }
    dist = clamp(dist, 0.0f, 1.0f);
  }
  if (invert > 0.0f) {
    dist = 1.0f - dist;
  }
  return dist;
}

ccl_device_inline float3 sdf_op_twist(float3 p, float k, float offset)
{
  return rotate_2d(p, k * p.z + offset);
}

ccl_device_inline float3 sdf_op_spin(float3 p, float offset)
{
  return make_float3(len(float3_to_float2(p)) - offset, p.z, p.y);
}

/* Produces a cylindical pipe that runs along the intersect.
 No objects remain, only the pipe. This is not a boolean operator. */
ccl_device_inline float sdf_op_pipe(float a, float b, float r)
{
  return len(make_float2(a, b)) - r;
}

ccl_device_inline float sdf_op_engrave(float a, float b, float r)
{
  return max(a, (a + r - fabsf(b)) * M_SQRT1_2_F);
}

ccl_device_inline float sdf_op_groove(float a, float b, float ra, float rb)
{
  return max(a, min(a + ra, rb - fabsf(b)));
}

ccl_device_inline float sdf_op_tongue(float a, float b, float ra, float rb)
{
  return min(a, max(a - ra, fabsf(b) - rb));
}

ccl_device_inline float sdf_op_polar(float3 *p, float repetitions)
{
  float2 pxy = float3_to_float2(*p);
  float angle = safe_divide(2.0f * M_PI_F, repetitions);
  float a = atan2f(pxy.y, pxy.x) + angle / 2.0f;
  float r = len(pxy);
  float c = floorf(a / angle);
  a = safe_mod(a, angle) - angle / 2.0f;
  pxy = make_float2(cosf(a), sinf(a)) * r;
  *p = make_float3(pxy.x, pxy.y, p->z);
  // For an odd number of repetitions, fix cell index of the cell in -x direction
  // (cell index would be e.g. -5 and 5 in the two halves of the cell):
  if (fabsf(c) >= (repetitions / 2.0f)) {
    c = fabsf(c);
  }
  return c;
}

ccl_device_inline float sdf_op_reflect(float3 *p, float3 plane_normal, float offset)
{
  float t = dot(*p, plane_normal) + offset;
  if (t < 0.0f) {
    *p = *p - (2.0f * t) * plane_normal;
  }
  return signf(t);
}

ccl_device_inline float sdf_2d_circle(float2 p)
{
  return len(p) - 1.0f;
}

ccl_device_inline float sdf_2d_line(float2 p, float2 a, float2 b)
{
  float2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0f, 1.0f);
  return len(pa - ba * h);
}

ccl_device_inline float sdf_2d_rectangle(float2 p, float w, float h)
{
  float2 b = make_float2(w, h);
  float2 d = fabs(p) - b * 0.5f;
  return len(max(d, zero_float2())) + min(max(d.x, d.y), 0.0f);
}

ccl_device_inline float sdf_2d_rhombus(float2 p, float2 w)
{
  float2 b = w * 0.5f;

  if (len(b) == 0.0f) {
    return len(p);
  }

  float2 q = fabs(p);

  float h = clamp(safe_divide((-2.0f * ndot(q, b) + ndot(b, b)), dot(b, b)), -1.0f, 1.0f);
  float d = len(q - 0.5f * b * make_float2(1.0f - h, 1.0f + h));
  d *= signf(q.x * b.y + q.y * b.x - b.x * b.y);

  return d;
}

ccl_device_inline float sdf_2d_hexagon(float2 p, float r)
{
  const float3 k = make_float3(-0.866025404f, 0.5f, 0.577350269f);
  float2 kxy = make_float2(k.x, k.y);
  p = fabs(p);
  p = p - (2.0f * min(dot(kxy, p), 0.0f) * kxy);
  p = p - make_float2(clamp(p.x, -k.z * r, k.z * r), r);
  return len(p) * signf(p.y);
}

ccl_device_inline float sdf_2d_triangle(float2 p, float r)
{
  const float k = safe_sqrtf(3.0f);
  p.x = fabsf(p.x) - r;
  p.y = p.y + r / k;
  if (p.x + k * p.y > 0.0f)
    p = make_float2(p.x - k * p.y, -k * p.x - p.y) / 2.0f;
  p.x -= clamp(p.x, -2.0f * r, 0.0f);
  return -len(p) * signf(p.y);
}

ccl_device_inline float sdf_2d_star(float2 p, float r, float n, float m, float x)
{
  float an = M_PI_F / n;
  float en = M_PI_F / m;  // m is between 2 and n
  float2 acs = make_float2(cosf(an), sinf(an));
  float2 ecs = make_float2(cosf(en), sinf(en));  // ecs=make_float2(0,1) for regular polygon,

  float bn = safe_mod(atan2f(p.x, p.y), 2.0f * an) - an;
  p = len(p) * make_float2(cosf(bn), fabsf(sinf(bn)));
  p = p - r * acs;
  p += ecs * clamp(-dot(p, ecs), 0.0f, safe_divide(r * acs.y, ecs.y));
  return len(p) * signf(p.x);
}

ccl_device_inline float sdf_3d_hex_prism(float3 p, float2 h)
{
  h *= 0.5f;
  float3 k = make_float3(-M_SQRT3_2_F, 0.5f, M_SQRT1_3_F);
  p = fabs(p);
  float2 pxy = float3_to_float2(p);
  float2 kxy = float3_to_float2(k);
  pxy = pxy - 2.0f * min(dot(kxy, pxy), 0.0f) * kxy;
  float2 d = make_float2(len(pxy - make_float2(clamp(pxy.x, -k.z * h.x, k.z * h.x), h.x)) *
                             signf(pxy.y - h.x),
                         p.z - h.y);
  return min(max(d.x, d.y), 0.0f) + len(max(d, zero_float2()));
}

ccl_device_inline float sdf_3d_hex_prism_incircle(float3 p, float2 h)
{
  return sdf_3d_hex_prism(p, make_float2(h.x * M_SQRT3_F * 0.5f, h.y));
}

ccl_device_inline float sdf_capped_cylinder(float3 p, float r, float h)
{
  float2 d = fabs(make_float2(len(float3_to_float2(p)), p.z)) - make_float2(r, h * 0.5f);
  return min(max(d.x, d.y), 0.0f) + len(max(d, zero_float2()));
}

ccl_device_inline float sdf_3d_sphere(float3 p, float s)
{
  return len(p) - s;
}

ccl_device_inline float sdf_3d_solid_angle(float3 p, float a, float ra)
{
  float2 sc = sincos(clamp(a * 0.5f, -M_PI_F, M_PI_F));
  float2 q = make_float2(len(float3_to_float2(p)), p.z);
  float l = len(q) - ra;
  float m = len(q - sc * clamp(dot(q, sc), 0.0f, ra));
  return max(l, m * signf(sc.y * q.x - sc.x * q.y));
}

ccl_device_inline float sdf_3d_pyramid(float3 p, float w, float h)
{
  p = make_float3(p.x, p.z, p.y); /* z-up */

  p /= w;
  h /= w;

  float m2 = h * h + 0.25f;

  /* symmetry */
  p.x = fabsf(p.x);
  p.z = fabsf(p.z);
  if (p.z > p.x) {
    float tmp = p.z;
    p.z = p.x;
    p.x = tmp;
  }
  p.x -= 0.5f;
  p.z -= 0.5f;

  /* project into face plane (2D) */
  float3 q = make_float3(p.z, h * p.y - 0.5f * p.x, h * p.x + 0.5f * p.y);

  float s = max(-q.x, 0.0f);
  float t = clamp((q.y - 0.5f * p.z) / (m2 + 0.25f), 0.0f, 1.0f);

  float a = m2 * (q.x + s) * (q.x + s) + q.y * q.y;
  float b = m2 * (q.x + 0.5f * t) * (q.x + 0.5f * t) + (q.y - m2 * t) * (q.y - m2 * t);

  float d2 = min(q.y, -q.x * m2 - q.y * 0.5f) > 0.0f ? 0.0f : min(a, b);

  /* recover 3D and scale, and add sign */
  float d = safe_sqrtf((d2 + q.z * q.z) / m2) * signf(max(q.z, -p.y));

  return d * w;
}

ccl_device_inline float sdf_3d_box(float3 p, float3 b)
{
  b *= 0.5f;
  float3 q = fabs(p) - b;
  return len(max(q, zero_float3())) + min(max(q.x, max(q.y, q.z)), 0.0f);
}

ccl_device_inline float sdf_3d_torus(float3 p, float2 t)
{
  float2 pxz = make_float2(p.x, p.y);
  float2 q = make_float2(len(pxz) - t.x, p.z);
  return len(q) - t.y;
}

ccl_device_inline float sdf_3d_cone(float3 p, float a)
{
  // c is the sin/cos of the angle
  float2 c = make_float2(sinf(a), cosf(a));
  float2 pxy = make_float2(p.x, p.y);
  float q = len(pxy);
  return dot(c, make_float2(q, p.z));
}

ccl_device_inline float sdf_3d_point_cone(float3 p, float3 a, float3 b, float ra, float rb)
{
  float rba = rb - ra;
  float baba = dot(b - a, b - a);
  float papa = dot(p - a, p - a);
  float paba = dot(p - a, b - a) / baba;

  float x = safe_sqrtf(papa - paba * paba * baba);

  float cax = max(0.0f, x - ((paba < 0.5f) ? ra : rb));
  float cay = fabsf(paba - 0.5f) - 0.5f;

  float k = rba * rba + baba;
  float f = clamp((rba * (x - ra) + paba * baba) / k, 0.0f, 1.0f);

  float cbx = x - ra - f * rba;
  float cby = paba - f;

  float s = (cbx < 0.0f && cay < 0.0f) ? -1.0f : 1.0f;

  return s * safe_sqrtf(min(cax * cax + cay * cay * baba, cbx * cbx + cby * cby * baba));
}

ccl_device_inline float sdf_3d_upright_cone(float3 p, float radius, float height, float offset)
{
  /* Sanitise inputs. */
  p.z -= offset * signf(height);

  if (len(make_float2(radius, height)) == 0.0f) {
    return len(p);
  }

  if (height < 0.0f) {
    height = fabsf(height);
    p.z = -p.z;
  }

  radius = fabsf(radius);

  /* Cone. */
  float2 q = make_float2(len(float3_to_float2(p)), p.z);
  float2 tip = q - make_float2(0.0f, height);
  float2 mantleDir = normalize(make_float2(height, radius));
  float mantle = dot(tip, mantleDir);
  float d = max(mantle, -q.y);
  float projected = dot(tip, make_float2(mantleDir.y, -mantleDir.x));

  // distance to tip
  if ((q.y > height) && (projected < 0.0f)) {
    d = max(d, len(tip));
  }

  // distance to base ring
  if ((q.x > radius) && (projected > len(make_float2(height, radius)))) {
    d = max(d, len(q - make_float2(radius, 0.0f)));
  }
  return d;
}

ccl_device_inline float sdf_3d_cylinder(float3 p, float3 c)
{
  float2 pxz = make_float2(p.x, p.z);
  float2 cxy = make_float2(c.x, c.y);
  return len(pxz - cxy) - c.z;
}

ccl_device_inline float sdf_3d_cylinder(float3 p, float3 a, float3 b, float r)
{
  float3 pa = p - a;
  float3 ba = b - a;
  float baba = dot(ba, ba);
  float paba = dot(pa, ba);

  float x = len(pa * baba - ba * paba) - r * baba;
  float y = fabsf(paba - baba * 0.5f) - baba * 0.5f;
  float x2 = x * x;
  float y2 = y * y * baba;
  float d = (max(x, y) < 0.0f) ? -min(x2, y2) :
                                 (((x > 0.0f) ? x2 : 0.0f) + ((y > 0.0f) ? y2 : 0.0f));
  return signf(d) * safe_sqrtf(fabsf(d)) / baba;
}

ccl_device_inline float sdf_3d_circle(float3 p, float r)
{
  float l = len(float3_to_float2(p)) - r;
  return len(make_float2(p.z, l));
}

// A circular disc with no thickness (i.e. a cylinder with no height).
// Subtract some value to make a flat disc with rounded edge.
ccl_device_inline float sdf_3d_disc(float3 p, float r)
{
  float l = len(float3_to_float2(p)) - r;
  return l < 0.0f ? fabsf(p.z) : len(make_float2(p.z, l));
}

ccl_device_inline float sdf_2d_corner(float2 p)
{
  return len(max(p, zero_float2())) + vmax(min(p, zero_float2()));
}

ccl_device_inline float sdf_3d_capsule(float3 p, float3 a, float3 b, float r)
{
  float3 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0f, 1.0f);
  return len(pa - ba * h) - r;
}

ccl_device_inline float sdf_3d_octahedron(float3 p, float s)
{
  p = fabs(p);
  float m = p.x + p.y + p.z - s;

  float3 q;
  if (3.0f * p.x < m) {
    q = p;
  }
  else if (3.0f * p.y < m) {
    q = make_float3(p.y, p.z, p.x);
  }
  else if (3.0f * p.z < m) {
    q = make_float3(p.z, p.x, p.y);
  }
  else {
    return m * M_SQRT1_3_F;
  }
  float k = clamp(0.5f * (q.z - q.y + s), 0.0f, s);
  return len(make_float3(q.x, q.y - s + k, q.z - k));
}

ccl_device_inline float sdf_2d_pie(float2 p, float r, float a)
{
  float2 sc = sincos(clamp(a * 0.5f, 0.0f, M_PI_F));
  p.x = fabsf(p.x);
  float l = len(p) - r;
  float m = len(p - sc * clamp(dot(p, sc), 0.0f, r));
  return max(l, m * signf(sc.y * p.x - sc.x * p.y));
}

ccl_device_inline float sdf_2d_arc(float2 p, float a, float ra)
{
  float2 sc = sincos(clamp(a * 0.5f, 0.0f, M_PI_F));
  p.x = fabsf(p.x);
  float k = (sc.y * p.x > sc.x * p.y) ? dot(p, sc) : len(p);
  return sqrtf(dot(p, p) + ra * ra - 2.0f * ra * k);
}

ccl_device_inline float sdf_2d_horseshoe(float2 p, float r, float a, float overshoot, float lw)
{
  float2 cs = cosin(clamp(a * 0.5f, 0.0f, M_PI_F));
  p.x = fabsf(p.x);
  float l = len(p);

  p = mat2_float2(-cs.x, cs.y, cs.y, cs.x, p);
  p = make_float2((p.y > 0.0f || p.x > 0.0f) ? p.x : l * signf(-cs.x), (p.x > 0.0f) ? p.y : l);
  p = make_float2(p.x, fabsf(p.y - r)) - make_float2(overshoot, lw) * 0.5f;
  return len(max(p, zero_float2())) + min(0.0f, max(p.x, p.y));
}

ccl_device_inline float sdf_2d_bezier(float2 pos, float2 A, float2 C, float2 B)
{
  float2 a = B - A;
  float2 b = A - 2.0f * B + C;
  float2 c = a * 2.0f;
  float2 d = A - pos;
  float kk = 1.0f / dot(b, b);
  float kx = kk * dot(a, b);
  float ky = kk * (2.0f * dot(a, a) + dot(d, b)) / 3.0f;
  float kz = kk * dot(d, a);
  float res = 0.0f;
  float p = ky - kx * kx;
  float p3 = p * p * p;
  float q = kx * (2.0f * kx * kx - 3.0f * ky) + kz;
  float h = q * q + 4.0f * p3;
  if (h >= 0.0f) {
    h = safe_sqrtf(h);
    float2 x = (make_float2(h, -h) - q) / 2.0f;
    float2 powx = make_float2(powf(fabsf(x.x), 1.0f / 3.0f), powf(fabsf(x.y), 1.0f / 3.0f));
    float2 uv = sgn(x) * powx;
    float t = clamp(uv.x + uv.y - kx, 0.0f, 1.0f);
    res = dot2(d + (c + b * t) * t);
  }
  else {
    float z = safe_sqrtf(-p);
    float v = acosf(q / (p * z * 2.0f)) / 3.0f;
    float m = cosf(v);
    float n = sinf(v) * M_SQRT3_F;
    float3 t = clamp(make_float3(m + m, -n - m, n - m) * z - kx, zero_float3(), one_float3());
    res = min(dot2(d + (c + b * t.x) * t.x), dot2(d + (c + b * t.y) * t.y));
  }
  return safe_sqrtf(res);
}

ccl_device_inline float sdf_2d_ellipse(float2 p, float2 ab)
{
  ab *= 0.5f;
  if (ab.x == ab.y) {
    return len(p) - ab.x;
  }
  p = fabs(p);
  if (p.x > p.y) {
    p = make_float2(p.y, p.x);
    ab = make_float2(ab.y, ab.x);
  }
  float l = ab.y * ab.y - ab.x * ab.x;
  float m = ab.x * p.x / l;
  float m2 = m * m;
  float n = ab.y * p.y / l;
  float n2 = n * n;
  float c = (m2 + n2 - 1.0f) / 3.0f;
  float c3 = c * c * c;
  float q = c3 + m2 * n2 * 2.0f;
  float d = c3 + m2 * n2;
  float g = m + m * n2;
  float co;
  if (d < 0.0f) {
    float h = acosf(q / c3) / 3.0f;
    float s = cosf(h);
    float t = sinf(h) * M_SQRT3_F;
    float rx = safe_sqrtf(-c * (s + t + 2.0f) + m2);
    float ry = safe_sqrtf(-c * (s - t + 2.0f) + m2);
    co = (ry + signf(l) * rx + fabsf(g) / (rx * ry) - m) / 2.0f;
  }
  else {
    float h = 2.0f * m * n * safe_sqrtf(d);
    float s = signf(q + h) * safe_powf(fabsf(q + h), 1.0f / 3.0f);
    float u = signf(q - h) * safe_powf(fabsf(q - h), 1.0f / 3.0f);
    float rx = -s - u - c * 4.0f + 2.0f * m2;
    float ry = (s - u) * M_SQRT3_F;
    float rm = safe_sqrtf(rx * rx + ry * ry);
    co = (ry / safe_sqrtf(rm - rx) + 2.0f * g / rm - m) / 2.0f;
  }
  float2 r = ab * make_float2(co, safe_sqrtf(1.0f - co * co));
  return len(r - p) * signf(p.y - r.y);
}

ccl_device_inline float sdf_2d_heart(float2 p, float r)
{
  r *= 2.0f;
  p.y += r * 0.5f;

  p.x = fabsf(p.x);

  if (p.y + p.x > r) {
    return safe_sqrtf(dot2(p - make_float2(0.25f, 0.75f) * r)) - M_SQRT2_F / 4.0f * r;
  }
  else {
    return safe_sqrtf(
               min(dot2(p - make_float2(0.0f, 1.0f) * r), dot2(p - 0.5f * max(p.x + p.y, 0.0f)))) *
           signf(p.x - p.y);
  }
}

ccl_device_inline float sdf_2d_quad(float2 p, float2 p0, float2 p1, float2 p2, float2 p3)
{
  float2 e0 = p1 - p0;
  float2 v0 = p - p0;
  float2 e1 = p2 - p1;
  float2 v1 = p - p1;
  float2 e2 = p3 - p2;
  float2 v2 = p - p2;
  float2 e3 = p0 - p3;
  float2 v3 = p - p3;

  float2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0f, 1.0f);
  float2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0f, 1.0f);
  float2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0f, 1.0f);
  float2 pq3 = v3 - e3 * clamp(dot(v3, e3) / dot(e3, e3), 0.0f, 1.0f);

  float2 ds = min(min(make_float2(dot(pq0, pq0), v0.x * e0.y - v0.y * e0.x),
                      make_float2(dot(pq1, pq1), v1.x * e1.y - v1.y * e1.x)),
                  min(make_float2(dot(pq2, pq2), v2.x * e2.y - v2.y * e2.x),
                      make_float2(dot(pq3, pq3), v3.x * e3.y - v3.y * e3.x)));

  float d = safe_sqrtf(ds.x);

  return (ds.y > 0.0f) ? -d : d;
}

ccl_device_inline float sdf_2d_uneven_capsule(float2 p, float2 pa, float2 pb, float ra, float rb)
{
  p = p - pa;
  pb = pb - pa;
  float h = dot(pb, pb);
  float2 q = safe_divide_float2_float(make_float2(dot(p, make_float2(pb.y, -pb.x)), dot(p, pb)), h);

  q.x = fabsf(q.x);

  float b = ra - rb;
  float2 c = make_float2(safe_sqrtf(h - b * b), b);

  float k = cross2(c, q);
  float m = dot(c, q);
  float n = dot(q, q);

  if (k < 0.0f) {
    return safe_sqrtf(h * (n)) - ra;
  }
  else if (k > c.x) {
    return safe_sqrtf(h * (n + 1.0f - 2.0f * q.y)) - rb;
  }
  return m - ra;
}

ccl_device_inline float sdf_2d_parabola(float2 pos, float k)
{
  float d = pos.y;

  if (k != 0.0f) {
    pos.x = fabsf(pos.x);

    float p = (1.0f - 2.0f * k * pos.y) / (6.0f * k * k);
    float q = -fabsf(pos.x) / (4.0f * k * k);

    float h = q * q + p * p * p;
    float r = safe_sqrtf(fabsf(h));

    float x = (h > 0.0f) ? safe_powf(-q + r, 1.0f / 3.0f) -
                               safe_powf(fabsf(-q - r), 1.0f / 3.0f) * signf(q + r) :
                           2.0f * cosf(atan2f(r, -q) / 3.0f) * safe_sqrtf(-p);

    d = len(pos - make_float2(x, k * x * x)) * signf(pos.x - x);
  }

  return d;
}

ccl_device_inline float sdf_2d_parabola_segment(float2 pos, float wi, float he)
{
  wi *= 0.5f;
  pos.x = fabsf(pos.x);

  float ik = wi * wi / he;
  float p = ik * (he - pos.y - 0.5f * ik) / 3.0f;
  float q = pos.x * ik * ik * 0.25f;
  float h = q * q - p * p * p;

  float x;
  if (h > 0.0f)  // 1 root
  {
    float r = safe_sqrtf(h);
    x = safe_powf(q + r, 1.0f / 3.0f) - safe_powf(fabsf(q - r), 1.0f / 3.0f) * signf(r - q);
  }
  else  // 3 roots
  {
    float r = safe_sqrtf(p);
    x = 2.0f * r * cosf(acosf(q / (p * r)) / 3.0f);
  }

  x = min(x, wi);

  float d = len(pos - make_float2(x, he - x * x / ik)) * signf(ik * (pos.y - he) + pos.x * pos.x);

  return d;
}

ccl_device_inline float sdf_2d_vesica(float2 p, float r, float d)
{
  p = fabs(p);

  float b = safe_sqrtf(r * r - d * d);  // can delay this sqrt by rewriting the comparison
  return ((p.y - b) * d > p.x * b) ? len(p - make_float2(0.0f, b)) * signf(d) :
                                     len(p - make_float2(-d, 0.0f)) - r;
}

ccl_device_inline float sdf_2d_flat_joint(float2 p, float l, float a, float lw)
{
  if (fabsf(a) < 0.001f) {
    a = 0.001f;
  }

  lw *= 0.5f;

  float2 sc = sincos(clamp(a * 0.5f, -M_PI_F, M_PI_F));
  float ra = safe_divide(0.5f * l, a);

  p.x -= ra;

  float2 q = p - 2.0f * sc * max(0.0f, dot(sc, p));

  float u = fabsf(ra) - len(q);
  float d = max(len(make_float2(q.x + ra - clamp(q.x + ra, -lw, lw), q.y)) * signf(-q.y),
                fabsf(u) - lw);

  return d;
}

ccl_device_inline float sdf_2d_round_joint(float2 p, float l, float a, float lw)
{
  if (fabsf(a) < 0.001f) {
    a = 0.001f;
  }

  lw *= 0.5f;

  float2 sc = sincos(clamp(a * 0.5f, -M_PI_F, M_PI_F));
  float ra = safe_divide(0.5f * l, a);

  p.x -= ra;

  float2 q = p - 2.0f * sc * max(0.0f, dot(sc, p));

  float u = fabsf(ra) - len(q);
  float d = (q.y < 0.0f) ? len(q + make_float2(ra, 0.0f)) : fabsf(u);

  return d - lw;
}

ccl_device_inline float sdf_2d_trapezoid(float2 p, float w2, float h, float w1)
{
  float r2 = w2 * 0.5f;
  float r1 = w1 * 0.5f;
  float he = h * 0.5f;
  float2 k1 = make_float2(r2, he);
  float2 k2 = make_float2(r2 - r1, 2.0f * he);
  p.x = fabsf(p.x);
  float2 ca = make_float2(p.x - min(p.x, (p.y < 0.0f) ? r1 : r2), fabsf(p.y) - he);
  float2 cb = p - k1 + k2 * clamp(dot(k1 - p, k2) / dot2(k2), 0.0f, 1.0f);
  float s = ((cb.x < 0.0f) && (ca.y < 0.0f)) ? -1.0f : 1.0f;
  return s * safe_sqrtf(min(dot2(ca), dot2(cb)));
}

ccl_device_inline float sdf_2d_rounded_x(float2 p, float w)
{
  p = fabs(p);
  return len(p - min(p.x + p.y, w) * 0.5f);
}

ccl_device_inline float sdf_2d_blobby_cross(float2 pos, float he)
{
  if (fabsf(he) < 0.001f) {
    he = 0.001f;
  }
  pos = fabs(pos);
  pos = make_float2(fabsf(pos.x - pos.y), 1.0f - pos.x - pos.y) / M_SQRT2_F;

  float p = (he - pos.y - 0.25f / he) / (6.0f * he);
  float q = pos.x / (he * he * 16.0f);
  float h = q * q - p * p * p;

  float x;
  if (h > 0.0f) {
    float r = safe_sqrtf(h);
    x = safe_powf(q + r, 1.0f / 3.0f) - safe_powf(fabsf(q - r), 1.0f / 3.0f) * signf(r - q);
  }
  else {
    float r = safe_sqrtf(p);
    x = 2.0f * r * cosf(acosf(safe_divide(q, (p * r))) / 3.0f);
  }
  x = min(x, M_SQRT2_F / 2.0f);

  float2 z = make_float2(x, he * (1.0f - 2.0f * x * x)) - pos;
  return len(z) * signf(z.y);
}

ccl_device_inline float sdf_2d_isosceles(float2 p, float2 q)
{
  if (len(q) == 0.0f) {
    return len(p);
  }
  p.x = fabsf(p.x);
  q.y = -q.y;
  p.y += q.y * 0.5f;
  q.x *= 0.5f;
  float2 a = p - q * clamp(dot(p, q) / dot(q, q), 0.0f, 1.0f);
  float2 b = p - q * make_float2(clamp(p.x / q.x, 0.0f, 1.0f), 1.0f);
  float s = -signf(q.y);
  float2 d = min(make_float2(dot(a, a), s * (p.x * q.y - p.y * q.x)),
                 make_float2(dot(b, b), s * (p.y - q.y)));
  return -safe_sqrtf(d.x) * signf(d.y);
}

ccl_device_inline float sdf_2d_moon(float2 p, float d, float ra, float rb)
{
  p.y = fabsf(p.y);

  float a = (ra * ra - rb * rb + d * d) / (2.0f * d);
  float b = safe_sqrtf(max(ra * ra - a * a, 0.0f));
  if (d * (p.x * b - p.y * a) > d * d * max(b - p.y, 0.0f)) {
    return len(p - make_float2(a, b));
  }

  return max((len(p) - ra), -(len(p - make_float2(d, 0)) - rb));
}

ccl_device_inline float sdf_2d_pentagon(float2 p, float r)
{
  const float3 k = make_float3(0.809016994f, 0.587785252f, 0.726542528f);  // pi/5: cos, sin, tan
  p.y = -p.y;
  p.x = fabsf(p.x);
  p = p - (2.0f * min(dot(make_float2(-k.x, k.y), p), 0.0f) * make_float2(-k.x, k.y));
  p = p - (2.0f * min(dot(make_float2(k.x, k.y), p), 0.0f) * make_float2(k.x, k.y));
  p = p - make_float2(clamp(p.x, -r * k.z, r * k.z), r);
  return len(p) * signf(p.y);
}

ccl_device_inline float sdf_2d_point_triangle(float2 p, float2 p0, float2 p1, float2 p2)
{
  float2 e0 = p1 - p0;
  float2 e1 = p2 - p1;
  float2 e2 = p0 - p2;

  float2 v0 = p - p0;
  float2 v1 = p - p1;
  float2 v2 = p - p2;

  float2 pq0 = v0 - e0 * clamp(safe_divide(dot(v0, e0), dot(e0, e0)), 0.0f, 1.0f);
  float2 pq1 = v1 - e1 * clamp(safe_divide(dot(v1, e1), dot(e1, e1)), 0.0f, 1.0f);
  float2 pq2 = v2 - e2 * clamp(safe_divide(dot(v2, e2), dot(e2, e2)), 0.0f, 1.0f);
  float s = signf(e0.x * e2.y - e0.y * e2.x);
  float2 d = min(min(make_float2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                     make_float2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                 make_float2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
  return -safe_sqrtf(d.x) * signf(d.y);
}

/* Sdf Ops */

ccl_device_inline float sdf_op_union(float a, float b)
{
  return min(a, b);
}

ccl_device_inline float sdf_op_intersection(float a, float b)
{
  return max(a, b);
}

ccl_device_inline float sdf_op_difference(float a, float b)
{
  return max(-a, b);
}

ccl_device_inline float sdf_op_union_smooth(float a, float b, float k)
{
  if (k != 0.0f) {
    float h = max(k - fabsf(a - b), 0.0f);
    return min(a, b) - h * h * 0.25f / k;
  }
  else {
    return min(a, b);
  }
}

ccl_device_inline float sdf_op_diff_smooth(float a, float b, float k)
{
  if (k != 0.0f) {
    float h = max(k - fabsf(-b - a), 0.0f);
    return max(a, -b) + h * h * 0.25f / k;
  }
  else {
    return max(a, -b);
  }
}

ccl_device_inline float sdf_op_intersect_smooth(float a, float b, float k)
{
  if (k != 0.0f) {
    float h = max(k - fabsf(a - b), 0.0f);
    return max(a, b) + h * h * 0.25f / k;
  }
  else {
    return max(a, b);
  }
}

// The "Chamfer" flavour makes a 45-degree chamfered edge (the diagonal of a square of size <r>):
ccl_device_inline float sdf_op_union_chamfer(float a, float b, float r)
{
  return min(min(a, b), (a - r + b) * M_SQRT1_2_F);
}

// intersect has to deal with what is normally the inside of the resulting object
// when using union, which we normally don't care about too much. Thus, intersect
// implementations sometimes differ from union implementations.
ccl_device_inline float sdf_op_intersect_chamfer(float a, float b, float r)
{
  return max(max(a, b), (a + r + b) * M_SQRT1_2_F);
}

// Difference can be built from intersect or Union:
ccl_device_inline float sdf_op_diff_chamfer(float a, float b, float r)
{
  return sdf_op_intersect_chamfer(a, -b, r);
}

ccl_device_inline float sdf_op_dilate(float a, float k)
{
  return a - k;
}

ccl_device_inline float sdf_op_onion(float a, float k, int n)
{
  k *= 0.5f;
  if (n > 0) {
    float d = a;
    for (int i = 0; i < n; i++) {
      d = fabsf(d) - k;
    }
    return d;
  }
  else {
    return fabsf(a) - k;
  }
}

ccl_device_inline float sdf_op_blend(float a, float b, float k)
{
  return mix(a, b, k);
}

ccl_device_inline float sdf_op_flatten(float a, float b, float v)
{
  if (b > a) {
    v = map_value(v, a, b, 0.0f, 1.0f, 0.0f);
    return clamp(v, 0.0f, 1.0f);
  }
  else {
    v = map_value(v, b, a, 0.0f, 1.0f, 0.0f);
    return clamp(v, 0.0f, 1.0f);
  }
}

CCL_NAMESPACE_END
