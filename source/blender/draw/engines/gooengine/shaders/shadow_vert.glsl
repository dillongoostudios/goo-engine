/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gooengine_nodetree_lib.glsl"
#include "draw_curves_lib.glsl"
#include "surface_lib.glsl"

void main()
{
  GPU_INTEL_VERTEX_SHADER_WORKAROUND

#ifdef HAIR_SHADER
  hairStrandID = hair_get_strand_id();
  hairBary = hair_get_barycentric();
  float3 pos, binor;
  hair_get_pos_tan_binor_time((ProjectionMatrixGoo[3][3] == 0.0f),
                              ModelMatrixInverse,
                              ViewMatrixInverseGoo[3].xyz,
                              ViewMatrixInverseGoo[2].xyz,
                              pos,
                              hairTangent,
                              binor,
                              hairTime,
                              hairThickness,
                              hairThickTime);

  worldNormal = cross(hairTangent, binor);
  float3 world_pos = pos;
#elif defined(POINTCLOUD_SHADER)
  pointcloud_get_pos_and_radius(pointPosition, pointRadius);
  pointID = pointcloud_get_point_id();
#else
  float3 world_pos = point_object_to_world(pos);
#endif

  gl_Position = point_world_to_ndc(world_pos);
#ifdef MESH_SHADER
  worldPosition = world_pos;
  viewPosition = point_world_to_view(worldPosition);

#  ifndef HAIR_SHADER
  worldNormal = normalize(normal_object_to_world(nor));
#  endif

  /* No need to normalize since this is just a rotation. */
  viewNormal = normal_world_to_view(worldNormal);
#  ifdef USE_ATTR
#    ifdef HAIR_SHADER
  pos = hair_get_strand_pos();
#    endif
  pass_attr(pos, NormalMatrix, ModelMatrixInverse);
#  endif
#endif
}

#ifdef HAIR_SHADER
#  ifdef OBINFO_LIB
float3 attr_load_orco(samplerBuffer cd_buf)
{
  float3 P = hair_get_strand_pos();
  float3 lP = transform_point(ModelMatrixInverse, P);
  return OrcoAdd + lP * OrcoMul;
}
#  endif

/* Per attribute scope follows loading order. */
int g_curves_attr_id = 0;

/* Return the index to use for looking up the attribute value in the sampler
 * based on the attribute scope (point or spline). */
int curves_attribute_element_id()
{
  int id = hairStrandID;
  if (drw_curves.is_point_attribute[g_curves_attr_id][0] != 0u) {
    id = hair_get_base_id();
  }

  g_curves_attr_id += 1;
  return id;
}

float4 attr_load_tangent(samplerBuffer cd_buf)
{
  /* Not supported. */
  return float4(0.0f, 0.0f, 0.0f, 1.0f);
}

float4 attr_load_vec4(samplerBuffer cd_buf)
{
  return texelFetch(cd_buf, curves_attribute_element_id()).rgba;
}

float3 attr_load_vec3(samplerBuffer cd_buf)
{
  return texelFetch(cd_buf, curves_attribute_element_id()).rgb;
}

float2 attr_load_vec2(samplerBuffer cd_buf)
{
  return texelFetch(cd_buf, curves_attribute_element_id()).rg;
}

float attr_load_float(samplerBuffer cd_buf)
{
  return texelFetch(cd_buf, curves_attribute_element_id()).r;
}

#else

#  ifdef OBINFO_LIB
float3 attr_load_orco(samplerBuffer cd_buf)
{
  float3 P = hair_get_strand_pos();
  float3 lP = transform_point(ModelMatrixInverse, P);
  return OrcoAdd + lP * OrcoMul;
}
#  endif

float4 attr_load_tangent(float4 tangent)
{
  tangent.xyz = safe_normalize(normal_object_to_world(tangent.xyz));
  return tangent;
}

/* Simple passthrough. */
float4 attr_load_vec4(float4 attr)
{
  return attr;
}
float3 attr_load_vec3(float3 attr)
{
  return attr;
}
float2 attr_load_vec2(float2 attr)
{
  return attr;
}
float2 attr_load_float(float2 attr)
{
  return attr;
}
#endif

/* Passthrough. */
float attr_load_temperature_post(float attr)
{
  return attr;
}
float4 attr_load_color_post(float4 attr)
{
  return attr;
}
float4 attr_load_uniform(float4 attr, const uint attr_hash)
{
  return attr;
}
