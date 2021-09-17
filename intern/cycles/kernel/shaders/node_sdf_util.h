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

float safe_divide(float a, float b)
{
  return (b != 0.0) ? a / b : 0.0;
}

float ndot(vector2 a, vector2 b)
{
  return a.x * b.x - a.y * b.y;
}

float sdf_2d_circle(vector2 p, float r)
{
  return length(p) - r;
}

float sdf_2d_line(vector2 p, vector2 a, vector2 b)
{
  vector2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h);
}

float sdf_2d_box(vector2 p, vector2 b)
{
  vector2 d = abs(p) - b;
  return length(max(d, vector2(0.0, 0.0))) + min(max(d.x, d.y), 0.0);
}

float sdf_2d_rhombus(vector2 p, vector2 b)
{
  vector2 q = abs(p);
  float h = clamp((-2.0 * ndot(q, b) + ndot(b, b)) / dot(b, b), -1.0, 1.0);
  float d = length(q - 0.5 * b * vector2(1.0 - h, 1.0 + h));
  return d * sign(q.x * b.y + q.y * b.x - b.x * b.y);
}

float sdf_2d_hexagon(vector2 p0, float r)
{
  point k = point(-0.866025404, 0.5, 0.577350269);
  vector2 kxy = vector2(k[0], k[1]);
  vector2 p = abs(p0);
  p = p - (2.0 * min(dot(kxy, p), 0.0) * kxy);
  p = p - vector2(clamp(p.x, -k[2] * r, k[2] * r), r);
  return length(p) * sign(p.y);
}

float sdf_2d_triangle(vector2 p0, float r)
{
  vector2 p = p0;
  float k = sqrt(3.0);
  p.x = abs(p.x) - r;
  p.y = p.y + r / k;
  if (p.x + k * p.y > 0.0)
    p = vector2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
  p.x -= clamp(p.x, -2.0 * r, 0.0);
  return -length(p) * sign(p.y);
}

float sdf_2d_star(vector2 p0, float r, int n, float m)
{
  vector2 p = p0;
  float an = M_PI / float(n);
  float en = M_PI / m;  // m is between 2 and n
  vector2 acs = vector2(cos(an), sin(an));
  vector2 ecs = vector2(cos(en), sin(en));  // ecs=vector2(0,1) for regular polygon,

  float bn = mod(atan2(p.x, p.y), 2.0 * an) - an;
  p = length(p) * vector2(cos(bn), abs(sin(bn)));
  p -= r * acs;
  p += ecs * clamp(-dot(p, ecs), 0.0, safe_divide(r * acs.y, ecs.y));
  return length(p) * sign(p.x);
}

float sdf_3d_hex_prism(point p, point s)
{
  point q = abs(p);
  return max(q[2] - s[1], max((q[0] * 0.866025 + q[1] * 0.5), q[1]) - s[0]);
}

float sdf_3d_sphere(point p, float size)
{
  return length(p) - size;
}

float sdf_3d_box(point p, point b)
{
  point q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q[0], max(q[1], q[2])), 0.0);
}

float sdf_3d_torus(point p, vector2 t)
{
  vector2 pxz = vector2(p[0], p[2]);
  vector2 q = vector2(length(pxz) - t.x, p[1]);
  return length(q) - t.y;
}

float sdf_3d_cone(point p, float a)
{
  // c is the sin/cos of the angle
  vector2 c = vector2(sin(a), cos(a));
  vector2 pxy = vector2(p[0], p[1]);
  float q = length(pxy);
  return dot(c, vector2(q, p[2]));
}

float sdf_3d_cylinder(point p, point c)
{
  vector2 pxz = vector2(p[0], p[2]);
  vector2 cxy = vector2(c[0], c[1]);
  return length(pxz - cxy) - c[2];
}

float sdf_3d_capsule(point p, point a, point b)
{
  point pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h);
}

float sdf_3d_octahedron(point p0)
{
  point p = p0;
  p = abs(p);
  float m = p[0] + p[1] + p[2] - 1.0;
  point q;
  if (3.0 * p[0] < m) {
    q = point(p[0], p[1], p[2]);
  }
  else if (3.0 * p[1] < m) {
    q = point(p[1], p[2], p[0]);
  }
  else if (3.0 * p[2] < m) {
    q = point(p[2], p[0], p[1]);
  }
  else {
    return m * 0.57735027;
  }

  float k = clamp(0.5 * (q[2] - q[1] + 1.0), 0.0, 1.0);
  return length(point(q[0], q[1] - 1.0 + k, q[2] - k));
}

/* Utility functions. */

float sdf_alteration(float fac, float size, float rounding, float thickness)
{
  // fac -= (rounding / size);
  float f;
  if (thickness != 0.0) {
    f = abs(fac) - thickness;
  }

  return f;
}

/* Operator functions. */

void pR45(output vector2 p)
{
  p = (p + vector2(p.y, -p.x)) * sqrt(0.5);
}

float pMod1(float p0, float size)
{
  float p = p0;
  float halfsize = size * 0.5;
  float c = floor((p + halfsize) / size);
  p = mod(p + halfsize, size) - halfsize;
  return c;
}

float sdf_op_union_chamfer(float a, float b, float r)
{
  return min(min(a, b), (a - r + b) * sqrt(0.5));
}

// intersection has to deal with what is normally the inside of the resulting object
// when using union, which we normally don't care about too much. Thus, intersection
// implementations sometimes differ from union implementations.
float sdf_op_intersection_chamfer(float a, float b, float r)
{
  return max(max(a, b), (a + r + b) * sqrt(0.5));
}

