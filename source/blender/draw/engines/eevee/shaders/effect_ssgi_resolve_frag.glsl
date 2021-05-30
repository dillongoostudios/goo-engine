
#pragma BLENDER_REQUIRE(common_math_lib.glsl)
#pragma BLENDER_REQUIRE(common_math_geom_lib.glsl)
#pragma BLENDER_REQUIRE(closure_eval_glossy_lib.glsl)
#pragma BLENDER_REQUIRE(closure_eval_lib.glsl)
#pragma BLENDER_REQUIRE(lightprobe_lib.glsl)
#pragma BLENDER_REQUIRE(bsdf_common_lib.glsl)
#pragma BLENDER_REQUIRE(surface_lib.glsl)
#pragma BLENDER_REQUIRE(effect_reflection_lib.glsl)
#pragma BLENDER_REQUIRE(common_colorpacking_lib.glsl)

/* Based on:
 * "Stochastic Screen Space Reflections"
 * by Tomasz Stachowiak.
 * https://www.ea.com/frostbite/news/stochastic-screen-space-reflections
 * and
 * "Stochastic all the things: raytracing in hybrid real-time rendering"
 * by Tomasz Stachowiak.
 * https://media.contentapi.ea.com/content/dam/ea/seed/presentations/dd18-seed-raytracing-in-hybrid-real-time-rendering.pdf
 * and
 * adapted to do diffuse reflections only, based on the default Eevee SSR implementation.
 */

uniform sampler2D colorBuffer;
uniform sampler2D normalBuffer;
uniform sampler2D specroughBuffer;
uniform sampler2D hitBuffer;
uniform sampler2D hitDepth;
uniform sampler2D ssgiHitBuffer;
uniform sampler2D ssgiHitDepth;


uniform int samplePoolOffset;

in vec4 uvcoordsvar;

//layout(location = 4) out vec4 ssgiFilterInput;

out vec4 fragColor;

vec4 ssgi_get_scene_color_and_mask(vec3 hit_vP, float mip)
{
  vec2 uv;
  /* Find hit position in previous frame. */
  /* TODO Combine matrices. */
  vec3 hit_P = transform_point(ViewMatrixInverse, hit_vP);
  /* TODO real reprojection with motion vectors, etc... */
  uv = project_point(pastViewProjectionMatrix, hit_P).xy * 0.5 + 0.5;

  vec3 color;
  color = textureLod(colorBuffer, uv * hizUvScale.xy, mip).rgb;

  /* Clamped brightness. */
  float luma = max_v3(color);
  color *= 1.0 - max(0.0, luma - ssrDiffuseClamp) * safe_rcp(luma);

  float mask = screen_border_mask(uv);
  return vec4(color, mask);
}

void resolve_reflection_sample(vec2 sample_uv,
                               vec3 vP,
                               vec3 vN,
                               vec3 vV,
                               float roughness_squared, //hardcoded constant
                               float cone_tan, //hardcoded constant
                               inout float weight_accum,
                               inout vec4 ssgi_accum)
{
  vec4 hit_data = texture(ssgiHitBuffer, sample_uv);
  float hit_depth = texture(ssgiHitDepth, sample_uv).r;
  SsgiHitData data = ssgi_decode_hit_data(hit_data, hit_depth);

  float hit_dist = length(data.hit_dir);

  /* Slide 54. */
  float bsdf = bsdf_ggx(vN, data.hit_dir / hit_dist, vV, roughness_squared); /* TODO - Cosine */

  float weight = bsdf * data.ray_pdf_inv;

  /* Do not add light if ray has failed but still weight it. */
  if (!data.is_hit) {
    weight_accum += weight;
    return;
  }

  vec3 hit_vP = vP + data.hit_dir;

  /* Compute cone footprint in screen space. */
  float cone_footprint = hit_dist * cone_tan;
  float homcoord = ProjectionMatrix[2][3] * hit_vP.z + ProjectionMatrix[3][3];
  cone_footprint *= max(ProjectionMatrix[0][0], ProjectionMatrix[1][1]) / homcoord;
  cone_footprint *= ssrDiffuseResolveBias * 0.5;
  /* Estimate a cone footprint to sample a corresponding mipmap level. */
  float mip = log2(cone_footprint * max_v2(vec2(textureSize(specroughBuffer, 0))));

  vec4 radiance_mask = ssgi_get_scene_color_and_mask(hit_vP, mip);

  ssgi_accum += radiance_mask * weight;
  weight_accum += weight;
}

