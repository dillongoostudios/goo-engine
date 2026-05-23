/* SPDX-FileCopyrightText: 2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Requires all common matrices declared. */

void normal_transform_object_to_world(float3 vin, out float3 vout)
{
  /* Expansion of NormalMatrix. */
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = vin * to_float3x3(ModelMatrixInverse);
#else
  vout = vin * to_float3x3(drw_modelinv());
#endif
}

void normal_transform_world_to_object(float3 vin, out float3 vout)
{
  /* Expansion of NormalMatrixInverse. */
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = vin * to_float3x3(ModelMatrix);
#else
  vout = vin * to_float3x3(drw_modelmat());
#endif
}

void direction_transform_object_to_world(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = to_float3x3(ModelMatrix) * vin;
#else
  vout = to_float3x3(drw_modelmat()) * vin;
#endif
}

void direction_transform_object_to_view(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = to_float3x3(ModelMatrix) * vin;
  vout = to_float3x3(ViewMatrixGoo) * vout;
#else
  vout = to_float3x3(drw_modelmat()) * vin;
  vout = to_float3x3(drw_view().viewmat) * vout;
#endif
}

void direction_transform_view_to_world(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = to_float3x3(ViewMatrixInverseGoo) * vin;
#else
  vout = to_float3x3(drw_view().viewinv) * vin;
#endif
}

void direction_transform_view_to_object(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = to_float3x3(ViewMatrixInverseGoo) * vin;
  vout = to_float3x3(ModelMatrixInverse) * vout;
#else
  vout = to_float3x3(drw_view().viewinv) * vin;
  vout = to_float3x3(drw_modelinv()) * vout;
#endif
}

void direction_transform_world_to_view(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = to_float3x3(ViewMatrixGoo) * vin;
#else
  vout = to_float3x3(drw_view().viewmat) * vin;
#endif
}

void direction_transform_world_to_object(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = to_float3x3(ModelMatrixInverse) * vin;
#else
  vout = to_float3x3(drw_modelinv()) * vin;
#endif
}

void point_transform_object_to_world(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = (ModelMatrix * vec4(vin, 1.0f)).xyz;
#else
  vout = (drw_modelmat() * float4(vin, 1.0f)).xyz;
#endif
}

void point_transform_object_to_view(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = (ViewMatrixGoo * (ModelMatrix * vec4(vin, 1.0f))).xyz;
#else
  vout = (drw_view().viewmat * (drw_modelmat() * float4(vin, 1.0f))).xyz;
#endif
}

void point_transform_view_to_world(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = (ViewMatrixInverseGoo * vec4(vin, 1.0f)).xyz;
#else
  vout = (drw_view().viewinv * float4(vin, 1.0f)).xyz;
#endif
}

void point_transform_view_to_object(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = (ModelMatrixInverse * (ViewMatrixInverseGoo * vec4(vin, 1.0f))).xyz;
#else
  vout = (drw_modelinv() * (drw_view().viewinv * float4(vin, 1.0f))).xyz;
#endif
}

void point_transform_world_to_view(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = (ViewMatrixGoo * vec4(vin, 1.0f)).xyz;
#else
  vout = (drw_view().viewmat * float4(vin, 1.0f)).xyz;
#endif
}

void point_transform_world_to_object(float3 vin, out float3 vout)
{
#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  vout = (ModelMatrixInverse * vec4(vin, 1.0f)).xyz;
#else
  vout = (drw_modelinv() * float4(vin, 1.0f)).xyz;
#endif
}
