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

// "Generalized Distance Functions" by Akleman and Chen.
// see the Paper at
// https://www.viz.tamu.edu/faculty/ergun/research/implicitmodeling/papers/sm99.pdf
//
// This set of constants is used to construct a large variety of geometric primitives.
// Indices are shifted by 1 compared to the paper because we start counting at Zero.
// Some of those are slow whenever a driver decides to not unroll the loop,
// which seems to happen for fIcosahedron und fTruncatedIcosahedron on nvidia 350.12 at least.
// Specialized implementations can well be faster in all cases.
//

const vec3 GDFVectors[19] = vec3[](normalize(vec3(1.0, 0.0, 0.0)),
                                   normalize(vec3(0.0, 1.0, 0.0)),
                                   normalize(vec3(0.0, 0.0, 1.0)),

                                   normalize(vec3(1.0, 1.0, 1.0)),
                                   normalize(vec3(-1.0, 1.0, 1.0)),
                                   normalize(vec3(1.0, -1.0, 1.0)),
                                   normalize(vec3(1.0, 1.0, -1.0)),

                                   normalize(vec3(0.0, 1.0, M_PHI + 1.0)),
                                   normalize(vec3(0.0, -1.0, M_PHI + 1.0)),
                                   normalize(vec3(M_PHI + 1.0, 0.0, 1.0)),
                                   normalize(vec3(-M_PHI - 1.0, 0.0, 1.0)),
                                   normalize(vec3(1.0, M_PHI + 1.0, 0.0)),
                                   normalize(vec3(-1.0, M_PHI + 1.0, 0.0)),

                                   normalize(vec3(0.0, M_PHI, 1.0)),
                                   normalize(vec3(0.0, -M_PHI, 1.0)),
                                   normalize(vec3(1.0, 0.0, M_PHI)),
                                   normalize(vec3(-1.0, 0.0, M_PHI)),
                                   normalize(vec3(M_PHI, 1.0, 0.0)),
                                   normalize(vec3(-M_PHI, 1.0, 0.0)));

// Version with variable exponent.
// This is slow and does not produce correct distances, but allows for bulging of objects.
float sdf_3d_gdf_exp(vec3 p, float r, float e, int begin, int end)
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
float sdf_3d_gdf(vec3 p, float r, int begin, int end)
{
  begin = clamp(begin, 0, 18);
  end = clamp(end, 0, 18);

  float d = 0;
  for (int i = begin; i <= end; ++i) {
    d = max(d, abs(dot(p, GDFVectors[i])));
  }
  return d - r;
}

float sdf_2d_line(vec2 p, vec2 a, vec2 b)
{
  vec2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h);
}

float sdf_2d_rectangle(vec2 p, float w, float h)
{
  vec2 b = vec2(w, h);
  vec2 d = abs(p) - b * 0.5;
  return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}

float sdf_2d_rhombus(vec2 p, vec2 w)
{
  vec2 b = w * 0.5;

  if (length(b) == 0.0) {
    return length(p);
  }

  vec2 q = abs(p);

  float h = clamp(safe_divide((-2.0 * ndot(q, b) + ndot(b, b)), dot(b, b)), -1.0, 1.0);
  float d = length(q - 0.5 * b * vec2(1.0 - h, 1.0 + h));
  d *= sgn(q.x * b.y + q.y * b.x - b.x * b.y);

  return d;
}

float sdf_2d_isosceles(vec2 p, vec2 q)
{
  if (length(q) == 0.0) {
    return length(p);
  }
  p.x = abs(p.x);
  q.y = -q.y;
  p.y += q.y * 0.5;
  q.x *= 0.5;
  vec2 a = p - q * clamp(dot(p, q) / dot(q, q), 0.0, 1.0);
  vec2 b = p - q * vec2(clamp(p.x / q.x, 0.0, 1.0), 1.0);
  float s = -sign(q.y);
  vec2 d = min(vec2(dot(a, a), s * (p.x * q.y - p.y * q.x)), vec2(dot(b, b), s * (p.y - q.y)));
  return -safe_sqrt(d.x) * sign(d.y);
}

float sdf_2d_hexagon(vec2 p, float r)
{
  const vec3 k = vec3(-M_SQRT3_2, 0.5, M_SQRT1_3);
  p = abs(p);
  p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
  p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
  return length(p) * sign(p.y);
}

float sdf_2d_triangle(vec2 p, float r)
{
  const float k = M_SQRT3;
  p.x = abs(p.x) - r;
  p.y = p.y + r / k;
  if (p.x + k * p.y > 0.0) {
    p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
  }
  p.x -= clamp(p.x, -2.0 * r, 0.0);
  return -length(p) * sign(p.y);
}

float sdf_2d_trapezoid(vec2 p, float w2, float h, float w1)
{
  float r2 = w2 * 0.5;
  float r1 = w1 * 0.5;
  float he = h * 0.5;
  vec2 k1 = vec2(r2, he);
  vec2 k2 = vec2(r2 - r1, 2.0 * he);
  p.x = abs(p.x);
  vec2 ca = vec2(p.x - min(p.x, (p.y < 0.0) ? r1 : r2), abs(p.y) - he);
  vec2 cb = p - k1 + k2 * clamp(dot(k1 - p, k2) / dot2(k2), 0.0, 1.0);
  float s = ((cb.x < 0.0) && (ca.y < 0.0)) ? -1.0 : 1.0;
  return s * safe_sqrt(min(dot2(ca), dot2(cb)));
}

float sdf_2d_rounded_x(vec2 p, float w)
{
  p = abs(p);
  return length(p - min(p.x + p.y, w) * 0.5);
}

float sdf_2d_blobby_cross(vec2 pos, float he)
{
  if (abs(he) < 0.001) {
    he = 0.001;
  }
  pos = abs(pos);
  pos = vec2(abs(pos.x - pos.y), 1.0 - pos.x - pos.y) / M_SQRT2;

  float p = (he - pos.y - 0.25 / he) / (6.0 * he);
  float q = pos.x / (he * he * 16.0);
  float h = q * q - p * p * p;

  float x;
  if (h > 0.0) {
    float r = safe_sqrt(h);
    x = pow(q + r, 1.0 / 3.0) - pow(abs(q - r), 1.0 / 3.0) * sign(r - q);
  }
  else {
    float r = safe_sqrt(p);
    x = 2.0 * r * cos(acos(safe_divide(q, (p * r))) / 3.0);
  }
  x = min(x, M_SQRT2 / 2.0);

  vec2 z = vec2(x, he * (1.0 - 2.0 * x * x)) - pos;
  return length(z) * sign(z.y);
}

