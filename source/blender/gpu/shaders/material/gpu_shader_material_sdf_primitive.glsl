#include "gpu_shader_material_sdf_util.glsl"

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

// "Generalized Distance Functions" by Akleman and Chen.
// see the Paper at
// https://www.viz.tamu.edu/faculty/ergun/research/implicitmodeling/papers/sm99.pdf
//
// This set of constants is used to construct a large variety of geometric primitives.
// Indices are shifted by 1 compared to the paper because we start counting at Zero.
// Some of those are slow whenever a driver decides to not unroll the loop,
// which seems to happen for fIcosahedron und fTruncatedIcosahedron on nvidia 350.12f at least.
// Specialized implementations can well be faster in all cases.
//

constexpr float3 GDFVectors[19] = float3_array(normalize(float3(1.0f, 0.0f, 0.0f)),
                                          normalize(float3(0.0f, 1.0f, 0.0f)),
                                          normalize(float3(0.0f, 0.0f, 1.0f)),

                                          normalize(float3(1.0f, 1.0f, 1.0f)),
                                          normalize(float3(-1.0f, 1.0f, 1.0f)),
                                          normalize(float3(1.0f, -1.0f, 1.0f)),
                                          normalize(float3(1.0f, 1.0f, -1.0f)),

                                          normalize(float3(0.0f, 1.0f, M_PHI + 1.0f)),
                                          normalize(float3(0.0f, -1.0f, M_PHI + 1.0f)),
                                          normalize(float3(M_PHI + 1.0f, 0.0f, 1.0f)),
                                          normalize(float3(-M_PHI - 1.0f, 0.0f, 1.0f)),
                                          normalize(float3(1.0f, M_PHI + 1.0f, 0.0f)),
                                          normalize(float3(-1.0f, M_PHI + 1.0f, 0.0f)),

                                          normalize(float3(0.0f, M_PHI, 1.0f)),
                                          normalize(float3(0.0f, -M_PHI, 1.0f)),
                                          normalize(float3(1.0f, 0.0f, M_PHI)),
                                          normalize(float3(-1.0f, 0.0f, M_PHI)),
                                          normalize(float3(M_PHI, 1.0f, 0.0f)),
                                          normalize(float3(-M_PHI, 1.0f, 0.0f)));

// Version with variable exponent.
// This is slow and does not produce correct distances, but allows for bulging of objects.
float sdf_3d_gdf_exp(float3 p, float r, float e, int begin, int end)
{
  begin = clamp(begin, 0, 18);
  end = clamp(end, 0, 18);

  float d = 0;
  for (int i = begin; i <= end; ++i) {
    d += pow(abs(dot(p, GDFVectors[i])), e);
  }
  return pow(d, 1 / e) - r;
}

// Version with without exponent, creates objects with sharp edges and flat faces
float sdf_3d_gdf(float3 p, float r, int begin, int end)
{
  begin = clamp(begin, 0, 18);
  end = clamp(end, 0, 18);

  float d = 0;
  for (int i = begin; i <= end; ++i) {
    d = max(d, abs(dot(p, GDFVectors[i])));
  }
  return d - r;
}

float sdf_2d_line(float2 p, float2 a, float2 b)
{
  float2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0f, 1.0f);
  return length(pa - ba * h);
}

float sdf_2d_rectangle(float2 p, float w, float h)
{
  float2 b = float2(w, h);
  float2 d = abs(p) - b * 0.5f;
  return length(max(d, float2(0.0f))) + min(max(d.x, d.y), 0.0f);
}

float sdf_2d_rhombus(float2 p, float2 w)
{
  float2 b = w * 0.5f;

  if (length(b) == 0.0f) {
    return length(p);
  }

  float2 q = abs(p);

  float h = clamp(safe_divide((-2.0f * ndot(q, b) + ndot(b, b)), dot(b, b)), -1.0f, 1.0f);
  float d = length(q - 0.5f * b * float2(1.0f - h, 1.0f + h));
  d *= sgn(q.x * b.y + q.y * b.x - b.x * b.y);

  return d;
}

float sdf_2d_isosceles(float2 p, float2 q)
{
  if (length(q) == 0.0f) {
    return length(p);
  }
  p.x = abs(p.x);
  q.y = -q.y;
  p.y += q.y * 0.5f;
  q.x *= 0.5f;
  float2 a = p - q * clamp(dot(p, q) / dot(q, q), 0.0f, 1.0f);
  float2 b = p - q * float2(clamp(p.x / q.x, 0.0f, 1.0f), 1.0f);
  float s = -sign(q.y);
  float2 d = min(float2(dot(a, a), s * (p.x * q.y - p.y * q.x)), float2(dot(b, b), s * (p.y - q.y)));
  return -sdf_safe_sqrt(d.x) * sign(d.y);
}

