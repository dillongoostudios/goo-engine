#include "gpu_shader_common_math_utils.glsl"

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

#define M_SQRT3 1.73205080756887729352f   /* sqrt(3) */
#define M_SQRT3_2 0.86602540378443864676f /* sqrt(3)/2 */
#define M_SQRT1_3 0.57735026918962576450f /* sqrt(1/3) */
#define M_SQRT1_2 0.70710678118654752440f /* sqrt(1/2) */
#define M_PHI 4.97213595499957939281f     /* (sqrt(5)*0.5f + 0.5f) */

/**
 * Safe square root function. Returns `sqrt(a)`.
 * If `a` is less or equal to 0 then the result will be 0.
 */
float sdf_safe_sqrt(float a)
{
  return sqrt(max(0.0f, a));
}

float sdf_safe_mod(float a, float b)
{
  return (b != 0.0f) ? a - b * floor(a / b) : 0.0f;
}

float3 sdf_safe_mod(float3 a, float b)
{
  a.x = sdf_safe_mod(a.x, b);
  a.y = sdf_safe_mod(a.y, b);
  a.z = sdf_safe_mod(a.z, b);
  return a;
}

float3 sdf_safe_mod(float3 a, float3 b)
{
  a.x = sdf_safe_mod(a.x, b.x);
  a.y = sdf_safe_mod(a.y, b.y);
  a.z = sdf_safe_mod(a.z, b.z);
  return a;
}

float2 sdf_safe_mod(float2 a, float2 b)
{
  a.x = sdf_safe_mod(a.x, b.x);
  a.y = sdf_safe_mod(a.y, b.y);
  return a;
}

float ndot(float2 a, float2 b)
{
  return a.x * b.x - a.y * b.y;
}

float dot2(in float2 v)
{
  return dot(v, v);
}

float cross2(in float2 a, in float2 b)
{
  return a.x * b.y - a.y * b.x;
}

float2 sincos(float a)
{
  return float2(sin(a), cos(a));
}

/* Sign function that doesn't return 0 */
float sgn(float v)
{
  return (v < 0.0f) ? -1.0f : 1.0f;
}

float2 sgn(float2 v)
{
  return float2((v.x < 0.0f) ? -1.0f : 1.0f, (v.y < 0.0f) ? -1.0f : 1.0f);
}

float3 sgn(float3 v)
{
  return float3((v.x < 0.0f) ? -1.0f : 1.0f, (v.y < 0.0f) ? -1.0f : 1.0f, (v.z < 0.0f) ? -1.0f : 1.0f);
}

// Maximum/minumum elements of a vector
float vmax(float2 v)
{
  return max(v.x, v.y);
}

float vmax(float3 v)
{
  return max(max(v.x, v.y), v.z);
}

float vmax(float4 v)
{
  return max(max(v.x, v.y), max(v.z, v.w));
}

float vmin(float2 v)
{
  return min(v.x, v.y);
}

float vmin(float3 v)
{
  return min(min(v.x, v.y), v.z);
}

float vmin(float4 v)
{
  return min(min(v.x, v.y), min(v.z, v.w));
}

float map_value(float value, float from_min, float from_max, float to_min, float to_max, float d)
{
  return (from_max != from_min) ?
             to_min + ((value - from_min) / (from_max - from_min)) * (to_max - to_min) :
             d;
}

float2 map_value(float2 p, float from_min, float from_max, float to_min, float to_max, float d)
{
  p.x = map_value(p.x, from_min, from_max, to_min, to_max, d);
  p.y = map_value(p.y, from_min, from_max, to_min, to_max, d);
  return p;
}

float3 map_value(float3 p, float from_min, float from_max, float to_min, float to_max, float d)
{
  p.x = map_value(p.x, from_min, from_max, to_min, to_max, d);
  p.y = map_value(p.y, from_min, from_max, to_min, to_max, d);
  p.z = map_value(p.z, from_min, from_max, to_min, to_max, d);
  return p;
}

/**
 * Equivalent of smoothstep(c-w,c,x)-smoothstep(c,c+w,x)
 * t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
 * return t * t * (3.0f - 2.0f * t);
 */
float cubic_pulse(float center, float width, float x)
{
  x = abs(x - center);
  float inv = sign(width);
  width *= 0.5f;
  width = abs(width);
  if (x > width) {
    return inv >= 0.0f ? 0.0f : 1.0f;
  }
  else {
    x /= width;
    x = 1.0f - x * x * (3.0f - 2.0f * x);
    return inv > 0.0f ? x : 1.0f - x;
  }
}

