/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Based on Frosbite Unified Volumetric.
 * https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite */

/* Step 2 : Evaluate all light scattering for each froxels.
 * Also do the temporal reprojection to fight aliasing artifacts. */

#include "gooengine_nodetree_lib.glsl"
#include "volumetric_lib.glsl"

void main()
{
  int3 volume_cell = int3(int2(gl_FragCoord.xy), volumetric_geom_iface.slice);

  /* Emission */
  outScattering = texelFetch(volumeEmission, volume_cell, 0);
  outTransmittance = texelFetch(volumeExtinction, volume_cell, 0);
  float3 s_scattering = texelFetch(volumeScattering, volume_cell, 0).rgb;
  float3 volume_ndc = volume_to_ndc((float3(volume_cell) + volJitter.xyz) * volInvTexSize.xyz);
  float3 P = get_world_space_from_depth(volume_ndc.xy, volume_ndc.z);
  float3 V = cameraVec(P);

  float2 phase = texelFetch(volumePhase, volume_cell, 0).rg;
  float s_anisotropy = phase.x / max(1.0f, phase.y);

  /* Environment : Average color. */
  outScattering.rgb += irradiance_volumetric(P) * s_scattering * phase_function_isotropic();

#ifdef VOLUME_LIGHTING /* Lights */
  for (int i = 0; i < MAX_LIGHT && i < laNumLight; i++) {
    LightData ld = lights_data[i];

    if (ld.l_volume == 0.0f) {
      continue;
    }

    float4 l_vector;
    l_vector.xyz = light_volume_light_vector(ld, P);
    l_vector.w = length(l_vector.xyz);

    float vis = light_visibility(ld, P, l_vector, int4(-1), int4(-1));

    if (vis < 1e-4f) {
      continue;
    }

    float3 Li = light_volume(ld, l_vector) * light_volume_shadow(ld, P, l_vector, volumeExtinction);

    outScattering.rgb += Li * vis * s_scattering *
                         phase_function(-V, l_vector.xyz / l_vector.w, s_anisotropy);
  }
#endif

  /* Temporal supersampling */
  /* NOTE: this uses the cell non-jittered position (texel center). */
  float3 curr_ndc = volume_to_ndc(float3(gl_FragCoord.xy, float(volumetric_geom_iface.slice) + 0.5f) *
                                volInvTexSize.xyz);
  float3 wpos = get_world_space_from_depth(curr_ndc.xy, curr_ndc.z);
  float3 prev_ndc = project_point(pastViewProjectionMatrix, wpos);
  float3 prev_volume = ndc_to_volume(prev_ndc * 0.5f + 0.5f);

  if ((volHistoryAlpha > 0.0f) && all(greaterThan(prev_volume, float3(0.0f))) &&
      all(lessThan(prev_volume, float3(1.0f))))
  {
    float4 h_Scattering = texture(historyScattering, prev_volume);
    float4 h_Transmittance = texture(historyTransmittance, prev_volume);
    outScattering = mix(outScattering, h_Scattering, volHistoryAlpha);
    outTransmittance = mix(outTransmittance, h_Transmittance, volHistoryAlpha);
  }

  /* Catch NaNs */
  if (any(isnan(outScattering)) || any(isnan(outTransmittance))) {
    outScattering = float4(0.0f);
    outTransmittance = float4(1.0f);
  }
}