float sdf_2d_uneven_capsule(vec2 p, vec2 pa, vec2 pb, float ra, float rb)
{
  p -= pa;
  pb -= pa;
  float h = dot(pb, pb);
  vec2 q = safe_divide(vec2(dot(p, vec2(pb.y, -pb.x)), dot(p, pb)), h);

  q.x = abs(q.x);

  float b = ra - rb;
  vec2 c = vec2(safe_sqrt(h - b * b), b);

  float k = cross2(c, q);
  float m = dot(c, q);
  float n = dot(q, q);

  if (k < 0.0) {
    return safe_sqrt(h * (n)) - ra;
  }
  else if (k > c.x) {
    return safe_sqrt(h * (n + 1.0 - 2.0 * q.y)) - rb;
  }
  return m - ra;
}

float sdf_2d_parabola(vec2 pos, float k)
{
  float d = pos.y;

  if (k != 0.0) {
    pos.x = abs(pos.x);

    float p = (1.0 - 2.0 * k * pos.y) / (6.0 * k * k);
    float q = -abs(pos.x) / (4.0 * k * k);

    float h = q * q + p * p * p;
    float r = safe_sqrt(abs(h));

    float x = (h > 0.0) ? pow(-q + r, 1.0 / 3.0) - pow(abs(-q - r), 1.0 / 3.0) * sgn(q + r) :
                          2.0 * cos(atan(r, -q) / 3.0) * safe_sqrt(-p);

    d = length(pos - vec2(x, k * x * x)) * sgn(pos.x - x);
  }

  return d;
}

float sdf_2d_parabola_segment(vec2 pos, float wi, float he)
{
  wi *= 0.5;
  pos.x = abs(pos.x);

  float ik = wi * wi / he;
  float p = ik * (he - pos.y - 0.5 * ik) / 3.0;
  float q = pos.x * ik * ik * 0.25;
  float h = q * q - p * p * p;

  float x;
  if (h > 0.0)  // 1 root
  {
    float r = safe_sqrt(h);
    x = pow(q + r, 1.0 / 3.0) - pow(abs(q - r), 1.0 / 3.0) * sign(r - q);
  }
  else  // 3 roots
  {
    float r = safe_sqrt(p);
    x = 2.0 * r * cos(acos(q / (p * r)) / 3.0);
  }

  x = min(x, wi);

  float d = length(pos - vec2(x, he - x * x / ik)) * sign(ik * (pos.y - he) + pos.x * pos.x);

  return d;
}

float sdf_2d_bezier(vec2 pos, vec2 A, vec2 C, vec2 B)
{
  vec2 a = B - A;
  vec2 b = A - 2.0 * B + C;
  vec2 c = a * 2.0;
  vec2 d = A - pos;
  float kk = 1.0 / dot(b, b);
  float kx = kk * dot(a, b);
  float ky = kk * (2.0 * dot(a, a) + dot(d, b)) / 3.0;
  float kz = kk * dot(d, a);
  float res = 0.0;
  float p = ky - kx * kx;
  float p3 = p * p * p;
  float q = kx * (2.0 * kx * kx - 3.0 * ky) + kz;
  float h = q * q + 4.0 * p3;
  if (h >= 0.0) {
    h = safe_sqrt(h);
    vec2 x = (vec2(h, -h) - q) / 2.0;
    vec2 uv = sign(x) * pow(abs(x), vec2(1.0 / 3.0));
    float t = clamp(uv.x + uv.y - kx, 0.0, 1.0);
    res = dot2(d + (c + b * t) * t);
  }
  else {
    float z = safe_sqrt(-p);
    float v = acos(q / (p * z * 2.0)) / 3.0;
    float m = cos(v);
    float n = sin(v) * M_SQRT3;
    vec3 t = clamp(vec3(m + m, -n - m, n - m) * z - kx, 0.0, 1.0);
    res = min(dot2(d + (c + b * t.x) * t.x), dot2(d + (c + b * t.y) * t.y));
  }
  return safe_sqrt(res);
}