float p_mod1(inout float p, float size)
{
  float halfsize = size * 0.5f;
  float c = floor((p + halfsize) / size);
  p = sdf_safe_mod(p + halfsize, size) - halfsize;
  return c;
}

float p_mirror(inout float p, float dist)
{
  float s = sgn(p);
  p = abs(p) - dist;
  return s;
}

float3 p_mod_mirror3(inout float3 p, float3 size)
{
  float3 halfsize = size * 0.5f;
  float3 c = floor(safe_divide((p + halfsize), size));
  p = sdf_safe_mod(p + halfsize, size) - halfsize;
  p = p * (sdf_safe_mod(c, float3(2.0f)) * 2.0f - float3(1.0f));
  return c;
}

float2 p_mod_grid2(inout float2 p, float2 size)
{
  float2 c = floor(safe_divide((p + size * 0.5f), size));
  p = sdf_safe_mod(p + size * 0.5f, size) - size * 0.5f;
  p = p * (sdf_safe_mod(c, float2(2.0f)) * 2.0f - float2(1.0f));
  p -= size / 2.0f;
  if (p.x > p.y) {
    p.xy = p.yx;
  }
  c = floor(c / 2.0f);
  return c;
}

float2 rotate_45(float2 p)
{
  return (p + float2(p.y, -p.x)) * M_SQRT1_2;
}

/* SDF Functions. */

float sdf_op_union(float a, float b)
{
  return min(a, b);
}

float sdf_op_diff(float a, float b)
{
  return max(a, -b);
}

float sdf_op_intersect(float a, float b)
{
  return max(a, b);
}

float sdf_op_divide(float a, float b, float gap, float gap2)
{
  float di = max(a, -b);
  float da = max(a, -(b - gap));
  float db = max(b, -(di - gap2));
  return min(da, db);
}

float sdf_op_exclusion(float a, float b, float gap, float gap2)
{
  return max(min(a, b) - gap2, -(max(a, b)) - gap);
}

float3 sdf_op_bend(in float3 p, float k)
{
  float c = cos(k * p.x);
  float s = sin(k * p.x);
  mat2 m = mat2(c, -s, s, c);
  return float3(m * p.xy, p.z);
}

float sdf_op_onion(float a, float k, int n)
{
  k *= 0.5f;
  if (n > 0) {
    float d = a;
    for (int i = 0; i < n; i++) {
      d = abs(d) - k;
    }
    return d;
  }
  else {
    return abs(a) - k;
  }
}

float sdf_op_flatten(float a, float b, float v)
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

// The "Columns" flavour makes n-1 circular columns at a 45 degree angle:
float sdf_op_union_columns(float a, float b, float r, float n)
{
  n += 1.0f;
  if ((a < r) && (b < r) && (n > 0.0f)) {
    float2 p = float2(a, b);
    float columnradius = r * M_SQRT2 / ((n - 1.0f) * 2.0f + M_SQRT2);
    p = rotate_45(p);
    p.x -= M_SQRT2 / 2.0f * r;
    p.x += columnradius * M_SQRT2;
    if (sdf_safe_mod(n, 2.0f) == 1.0f) {
      p.y += columnradius;
    }
    // At this point, we have turned 45 degrees and moved at a point on the
    // diagonal that we want to place the columns on.
    // Now, repeat the domain along this direction and place a circle.
    p_mod1(p.y, columnradius * 2.0f);
    float result = length(p) - columnradius;
    result = min(result, p.x);
    result = min(result, a);
    return min(result, b);
  }
  else {
    return min(a, b);
  }
}

float sdf_op_diff_columns(float a, float b, float r, float n)
{
  a = -a;
  float m = min(a, b);
  if ((a < r) && (b < r)) {
    float2 p = float2(a, b);
    float columnradius = r * M_SQRT2 / n / 2.0f;
    columnradius = r * M_SQRT2 / ((n - 1.0f) * 2.0f + M_SQRT2);
    p = rotate_45(p);
    p.y += columnradius;
    p.x -= M_SQRT2 / 2.0f * r;
    p.x += -columnradius * M_SQRT2 / 2.0f;

    if (sdf_safe_mod(n, 2.0f) == 1.0f) {
      p.y += columnradius;
    }
    p_mod1(p.y, columnradius * 2.0f);

    float result = -length(p) + columnradius;
    result = max(result, p.x);
    result = min(result, a);
    return -min(result, b);
  }
  else {
    return -m;
  }
}

float sdf_op_intersect_columns(float a, float b, float r, float n)
{
  return sdf_op_diff_columns(a, -b, r, n);
}

