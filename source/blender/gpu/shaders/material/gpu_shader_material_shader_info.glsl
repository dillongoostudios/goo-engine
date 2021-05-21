#pragma BLENDER_REQUIRE(lights_lib.glsl)
#pragma BLENDER_REQUIRE(lightprobe_lib.glsl)


void node_shader_info(vec3 position, vec3 normal, 
    out vec4 half_light, out vec4 shadows, out vec4 ambient) {
    ClosureEvalCommon cl_common = closure_Common_eval_init(CLOSURE_INPUT_COMMON_DEFAULT);
    cl_common.P = position;
    vec3 n_n = normalize(normal);

    float shadow_accum = 0.0;
    int light_accum = 0;
    half_light = vec4(0.0);

    for (int i = 0; i < laNumLight && i < MAX_LIGHT; i++) {
        ClosureLightData light = closure_light_eval_init(cl_common, i);
        if ((light.data.light_group_bits.x & lightGroups) == 0) {
            continue;
        }
        // shadows *= light.data.l_color * (light.data.l_diff * light.vis * light.contact_shadow);
        shadow_accum += (1 - light.vis);
        light_accum++;

        float radiance = light_diffuse(light.data, n_n, cl_common.V, light.L);
        half_light += vec4(light.data.l_color * light.data.l_diff * radiance, 0.0);
    }

    shadows = vec4(1 - (shadow_accum / max(light_accum, 1)));
    ambient = vec4(probe_evaluate_world_diff(normal), 1.0);
}