float sdf_2d_hexagon(float2 p, float r)
{
  constexpr float3 k = float3(-M_SQRT3_2, 0.5f, M_SQRT1_3);
  p = abs(p);
  p -= 2.0f * min(dot(k.xy, p), 0.0f) * k.xy;
  p -= float2(clamp(p.x, -k.z * r, k.z * r), r);
  return length(p) * sign(p.y);
}

float sdf_2d_triangle(float2 p, float r)
{
  constexpr float k = M_SQRT3;
  p.x = abs(p.x) - r;
  p.y = p.y + r / k;
  if (p.x + k * p.y > 0.0f) {
    p = float2(p.x - k * p.y, -k * p.x - p.y) / 2.0f;
  }
  p.x -= clamp(p.x, -2.0f * r, 0.0f);
  return -length(p) * sign(p.y);
}

float sdf_2d_trapezoid(float2 p, float w2, float h, float w1)
{
  float r2 = w2 * 0.5f;
  float r1 = w1 * 0.5f;
  float he = h * 0.5f;
  float2 k1 = float2(r2, he);
  float2 k2 = float2(r2 - r1, 2.0f * he);
  p.x = abs(p.x);
  float2 ca = float2(p.x - min(p.x, (p.y < 0.0f) ? r1 : r2), abs(p.y) - he);
  float2 cb = p - k1 + k2 * clamp(dot(k1 - p, k2) / dot2(k2), 0.0f, 1.0f);
  float s = ((cb.x < 0.0f) && (ca.y < 0.0f)) ? -1.0f : 1.0f;
  return s * sdf_safe_sqrt(min(dot2(ca), dot2(cb)));
}

float sdf_2d_rounded_x(float2 p, float w)
{
  p = abs(p);
  return length(p - min(p.x + p.y, w) * 0.5f);
}

float sdf_2d_blobby_cross(float2 pos, float he)
{
  if (abs(he) < 0.001f) {
    he = 0.001f;
  }
  pos = abs(pos);
  pos = float2(abs(pos.x - pos.y), 1.0f - pos.x - pos.y) / M_SQRT2;

  float p = (he - pos.y - 0.25f / he) / (6.0f * he);
  float q = pos.x / (he * he * 16.0f);
  float h = q * q - p * p * p;

  float x;
  if (h > 0.0f) {
    float r = sdf_safe_sqrt(h);
    x = pow(q + r, 1.0f / 3.0f) - pow(abs(q - r), 1.0f / 3.0f) * sign(r - q);
  }
  else {
    float r = sdf_safe_sqrt(p);
    x = 2.0f * r * cos(acos(safe_divide(q, (p * r))) / 3.0f);
  }
  x = min(x, M_SQRT2 / 2.0f);

  float2 z = float2(x, he * (1.0f - 2.0f * x * x)) - pos;
  return length(z) * sign(z.y);
}

float sdf_2d_uneven_capsule(float2 p, float2 pa, float2 pb, float ra, float rb)
{
  p -= pa;
  pb -= pa;
  float h = dot(pb, pb);
  float2 q = safe_divide(float2(dot(p, float2(pb.y, -pb.x)), dot(p, pb)), h);

  q.x = abs(q.x);

  float b = ra - rb;
  float2 c = float2(sdf_safe_sqrt(h - b * b), b);

  float k = cross2(c, q);
  float m = dot(c, q);
  float n = dot(q, q);

  if (k < 0.0f) {
    return sdf_safe_sqrt(h * (n)) - ra;
  }
  else if (k > c.x) {
    return sdf_safe_sqrt(h * (n + 1.0f - 2.0f * q.y)) - rb;
  }
  return m - ra;
}

float sdf_2d_parabola(float2 pos, float k)
{
  float d = pos.y;

  if (k != 0.0f) {
    pos.x = abs(pos.x);

    float p = (1.0f - 2.0f * k * pos.y) / (6.0f * k * k);
    float q = -abs(pos.x) / (4.0f * k * k);

    float h = q * q + p * p * p;
    float r = sdf_safe_sqrt(abs(h));

    float x = (h > 0.0f) ? pow(-q + r, 1.0f / 3.0f) - pow(abs(-q - r), 1.0f / 3.0f) * sgn(q + r) :
                          2.0f * cos(atan(r, -q) / 3.0f) * sdf_safe_sqrt(-p);

    d = length(pos - float2(x, k * x * x)) * sgn(pos.x - x);
  }

  return d;
}

float sdf_2d_parabola_segment(float2 pos, float wi, float he)
{
  wi *= 0.5f;
  pos.x = abs(pos.x);

  float ik = wi * wi / he;
  float p = ik * (he - pos.y - 0.5f * ik) / 3.0f;
  float q = pos.x * ik * ik * 0.25f;
  float h = q * q - p * p * p;

  float x;
  if (h > 0.0f)  // 1 root
  {
    float r = sdf_safe_sqrt(h);
    x = pow(q + r, 1.0f / 3.0f) - pow(abs(q - r), 1.0f / 3.0f) * sign(r - q);
  }
  else  // 3 roots
  {
    float r = sdf_safe_sqrt(p);
    x = 2.0f * r * cos(acos(q / (p * r)) / 3.0f);
  }

  x = min(x, wi);

  float d = length(pos - float2(x, he - x * x / ik)) * sign(ik * (pos.y - he) + pos.x * pos.x);

  return d;
}

