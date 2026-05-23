/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Based on Frosbite Unified Volumetric.
 * https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite */

#include "volumetric_lib.glsl"

/* Store volumetric properties into the froxel textures. */

/* WARNING: these are not attributes, these are global vars. */
float3 worldPosition = float3(0.0f);
float3 objectPosition = float3(0.0f);
float3 viewPosition = float3(0.0f);
float3 viewNormal = float3(0.0f);
float3 volumeOrco = float3(0.0f);

int attr_id = 0;

#ifndef CLEAR
GlobalData init_globals(void)
{
  GlobalData surf;
  surf.P = worldPosition;
  surf.N = float3(0.0f);
  surf.Ng = float3(0.0f);
  surf.is_strand = false;
  surf.hair_time = 0.0f;
  surf.hair_thickness = 0.0f;
  surf.hair_strand_id = 0;
  surf.barycentric_coords = float2(0.0f);
  surf.barycentric_dists = float3(0.0f);
  surf.ray_type = RAY_TYPE_CAMERA;
  surf.ray_depth = 0.0f;
  surf.ray_length = distance(surf.P, cameraPos);
  return surf;
}

float3 coordinate_camera(float3 P)
{
  float3 vP;
  vP = transform_point(ViewMatrixGoo, P);
  vP.z = -vP.z;
  return vP;
}

float3 coordinate_screen(float3 P)
{
  float3 window = float3(0.0f);
  window.xy = project_point(ProjectionMatrixGoo, transform_point(ViewMatrixGoo, P)).xy * 0.5f + 0.5f;
  window.xy = window.xy * cameraUvScaleBias.xy + cameraUvScaleBias.zw;
  return window;
}

float3 coordinate_reflect(float3 P, float3 N)
{
  return float3(0.0f);
}

float3 coordinate_incoming(float3 P)
{
  return cameraVec(P);
}

float texture_lod_bias_get()
{
  return 1.0f;
}
#endif

void main()
{
  int3 volume_cell = int3(int2(gl_FragCoord.xy), volumetric_geom_iface.slice);
  float3 ndc_cell = volume_to_ndc((float3(volume_cell) + volJitter.xyz) * volInvTexSize.xyz);

  viewPosition = get_view_space_from_depth(ndc_cell.xy, ndc_cell.z);
  worldPosition = point_view_to_world(viewPosition);
#ifdef MESH_SHADER
  objectPosition = point_world_to_object(worldPosition);
  volumeOrco = OrcoAdd + objectPosition * OrcoMul;

  if (any(lessThan(volumeOrco, float3(0.0f))) || any(greaterThan(volumeOrco, float3(1.0f)))) {
    /* NOTE: Discard is not an explicit return in Metal prior to versions 2.3f.
     * adding return after discard ensures consistent behavior and avoids GPU
     * side-effects where control flow continues with undefined values. */
    gpu_discard_fragment();
    return;
  }
#else /* WORLD_SHADER */
  volumeOrco = worldPosition;
#endif

#ifdef CLEAR
  volumeScattering = float4(0.0f, 0.0f, 0.0f, 1.0f);
  volumeExtinction = float4(0.0f, 0.0f, 0.0f, 1.0f);
  volumeEmissive = float4(0.0f, 0.0f, 0.0f, 1.0f);
  volumePhase = float4(0.0f, 0.0f, 0.0f, 0.0f);
#else
  g_data = init_globals();
#  ifndef NO_ATTRIB_LOAD
  attrib_load();
#  endif
  Closure cl = nodetree_exec();
#  ifdef MESH_SHADER
  cl.scatter *= drw_volume.density_scale;
  cl.absorption *= drw_volume.density_scale;
  cl.emission *= drw_volume.density_scale;
#  endif

  volumeScattering = float4(cl.scatter, 1.0f);
  volumeExtinction = float4(cl.absorption + cl.scatter, 1.0f);
  volumeEmissive = float4(cl.emission, 1.0f);
  /* Do not add phase weight if no scattering. */
  if (all(equal(cl.scatter, float3(0.0f)))) {
    volumePhase = float4(0.0f);
  }
  else {
    volumePhase = float4(cl.anisotropy, float3(1.0f));
  }
#endif
}

float3 grid_coordinates()
{
  float3 co = volumeOrco;
#ifdef MESH_SHADER
  co = (drw_volume.grids_xform[attr_id] * float4(objectPosition, 1.0f)).xyz;
#endif
  attr_id += 1;
  return co;
}

float3 attr_load_orco(sampler3D orco)
{
  attr_id += 1;
  return volumeOrco;
}
float4 attr_load_tangent(sampler3D tangent)
{
  attr_id += 1;
  return float4(0);
}
float4 attr_load_vec4(sampler3D tex)
{
  return texture(tex, grid_coordinates());
}
float3 attr_load_vec3(sampler3D tex)
{
  return texture(tex, grid_coordinates()).rgb;
}
float2 attr_load_vec2(sampler3D tex)
{
  return texture(tex, grid_coordinates()).rg;
}
float attr_load_float(sampler3D tex)
{
  return texture(tex, grid_coordinates()).r;
}

/* TODO(@fclem): These implementation details should concern the DRWManager and not be a fix on
 * the engine side. But as of now, the engines are responsible for loading the attributes. */
float attr_load_temperature_post(float attr)
{
#ifdef MESH_SHADER
  /* Bring the into standard range without having to modify the grid values */
  attr = (attr > 0.01f) ? (attr * drw_volume.temperature_mul + drw_volume.temperature_bias) : 0.0f;
#endif
  return attr;
}
float4 attr_load_color_post(float4 attr)
{
#ifdef MESH_SHADER
  /* Density is premultiplied for interpolation, divide it out here. */
  attr.rgb *= safe_rcp(attr.a);
  attr.rgb *= drw_volume.color_mul.rgb;
  attr.a = 1.0f;
#endif
  return attr;
}
float4 attr_load_uniform(float4 attr, const uint attr_hash)
{
  return attr;
}