/* The "Round" variant uses a quarter-circle to join the two objects smoothly. */
float sdf_op_union_round(float a, float b, float r)
{
  float2 u = max(float2(r - a, r - b), float2(0.0f));
  return max(r, min(a, b)) - length(u);
}

float sdf_op_intersect_round(float a, float b, float r)
{
  float2 u = max(float2(r + a, r + b), float2(0.0f));
  return min(-r, max(a, b)) + length(u);
}

float sdf_op_diff_round(float a, float b, float r)
{
  return sdf_op_intersect_round(a, -b, r);
}

/* The "Chamfer" makes a 45-degree chamfered edge (the diagonal of a square of size <r>). */
float sdf_op_union_chamfer(float a, float b, float r)
{
  return min(min(a, b), (a - r + b) * M_SQRT1_2);
}

float sdf_op_intersect_chamfer(float a, float b, float r)
{
  return max(max(a, b), (a + r + b) * M_SQRT1_2);
}

float sdf_op_diff_chamfer(float a, float b, float r)
{
  return sdf_op_intersect_chamfer(a, -b, r);
}

float sdf_op_union_smooth(float a, float b, float k)
{
  if (k != 0.0f) {
    float h = max(k - abs(a - b), 0.0f);
    return min(a, b) - h * h * 0.25f / k;
  }
  else {
    return min(a, b);
  }
}

float sdf_op_diff_smooth(float a, float b, float k)
{
  if (k != 0.0f) {
    float h = max(k - abs(-b - a), 0.0f);
    return max(a, -b) + h * h * 0.25f / k;
  }
  else {
    return max(a, -b);
  }
}

float sdf_op_intersect_smooth(float a, float b, float k)
{
  if (k != 0.0f) {
    float h = max(k - abs(a - b), 0.0f);
    return max(a, b) + h * h * 0.25f / k;
  }
  else {
    return max(a, b);
  }
}

float sdf_op_union_stairs(float a, float b, float r, float n)
{
  float s = r / n;
  float u = b - r;
  return min(min(a, b), 0.5f * (u + a + abs((sdf_safe_mod(u - a + s, 2.0f * s)) - s)));
}

float sdf_op_intersect_stairs(float a, float b, float r, float n)
{
  return -sdf_op_union_stairs(-a, -b, r, n);
}

float sdf_op_diff_stairs(float a, float b, float r, float n)
{
  return -sdf_op_union_stairs(-a, b, r, n);
}

/* Produces a cylindical pipe that runs along the intersect. */
float sdf_op_pipe(float a, float b, float r)
{
  return length(float2(a, b)) - r;
}

float sdf_op_engrave(float a, float b, float r)
{
  return max(a, (a + r - abs(b)) * M_SQRT1_2);
}

float sdf_op_groove(float a, float b, float ra, float rb)
{
  return max(a, min(a + ra, rb - abs(b)));
}

float sdf_op_tongue(float a, float b, float ra, float rb)
{
  return min(a, max(a - ra, abs(b) - rb));
}

float sdf_op_extrude(inout float3 p, in float3 h)
{
  float3 q = abs(p) - h;
  float3 b = sign(p) * max(q, 0.0f);
  float4 r = float4(b, min(max(q.x, max(q.y, q.z)), 0.0f));
  p = r.xyz;
  return -r.w;
}

float3 sdf_op_spin(in float3 p, float offset)
{
  return float3(length(p.xy) - offset, p.z, p.y);
}

float3 sdf_op_twist(in float3 p, float k, float offset)
{
  float c = cos(k * p.z + offset);
  float s = sin(k * p.z + offset);
  mat2 m = mat2(c, -s, s, c);
  return float3(m * p.xy, p.z);
}

float sdf_op_reflect(inout float3 p, float3 plane_normal, float offset)
{
  float t = dot(p, plane_normal) + offset;
  if (t < 0.0f) {
    p = p - (2.0f * t) * plane_normal;
  }
  return sgn(t);
}

float2 sdf_op_mirror(inout float3 p, float3 dist)
{
  return p_mod_grid2(p.xy, dist.xy);
}

float sdf_op_polar(inout float2 p, float repetitions)
{
  float angle = safe_divide(2.0f * M_PI, repetitions);
  float a = atan(p.y, p.x) + angle / 2.0f;
  float r = length(p);
  float c = floor(a / angle);
  a = sdf_safe_mod(a, angle) - angle / 2.0f;
  p = float2(cos(a), sin(a)) * r;
  // For an odd number of repetitions, fix cell index of the cell in -x direction
  // (cell index would be e.g. -5 and 5 in the two halves of the cell):
  if (abs(c) >= (repetitions / 2.0f))
    c = abs(c);
  return c;
}