float sdf_2d_ellipse(vec2 p, vec2 ab)
{
  ab *= 0.5;
  if (ab.x == ab.y) {
    return length(p) - ab.x;
  }
  else if (ab.x == 0.0 || ab.y == 0.0) {
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
  float c = (m2 + n2 - 1.0) / 3.0;
  float c3 = c * c * c;
  float q = c3 + m2 * n2 * 2.0;
  float d = c3 + m2 * n2;
  float g = m + m * n2;
  float co;
  if (d < 0.0) {
    float h = acos(q / c3) / 3.0;
    float s = cos(h);
    float t = sin(h) * M_SQRT3;
    float rx = safe_sqrt(-c * (s + t + 2.0) + m2);
    float ry = safe_sqrt(-c * (s - t + 2.0) + m2);
    co = (ry + sign(l) * rx + abs(g) / (rx * ry) - m) / 2.0;
  }
  else {
    float h = 2.0 * m * n * safe_sqrt(d);
    float s = sign(q + h) * pow(abs(q + h), 1.0 / 3.0);
    float u = sign(q - h) * pow(abs(q - h), 1.0 / 3.0);
    float rx = -s - u - c * 4.0 + 2.0 * m2;
    float ry = (s - u) * M_SQRT3;
    float rm = safe_sqrt(rx * rx + ry * ry);
    co = (ry / safe_sqrt(rm - rx) + 2.0 * g / rm - m) / 2.0;
  }
  vec2 r = ab * vec2(co, safe_sqrt(1.0 - co * co));
  return length(r - p) * sign(p.y - r.y);
}

float sdf_2d_heart(vec2 p, float r)
{
  r *= 2.0;
  p.y += r * 0.5;

  p.x = abs(p.x);

  if (p.y + p.x > r) {
    return safe_sqrt(dot2(p - vec2(0.25, 0.75) * r)) - M_SQRT2 / 4.0 * r;
  }
  else {
    return safe_sqrt(min(dot2(p - vec2(0.0, 1.0) * r), dot2(p - 0.5 * max(p.x + p.y, 0.0)))) *
           sign(p.x - p.y);
  }
}

float sdf_2d_quad(vec2 p, vec2 p0, vec2 p1, vec2 p2, vec2 p3)
{
  vec2 e0 = p1 - p0;
  vec2 v0 = p - p0;
  vec2 e1 = p2 - p1;
  vec2 v1 = p - p1;
  vec2 e2 = p3 - p2;
  vec2 v2 = p - p2;
  vec2 e3 = p0 - p3;
  vec2 v3 = p - p3;

  vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
  vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
  vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
  vec2 pq3 = v3 - e3 * clamp(dot(v3, e3) / dot(e3, e3), 0.0, 1.0);

  vec2 ds = min(min(vec2(dot(pq0, pq0), v0.x * e0.y - v0.y * e0.x),
                    vec2(dot(pq1, pq1), v1.x * e1.y - v1.y * e1.x)),
                min(vec2(dot(pq2, pq2), v2.x * e2.y - v2.y * e2.x),
                    vec2(dot(pq3, pq3), v3.x * e3.y - v3.y * e3.x)));

  float d = safe_sqrt(ds.x);

  return (ds.y > 0.0) ? -d : d;
}

float sdf_2d_vesica(vec2 p, float r, float d)
{
  p = abs(p);

  float b = safe_sqrt(r * r - d * d);
  if ((p.y - b) * d > p.x * b) {
    return length(vec2(p.x, p.y - b)) * sign(d);
  }
  else {
    return length(vec2(p.x + d, p.y)) - r;
  }
}

float sdf_2d_moon(vec2 p, float d, float ra, float rb)
{
  p.y = abs(p.y);

  float a = (ra * ra - rb * rb + d * d) / (2.0 * d);
  float b = safe_sqrt(ra * ra - a * a);
  float m = d * (p.x * b - p.y * a);
  float n = d * d * max(b - p.y, 0.0);
  if (m > n) {
    return length(p - vec2(a, b));
  }
  return max((length(p) - ra), -(length(p - vec2(d, 0.0)) - rb));
}

float sdf_2d_flat_joint(vec2 p, float l, float a, float lw)
{
  if (abs(a) < 0.001) {
    a = 0.001;
  }

  lw *= 0.5;

  vec2 sc = sincos(clamp(a * 0.5, -M_PI, M_PI));
  float ra = safe_divide(0.5 * l, a);

  p.x -= ra;

  vec2 q = p - 2.0 * sc * max(0.0, dot(sc, p));

  float u = abs(ra) - length(q);
  float d = max(length(vec2(q.x + ra - clamp(q.x + ra, -lw, lw), q.y)) * sign(-q.y), abs(u) - lw);

  return d;
}

float sdf_2d_round_joint(vec2 p, float l, float a, float lw)
{
  if (abs(a) < 0.001) {
    a = 0.001;
  }

  lw *= 0.5;

  vec2 sc = sincos(clamp(a * 0.5, -M_PI, M_PI));
  float ra = safe_divide(0.5 * l, a);

  p.x -= ra;

  vec2 q = p - 2.0 * sc * max(0.0, dot(sc, p));

  float u = abs(ra) - length(q);
  float d = (q.y < 0.0) ? length(q + vec2(ra, 0.0)) : abs(u);

  return d - lw;
}

float sdf_2d_pie(vec2 p, float r, float a)
{
  vec2 sc = sincos(clamp(a * 0.5, 0.0, M_PI));
  p.x = abs(p.x);
  float l = length(p) - r;
  float m = length(p - sc * clamp(dot(p, sc), 0.0, r));
  return max(l, m * sign(sc.y * p.x - sc.x * p.y));
}

float sdf_2d_arc(vec2 p, float a, float ra)
{
  vec2 sc = sincos(clamp(a * 0.5, 0.0, M_PI));
  p.x = abs(p.x);
  float k = (sc.y * p.x > sc.x * p.y) ? dot(p.xy, sc) : length(p.xy);
  return safe_sqrt(dot(p, p) + ra * ra - 2.0 * ra * k);
}

float sdf_2d_horseshoe(vec2 p, float r, float a, float overshoot, float lw)
{
  vec2 sc = sincos(clamp(a * 0.5, 0.0, M_PI));
  p.x = abs(p.x);
  float l = length(p);
  p = mat2(-sc.y, sc.x, sc.x, sc.y) * p;
  p = vec2((p.y > 0.0 || p.x > 0.0) ? p.x : l * sign(-sc.y), (p.x > 0.0) ? p.y : l);
  p = vec2(p.x, abs(p.y - r)) - vec2(overshoot, lw) * 0.5;
  return length(max(p, 0.0)) + min(0.0, max(p.x, p.y));
}

float sdf_2d_point_triangle(vec2 p, vec2 p0, vec2 p1, vec2 p2)
{
  vec2 e0 = p1 - p0;
  vec2 e1 = p2 - p1;
  vec2 e2 = p0 - p2;

  vec2 v0 = p - p0;
  vec2 v1 = p - p1;
  vec2 v2 = p - p2;

  vec2 pq0 = v0 - e0 * clamp(safe_divide(dot(v0, e0), dot(e0, e0)), 0.0, 1.0);
  vec2 pq1 = v1 - e1 * clamp(safe_divide(dot(v1, e1), dot(e1, e1)), 0.0, 1.0);
  vec2 pq2 = v2 - e2 * clamp(safe_divide(dot(v2, e2), dot(e2, e2)), 0.0, 1.0);
  float s = sgn(e0.x * e2.y - e0.y * e2.x);
  vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                   vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
               vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
  return -safe_sqrt(d.x) * sgn(d.y);
}

float sdf_2d_star_x(vec2 p, float r, float sides, float inset)
{

  float m = sgn(inset) * 2.0 + inset * inset * (sides - 2.0);
  float an = safe_divide(M_PI, sides);

  float en = safe_divide(M_PI, m);  // m is between 2 and n
  vec2 acs = vec2(cos(an), sin(an));
  vec2 ecs = vec2(cos(en), sin(en));  // ecs=vec2(0,1) for regular polygon,

  float bn = safe_mod(atan(p.x, p.y), 2.0 * an) - an;
  p = length(p) * vec2(cos(bn), abs(sin(bn)));
  p -= r * acs;
  p += ecs * clamp(-dot(p, ecs), 0.0, safe_divide(r * acs.y, ecs.y));
  return length(p) * sign(p.x);
}

float sdf_2d_star(vec2 p, float r, float sides, float inset, float inradius)  // m=[2,n]
{
  /* Sanitise inputs. */
  sides = max(sides, 2.0);

  float m;
  if (inset > 1.0) {
    // range from edge to centre
    m = mix(2.0, 2.0 - 2.0 / (sides + 1.0), inset - 1.0);
  }
  else {
    // range from centre to edge
    inset = 1.0 - inset;
    m = 2.0 + inset * inset * (sides - 2.0);
  }

  // scale incircle to circumcircle
  float scale = 1.0;
  if (inradius > 0.0) {
    float scalefac = 1.0 / cos(M_PI / sides);
    if (inradius < 1.0) {
      scale = (scalefac - 1.0) * inradius + 1.0;
    }
    else {
      scale = scalefac * inradius;
    }
  }

  p /= scale;

  // these 4 lines can be precomputed for a given shape
  float an = M_PI / sides;
  float en = M_PI / m;
  vec2 acs = vec2(cos(an), sin(an));
  vec2 ecs = vec2(cos(en), sin(en));  // ecs=vec2(0,1) and simplify, for regular polygon,

  // reduce to first sector
  float bn = safe_mod(atan(p.x, p.y) * sign(p.x), 2.0 * an) - an;
  p = length(p) * vec2(cos(bn), abs(sin(bn)));

  // line sdf
  p -= r * acs;
  p += ecs * clamp(-dot(p, ecs), 0.0, r * acs.y / ecs.y);
  return length(p) * sign(p.x) * scale;
}

float sdf_2d_pentagon(vec2 p, float r)
{
  const vec3 k = vec3(0.809016994, 0.587785252, 0.726542528);  // pi/5: cos, sin, tan
  p.y = -p.y;
  p.x = abs(p.x);
  p -= 2.0 * min(dot(vec2(-k.x, k.y), p), 0.0) * vec2(-k.x, k.y);
  p -= 2.0 * min(dot(vec2(k.x, k.y), p), 0.0) * vec2(k.x, k.y);
  p -= vec2(clamp(p.x, -r * k.z, r * k.z), r);
  return length(p) * sign(p.y);
}

/* 3D */
float sdf_3d_hex_prism(vec3 p, vec2 h)
{

  h *= 0.5;
  const vec3 k = vec3(-M_SQRT3_2, 0.5, M_SQRT1_3);
  p = abs(p);
  p.xy -= 2.0 * min(dot(k.xy, p.xy), 0.0) * k.xy;
  vec2 d = vec2(length(p.xy - vec2(clamp(p.x, -k.z * h.x, k.z * h.x), h.x)) * sign(p.y - h.x),
                p.z - h.y);
  return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float sdf_3d_hex_prism_incircle(vec3 p, vec2 h)
{
  return sdf_3d_hex_prism(p, vec2(h.x * M_SQRT3 * 0.5, h.y));
}

float sdf_3d_box(vec3 p, vec3 b)
{
  b *= 0.5;
  vec3 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdf_3d_torus(vec3 p, vec2 t)
{
  // t.x *= 0.5;
  vec2 q = vec2(length(p.xy) - t.x, p.z);
  return length(q) - t.y;
}

float sdf_3d_circle(vec3 p, float r)
{
  float l = length(p.xy) - r;
  return length(vec2(p.z, l));
}

// A circular disc with no thickness (i.e. a cylinder with no height).
// Subtract some value to make a flat disc with rounded edge.
float sdf_3d_disc(vec3 p, float r)
{
  float l = length(p.xy) - r;
  return l < 0 ? abs(p.z) : length(vec2(p.z, l));
}

// Endless "corner"
float sdf_2d_corner(vec2 p)
{
  return length(max(p, vec2(0.0))) + vmax(min(p, vec2(0.0)));
}

float sdf_3d_upright_cone(vec3 p, float radius, float height, float offset)
{
  /* Sanitise inputs. */
  p.z -= offset * sgn(height);

  if (length(vec2(radius, height)) == 0.0) {
    return length(p);
  }

  if (height < 0.0) {
    height = abs(height);
    p.z = -p.z;
  }

  radius = abs(radius);

  /* Cone. */
  vec2 q = vec2(length(p.xy), p.z);
  vec2 tip = q - vec2(0.0, height);
  vec2 mantleDir = normalize(vec2(height, radius));
  float mantle = dot(tip, mantleDir);
  float d = max(mantle, -q.y);
  float projected = dot(tip, vec2(mantleDir.y, -mantleDir.x));

  // distance to tip
  if ((q.y > height) && (projected < 0.0)) {
    d = max(d, length(tip));
  }

  // distance to base ring
  if ((q.x > radius) && (projected > length(vec2(height, radius)))) {
    d = max(d, length(q - vec2(radius, 0.0)));
  }
  return d;
}

float sdf_3d_cone(vec3 p, float a)
{
  p *= 0.5;
  vec2 sc = sincos(clamp(a * 0.5, 0.0, M_PI));
  float q = length(p.xy);
  return dot(sc, vec2(q, p.z));
}

float sdf_3d_point_cone(vec3 p, vec3 a, vec3 b, float ra, float rb)
{
  float rba = rb - ra;
  float baba = dot(b - a, b - a);
  float papa = dot(p - a, p - a);
  float paba = safe_divide(dot(p - a, b - a), baba);

  float x = safe_sqrt(papa - paba * paba * baba);

  float cax = max(0.0, x - ((paba < 0.5) ? ra : rb));
  float cay = abs(paba - 0.5) - 0.5;

  float k = rba * rba + baba;
  float f = clamp(safe_divide((rba * (x - ra) + paba * baba), k), 0.0, 1.0);

  float cbx = x - ra - f * rba;
  float cby = paba - f;

  float s = (cbx < 0.0 && cay < 0.0) ? -1.0 : 1.0;

  return s * safe_sqrt(min(cax * cax + cay * cay * baba, cbx * cbx + cby * cby * baba));
}

float sdf_3d_capsule(vec3 p, vec3 a, vec3 b, float r)
{
  vec3 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h) - r;
}

float sdf_3d_cylinder(vec3 p, vec3 c)
{
  return length(p.xz - c.xy) - c.z;
}

float sdf_capped_cylinder(vec3 p, float r, float h)
{
  vec2 d = abs(vec2(length(p.xy), p.z)) - vec2(r, h * 0.5);
  return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float sdf_3d_cylinder(vec3 p, vec3 a, vec3 b, float r)
{
  vec3 pa = p - a;
  vec3 ba = b - a;
  float baba = dot(ba, ba);
  float paba = dot(pa, ba);

  float x = length(pa * baba - ba * paba) - r * baba;
  float y = abs(paba - baba * 0.5) - baba * 0.5;
  float x2 = x * x;
  float y2 = y * y * baba;
  float d = (max(x, y) < 0.0) ? -min(x2, y2) : (((x > 0.0) ? x2 : 0.0) + ((y > 0.0) ? y2 : 0.0));
  return sign(d) * safe_sqrt(abs(d)) / baba;
}

float sdf_3d_solid_angle(vec3 p, float a, float ra)
{
  vec2 sc = sincos(clamp(a * 0.5, -M_PI, M_PI));
  vec2 q = vec2(length(p.xy), p.z);
  float l = length(q) - ra;
  float m = length(q - sc * clamp(dot(q, sc), 0.0, ra));
  return max(l, m * sign(sc.y * q.x - sc.x * q.y));
}

float sdf_3d_pyramid(vec3 p, float w, float h)
{
  p = p.xzy; /* z-up */

  p /= w;
  h /= w;

  float m2 = h * h + 0.25;

  /* symmetry */
  p.xz = abs(p.xz);
  p.xz = (p.z > p.x) ? p.zx : p.xz;
  p.xz -= 0.5;

  /* project into face plane (2D) */
  vec3 q = vec3(p.z, h * p.y - 0.5 * p.x, h * p.x + 0.5 * p.y);

  float s = max(-q.x, 0.0);
  float t = clamp((q.y - 0.5 * p.z) / (m2 + 0.25), 0.0, 1.0);

  float a = m2 * (q.x + s) * (q.x + s) + q.y * q.y;
  float b = m2 * (q.x + 0.5 * t) * (q.x + 0.5 * t) + (q.y - m2 * t) * (q.y - m2 * t);

  float d2 = min(q.y, -q.x * m2 - q.y * 0.5) > 0.0 ? 0.0 : min(a, b);

  /* recover 3D and scale, and add sign */
  float d = safe_sqrt((d2 + q.z * q.z) / m2) * sign(max(q.z, -p.y));

  return d * w;
}

float sdf_3d_octahedron(vec3 p, float s)
{
  p = abs(p);
  float m = p.x + p.y + p.z - s;

  vec3 q;
  if (3.0 * p.x < m) {
    q = p.xyz;
  }
  else if (3.0 * p.y < m) {
    q = p.yzx;
  }
  else if (3.0 * p.z < m) {
    q = p.zxy;
  }
  else {
    return m * M_SQRT1_3;
  }
  float k = clamp(0.5 * (q.z - q.y + s), 0.0, s);
  return length(vec3(q.x, q.y - s + k, q.z - k));
}

float sdf_alteration(float size, float dist, float lw, float invert)
{
  if (lw != 0.0) {
    dist = abs(dist) - lw * 0.5;
  }

  dist *= size;

  return (invert > 0.0) ? -dist : dist;
}

float sdf_dimension(float w, inout float roundness)
{
  float sw = sign(w);
  w = abs(w);
  roundness = mix(0.0, w, clamp(roundness, 0.0, 1.0));
  float dim = max(w - roundness, 0.0);
  roundness *= 0.5;
  return dim * sw;
}

vec2 sdf_dimension(float w, float d, inout float roundness)
{
  float sw = sign(w);
  float sd = sign(d);
  w = abs(w);
  d = abs(d);
  roundness = mix(0.0, min(w, d), clamp(roundness, 0.0, 1.0));
  vec2 dim = vec2(max(w - roundness, 0.0), max(d - roundness, 0.0));
  roundness *= 0.5;
  return dim * vec2(sw, sd);
}

vec3 sdf_dimension(float w, float d, float h, inout float roundness)
{
  float sw = sign(w);
  float sd = sign(d);
  float sh = sign(h);
  w = abs(w);
  d = abs(d);
  h = abs(h);
  roundness = mix(0.0, min(w, min(d, h)), clamp(roundness, 0.0, 1.0));
  vec3 dim = vec3(max(w - roundness, 0.0), max(d - roundness, 0.0), max(h - roundness, 0.0));
  roundness *= 0.5;
  return dim * vec3(sw, sd, sh);
}

/* 3D Node Functions. */

void node_sdf_prim_3d_sphere(vec3 co,
                             float size,
                             float radius,
                             float v1,
                             float v2,
                             float v3,
                             float v4,
                             vec3 p1,
                             vec3 p2,
                             vec3 p3,
                             vec3 p4,
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{
  if (size != 0.0) {
    co /= size;
    dist = length(co) - radius;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_hex_prism(vec3 co,
                                float size,
                                float radius,
                                float v1,
                                float v2,
                                float v3,
                                float v4,
                                vec3 p1,
                                vec3 p2,
                                vec3 p3,
                                vec3 p4,
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{
  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(v1, v3, roundness);
    dist = sdf_3d_hex_prism(co, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_hex_prism_incircle(vec3 co,
                                         float size,
                                         float radius,
                                         float v1,
                                         float v2,
                                         float v3,
                                         float v4,
                                         vec3 p1,
                                         vec3 p2,
                                         vec3 p3,
                                         vec3 p4,
                                         float a1,
                                         float roundness,
                                         float lw,
                                         float invert,
                                         out float dist)
{
  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(v1, v3, roundness);
    dist = sdf_3d_hex_prism_incircle(co, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_box(vec3 co,
                          float size,
                          float radius,
                          float v1,
                          float v2,
                          float v3,
                          float v4,
                          vec3 p1,
                          vec3 p2,
                          vec3 p3,
                          vec3 p4,
                          float a1,
                          float roundness,
                          float lw,
                          float invert,
                          out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec3 dim = sdf_dimension(v1, v2, v3, roundness);
    dist = sdf_3d_box(co, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_torus(vec3 co,
                            float size,
                            float radius,
                            float v1,
                            float v2,
                            float v3,
                            float v4,
                            vec3 p1,
                            vec3 p2,
                            vec3 p3,
                            vec3 p4,
                            float a1,
                            float roundness,
                            float lw,
                            float invert,
                            out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_3d_torus(co, vec2(radius, v1));
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_point_cone(vec3 co,
                                 float size,
                                 float radius,
                                 float v1,
                                 float v2,
                                 float v3,
                                 float v4,
                                 vec3 p1,
                                 vec3 p2,
                                 vec3 p3,
                                 vec3 p4,
                                 float a1,
                                 float roundness,
                                 float lw,
                                 float invert,
                                 out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(radius, v1, roundness);
    dist = sdf_3d_point_cone(co, p1, p2, dim.x, dim.y) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_cone(vec3 co,
                           float size,
                           float radius,
                           float v1,
                           float v2,
                           float v3,
                           float v4,
                           vec3 p1,
                           vec3 p2,
                           vec3 p3,
                           vec3 p4,
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(radius, v3, roundness);
    dist = sdf_3d_upright_cone(co, dim.x, dim.y, roundness) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_cylinder(vec3 co,
                               float size,
                               float radius,
                               float v1,
                               float v2,
                               float v3,
                               float v4,
                               vec3 p1,
                               vec3 p2,
                               vec3 p3,
                               vec3 p4,
                               float a1,
                               float roundness,
                               float lw,
                               float invert,
                               out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(radius, v3, roundness);
    dist = sdf_capped_cylinder(co, dim.x, dim.y - roundness * 2.0) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_point_cylinder(vec3 co,
                                     float size,
                                     float radius,
                                     float v1,
                                     float v2,
                                     float v3,
                                     float v4,
                                     vec3 p1,
                                     vec3 p2,
                                     vec3 p3,
                                     vec3 p4,
                                     float a1,
                                     float roundness,
                                     float lw,
                                     float invert,
                                     out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_3d_cylinder(co, p1, p2, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_capsule(vec3 co,
                              float size,
                              float radius,
                              float v1,
                              float v2,
                              float v3,
                              float v4,
                              vec3 p1,
                              vec3 p2,
                              vec3 p3,
                              vec3 p4,
                              float a1,
                              float roundness,
                              float lw,
                              float invert,
                              out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_3d_capsule(co, p1, p2, radius);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_octahedron(vec3 co,
                                 float size,
                                 float radius,
                                 float v1,
                                 float v2,
                                 float v3,
                                 float v4,
                                 vec3 p1,
                                 vec3 p2,
                                 vec3 p3,
                                 vec3 p4,
                                 float a1,
                                 float roundness,
                                 float lw,
                                 float invert,
                                 out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_3d_octahedron(co, dim) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_solid_angle(vec3 co,
                                  float size,
                                  float radius,
                                  float v1,
                                  float v2,
                                  float v3,
                                  float v4,
                                  vec3 p1,
                                  vec3 p2,
                                  vec3 p3,
                                  vec3 p4,
                                  float a1,
                                  float roundness,
                                  float lw,
                                  float invert,
                                  out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_3d_solid_angle(co, a1, dim) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_pyramid(vec3 co,
                              float size,
                              float radius,
                              float v1,
                              float v2,
                              float v3,
                              float v4,
                              vec3 p1,
                              vec3 p2,
                              vec3 p3,
                              vec3 p4,
                              float a1,
                              float roundness,
                              float lw,
                              float invert,
                              out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(v1, v3, roundness);
    if (dim.y == 0.0) {
      dist = sdf_3d_box(co, vec3(dim.x, dim.x, 0.0)) - roundness * 2.0;
    }
    else if (dim.x == 0.0) {
      co.z -= dim.y * 0.5;
      dist = sdf_3d_box(co, vec3(0.0, 0.0, dim.y)) - roundness * 2.0;
    }
    else {
      dist = sdf_3d_pyramid(co, dim.x, dim.y) - roundness * 2.0;
    }

    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_plane(vec3 co,
                            float size,
                            float radius,
                            float v1,
                            float v2,
                            float v3,
                            float v4,
                            vec3 p1,
                            vec3 p2,
                            vec3 p3,
                            vec3 p4,
                            float a1,
                            float roundness,
                            float lw,
                            float invert,
                            out float dist)
{

  if ((size != 0.0) && (length(p1) != 0.0)) {
    co /= size;
    dist = dot(co, normalize(p1)) + v1;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

/* 2D Node Functions. */

void node_sdf_prim_2d_circle(vec3 co,
                             float size,
                             float radius,
                             float v1,
                             float v2,
                             float v3,
                             float v4,
                             vec3 p1,
                             vec3 p2,
                             vec3 p3,
                             vec3 p4,
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = length(co.xy) - radius;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_line(vec3 co,
                           float size,
                           float radius,
                           float v1,
                           float v2,
                           float v3,
                           float v4,
                           vec3 p1,
                           vec3 p2,
                           vec3 p3,
                           vec3 p4,
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_2d_line(co.xy, p1.xy, p2.xy);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_corner(vec3 co,
                             float size,
                             float radius,
                             float v1,
                             float v2,
                             float v3,
                             float v4,
                             vec3 p1,
                             vec3 p2,
                             vec3 p3,
                             vec3 p4,
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_2d_corner(co.xy);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_rectangle(vec3 co,
                                float size,
                                float radius,
                                float v1,
                                float v2,
                                float v3,
                                float v4,
                                vec3 p1,
                                vec3 p2,
                                vec3 p3,
                                vec3 p4,
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(v1, v2, roundness);
    dist = sdf_2d_rectangle(co.xy, dim.x, dim.y) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_rhombus(vec3 co,
                              float size,
                              float radius,
                              float v1,
                              float v2,
                              float v3,
                              float v4,
                              vec3 p1,
                              vec3 p2,
                              vec3 p3,
                              vec3 p4,
                              float a1,
                              float roundness,
                              float lw,
                              float invert,
                              out float dist)
{

  if (size != 0.0) {
    co /= size;
    roundness = min(roundness, 0.9999);
    vec2 dim = sdf_dimension(v1, v2, roundness);
    dist = sdf_2d_rhombus(co.xy, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_star(vec3 co,
                           float size,
                           float radius,
                           float v1,
                           float v2,
                           float v3,
                           float v4,
                           vec3 p1,
                           vec3 p2,
                           vec3 p3,
                           vec3 p4,
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_star(co.xy, dim, v1, v2, v3) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_triangle(vec3 co,
                               float size,
                               float radius,
                               float v1,
                               float v2,
                               float v3,
                               float v4,
                               vec3 p1,
                               vec3 p2,
                               vec3 p3,
                               vec3 p4,
                               float a1,
                               float roundness,
                               float lw,
                               float invert,
                               out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_triangle(co.xy, dim) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_hexagon(vec3 co,
                              float size,
                              float radius,
                              float v1,
                              float v2,
                              float v3,
                              float v4,
                              vec3 p1,
                              vec3 p2,
                              vec3 p3,
                              vec3 p4,
                              float a1,
                              float roundness,
                              float lw,
                              float invert,
                              out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_hexagon(co.xy, dim) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_pentagon(vec3 co,
                               float size,
                               float radius,
                               float v1,
                               float v2,
                               float v3,
                               float v4,
                               vec3 p1,
                               vec3 p2,
                               vec3 p3,
                               vec3 p4,
                               float a1,
                               float roundness,
                               float lw,
                               float invert,
                               out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_pentagon(co.xy, dim) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_pie(vec3 co,
                          float size,
                          float radius,
                          float v1,
                          float v2,
                          float v3,
                          float v4,
                          vec3 p1,
                          vec3 p2,
                          vec3 p3,
                          vec3 p4,
                          float a1,
                          float roundness,
                          float lw,
                          float invert,
                          out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_pie(co.xy, dim, a1) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_arc(vec3 co,
                          float size,
                          float radius,
                          float v1,
                          float v2,
                          float v3,
                          float v4,
                          vec3 p1,
                          vec3 p2,
                          vec3 p3,
                          vec3 p4,
                          float a1,
                          float roundness,
                          float lw,
                          float invert,
                          out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_arc(co.xy, a1, dim) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_bezier(vec3 co,
                             float size,
                             float radius,
                             float v1,
                             float v2,
                             float v3,
                             float v4,
                             vec3 p1,
                             vec3 p2,
                             vec3 p3,
                             vec3 p4,
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_2d_bezier(co.xy, p1.xy, p2.xy, p3.xy);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_uneven_capsule(vec3 co,
                                     float size,
                                     float radius,
                                     float v1,
                                     float v2,
                                     float v3,
                                     float v4,
                                     vec3 p1,
                                     vec3 p2,
                                     vec3 p3,
                                     vec3 p4,
                                     float a1,
                                     float roundness,
                                     float lw,
                                     float invert,
                                     out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(radius, v1, roundness);
    dist = sdf_2d_uneven_capsule(co.xy, p1.xy, p2.xy, dim.x, dim.y);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_point_triangle(vec3 co,
                                     float size,
                                     float radius,
                                     float v1,
                                     float v2,
                                     float v3,
                                     float v4,
                                     vec3 p1,
                                     vec3 p2,
                                     vec3 p3,
                                     vec3 p4,
                                     float a1,
                                     float roundness,
                                     float lw,
                                     float invert,
                                     out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_2d_point_triangle(co.xy, p1.xy, p2.xy, p3.xy);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_trapezoid(vec3 co,
                                float size,
                                float radius,
                                float v1,
                                float v2,
                                float v3,
                                float v4,
                                vec3 p1,
                                vec3 p2,
                                vec3 p3,
                                vec3 p4,
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec3 dim = sdf_dimension(v1, v2, v3, roundness);
    dist = sdf_2d_trapezoid(co.xy, dim.x, dim.y, dim.z) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_moon(vec3 co,
                           float size,
                           float radius,
                           float v1,
                           float v2,
                           float v3,
                           float v4,
                           vec3 p1,
                           vec3 p2,
                           vec3 p3,
                           vec3 p4,
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(radius, v1, roundness);
    dist = sdf_2d_moon(co.xy, v2, dim.x, dim.y) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_vesica(vec3 co,
                             float size,
                             float radius,
                             float v1,
                             float v2,
                             float v3,
                             float v4,
                             vec3 p1,
                             vec3 p2,
                             vec3 p3,
                             vec3 p4,
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(radius, v1, roundness);
    dist = sdf_2d_vesica(co.xy, dim.x, dim.y) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_rounded_x(vec3 co,
                                float size,
                                float radius,
                                float v1,
                                float v2,
                                float v3,
                                float v4,
                                vec3 p1,
                                vec3 p2,
                                vec3 p3,
                                vec3 p4,
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_rounded_x(co.xy, dim) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_cross(vec3 co,
                            float size,
                            float radius,
                            float v1,
                            float v2,
                            float v3,
                            float v4,
                            vec3 p1,
                            vec3 p2,
                            vec3 p3,
                            vec3 p4,
                            float a1,
                            float roundness,
                            float lw,
                            float invert,
                            out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_blobby_cross(co.xy, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_heart(vec3 co,
                            float size,
                            float radius,
                            float v1,
                            float v2,
                            float v3,
                            float v4,
                            vec3 p1,
                            vec3 p2,
                            vec3 p3,
                            vec3 p4,
                            float a1,
                            float roundness,
                            float lw,
                            float invert,
                            out float dist)
{

  if (size != 0.0) {
    co /= size;
    float dim = sdf_dimension(radius, roundness);
    dist = sdf_2d_heart(co.xy, dim) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_horseshoe(vec3 co,
                                float size,
                                float radius,
                                float v1,
                                float v2,
                                float v3,
                                float v4,
                                vec3 p1,
                                vec3 p2,
                                vec3 p3,
                                vec3 p4,
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(radius, v1, roundness);
    dist = sdf_2d_horseshoe(co.xy, dim.x, a1, dim.y, lw) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_parabola(vec3 co,
                               float size,
                               float radius,
                               float v1,
                               float v2,
                               float v3,
                               float v4,
                               vec3 p1,
                               vec3 p2,
                               vec3 p3,
                               vec3 p4,
                               float a1,
                               float roundness,
                               float lw,
                               float invert,
                               out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_2d_parabola(co.xy, v1);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_parabola_segment(vec3 co,
                                       float size,
                                       float radius,
                                       float v1,
                                       float v2,
                                       float v3,
                                       float v4,
                                       vec3 p1,
                                       vec3 p2,
                                       vec3 p3,
                                       vec3 p4,
                                       float a1,
                                       float roundness,
                                       float lw,
                                       float invert,
                                       out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_2d_parabola_segment(co.xy, v1, v2);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_ellipse(vec3 co,
                              float size,
                              float radius,
                              float v1,
                              float v2,
                              float v3,
                              float v4,
                              vec3 p1,
                              vec3 p2,
                              vec3 p3,
                              vec3 p4,
                              float a1,
                              float roundness,
                              float lw,
                              float invert,
                              out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_2d_ellipse(co.xy, vec2(v1, v2));
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_isosceles(vec3 co,
                                float size,
                                float radius,
                                float v1,
                                float v2,
                                float v3,
                                float v4,
                                vec3 p1,
                                vec3 p2,
                                vec3 p3,
                                vec3 p4,
                                float a1,
                                float roundness,
                                float lw,
                                float invert,
                                out float dist)
{

  if (size != 0.0) {
    co /= size;
    vec2 dim = sdf_dimension(v1, v3, roundness);
    dist = sdf_2d_isosceles(co.xy, dim) - roundness;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_round_joint(vec3 co,
                                  float size,
                                  float radius,
                                  float v1,
                                  float v2,
                                  float v3,
                                  float v4,
                                  vec3 p1,
                                  vec3 p2,
                                  vec3 p3,
                                  vec3 p4,
                                  float a1,
                                  float roundness,
                                  float lw,
                                  float invert,
                                  out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_2d_round_joint(co.xy, v3, a1, lw);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_flat_joint(vec3 co,
                                 float size,
                                 float radius,
                                 float v1,
                                 float v2,
                                 float v3,
                                 float v4,
                                 vec3 p1,
                                 vec3 p2,
                                 vec3 p3,
                                 vec3 p4,
                                 float a1,
                                 float roundness,
                                 float lw,
                                 float invert,
                                 out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_2d_flat_joint(co.xy, v3, a1, lw);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_2d_quad(vec3 co,
                           float size,
                           float radius,
                           float v1,
                           float v2,
                           float v3,
                           float v4,
                           vec3 p1,
                           vec3 p2,
                           vec3 p3,
                           vec3 p4,
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0) {
    co /= size;
    dist = sdf_2d_quad(co.xy, p1.xy, p2.xy, p3.xy, p4.xy);
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_disc(vec3 co,
                           float size,
                           float radius,
                           float v1,
                           float v2,
                           float v3,
                           float v4,
                           vec3 p1,
                           vec3 p2,
                           vec3 p3,
                           vec3 p4,
                           float a1,
                           float roundness,
                           float lw,
                           float invert,
                           out float dist)
{

  if (size != 0.0) {
    co /= size;
    float r = sdf_dimension(radius, roundness);
    dist = sdf_3d_disc(co, r) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

void node_sdf_prim_3d_circle(vec3 co,
                             float size,
                             float radius,
                             float v1,
                             float v2,
                             float v3,
                             float v4,
                             vec3 p1,
                             vec3 p2,
                             vec3 p3,
                             vec3 p4,
                             float a1,
                             float roundness,
                             float lw,
                             float invert,
                             out float dist)
{

  if (size != 0.0) {
    co /= size;
    float r = sdf_dimension(radius, roundness);
    dist = sdf_3d_circle(co, r) - roundness * 2.0;
    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}

/* Test. */

float sdCrescent(vec2 p, float d, float r0, float r1)
{

  float sign0 = sign(r0);
  float sign1 = sign(r1);
  r0 = abs(r0);
  r1 = abs(r1);
  float a = (r0 * r0 - r1 * r1 + d * d) / (2.0 * d);

  if (a < r0) {
    p.y = abs(p.y);
    float b = safe_sqrt(r0 * r0 - a * a);
    float k = p.y * a - p.x * b;
    float h = min(sign0 * (d * (p.y - b) - k), sign1 * k);
    if (h > 0.0) {
      return length(p - vec2(a, b));
    }
  }

  return max(sign0 * (length(p) - r0), sign1 * (length(p - vec2(d, 0.0)) - r1));
}

/* TEst */

void node_sdf_test(vec3 co,
                   float size,
                   float radius,
                   float v1,
                   float v2,
                   float v3,
                   float v4,
                   vec3 p1,
                   vec3 p2,
                   vec3 p3,
                   vec3 p4,
                   float a1,
                   float roundness,
                   float lw,
                   float invert,
                   out float dist)
{

  if (size != 0.0) {
    co /= size;
    int n = int(v4);
    if (n == 0) {
      vec2 dim = sdf_dimension(radius, v1, roundness);
      dist = sdCrescent(co.xy, v2, dim.x, dim.y) - roundness * 2.0;
    }
    else if (n == 4) {
      float r = sdf_dimension(radius, roundness);
      dist = sdf_3d_gdf(co, r, int(v2), int(v3)) - roundness * 2.0;
    }
    else if (n == 5) {
      float r = sdf_dimension(radius, roundness);
      dist = sdf_3d_gdf_exp(co, r, v1, int(v2), int(v3)) - roundness * 2.0;
    }

    dist = sdf_alteration(size, dist, lw, invert);
  }
  else {
    dist = length(co);
  }
}
