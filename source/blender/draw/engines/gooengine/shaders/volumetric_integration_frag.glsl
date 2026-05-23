/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Based on Frosbite Unified Volumetric.
 * https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite */

/* Step 3 : Integrate for each froxel the final amount of light
 * scattered back to the viewer and the amount of transmittance. */

#include "volumetric_lib.glsl"

/* Globals when using OPTI */
#ifdef USE_VOLUME_OPTI
float3 finalScattering;
float3 finalTransmittance;
#endif

void main()
{
  /* Start with full transmittance and no scattered light. */
  finalScattering = float3(0.0f);
  finalTransmittance = float3(1.0f);

  float3 tex_size = float3(textureSize(volumeScattering, 0).xyz);

  /* Compute view ray. */
  float2 uvs = gl_FragCoord.xy / tex_size.xy;
  float3 ndc_cell = volume_to_ndc(float3(uvs, 1e-5f));
  float3 view_cell = get_view_space_from_depth(ndc_cell.xy, ndc_cell.z);

  /* Ortho */
  float prev_ray_len = view_cell.z;
  float orig_ray_len = 1.0f;

  /* Persp */
  if (ProjectionMatrixGoo[3][3] == 0.0f) {
    prev_ray_len = length(view_cell);
    orig_ray_len = prev_ray_len / view_cell.z;
  }

#ifdef USE_VOLUME_OPTI
  int slice = textureSize(volumeScattering, 0).z;
  int2 texco = int2(gl_FragCoord.xy);
#else
  int slice = volumetric_geom_iface.slice;
#endif
  for (int i = 0; i <= slice; i++) {
    int3 volume_cell = int3(int2(gl_FragCoord.xy), i);

    float3 Lscat = texelFetch(volumeScattering, volume_cell, 0).rgb;
    float3 s_extinction = texelFetch(volumeExtinction, volume_cell, 0).rgb;

    float cell_depth = volume_z_to_view_z((float(i) + 1.0f) / tex_size.z);
    float ray_len = orig_ray_len * cell_depth;

    /* Emission does not work of there is no extinction because
     * Tr evaluates to 1.0f leading to Lscat = 0.0f. (See #65771) */
    s_extinction = max(float3(1e-7f) * step(1e-5f, Lscat), s_extinction);

    /* Evaluate Scattering */
    float s_len = abs(ray_len - prev_ray_len);
    prev_ray_len = ray_len;
    float3 Tr = exp(-s_extinction * s_len);

    /* integrate along the current step segment */
    /* NOTE: Original calculation carries precision issues when compiling for AMD GPUs
     * and running Metal. This version of the equation retains precision well for all
     * macOS HW configurations. */
    Lscat = (Lscat * (1.0f - Tr)) / max(float3(1e-8f), s_extinction);

    /* accumulate and also take into account the transmittance from previous steps */
    finalScattering += finalTransmittance * Lscat;

    finalTransmittance *= Tr;

#ifdef USE_VOLUME_OPTI
    int3 coord = int3(texco, i);
    imageStore(finalScattering_img, coord, float4(finalScattering, 0.0f));
    imageStore(finalTransmittance_img, coord, float4(finalTransmittance, 0.0f));
#endif
  }
}
