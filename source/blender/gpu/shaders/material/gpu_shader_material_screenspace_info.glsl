
void node_screenspace_info(float3 viewPos, out float4 scene_col, out float scene_depth) {
#ifndef EEVEE_ENGINE
    return;
#else
    screenspace_info(viewPos, scene_col, scene_depth);
#endif
}
