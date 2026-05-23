/* SPDX-FileCopyrightText: 2017-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

void main()
{
#if defined(IRRADIANCE_SH_L2)
  constexpr int2 data_size = int2(3, 3);
#elif defined(IRRADIANCE_HL2)
  constexpr int2 data_size = int2(3, 2);
#endif
  int2 coord = int2(gl_FragCoord.xy) % data_size;
  FragColor = texelFetch(irradianceGrid, int3(coord, 0), 0);

  if (any(greaterThanEqual(int2(gl_FragCoord.xy), data_size))) {
    FragColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
  }
}