float sdf_2d_bezier(float2 pos, float2 A, float2 C, float2 B)
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
    h = sdf_safe_sqrt(h);
    float2 x = (float2(h, -h) - q) / 2.0f;
    float2 uv = sign(x) * pow(abs(x), float2(1.0f / 3.0f));
    float t = clamp(uv.x + uv.y - kx, 0.0f, 1.0f);
    res = dot2(d + (c + b * t) * t);
  }
  else {
    float z = sdf_safe_sqrt(-p);
    float v = acos(q / (p * z * 2.0f)) / 3.0f;
    float m = cos(v);
    float n = sin(v) * M_SQRT3;
    float3 t = clamp(float3(m + m, -n - m, n - m) * z - kx, 0.0f, 1.0f);
    res = min(dot2(d + (c + b * t.x) * t.x), dot2(d + (c + b * t.y) * t.y));
  }
  return sdf_safe_sqrt(res);
}

float sdf_2d_ellipse(float2 p, float2 ab)
{
  ab *= 0.5f;
  if (ab.x == ab.y) {
    return length(p) - ab.x;
  }
  else if (ab.x == 0.0f || ab.y == 0.0f) {
    return sdf_2d_rectangle(p, ab.x, ab.y);
  }
  p = abs(p);
  if (p.x > p.y) {
    p = p.yx;
    ab = ab.yx;
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
    float h = acos(q / c3) / 3.0f;
    float s = cos(h);
    float t = sin(h) * M_SQRT3;
    float rx = sdf_safe_sqrt(-c * (s + t + 2.0f) + m2);
    float ry = sdf_safe_sqrt(-c * (s - t + 2.0f) + m2);
    co = (ry + sign(l) * rx + abs(g) / (rx * ry) - m) / 2.0f;
  }
  else {
    float h = 2.0f * m * n * sdf_safe_sqrt(d);
    float s = sign(q + h) * pow(abs(q + h), 1.0f / 3.0f);
    float u = sign(q - h) * pow(abs(q - h), 1.0f / 3.0f);
    float rx = -s - u - c * 4.0f + 2.0f * m2;
    float ry = (s - u) * M_SQRT3;
    float rm = sdf_safe_sqrt(rx * rx + ry * ry);
    co = (ry / sdf_safe_sqrt(rm - rx) + 2.0f * g / rm - m) / 2.0f;
  }
  float2 r = ab * float2(co, sdf_safe_sqrt(1.0f - co * co));
  return length(r - p) * sign(p.y - r.y);
}

float sdf_2d_heart(float2 p, float r)
{
  r *= 2.0f;
  p.y += r * 0.5f;

  p.x = abs(p.x);

  if (p.y + p.x > r) {
    return sdf_safe_sqrt(dot2(p - float2(0.25f, 0.75f) * r)) - M_SQRT2 / 4.0f * r;
  }
  else {
    return sdf_safe_sqrt(min(dot2(p - float2(0.0f, 1.0f) * r), dot2(p - 0.5f * max(p.x + p.y, 0.0f)))) *
           sign(p.x - p.y);
  }
}

float sdf_2d_quad(float2 p, float2 p0, float2 p1, float2 p2, float2 p3)
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

  float2 ds = min(min(float2(dot(pq0, pq0), v0.x * e0.y - v0.y * e0.x),
                    float2(dot(pq1, pq1), v1.x * e1.y - v1.y * e1.x)),
                min(float2(dot(pq2, pq2), v2.x * e2.y - v2.y * e2.x),
                    float2(dot(pq3, pq3), v3.x * e3.y - v3.y * e3.x)));

  float d = sdf_safe_sqrt(ds.x);

  return (ds.y > 0.0f) ? -d : d;
}

float sdf_2d_vesica(float2 p, float r, float d)
{
  p = abs(p);

  float b = sdf_safe_sqrt(r * r - d * d);
  if ((p.y - b) * d > p.x * b) {
    return length(float2(p.x, p.y - b)) * sign(d);
  }
  else {
    return length(float2(p.x + d, p.y)) - r;
  }
}

float sdf_2d_moon(float2 p, float d, float ra, float rb)
{
  p.y = abs(p.y);

  float a = (ra * ra - rb * rb + d * d) / (2.0f * d);
  float b = sdf_safe_sqrt(ra * ra - a * a);
  float m = d * (p.x * b - p.y * a);
  float n = d * d * max(b - p.y, 0.0f);
  if (m > n) {
    return length(p - float2(a, b));
  }
  return max((length(p) - ra), -(length(p - float2(d, 0.0f)) - rb));
}

