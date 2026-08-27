/* SPDX-FileCopyrightText: 2019 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup EEVEE
 */

#include "eevee_private.hh"

#include "BLI_math_rotation.h"
#include "BLI_math_geom.h"
#include "BLI_string.h"

#include "GPU_debug.hh"
#include "GPU_texture.hh"

#include <cstdio>
#include <cstdlib>

void EEVEE_shadows_cube_add(EEVEE_LightsInfo *linfo, EEVEE_Light *evli, Object *ob)
{
  if (linfo->cube_len >= MAX_SHADOW_CUBE) {
    return;
  }

  const Light *la = (Light *)ob->data;
  EEVEE_Shadow *sh_data = linfo->shadow_data + linfo->shadow_len;

  /* Always update dupli lights as EEVEE_LightEngineData is not saved.
   * Same issue with dupli shadow casters. */
  bool update = (ob->base_flag & BASE_FROM_DUPLI) != 0;
  if (!update) {
    EEVEE_LightEngineData *led = EEVEE_light_data_ensure(ob);
    if (led->need_update) {
      update = true;
      led->need_update = false;
    }
  }

  if (update) {
    BLI_BITMAP_ENABLE(&linfo->sh_cube_update[0], linfo->cube_len);
  }

  sh_data->near = max_ff(la->clipsta, 1e-8f);
  sh_data->bias = max_ff(la->bias * 0.05f, 0.0f);
  eevee_contact_shadow_setup(la, sh_data);

  /* Saving light bounds for later. */
  BoundSphere *cube_bound = linfo->shadow_bounds + linfo->cube_len;
  copy_v3_v3(cube_bound->center, evli->position);
  cube_bound->radius = sqrt(1.0f / min_ff(evli->invsqrdist, evli->invsqrdist_volume));

  linfo->shadow_cube_light_indices[linfo->cube_len] = linfo->num_light;
  evli->shadow_id = linfo->shadow_len++;
  sh_data->type_data_id = linfo->cube_len++;

  /* Same as linfo->cube_len, no need to save. */
  linfo->num_cube_layer++;
}

static void shadow_cube_random_position_set(const EEVEE_Light *evli,
                                            int sample_ofs,
                                            float ws_sample_pos[3])
{
  float jitter[3];
#ifdef DEBUG_SHADOW_DISTRIBUTION
  int i = 0;
start:
#else
  int i = sample_ofs;
#endif
  switch (int(evli->light_type)) {
    case LA_AREA:
      EEVEE_sample_rectangle(i, evli->rightvec, evli->upvec, evli->sizex, evli->sizey, jitter);
      break;
    case int(LAMPTYPE_AREA_ELLIPSE):
      EEVEE_sample_ellipse(i, evli->rightvec, evli->upvec, evli->sizex, evli->sizey, jitter);
      break;
    default:
      EEVEE_sample_ball(i, evli->radius, jitter);
  }
#ifdef DEBUG_SHADOW_DISTRIBUTION
  float p[3];
  add_v3_v3v3(p, jitter, ws_sample_pos);
  DRW_debug_sphere(p, 0.01f, blender::float4{1.0f, (sample_ofs == i) ? 1.0f : 0.0f, 0.0f, 1.0f});
  if (i++ < sample_ofs) {
    goto start;
  }
#endif
  add_v3_v3(ws_sample_pos, jitter);
}

bool EEVEE_shadows_cube_setup(EEVEE_LightsInfo *linfo, const EEVEE_Light *evli, int sample_ofs)
{
  EEVEE_Shadow *shdw_data = linfo->shadow_data + int(evli->shadow_id);
  EEVEE_ShadowCube *cube_data = linfo->shadow_cube_data + int(shdw_data->type_data_id);

  eevee_light_matrix_get(evli, cube_data->shadowmat);

  shdw_data->far = max_ff(sqrt(1.0f / min_ff(evli->invsqrdist, evli->invsqrdist_volume)), 3e-4);
  shdw_data->near = min_ff(shdw_data->near, shdw_data->far - 1e-4);

  bool update = false;

  if (linfo->soft_shadows) {
    shadow_cube_random_position_set(evli, sample_ofs, cube_data->shadowmat[3]);
    /* Update if position changes (avoid infinite update if soft shadows does not move).
     * Other changes are caught by depsgraph tagging. This one is for update between samples. */
    update = !compare_v3v3(cube_data->shadowmat[3], cube_data->position, 1e-10f);
    /**
     * Anti-Aliasing jitter: Add random rotation.
     *
     * The 2.0 factor is because texel angular size is not even across the cube-map,
     * so we make the rotation range a bit bigger.
     * This will not blur the shadow even if the spread is too big since we are just
     * rotating the shadow cube-map.
     * Note that this may be a rough approximation an may not converge to a perfectly
     * smooth shadow (because sample distribution is quite non-uniform) but is enough
     * in practice.
     */
    /* NOTE: this has implication for spotlight rendering optimization
     * (see EEVEE_shadows_draw_cubemap). */
    float angular_texel_size = 2.0f * DEG2RADF(90) / float(linfo->shadow_cube_size);
    EEVEE_random_rotation_m4(sample_ofs, angular_texel_size, cube_data->shadowmat);
  }

  copy_v3_v3(cube_data->position, cube_data->shadowmat[3]);
  invert_m4(cube_data->shadowmat);

  return update;
}

