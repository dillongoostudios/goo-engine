#pragma BLENDER_REQUIRE(lights_lib.glsl)
#pragma BLENDER_REQUIRE(lightprobe_lib.glsl)


float calc_self_shadows_only(LightData ld, vec3 P, vec4 l_vector) 
{
  float vis = light_attenuation(ld, l_vector);
  if (ld.l_shadowid >= 0.0 && vis > 0.001) {
    if (ld.l_type == SUN) {
      vis *= sample_cascade_shadow(int(ld.l_shadowid), P, false);
    }
    else {
      vis *= sample_cube_shadow(int(ld.l_shadowid), P, false);
    }
  }
  return vis;
}


void node_shader_info(vec3 position, vec3 normal, 
    out vec4 half_light, out float shadows, out float self_shadows, out vec4 ambient) {
    ClosureEvalCommon cl_common = closure_Common_eval_init(CLOSURE_INPUT_COMMON_DEFAULT);
    cl_common.P = position;
    vec3 n_n = normalize(normal);

    float shadow_accum = 0.0;
    float self_shadow_accum = 0.0;
    int light_accum = 0;
    half_light = vec4(0.0);

    for (int i = 0; i < laNumLight && i < MAX_LIGHT; i++) {
        ClosureLightData light = closure_light_eval_init(cl_common, i);
        if (
          (ld.light_group_bits.x & lightGroups.x) == 0
          && (ld.light_group_bits.y & lightGroups.y) == 0
          && (ld.light_group_bits.z & lightGroups.z) == 0
          && (ld.light_group_bits.w & lightGroups.w) == 0) 
          {
            continue;
        }
        // shadows *= light.data.l_color * (light.data.l_diff * light.vis * light.contact_shadow);
        shadow_accum += (1 - light.vis);
        self_shadow_accum += (1 - calc_self_shadows_only(light.data, position, light.L));
        light_accum++;

        float radiance = light_diffuse(light.data, n_n, cl_common.V, light.L);
        half_light += vec4(light.data.l_color * light.data.l_diff * radiance, 0.0);
    }

    shadows = (1 - (shadow_accum / max(light_accum, 1)));
    self_shadows = (1 - (self_shadow_accum / max(light_accum, 1)));
    ambient = vec4(probe_evaluate_world_diff(normal), 1.0);
}