float sdf_2d_flat_joint(float2 p, float l, float a, float lw)
{
  if (abs(a) < 0.001f) {
    a = 0.001f;
  }

  lw *= 0.5f;

  float2 sc = sincos(clamp(a * 0.5f, -M_PI, M_PI));
  float ra = safe_divide(0.5f * l, a);

  p.x -= ra;

  float2 q = p - 2.0f * sc * max(0.0f, dot(sc, p));

  float u = abs(ra) - length(q);
  float d = max(length(float2(q.x + ra - clamp(q.x + ra, -lw, lw), q.y)) * sign(-q.y), abs(u) - lw);

  return d;
}

float sdf_2d_round_joint(float2 p, float l, float a, float lw)
{
  if (abs(a) < 0.001f) {
    a = 0.001f;
  }

  lw *= 0.5f;

  float2 sc = sincos(clamp(a * 0.5f, -M_PI, M_PI));
  float ra = safe_divide(0.5f * l, a);

  p.x -= ra;

  float2 q = p - 2.0f * sc * max(0.0f, dot(sc, p));

  float u = abs(ra) - length(q);
  float d = (q.y < 0.0f) ? length(q + float2(ra, 0.0f)) : abs(u);

  return d - lw;
}

float sdf_2d_pie(float2 p, float r, float a)
{
  float2 sc = sincos(clamp(a * 0.5f, 0.0f, M_PI));
  p.x = abs(p.x);
  float l = length(p) - r;
  float m = length(p - sc * clamp(dot(p, sc), 0.0f, r));
  return max(l, m * sign(sc.y * p.x - sc.x * p.y));
}

float sdf_2d_arc(float2 p, float a, float ra)
{
  float2 sc = sincos(clamp(a * 0.5f, 0.0f, M_PI));
  p.x = abs(p.x);
  float k = (sc.y * p.x > sc.x * p.y) ? dot(p.xy, sc) : length(p.xy);
  return sdf_safe_sqrt(dot(p, p) + ra * ra - 2.0f * ra * k);
}

float sdf_2d_horseshoe(float2 p, float r, float a, float overshoot, float lw)
{
  float2 sc = sincos(clamp(a * 0.5f, 0.0f, M_PI));
  p.x = abs(p.x);
  float l = length(p);
  p = mat2(-sc.y, sc.x, sc.x, sc.y) * p;
  p = float2((p.y > 0.0f || p.x > 0.0f) ? p.x : l * sign(-sc.y), (p.x > 0.0f) ? p.y : l);
  p = float2(p.x, abs(p.y - r)) - float2(overshoot, lw) * 0.5f;
  return length(max(p, 0.0f)) + min(0.0f, max(p.x, p.y));
}