static void eevee_ensure_cube_views(
    float near, float far, int cube_res, const float viewmat[4][4], DRWView *view[6])
{
  float winmat[4][4];
  float side = near;

  /* TODO: shadow-cube array. */
  if (true) {
    /* This half texel offset is used to ensure correct filtering between faces. */
    /* FIXME: This exhibit float precision issue with lower cube_res.
     * But it seems to be caused by the perspective_m4. */
    side *= (float(cube_res) + 1.0f) / float(cube_res);
  }

  perspective_m4(winmat, -side, side, -side, side, near, far);

  /* NOTE: Do NOT pre-adjust winmat for Metal depth range [0,1].
   * mtl_shader_generator inserts z=(z+w)/2 automatically into the shadow vertex shader,
   * converting OpenGL NDC [-1,1] to Metal NDC [0,1]. Pre-adjusting here causes a
   * double-transform that compresses shadow depths to [0.5, 1.0], breaking comparisons.
   * buffer_depth() already returns [0,1] values matching the Metal shadow map. */
  for (int i = 0; i < 6; i++) {
    float tmp[4][4];
    mul_m4_m4m4(tmp, cubefacemat[i], viewmat);

    if (view[i] == nullptr) {
      view[i] = DRW_view_create(tmp, winmat, nullptr, nullptr, nullptr);
    }
    else {
      DRW_view_update(view[i], tmp, winmat, nullptr, nullptr);
    }
  }
}

/* Does a spot angle fits a single cube-face. */
static bool spot_angle_fit_single_face(const EEVEE_Light *evli)
{
  /* alpha = spot/cone half angle. */
  /* beta = scaled spot/cone half angle. */
  float cos_alpha = evli->spotsize;
  float sin_alpha = sqrtf(max_ff(0.0f, 1.0f - cos_alpha * cos_alpha));
  float cos_beta = min_ff(cos_alpha / hypotf(cos_alpha, sin_alpha * evli->sizex),
                          cos_alpha / hypotf(cos_alpha, sin_alpha * evli->sizey));
  /* Don't use 45 degrees because AA jitter can offset the face. */
  return cos_beta > cosf(DEG2RADF(42.0f));
}

