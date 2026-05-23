/* SPDX-FileCopyrightText: 2020-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "goo_common_view_lib.glsl"
#include "draw_curves_lib.glsl"

void main()
{
  GPU_INTEL_VERTEX_SHADER_WORKAROUND

#ifdef HAIR
  bool is_persp = (ProjectionMatrixGoo[3][3] == 0.0f);
  float time, thick_time, thickness;
  float3 tan, binor;
  float3 wpos;

  hair_get_pos_tan_binor_time(is_persp,
                              ModelMatrixInverse,
                              ViewMatrixInverseGoo[3].xyz,
                              ViewMatrixInverseGoo[2].xyz,
                              wpos,
                              tan,
                              binor,
                              time,
                              thickness,
                              thick_time);

  int id = hair_get_base_id();
  float3 pos = texelFetch(hairPointBuffer, id).point_position;
  float3 prv = texelFetch(prvBuffer, id).point_position;
  float3 nxt = texelFetch(nxtBuffer, id).point_position;
#endif
  prevWorldPos = (prevModelMatrix * float4(useDeform ? prv : pos, 1.0f)).xyz;
  currWorldPos = (currModelMatrix * float4(pos, 1.0f)).xyz;
  nextWorldPos = (nextModelMatrix * float4(useDeform ? nxt : pos, 1.0f)).xyz;
  /* Use jittered projmatrix to be able to match exact sample depth (depth equal test).
   * Note that currModelMatrix needs to also be equal to ModelMatrix for the samples to match. */
#ifndef HAIR
  gl_Position = ProjectionMatrixGoo * (ViewMatrixGoo * float4(currWorldPos, 1.0f));
#else
  gl_Position = ProjectionMatrixGoo * (ViewMatrixGoo * float4(wpos, 1.0f));
#endif
}