float sdf_2d_point_triangle(float2 p, float2 p0, float2 p1, float2 p2)
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
  float s = sgn(e0.x * e2.y - e0.y * e2.x);
  float2 d = min(min(float2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                   float2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
               float2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
  return -sdf_safe_sqrt(d.x) * sgn(d.y);
}

float sdf_2d_star_x(float2 p, float r, float sides, float inset)
{

  float m = sgn(inset) * 2.0f + inset * inset * (sides - 2.0f);
  float an = safe_divide(M_PI, sides);

  float en = safe_divide(M_PI, m);  // m is between 2 and n
  float2 acs = float2(cos(an), sin(an));
  float2 ecs = float2(cos(en), sin(en));  // ecs=float2(0,1) for regular polygon,

  float bn = sdf_safe_mod(atan(p.x, p.y), 2.0f * an) - an;
  p = length(p) * float2(cos(bn), abs(sin(bn)));
  p -= r * acs;
  p += ecs * clamp(-dot(p, ecs), 0.0f, safe_divide(r * acs.y, ecs.y));
  return length(p) * sign(p.x);
}

float sdf_2d_star(float2 p, float r, float sides, float inset, float inradius)  // m=[2,n]
{
  /* Sanitise inputs. */
  sides = max(sides, 2.0f);

  float m;
  if (inset > 1.0f) {
    // range from edge to centre
    m = mix(2.0f, 2.0f - 2.0f / (sides + 1.0f), inset - 1.0f);
  }
  else {
    // range from centre to edge
    inset = 1.0f - inset;
    m = 2.0f + inset * inset * (sides - 2.0f);
  }

  // scale incircle to circumcircle
  float scale = 1.0f;
  if (inradius > 0.0f) {
    float scalefac = 1.0f / cos(M_PI / sides);
    if (inradius < 1.0f) {
      scale = (scalefac - 1.0f) * inradius + 1.0f;
    }
    else {
      scale = scalefac * inradius;
    }
  }

  p /= scale;

  // these 4 lines can be precomputed for a given shape
  float an = M_PI / sides;
  float en = M_PI / m;
  float2 acs = float2(cos(an), sin(an));
  float2 ecs = float2(cos(en), sin(en));  // ecs=float2(0,1) and simplify, for regular polygon,

  // reduce to first sector
  float bn = sdf_safe_mod(atan(p.x, p.y) * sign(p.x), 2.0f * an) - an;
  p = length(p) * float2(cos(bn), abs(sin(bn)));

  // line sdf
  p -= r * acs;
  p += ecs * clamp(-dot(p, ecs), 0.0f, r * acs.y / ecs.y);
  return length(p) * sign(p.x) * scale;
}

float sdf_2d_pentagon(float2 p, float r)
{
  constexpr float3 k = float3(0.809016994f, 0.587785252f, 0.726542528f);  // pi/5: cos, sin, tan
  p.y = -p.y;
  p.x = abs(p.x);
  p -= 2.0f * min(dot(float2(-k.x, k.y), p), 0.0f) * float2(-k.x, k.y);
  p -= 2.0f * min(dot(float2(k.x, k.y), p), 0.0f) * float2(k.x, k.y);
  p -= float2(clamp(p.x, -r * k.z, r * k.z), r);
  return length(p) * sign(p.y);
}

/* 3D */
float sdf_3d_hex_prism(float3 p, float2 h)
{

  h *= 0.5f;
  constexpr float3 k = float3(-M_SQRT3_2, 0.5f, M_SQRT1_3);
  p = abs(p);
  p.xy -= 2.0f * min(dot(k.xy, p.xy), 0.0f) * k.xy;
  float2 d = float2(length(p.xy - float2(clamp(p.x, -k.z * h.x, k.z * h.x), h.x)) * sign(p.y - h.x),
                p.z - h.y);
  return min(max(d.x, d.y), 0.0f) + length(max(d, 0.0f));
}

float sdf_3d_hex_prism_incircle(float3 p, float2 h)
{
  return sdf_3d_hex_prism(p, float2(h.x * M_SQRT3 * 0.5f, h.y));
}

float sdf_3d_box(float3 p, float3 b)
{
  b *= 0.5f;
  float3 q = abs(p) - b;
  return length(max(q, 0.0f)) + min(max(q.x, max(q.y, q.z)), 0.0f);
}

float sdf_3d_torus(float3 p, float2 t)
{
  // t.x *= 0.5f;
  float2 q = float2(length(p.xy) - t.x, p.z);
  return length(q) - t.y;
}

float sdf_3d_circle(float3 p, float r)
{
  float l = length(p.xy) - r;
  return length(float2(p.z, l));
}

// A circular disc with no thickness (i.e. a cylinder with no height).
// Subtract some value to make a flat disc with rounded edge.
float sdf_3d_disc(float3 p, float r)
{
  float l = length(p.xy) - r;
  return l < 0 ? abs(p.z) : length(float2(p.z, l));
}

// Endless "corner"
float sdf_2d_corner(float2 p)
{
  return length(max(p, float2(0.0f))) + vmax(min(p, float2(0.0f)));
}

float sdf_3d_upright_cone(float3 p, float radius, float height, float offset)
{
  /* Sanitise inputs. */
  p.z -= offset * sgn(height);

  if (length(float2(radius, height)) == 0.0f) {
    return length(p);
  }

  if (height < 0.0f) {
    height = abs(height);
    p.z = -p.z;
  }

  radius = abs(radius);

  /* Cone. */
  float2 q = float2(length(p.xy), p.z);
  float2 tip = q - float2(0.0f, height);
  float2 mantleDir = normalize(float2(height, radius));
  float mantle = dot(tip, mantleDir);
  float d = max(mantle, -q.y);
  float projected = dot(tip, float2(mantleDir.y, -mantleDir.x));

  // distance to tip
  if ((q.y > height) && (projected < 0.0f)) {
    d = max(d, length(tip));
  }

  // distance to base ring
  if ((q.x > radius) && (projected > length(float2(height, radius)))) {
    d = max(d, length(q - float2(radius, 0.0f)));
  }
  return d;
}

float sdf_3d_cone(float3 p, float a)
{
  p *= 0.5f;
  float2 sc = sincos(clamp(a * 0.5f, 0.0f, M_PI));
  float q = length(p.xy);
  return dot(sc, float2(q, p.z));
}

float sdf_3d_point_cone(float3 p, float3 a, float3 b, float ra, float rb)
{
  float rba = rb - ra;
  float baba = dot(b - a, b - a);
  float papa = dot(p - a, p - a);
  float paba = safe_divide(dot(p - a, b - a), baba);

  float x = sdf_safe_sqrt(papa - paba * paba * baba);

  float cax = max(0.0f, x - ((paba < 0.5f) ? ra : rb));
  float cay = abs(paba - 0.5f) - 0.5f;

  float k = rba * rba + baba;
  float f = clamp(safe_divide((rba * (x - ra) + paba * baba), k), 0.0f, 1.0f);

  float cbx = x - ra - f * rba;
  float cby = paba - f;

  float s = (cbx < 0.0f && cay < 0.0f) ? -1.0f : 1.0f;

  return s * sdf_safe_sqrt(min(cax * cax + cay * cay * baba, cbx * cbx + cby * cby * baba));
}

float sdf_3d_capsule(float3 p, float3 a, float3 b, float r)
{
  float3 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0f, 1.0f);
  return length(pa - ba * h) - r;
}

