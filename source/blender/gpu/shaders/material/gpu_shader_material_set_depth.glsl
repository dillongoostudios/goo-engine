#ifdef GPU_VERTEX_SHADER
    #define FRAG_DEPTH float _frag_depth
#else
    #define FRAG_DEPTH gl_FragDepth
#endif

void view_z_get(out float z)
{
#ifndef EEVEE_ENGINE
    return;
#else
    z = abs(viewPosition.z);
#endif
}

void node_set_depth(in Closure _in, in float z_in, out Closure _out)
{
#ifndef EEVEE_ENGINE
    return;
#else
    _out = _in;
    float viewNear = get_view_z_from_depth(0.0f);
    float viewFar = get_view_z_from_depth(1.0f);
    float z_clipped = max(min(-z_in, viewNear), viewFar);
    FRAG_DEPTH = get_depth_from_view_z(z_clipped);
#endif
}

#undef FRAG_DEPTH
