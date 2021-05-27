#pragma BLENDER_REQUIRE(closure_type_lib.glsl)

void node_combine_closure(vec4 color, float alpha, float holdout, out Closure cl)
{
#ifdef VOLUMETRICS
    return CLOSURE_DEFAULT;
#else
    int flag = int(abs(ObjectInfo.w));
    if ((flag & DRW_BASE_HOLDOUT) != 0) {
        holdout = 1.0;
    }

    cl = CLOSURE_DEFAULT;
    cl.holdout = holdout;

#ifdef USE_ALPHA_BLEND
    cl.holdout *= alpha;

    cl.transmittance = mix(vec3(1 - alpha), vec3(1.0), holdout);
    cl.radiance = mix(color.xyz * alpha, vec3(0.0), holdout);
#else
    cl.radiance = color.xyz * alpha * alpha * (1 - holdout);
    cl.transmittance = vec3(1 - alpha);
#endif /* USE_ALPHA_BLEND */

    if (holdout > 0.0) {
        cl.flag = CLOSURE_HOLDOUT_FLAG;
    }

#endif /* VOLUMETRICS */
}
