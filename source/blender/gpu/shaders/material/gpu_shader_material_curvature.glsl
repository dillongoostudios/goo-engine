
void node_screenspace_curvature(float iiterations, float sample_scale, float clamp_dist, float scale_x, float scale_y, out float scene_curvature, out float scene_rim) {
    screenspace_curvature(iiterations, sample_scale, clamp_dist, scale_x, scale_y, scene_curvature, scene_rim);
}