vec4 raytrace_resolve_diffuse(vec4 difcol_roughness, vec3 viewPosition, vec3 worldPosition, vec3 worldNormal, out vec4 ssgi_resolve)
{
  float roughness = 1.0;

  vec4 ssgi_accum = vec4(0.0);
  vec4 ssgi_radiance = vec4(0.0);
  float weight_acc = 0.0;

  vec3 V, P, N;

  V = viewPosition;
  P = worldPosition;
  N = worldNormal;

  /* Using view space */
  vec3 vV = transform_direction(ViewMatrix, V);
  vec3 vP = transform_point(ViewMatrix, P);
  vec3 vN = transform_direction(ViewMatrix, N);

  float roughness_squared = 1.0; //Could hardcode in sample resolve
  float cone_cos = 11.0352189249; /* const 11.0352189249 // cone_cosine(roughness_squared) - GGX */ //Can be skipped since only tan is used in sample resolve
  float cone_tan = 0.9958856386; /* const 0.9958856386 // sqrt(1.0 - cone_cos * cone_cos) / cone_cos - GGX */ //Could hardcode precalculated value into sample resolve

  int sample_pool = int((uint(gl_FragCoord.x) & 1u) + (uint(gl_FragCoord.y) & 1u) * 2u);
  sample_pool = (sample_pool + (samplePoolOffset / 5)) % 4;

  for (int i = 0; i < resolve_samples_count; i++) {
    int sample_id = sample_pool * resolve_samples_count + i;
    vec2 texture_size = vec2(textureSize(ssgiHitBuffer, 0));
    vec2 sample_texel = texture_size * uvcoordsvar.xy * ssrUvScale;
    vec2 sample_uv = (sample_texel + resolve_sample_offsets[sample_id]) / texture_size;

    resolve_reflection_sample(
        sample_uv, vP, vN, vV, roughness_squared, cone_tan, weight_acc, ssgi_accum);
  }

  /* Compute SSR contribution */
  ssgi_accum *= safe_rcp(weight_acc);

  ssgi_radiance.rgb = ssgi_accum.rgb * ssgi_accum.a;
  ssgi_accum -= ssgi_accum.a; //Unused at this point, leaving in for potentintial future use

  ssgi_resolve = vec4(ssgi_radiance.rgb * (difcol_roughness.rgb * mix(1.0, difcol_roughness.a, ssrDiffuseAo) * ssrDiffuseIntensity), 1.0);
  //ssgi_resolve = vec4(ssgi_radiance.rgb, 1.0); // Out for filter
  return ssgi_resolve;
}

void main()
{

  if (ssrDiffuseIntensity == 0.0) { //Do check in screen_raytrace.c or multilpy the brdf check with this
    discard;
  }

  float depth = textureLod(maxzBuffer, uvcoordsvar.xy * hizUvScale.xy, 0.0).r;

  if (depth == 1.0) {
    discard;
  }

  ivec2 texel = ivec2(gl_FragCoord.xy);
  vec4 speccol_roughness = texelFetch(specroughBuffer, texel, 0).rgba;
  /* unpack A for Spec, B for Diffuse */ // TODO Separate input buffers
  vec4 difcol_roughness = vec4(0.0);
  unpackVec4(speccol_roughness, speccol_roughness, difcol_roughness);

  vec3 brdf = difcol_roughness.rgb; //pi canceled out //TODO Apply brdf in main or in filter shader
  float roughness = 1.0;

  if (max_v3(brdf) <= 0.0) { //Could consider AO and intensity also 
    discard;
  }

  FragDepth = depth;

  viewPosition = get_view_space_from_depth(uvcoordsvar.xy, depth);
  worldPosition = transform_point(ViewMatrixInverse, viewPosition);

  vec2 normal_encoded = texelFetch(normalBuffer, texel, 0).rg;
  viewNormal = normal_decode(normal_encoded, viewCameraVec(viewPosition));
  worldNormal = transform_direction(ViewMatrixInverse, viewNormal);

  vec4 ssgi_resolve = vec4(0.0);
  ssgi_resolve = raytrace_resolve_diffuse(difcol_roughness, viewPosition, worldPosition, worldNormal, ssgi_resolve);

  //ssgiFilterInput = vec4(ssgi_resolve.rgb, 1.0); //Output to filter step
  fragColor = vec4(ssgi_resolve.rgb, 1.0); //Combined with masking from direct lighting could derive additional large scale AO from this? Would be unstable, but could be useful with world lighting with some softer fallback
}
