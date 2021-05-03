#pragma BLENDER_REQUIRE(raytrace_lib.glsl)

void node_screenspace_info(vec3 viewPos, out vec4 scene_col, out float scene_depth) {
    vec2 uvs = get_uvs_from_view(viewPos * vec3(1.0, 1.0, -1.0));

    scene_col = texture(refractColorBuffer, uvs * hizUvScale.xy);

    float depth = textureLod(maxzBuffer, uvs * hizUvScale.xy, 0.0).r;
    scene_depth = -get_view_z_from_depth(depth);
}