#pragma BLENDER_REQUIRE(lights_lib.glsl)
#pragma BLENDER_REQUIRE(raytrace_lib.glsl)
#pragma BLENDER_REQUIRE(lightprobe_lib.glsl)


void node_shader_info(vec3 position, vec3 normal, vec3 misc, out float shadow_map, out vec4 scene_color, out vec3 scene_pos, out vec4 reflection) {
    ClosureEvalCommon cl_common = closure_Common_eval_init(CLOSURE_INPUT_COMMON_DEFAULT);
    cl_common.P = position;
    shadow_map = 1.0;
    for (int i = 0; i < laNumLight && i < MAX_LIGHT; i++) {
        ClosureLightData light = closure_light_eval_init(cl_common, i);
        shadow_map *= light.vis;
    }
    
    vec2 uv = get_uvs_from_view(position);

    scene_color.xyz = texture(refractColorBuffer, normal.xy * hizUvScale.xy).xyz;
    float depth = texture(maxzBuffer, normal.xy * hizUvScale.xy).x;
    scene_pos = get_view_space_from_depth(normal.xy * hizUvScale.xy, depth);

    reflection = texture(probePlanars, vec3(normal.xy, misc.x));
}