void EEVEE_shadows_draw_cubemap(EEVEE_ViewLayerData *sldata, EEVEE_Data *vedata, int cube_index)
{
  EEVEE_PassList *psl = vedata->psl;
  EEVEE_StorageList *stl = vedata->stl;
  EEVEE_PrivateData *g_data = stl->g_data;
  EEVEE_LightsInfo *linfo = sldata->lights;

  EEVEE_Light *evli = linfo->light_data + linfo->shadow_cube_light_indices[cube_index];
  EEVEE_Shadow *shdw_data = linfo->shadow_data + int(evli->shadow_id);
  EEVEE_ShadowCube *cube_data = linfo->shadow_cube_data + int(shdw_data->type_data_id);

  eevee_ensure_cube_views(shdw_data->near,
                          shdw_data->far,
                          linfo->shadow_cube_size,
                          cube_data->shadowmat,
                          g_data->cube_views);

  /* Render shadow cube */
  /* Render 6 faces separately: seems to be faster for the general case.
   * The only time it's more beneficial is when the CPU culling overhead
   * outweigh the instancing overhead. which is rarely the case. */
  /* Face name lookup for Xcode debug labels. */
  static const char *face_names[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};

  for (int j = 0; j < 6; j++) {
    /* Optimization: Only render the needed faces. */
    /* Skip all but -Z face. */
    if (ELEM(evli->light_type, LA_SPOT, LAMPTYPE_SPOT_DISK) && j != 5 &&
        spot_angle_fit_single_face(evli))
    {
      continue;
    }
    /* Skip +Z face. */
    if (!ELEM(evli->light_type, LA_LOCAL, LAMPTYPE_OMNI_DISK) && j == 4) {
      continue;
    }
    /* TODO(fclem): some cube sides can be invisible in the main views. Cull them. */
    // if (frustum_intersect(g_data->cube_views[j], main_view))
    //   continue;

    int layer = cube_index * 6 + j;

    /* Debug label visible in Xcode Metal Frame Debugger.
     * HOW TO FIND: Filter Command Encoders by "GOOENG:ShadowCube:ZClipTest".
     * Select face[5]=-Z layer[5] (the face where trapezoid acne appears).
     *
     * TEST B — z_clip sign error diagnosis:
     *   1. Click the draw call → right panel "Bound Resources" → Vertex Function
     *   2. Open Geometry Viewer (triangle icon in toolbar)
     *   3. Click any vertex on the Cube top face
     *   4. Check "gl_Position" (before injection) or "output._default_position_" (after):
     *        gl_Position.z:
     *          NORMAL  ≈ +6.34  → injection → out.z ≈ 6.38 → stored 0.993 ✓
     *          BROKEN  ≈ -6.34  → injection → out.z ≈ 0.04 → stored 0.006 ← root cause
     *   5. Depth Attachment → Texture Viewer → Slice=layer → verify stored depth:
     *        Normal: ≈ 0.993-0.994 | Broken: ≈ 0.001-0.006
     *   6. Bound Resources → "draw_view" UBO → ProjectionMatrix[2][2]:
     *        Normal: ≈ -1.003 (negative)  |  Broken: ≈ +1.003 (positive = sign error)
     *      ViewMatrix col[2] (3rd column, rows 0-2):
     *        Normal: (0, 0, +1) for Neg-Z face  |  Broken: (0, 0, -1) */
    /* ISS-011 Xcode anchor: this per-face group writes shadowCubePool layer[%d]. The face that
     * carries the trapezoid acne is the -Z face (layer 5). Search "ISS011_SHADOWFACE" then open the
     * Depth Attachment to read the STORED depth for that layer. */
    char dbg_label[160];
    SNPRINTF(dbg_label,
             "ISS011_SHADOWFACE ShadowCube cube[%d] face[%d]=%s layer[%d] (=shadowCubePool render "
             "target)",
             cube_index,
             j,
             face_names[j],
             layer);
    GPU_debug_group_begin(dbg_label);

    DRW_view_set_active(g_data->cube_views[j]);
    GPU_framebuffer_texture_layer_attach(sldata->shadow_fb, sldata->shadow_cube_pool, 0, layer, 0);
    GPU_framebuffer_texture_layer_attach(
        sldata->shadow_fb, sldata->shadow_cube_id_pool, 1, layer, 0);
    GPU_framebuffer_bind(sldata->shadow_fb);
    GPU_framebuffer_clear_depth(sldata->shadow_fb, 1.0f);
    DRW_draw_pass(psl->shadow_pass);

    GPU_debug_group_end();
  }

  /* ISS-011 non-perturbing measurement (session7): read back stored shadow depths and
   * compare to analytic window_z offline. Guarded by env GOO_DUMP_SHADOW so it is inert
   * in normal runs. Read-only — does NOT touch the depth comparison knife-edge (unlike
   * printf, which is a heisenbug here). Dumps the whole cube pool once for cube_index 0. */
  if (cube_index == 0 && getenv("GOO_DUMP_SHADOW") != nullptr) {
    const int res = linfo->shadow_cube_size;
    const int total_layers = max_ii(linfo->num_cube_layer, 1) * 6;
    const bool no_off = getenv("GOO_NO_SHADOW_OFFSET") != nullptr;
    const char *bin_path = no_off ? "/tmp/goo_shadow_dump_nooff.bin" : "/tmp/goo_shadow_dump.bin";
    float *data = static_cast<float *>(GPU_texture_read(sldata->shadow_cube_pool, GPU_DATA_FLOAT, 0));
    if (data != nullptr) {
      FILE *fb = fopen(bin_path, "wb");
      if (fb) {
        fwrite(data, sizeof(float), size_t(res) * res * total_layers, fb);
        fclose(fb);
      }
      FILE *fm = fopen("/tmp/goo_shadow_dump.txt", "w");
      if (fm) {
        fprintf(fm, "res %d\nlayers %d\nnear %.9g\nfar %.9g\nbias %.9g\n",
                res, total_layers, shdw_data->near, shdw_data->far, shdw_data->bias);
        /* world->light matrix (already inverted in eevee_shadow_cube_data_update). */
        for (int r = 0; r < 4; r++) {
          fprintf(fm, "shadowmat %.9g %.9g %.9g %.9g\n",
                  cube_data->shadowmat[r][0], cube_data->shadowmat[r][1],
                  cube_data->shadowmat[r][2], cube_data->shadowmat[r][3]);
        }
        fprintf(fm, "light_pos %.9g %.9g %.9g\n",
                cube_data->position[0], cube_data->position[1], cube_data->position[2]);
        fclose(fm);
      }
      MEM_freeN(data);
      printf("[GOO_DUMP_SHADOW] wrote /tmp/goo_shadow_dump.bin (res=%d layers=%d) "
             "near=%.6g far=%.6g bias=%.6g\n",
             res, total_layers, shdw_data->near, shdw_data->far, shdw_data->bias);
      fflush(stdout);
    }
  }

  BLI_BITMAP_SET(&linfo->sh_cube_update[0], cube_index, false);
}
