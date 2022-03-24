#pragma BLENDER_REQUIRE(raytrace_lib.glsl)

vec2 rotate(vec2 v, float a) {
	float s = sin(a);
	float c = cos(a);
	mat2 m = mat2(c, -s, s, c);
	return m * v;
}

/* Cavity sampling:
 * 
 * Sample a straight line of n_samples points in a fixed range (from sample_scale), then repeat in a rotating pattern.
 * Rotate all samples by the hash offset to reduce star shaped banding artifacts. 
 * 
 * Curvature is determined as the sum of curvature of each pair of samples (+ve and -ve along the line), with samples weighted 
 * inversely by distance from center. */
void node_screenspace_curvature(float iiterations, float sample_scale, float clamp_dist, out float scene_curvature, out float scene_rim) {
    vec2 uvs = get_uvs_from_view(viewPosition) * hizUvScale.xy;

    // Use a fixed texel size rather than adjusting to pixel space. Less accurate, wastes some samples, but gives more intuitive results.
    //vec2 texel_size = vec2(abs(dFdx(uvs.x)), abs(dFdy(uvs.y)));
    vec2 texel_size = vec2(1.0 / 1920, 1.0 / 1080);

    // Using the "real" depth here makes the precision issues even worse... something's not right here.
    float mid_depth = get_view_z_from_depth(textureLod(maxzBuffer, uvs, 0.0).r);
    // float mid_depth = viewPos.z;

    // Eyeballed value to clamp curvature sample separation to.
    float clamp_range = 0.001;
    int n_samples = int(iiterations);
    float i_samples = (64.0 / n_samples);

    // Curvature accumulation
    float accum = 0.0;
    float rim_accum = 0.0;

    // Rotate in 8x 22.5° increments, sample lines
    for (int r = 0; r < 8; r++) {
        vec2 offset = rotate(vec2(1.0, 0.0), (r + alphaHashOffset) * 3.1415 * 0.25 * 0.5) * texel_size * sample_scale;

        // Accumulate curvature in the line window
        for (int i = 1; i <= n_samples; i++) {
            float left = get_view_z_from_depth(textureLod(maxzBuffer, uvs + offset * i * i_samples, 0.0).r);
            float right = get_view_z_from_depth(textureLod(maxzBuffer, uvs - offset * i * i_samples, 0.0).r);

            float curve = clamp(left - mid_depth, -clamp_range, clamp_range) + clamp(right - mid_depth, -clamp_range, clamp_range);
            float afac = (1 - float(i-1) / n_samples);


            // Absolute distance between both samples. If greater than the clamp range, then reduce the influence.
            float ad = max(abs(max(left, mid_depth) - max(right, mid_depth)) - clamp_dist, 0.0);

            accum += curve * afac * 0.001;// * (max((clamp_dist - ad), 0.0) / clamp_dist);
            rim_accum += min(mid_depth - min(left, right), clamp_dist) * afac; 
        }
    }

    scene_curvature = - accum / length(texel_size) * i_samples;
    scene_rim = rim_accum / sample_scale * clamp_range;
}
