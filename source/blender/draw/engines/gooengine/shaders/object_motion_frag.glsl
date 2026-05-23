/* SPDX-FileCopyrightText: 2020-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

void main()
{
  float4 prev_wpos = prevViewProjMatrix * float4(prevWorldPos, 1.0f);
  float4 curr_wpos = currViewProjMatrix * float4(currWorldPos, 1.0f);
  float4 next_wpos = nextViewProjMatrix * float4(nextWorldPos, 1.0f);

  float2 prev_uv = (prev_wpos.xy / prev_wpos.w);
  float2 curr_uv = (curr_wpos.xy / curr_wpos.w);
  float2 next_uv = (next_wpos.xy / next_wpos.w);

  outData.xy = prev_uv - curr_uv;
  outData.zw = next_uv - curr_uv;

  /* Encode to unsigned normalized 16bit texture. */
  outData = outData * 0.5f + 0.5f;
}
