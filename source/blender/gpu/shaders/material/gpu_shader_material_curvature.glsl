
void node_screenspace_curvature(float iiterations, float sample_scale, float clamp_dist, vec3 scale, out float scene_curvature, out float scene_rim) {
    screenspace_curvature(iiterations, sample_scale, clamp_dist, scale, scene_curvature, scene_rim);
}
