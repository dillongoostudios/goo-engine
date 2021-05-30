
#pragma BLENDER_REQUIRE(common_math_lib.glsl)
#pragma BLENDER_REQUIRE(common_math_geom_lib.glsl)
#pragma BLENDER_REQUIRE(closure_eval_glossy_lib.glsl) //
#pragma BLENDER_REQUIRE(closure_eval_lib.glsl) //
#pragma BLENDER_REQUIRE(lightprobe_lib.glsl) //
#pragma BLENDER_REQUIRE(bsdf_common_lib.glsl) //
#pragma BLENDER_REQUIRE(surface_lib.glsl) //
#pragma BLENDER_REQUIRE(effect_reflection_lib.glsl) //
#pragma BLENDER_REQUIRE(common_colorpacking_lib.glsl)

/* SSGI Bilateral filtering */
/* TODO */

uniform sampler2D colorBuffer;
uniform sampler2D normalBuffer;
uniform sampler2D specroughBuffer;

uniform sampler2D hitBuffer;
uniform sampler2D hitDepth;
uniform sampler2D ssgiHitBuffer;
uniform sampler2D ssgiHitDepth;

uniform sampler2D ssgiFilterInput;

in vec4 uvcoordsvar;

out vec4 fragColor;

/* bilateral filter */

/* TEMP filtering stripped */

void main()
{
//   float depth = textureLod(maxzBuffer, uvcoordsvar.xy * hizUvScale.xy, 0.0).r;

//   vec4 input = vec4(0.0);

//   if (depth == 1.0) {
//     discard;
//   }

//   ivec2 texel = ivec2(gl_FragCoord.xy);
//   vec4 speccol_roughness = texelFetch(specroughBuffer, texel, 0).rgba;
//   /* unpack A for Spec, B for Diffuse */ // TODO Separate input buffers
//   vec4 difcol_roughness = vec4(0.0);
//   unpackVec4(speccol_roughness, speccol_roughness, difcol_roughness);

//   if (max_v3(difcol_roughness.rgb) <= 0.0) {
//     discard;
//   }

//   vec4 filtered = vec4(0.0);
//   filtered = vec4(doBlur(uvcoordsvar.xy, ssgiFilterInput, maxzBuffer), 1.0);

//   fragColor = vec4(filtered.rgb * (difcol_roughness.rgb * mix(1.0,difcol_roughness.a,ssrDiffuseAo) * ssrDiffuseIntensity), 1.0);
    discard;
}
