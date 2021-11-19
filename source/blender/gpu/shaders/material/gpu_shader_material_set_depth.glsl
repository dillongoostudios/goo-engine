#pragma BLENDER_REQUIRE(common_view_lib.glsl)

void view_z_get(out float z)
{
    z = abs(viewPosition.z);
}

void node_set_depth(in Closure _in, in float z_in, out Closure _out)
{
    _out = _in;

    /* Alpha hash must be enabled for depth prepass to use the shader. */
    /* Shader compiler *really* doesn't like using a #ifdef here, unfortunately. */
    float z_clipped = max(min(-z_in, ViewNear), ViewFar);
    gl_FragDepth = get_depth_from_view_z(z_clipped);
}
