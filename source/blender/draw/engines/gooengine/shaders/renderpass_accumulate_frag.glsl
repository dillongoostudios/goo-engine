/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** Stupidly simple shader to allow alpha blended accumulation. */

void main()
{
  int2 texel = int2(gl_FragCoord.xy);
  FragColor = texelFetch(inputBuffer, texel, 0);
}