// Difference can be built from intersection or Union:
float sdf_op_difference_chamfer(float a, float b, float r)
{
  return sdf_op_intersection_chamfer(a, -b, r);
}

// The "Round" variant uses a quarter-circle to join the two objects smoothly:
float sdf_op_union_round(float a, float b, float r)
{
  vector2 u = max(vector2(r - a, r - b), vector2(0.0, 0.0));
  return max(r, min(a, b)) - length(u);
}

float sdf_op_intersection_round(float a, float b, float r)
{
  vector2 u = max(vector2(r + a, r + b), vector2(0.0, 0.0));
  return min(-r, max(a, b)) + length(u);
}

float sdf_op_difference_round(float a, float b, float r)
{
  return sdf_op_intersection_round(a, -b, r);
}

// The "Columns" flavour makes n-1 circular columns at a 45 degree angle:
float sdf_op_union_columns(float a, float b, float r, float n)
{
  if ((a < r) && (b < r)) {
    vector2 p = vector2(a, b);
    float columnradius = r * sqrt(2) / ((n - 1) * 2 + sqrt(2));
    pR45(p);
    p.x -= sqrt(2) / 2 * r;
    p.x += columnradius * sqrt(2);
    if (mod(n, 2) == 1) {
      p.y += columnradius;
    }
    // At this point, we have turned 45 degrees and moved at a point on the
    // diagonal that we want to place the columns on.
    // Now, repeat the domain along this direction and place a circle.
    pMod1(p.y, columnradius * 2);
    float result = length(p) - columnradius;
    result = min(result, p.x);
    result = min(result, a);
    return min(result, b);
  }
  else {
    return min(a, b);
  }
}

float sdf_op_difference_columns(float a0, float b, float r, float n)
{
  float a = -a0;
  float m = min(a, b);
  // avoid the expensive computation where not needed (produces discontinuity though)
  if ((a < r) && (b < r)) {
    vector2 p = vector2(a, b);
    float columnradius = r * sqrt(2) / n / 2.0;
    columnradius = r * sqrt(2) / ((n - 1) * 2 + sqrt(2));

    pR45(p);
    p.y += columnradius;
    p.x -= sqrt(2) / 2 * r;
    p.x += -columnradius * sqrt(2) / 2;

    if (mod(n, 2) == 1) {
      p.y += columnradius;
    }
    pMod1(p.y, columnradius * 2);

    float result = -length(p) + columnradius;
    result = max(result, p.x);
    result = min(result, a);
    return -min(result, b);
  }
  else {
    return -m;
  }
}

float sdf_op_intersection_columns(float a, float b, float r, float n)
{
  return sdf_op_difference_columns(a, -b, r, n);
}

// The "stairs" flavour produces n-1 steps of a staircase:
// much less stupid version by paniq
float sdf_op_union_stairs(float a, float b, float r, float n)
{
  float s = r / n;
  float u = b - r;
  return min(min(a, b), 0.5 * (u + a + abs((mod(u - a + s, 2 * s)) - s)));
}

// We can just call Union since stairs are symmetric.
float sdf_op_intersection_stairs(float a, float b, float r, float n)
{
  return -sdf_op_union_stairs(-a, -b, r, n);
}

float sdf_op_difference_stairs(float a, float b, float r, float n)
{
  return -sdf_op_union_stairs(-a, b, r, n);
}

float sdf_op_union_smooth(float d1, float d2, float k)
{
  if (k != 0.0) {
    float h = max(k - abs(d1 - d2), 0.0);
    return min(d1, d2) - h * h * 0.25 / k;
  }
  else {
    return min(d1, d2);
  }
}

float sdf_op_diff_smooth(float d1, float d2, float k)
{
  if (k != 0.0) {
    float h = max(k - abs(-d1 - d2), 0.0);
    return max(-d1, d2) + h * h * 0.25 / k;
  }
  else {
    return max(-d1, d2);
  }
}

float sdf_op_intersect_smooth(float d1, float d2, float k)
{
  if (k != 0.0) {
    float h = max(k - abs(d1 - d2), 0.0);
    return max(d1, d2) + h * h * 0.25 / k;
  }
  else {
    return max(d1, d2);
  }
}

// produces a cylindical pipe that runs along the intersection.
// No objects remain, only the pipe. This is not a boolean operator.
float sdf_op_pipe(float a, float b, float r)
{
  return length(vector2(a, b)) - r;
}

// first object gets a v-shaped engraving where it intersect the second
float sdf_op_engrave(float a, float b, float r)
{
  return max(a, (a + r - abs(b)) * sqrt(0.5));
}

// first object gets a capenter-style groove cut out
float sdf_op_groove(float a, float b, float ra, float rb)
{
  return max(a, min(a + ra, rb - abs(b)));
}

// first object gets a capenter-style tongue attached
float sdf_op_tongue(float a, float b, float ra, float rb)
{
  return min(a, max(a - ra, abs(b) - rb));
}

float sdf_op_round(float a, float k)
{
  return a - k;
}

float sdf_op_blend(float a, float b, float k)
{
  return mix(a, b, k);
}

float sdf_op_onion(float a, float k, int n)
{
  if (n > 0) {
    float r = k;
    float onion = abs(a) - r;
    for (int i = 0; i < n; i++) {
      r *= 0.5;
      onion = abs(onion) - r;
    }
    return onion;
  }
  else {
    return abs(a) - k;
  }
}

float sdf_op_union(float a, float b)
{
  return min(a, b);
}

float sdf_op_intersection(float a, float b)
{
  return max(a, b);
}

float sdf_op_difference(float a, float b)
{
  return max(a, -b);
}
