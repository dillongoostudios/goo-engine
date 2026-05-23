/* SPDX-FileCopyrightText: 2019-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

void node_subsurface_scattering(float4 color,
                                float scale,
                                float3 radius,
                                float ior,
                                float roughness,
                                float anisotropy,
                                float3 N,
                                float weight,
                                const float do_sss,
                                out Closure result)
{
  color = max(color, float4(0.0f));
  ior = max(ior, 1e-5f);
  /* roughness = saturate(roughness) */
  N = safe_normalize(N);

  ClosureSubsurface sss_data;
  sss_data.weight = weight;
  sss_data.color = color.rgb;
  sss_data.N = N;
  sss_data.sss_radius = max(radius * scale, float3(0.0f));

#ifdef GPU_SHADER_EEVEE_LEGACY_DEFINES
  if (do_sss == 0.0f) {
    /* Flag as disabled. */
    sss_data.sss_radius.b = -1.0f;
  }
#endif
  result = closure_eval(sss_data);
}
