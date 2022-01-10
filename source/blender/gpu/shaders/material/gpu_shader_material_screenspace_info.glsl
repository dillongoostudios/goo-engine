#pragma BLENDER_REQUIRE(raytrace_lib.glsl)

void node_screenspace_info(vec3 viewPos, out vec4 scene_col, out float scene_depth, out float unused) {
    vec2 uvs = get_uvs_from_view(viewPos * vec3(1.0, 1.0, -1.0));

    scene_col = texture(refractColorBuffer, uvs * hizUvScale.xy);

    float depth = textureLod(maxzBuffer, uvs * hizUvScale.xy, 0.0).r;
    scene_depth = -get_view_z_from_depth(depth);
}

/*

Cross sampling:

X       X
 \     /
  X   X 
   \ /
    X
   / \
  X   X
 /     \
X       X

Side view (cavity sample)

X       X <- curvature 0.5
 \     /
  X   X <- curvature 1
  \\ //
    X <- centerpoint
*/

vec2 rotate(vec2 v, float a) {
	float s = sin(a);
	float c = cos(a);
	mat2 m = mat2(c, -s, s, c);
	return m * v;
}

void node_screenspace_curvature(vec3 viewPos, float iclamprange, float iiterations, float iiterfac, out vec4 scene_col, out float scene_depth, out float scene_curvature) {
    node_screenspace_info(viewPos, scene_col, scene_depth, scene_curvature);
    vec2 uvs = get_uvs_from_view(viewPos * vec3(1.0, 1.0, -1.0)) * hizUvScale.xy;
    vec2 texel_size = vec2(abs(dFdx(uvs.x)), abs(dFdy(uvs.y)));

    float mid_depth = textureLod(maxzBuffer, uvs, 0.0).r;

    int sample_radius = int(iiterations);
    float infl = 1.0;
    float accum = 0.0;

    float clamp_range = iclamprange;

    // vec2 offset_x = rotate(vec2(texel_size.x, 0.0), alphaHashOffset * 3.1415 * 2.0);
    // vec2 offset_y = rotate(vec2(0.0, texel_size.y), alphaHashOffset * 3.1415 * 2.0);

    vec2 offset_x = vec2(texel_size.x, 0.0);
    vec2 offset_y = vec2(0.0, texel_size.y);

    // Accumulate curvature in cross sample
    for (int i = 1; i <= sample_radius; i++) {
        float xp = textureLod(maxzBuffer, uvs + offset_x * i, 0.0).r;
        float xn = textureLod(maxzBuffer, uvs - offset_x * i, 0.0).r;

        float yp = textureLod(maxzBuffer, uvs + offset_y * i, 0.0).r;
        float yn = textureLod(maxzBuffer, uvs - offset_y * i, 0.0).r;

        float cx = clamp(xp - mid_depth, -clamp_range, clamp_range) + clamp(xn - mid_depth, -clamp_range, clamp_range);
        float cy = clamp(yp - mid_depth, -clamp_range, clamp_range) + clamp(yn - mid_depth, -clamp_range, clamp_range);

        // float cx = cp + cn - (2 * mid_depth);
        // float cy = yp + yn - (2 * mid_depth);
        float c_total = (cx + cy) * infl;
        accum += c_total;
        infl *= iiterfac;
    }

    scene_curvature = accum / length(texel_size);
}   