float sdf_3d_cylinder(float3 p, float3 c)
{
  return length(p.xz - c.xy) - c.z;
}

float sdf_capped_cylinder(float3 p, float r, float h)
{
  float2 d = abs(float2(length(p.xy), p.z)) - float2(r, h * 0.5f);
  return min(max(d.x, d.y), 0.0f) + length(max(d, 0.0f));
}

float sdf_3d_cylinder(float3 p, float3 a, float3 b, float r)
{
  float3 pa = p - a;
  float3 ba = b - a;
  float baba = dot(ba, ba);
  float paba = dot(pa, ba);

  float x = length(pa * baba - ba * paba) - r * baba;
  float y = abs(paba - baba * 0.5f) - baba * 0.5f;
  float x2 = x * x;
  float y2 = y * y * baba;
  float d = (max(x, y) < 0.0f) ? -min(x2, y2) : (((x > 0.0f) ? x2 : 0.0f) + ((y > 0.0f) ? y2 : 0.0f));
  return sign(d) * sdf_safe_sqrt(abs(d)) / baba;
}

float sdf_3d_solid_angle(float3 p, float a, float ra)
{
  float2 sc = sincos(clamp(a * 0.5f, -M_PI, M_PI));
  float2 q = float2(length(p.xy), p.z);
  float l = length(q) - ra;
  float m = length(q - sc * clamp(dot(q, sc), 0.0f, ra));
  return max(l, m * sign(sc.y * q.x - sc.x * q.y));
}

float sdf_3d_pyramid(float3 p, float w, float h)
{
  p = p.xzy; /* z-up */

  p /= w;
  h /= w;

  float m2 = h * h + 0.25f;

  /* symmetry */
  p.xz = abs(p.xz);
  p.xz = (p.z > p.x) ? p.zx : p.xz;
  p.xz -= 0.5f;

  /* project into face plane (2D) */
  float3 q = float3(p.z, h * p.y - 0.5f * p.x, h * p.x + 0.5f * p.y);

  float s = max(-q.x, 0.0f);
  float t = clamp((q.y - 0.5f * p.z) / (m2 + 0.25f), 0.0f, 1.0f);

  float a = m2 * (q.x + s) * (q.x + s) + q.y * q.y;
  float b = m2 * (q.x + 0.5f * t) * (q.x + 0.5f * t) + (q.y - m2 * t) * (q.y - m2 * t);

  float d2 = min(q.y, -q.x * m2 - q.y * 0.5f) > 0.0f ? 0.0f : min(a, b);

  /* recover 3D and scale, and add sign */
  float d = sdf_safe_sqrt((d2 + q.z * q.z) / m2) * sign(max(q.z, -p.y));

  return d * w;
}

float sdf_3d_octahedron(float3 p, float s)
{
  p = abs(p);
  float m = p.x + p.y + p.z - s;

  float3 q;
  if (3.0f * p.x < m) {
    q = p.xyz;
  }
  else if (3.0f * p.y < m) {
    q = p.yzx;
  }
  else if (3.0f * p.z < m) {
    q = p.zxy;
  }
  else {
    return m * M_SQRT1_3;
  }
  float k = clamp(0.5f * (q.z - q.y + s), 0.0f, s);
  return length(float3(q.x, q.y - s + k, q.z - k));
}

float sdf_alteration(float size, float dist, float lw, float invert)
{
  if (lw != 0.0f) {
    dist = abs(dist) - lw * 0.5f;
  }

  dist *= size;

  return (invert > 0.0f) ? -dist : dist;
}

float sdf_dimension(float w, inout float roundness)
{
  float sw = sign(w);
  w = abs(w);
  roundness = mix(0.0f, w, clamp(roundness, 0.0f, 1.0f));
  float dim = max(w - roundness, 0.0f);
  roundness *= 0.5f;
  return dim * sw;
}

float2 sdf_dimension(float w, float d, inout float roundness)
{
  float sw = sign(w);
  float sd = sign(d);
  w = abs(w);
  d = abs(d);
  roundness = mix(0.0f, min(w, d), clamp(roundness, 0.0f, 1.0f));
  float2 dim = float2(max(w - roundness, 0.0f), max(d - roundness, 0.0f));
  roundness *= 0.5f;
  return dim * float2(sw, sd);
}

float3 sdf_dimension(float w, float d, float h, inout float roundness)
{
  float sw = sign(w);
  float sd = sign(d);
  float sh = sign(h);
  w = abs(w);
  d = abs(d);
  h = abs(h);
  roundness = mix(0.0f, min(w, min(d, h)), clamp(roundness, 0.0f, 1.0f));
  float3 dim = float3(max(w - roundness, 0.0f), max(d - roundness, 0.0f), max(h - roundness, 0.0f));
  roundness *= 0.5f;
  return dim * float3(sw, sd, sh);
}

