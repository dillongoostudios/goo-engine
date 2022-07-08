#ifdef GPU_VERTEX_SHADER
    #define FRAG_DEPTH float _frag_depth
    #else
    #define FRAG_DEPTH gl_FragDepth
#endif

void view_z_get(out float z)
{
    z = abs(viewPosition.z);
}

void node_set_depth(in Closure _in, in float z_in, out Closure _out)
{
    _out = _in;

    float z_clipped = max(min(-z_in, ViewNear), ViewFar);
    FRAG_DEPTH = get_depth_from_view_z(z_clipped);
}

#undef FRAG_DEPTH
