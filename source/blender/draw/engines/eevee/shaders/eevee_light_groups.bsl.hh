/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "eevee_light_groups_shared.hh"
namespace eevee {
bool goo_groups_intersect(int4 a, int4 b)
{
  int4 overlap = a & b;
  return (overlap.x | overlap.y | overlap.z | overlap.w) != 0;
}
GooMaterialLightGroups goo_groups_default()
{
  return GooMaterialLightGroups{int4(0, 0, 0, 1), int4(0, 0, 0, 1)};
}
struct GooLightGroups {
  [[storage(GOO_LIGHT_GROUPS_BUF_SLOT,
            read)]] const GooMaterialLightGroups (&goo_light_groups_buf)[];
  GooMaterialLightGroups get(uint index) const
  {
    /* Default pixels don't need an SSBO fetch. Empty masks are NOT the default. */
    if (index == 0u) {
      return goo_groups_default();
    }
    return goo_light_groups_buf[index];
  }
};
struct GooCaptureGroups {
  [[push_constant]] int4 goo_capture_lighting;
  [[push_constant]] int4 goo_capture_shadows;
};
struct GooMaterialGroups {
  [[push_constant]] int goo_material_groups_id;
};
GooMaterialLightGroups goo_surface_groups()
{
  [[resource_table]] const GooLightGroups &groups = resource_table_get(eevee::GooLightGroups);
  [[resource_table]] const GooMaterialGroups &material = resource_table_get(
      eevee::GooMaterialGroups);
  return groups.get(uint(material.goo_material_groups_id));
}
}  // namespace eevee