/* 3D Node Functions. */

void node_sdf_prim_3d_sphere(float3 co,
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
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{
  if (size != 0.0f) {
    co /= size;
    dist = length(co) - radius;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_hex_prism(float3 co,
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
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{
  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(v1, v3, roundness);
    dist = sdf_3d_hex_prism(co, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_hex_prism_incircle(float3 co,
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
                                         float a1,
                                         float roundness,
                                         float lw,
                                         float invert,
                                         out float dist)
{
  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(v1, v3, roundness);
    dist = sdf_3d_hex_prism_incircle(co, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_box(float3 co,
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
                          float a1,
                          float roundness,
                          float lw,
                          float invert,
                          out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float3 dim = sdf_dimension(v1, v2, v3, roundness);
    dist = sdf_3d_box(co, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_torus(float3 co,
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
                            float a1,
                            float roundness,
                            float lw,
                            float invert,
                            out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_3d_torus(co, float2(radius, v1));
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_point_cone(float3 co,
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
                                 float a1,
                                 float roundness,
                                 float lw,
                                 float invert,
                                 out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(radius, v1, roundness);
    dist = sdf_3d_point_cone(co, p1, p2, dim.x, dim.y) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_cone(float3 co,
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
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(radius, v3, roundness);
    dist = sdf_3d_upright_cone(co, dim.x, dim.y, roundness) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_cylinder(float3 co,
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
                               float a1,
                               float roundness,
                               float lw,
                               float invert,
                               out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(radius, v3, roundness);
    dist = sdf_capped_cylinder(co, dim.x, dim.y - roundness * 2.0f) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_point_cylinder(float3 co,
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
                                     float a1,
                                     float roundness,
                                     float lw,
                                     float invert,
                                     out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_3d_cylinder(co, p1, p2, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_capsule(float3 co,
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
                              float a1,
                              float roundness,
                              float lw,
                              float invert,
                              out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_3d_capsule(co, p1, p2, radius);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_octahedron(float3 co,
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
                                 float a1,
                                 float roundness,
                                 float lw,
                                 float invert,
                                 out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_3d_octahedron(co, dim) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_solid_angle(float3 co,
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
                                  float a1,
                                  float roundness,
                                  float lw,
                                  float invert,
                                  out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_3d_solid_angle(co, a1, dim) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_pyramid(float3 co,
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
                              float a1,
                              float roundness,
                              float lw,
                              float invert,
                              out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(v1, v3, roundness);
    if (dim.y == 0.0f) {
      dist = sdf_3d_box(co, float3(dim.x, dim.x, 0.0f)) - roundness * 2.0f;
    }
    else if (dim.x == 0.0f) {
      co.z -= dim.y * 0.5f;
      dist = sdf_3d_box(co, float3(0.0f, 0.0f, dim.y)) - roundness * 2.0f;
    }
    else {
      dist = sdf_3d_pyramid(co, dim.x, dim.y) - roundness * 2.0f;
    }

    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_plane(float3 co,
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
                            float a1,
                            float roundness,
                            float lw,
                            float invert,
                            out float dist)
{

  if ((size != 0.0f) && (length(p1) != 0.0f)) {
    co /= size;
    dist = dot(co, normalize(p1)) + v1;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

/* 2D Node Functions. */

void node_sdf_prim_2d_circle(float3 co,
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
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = length(co.xy) - radius;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_line(float3 co,
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
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_2d_line(co.xy, p1.xy, p2.xy);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_corner(float3 co,
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
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_2d_corner(co.xy);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_rectangle(float3 co,
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
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(v1, v2, roundness);
    dist = sdf_2d_rectangle(co.xy, dim.x, dim.y) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_rhombus(float3 co,
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
                              float a1,
                              float roundness,
                              float lw,
                              float invert,
                              out float dist)
{

  if (size != 0.0f) {
    co /= size;
    roundness = min(roundness, 0.9999f);
    float2 dim = sdf_dimension(v1, v2, roundness);
    dist = sdf_2d_rhombus(co.xy, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_star(float3 co,
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
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_star(co.xy, dim, v1, v2, v3) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_triangle(float3 co,
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
                               float a1,
                               float roundness,
                               float lw,
                               float invert,
                               out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_triangle(co.xy, dim) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_hexagon(float3 co,
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
                              float a1,
                              float roundness,
                              float lw,
                              float invert,
                              out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_hexagon(co.xy, dim) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_pentagon(float3 co,
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
                               float a1,
                               float roundness,
                               float lw,
                               float invert,
                               out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_pentagon(co.xy, dim) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_pie(float3 co,
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
                          float a1,
                          float roundness,
                          float lw,
                          float invert,
                          out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_pie(co.xy, dim, a1) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_arc(float3 co,
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
                          float a1,
                          float roundness,
                          float lw,
                          float invert,
                          out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_arc(co.xy, a1, dim) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_bezier(float3 co,
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
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_2d_bezier(co.xy, p1.xy, p2.xy, p3.xy);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_uneven_capsule(float3 co,
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
                                     float a1,
                                     float roundness,
                                     float lw,
                                     float invert,
                                     out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(radius, v1, roundness);
    dist = sdf_2d_uneven_capsule(co.xy, p1.xy, p2.xy, dim.x, dim.y);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_point_triangle(float3 co,
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
                                     float a1,
                                     float roundness,
                                     float lw,
                                     float invert,
                                     out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_2d_point_triangle(co.xy, p1.xy, p2.xy, p3.xy);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_trapezoid(float3 co,
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
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float3 dim = sdf_dimension(v1, v2, v3, roundness);
    dist = sdf_2d_trapezoid(co.xy, dim.x, dim.y, dim.z) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_moon(float3 co,
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
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(radius, v1, roundness);
    dist = sdf_2d_moon(co.xy, v2, dim.x, dim.y) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_vesica(float3 co,
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
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(radius, v1, roundness);
    dist = sdf_2d_vesica(co.xy, dim.x, dim.y) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_rounded_x(float3 co,
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
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_rounded_x(co.xy, dim) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_cross(float3 co,
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
                            float a1,
                            float roundness,
                            float lw,
                            float invert,
                            out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_blobby_cross(co.xy, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_heart(float3 co,
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
                            float a1,
                            float roundness,
                            float lw,
                            float invert,
                            out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_heart(co.xy, dim) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_horseshoe(float3 co,
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
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(radius, v1, roundness);
    dist = sdf_2d_horseshoe(co.xy, dim.x, a1, dim.y, lw) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_parabola(float3 co,
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
                               float a1,
                               float roundness,
                               float lw,
                               float invert,
                               out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_2d_parabola(co.xy, v1);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_parabola_segment(float3 co,
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
                                       float a1,
                                       float roundness,
                                       float lw,
                                       float invert,
                                       out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_2d_parabola_segment(co.xy, v1, v2);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_ellipse(float3 co,
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
                              float a1,
                              float roundness,
                              float lw,
                              float invert,
                              out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_2d_ellipse(co.xy, float2(v1, v2));
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_isosceles(float3 co,
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
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float2 dim = sdf_dimension(v1, v3, roundness);
    dist = sdf_2d_isosceles(co.xy, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_round_joint(float3 co,
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
                                  float a1,
                                  float roundness,
                                  float lw,
                                  float invert,
                                  out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_2d_round_joint(co.xy, v3, a1, lw);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_flat_joint(float3 co,
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
                                 float a1,
                                 float roundness,
                                 float lw,
                                 float invert,
                                 out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_2d_flat_joint(co.xy, v3, a1, lw);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_quad(float3 co,
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
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0f) {
    co /= size;
    dist = sdf_2d_quad(co.xy, p1.xy, p2.xy, p3.xy, p4.xy);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_disc(float3 co,
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
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float r = sdf_dimension(radius, roundness);
    dist = sdf_3d_disc(co, r) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_circle(float3 co,
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
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{

  if (size != 0.0f) {
    co /= size;
    float r = sdf_dimension(radius, roundness);
    dist = sdf_3d_circle(co, r) - roundness * 2.0f;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

/* Test. */

float sdCrescent(float2 p, float d, float r0, float r1)
{

  float sign0 = sign(r0);
  float sign1 = sign(r1);
  r0 = abs(r0);
  r1 = abs(r1);
  float a = (r0 * r0 - r1 * r1 + d * d) / (2.0f * d);

  if (a < r0) {
    p.y = abs(p.y);
    float b = sdf_safe_sqrt(r0 * r0 - a * a);
    float k = p.y * a - p.x * b;
    float h = min(sign0 * (d * (p.y - b) - k), sign1 * k);
    if (h > 0.0f) {
      return length(p - float2(a, b));
    }
  }

  return max(sign0 * (length(p) - r0), sign1 * (length(p - float2(d, 0.0f)) - r1));
}

/* TEst */

void node_sdf_test(float3 co,
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
                   float a1,
                   float roundness,
                   float lw,
                   float invert,
                   out float dist)
{

  if (size != 0.0f) {
    co /= size;
    int n = int(v4);
    if (n == 0) {
      float2 dim = sdf_dimension(radius, v1, roundness);
      dist = sdCrescent(co.xy, v2, dim.x, dim.y) - roundness * 2.0f;
    }
    else if (n == 4) {
      float r = sdf_dimension(radius, roundness);
      dist = sdf_3d_gdf(co, r, int(v2), int(v3)) - roundness * 2.0f;
    }
    else if (n == 5) {
      float r = sdf_dimension(radius, roundness);
      dist = sdf_3d_gdf_exp(co, r, v1, int(v2), int(v3)) - roundness * 2.0f;
    }

    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}
