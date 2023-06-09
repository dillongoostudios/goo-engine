
#pragma BLENDER_REQUIRE(closure_eval_diffuse_lib.glsl)
#pragma BLENDER_REQUIRE(closure_eval_glossy_lib.glsl)
#pragma BLENDER_REQUIRE(closure_eval_refraction_lib.glsl)
#pragma BLENDER_REQUIRE(closure_eval_translucent_lib.glsl)
#pragma BLENDER_REQUIRE(renderpass_lib.glsl)

#if defined(USE_SHADER_TO_RGBA) || defined(USE_ALPHA_BLEND)
bool do_sss = false;
bool do_ssr = false;
#else
bool do_sss = true;
bool do_ssr = true;
#endif

vec3 out_sss_radiance;
vec3 out_sss_color;
float out_sss_radius;

float out_ssr_roughness;
vec3 out_ssr_color;
vec3 out_ssr_N;

bool aov_is_valid = false;
vec3 out_aov;

bool output_sss(ClosureDiffuse diffuse, ClosureOutputDiffuse diffuse_out)
{
  if (diffuse.sss_id == 0u || !do_sss || !sssToggle || outputSssId == 0) {
    return false;
  }
  if (renderPassSSSColor) {
    return false;
  }
  out_sss_radiance = diffuse_out.radiance;
  out_sss_color = diffuse.color * diffuse.weight;
  out_sss_radius = avg(diffuse.sss_radius);
  do_sss = false;
  return true;
}

bool output_ssr(ClosureReflection reflection)
{
  if (!do_ssr || !ssrToggle || outputSsrId == 0) {
    return false;
  }
  out_ssr_roughness = reflection.roughness;
  out_ssr_color = reflection.color * reflection.weight;
  out_ssr_N = reflection.N;
  do_ssr = false;
  return true;
}

void output_aov(vec4 color, float value, uint hash)
{
  /* Keep in sync with `render_pass_aov_hash` and `EEVEE_renderpasses_aov_hash`. */
  hash <<= 1u;

  if (renderPassAOV && !aov_is_valid && hash == render_pass_aov_hash()) {
    aov_is_valid = true;
    if (render_pass_aov_is_color()) {
      out_aov = color.rgb;
    }
    else {
      out_aov = vec3(value);
    }
  }
}

/* Single BSDFs. */
CLOSURE_EVAL_FUNCTION_DECLARE_1(DiffuseBSDF, Diffuse)
Closure closure_eval(ClosureDiffuse diffuse)
{
  /* Glue with the old system. */
  CLOSURE_VARS_DECLARE_1(Diffuse);

  in_Diffuse_0.N = diffuse.N;
  in_Diffuse_0.albedo = diffuse.color;

  CLOSURE_EVAL_FUNCTION_1(DiffuseBSDF, Diffuse);

  Closure closure = CLOSURE_DEFAULT;
  if (!output_sss(diffuse, out_Diffuse_0)) {
    closure.radiance += out_Diffuse_0.radiance * diffuse.color * diffuse.weight;
  }
  return closure;
}

CLOSURE_EVAL_FUNCTION_DECLARE_1(TranslucentBSDF, Translucent)
Closure closure_eval(ClosureTranslucent translucent)
{
  /* Glue with the old system. */
  CLOSURE_VARS_DECLARE_1(Translucent);

  in_Translucent_0.N = translucent.N;

  CLOSURE_EVAL_FUNCTION_1(TranslucentBSDF, Translucent);

  Closure closure = CLOSURE_DEFAULT;
  closure.radiance += out_Translucent_0.radiance * translucent.color * translucent.weight;
  return closure;
}

CLOSURE_EVAL_FUNCTION_DECLARE_1(GlossyBSDF, Glossy)
Closure closure_eval(ClosureReflection reflection, const bool do_output_ssr)
{
  /* Glue with the old system. */
  CLOSURE_VARS_DECLARE_1(Glossy);

  in_Glossy_0.N = reflection.N;
  in_Glossy_0.roughness = reflection.roughness;

  CLOSURE_EVAL_FUNCTION_1(GlossyBSDF, Glossy);

  Closure closure = CLOSURE_DEFAULT;

  bool output_radiance = true;
  if (do_output_ssr) {
    output_radiance = !output_ssr(reflection);
  }
  if (output_radiance) {
    closure.radiance += out_Glossy_0.radiance * reflection.color * reflection.weight;
  }
  return closure;
}

Closure closure_eval(ClosureReflection reflection)
{
  return closure_eval(reflection, true);
}

CLOSURE_EVAL_FUNCTION_DECLARE_1(RefractionBSDF, Refraction)
Closure closure_eval(ClosureRefraction refraction)
{
  /* Glue with the old system. */
  CLOSURE_VARS_DECLARE_1(Refraction);

  in_Refraction_0.N = refraction.N;
  in_Refraction_0.roughness = refraction.roughness;
  in_Refraction_0.ior = refraction.ior;

  CLOSURE_EVAL_FUNCTION_1(RefractionBSDF, Refraction);

  Closure closure = CLOSURE_DEFAULT;
  closure.radiance += out_Refraction_0.radiance * refraction.color * refraction.weight;
  return closure;
}

Closure closure_eval(ClosureEmission emission)
{
  Closure closure = CLOSURE_DEFAULT;
  closure.radiance += render_pass_emission_mask(emission.emission) * emission.weight;
  return closure;
}

Closure closure_eval(ClosureTransparency transparency)
{
  Closure closure = CLOSURE_DEFAULT;
  closure.transmittance += transparency.transmittance * transparency.weight;
  closure.holdout += transparency.holdout * transparency.weight;
  return closure;
}

/* Glass BSDF. */
CLOSURE_EVAL_FUNCTION_DECLARE_2(GlassBSDF, Glossy, Refraction)
Closure closure_eval(ClosureReflection reflection, ClosureRefraction refraction)
{

#if defined(DO_SPLIT_CLOSURE_EVAL)
  Closure closure = closure_eval(refraction);
  Closure closure_reflection = closure_eval(reflection);
  closure.radiance += closure_reflection.radiance;
  return closure;
#else
  /* Glue with the old system. */
  CLOSURE_VARS_DECLARE_2(Glossy, Refraction);

  in_Glossy_0.N = reflection.N;
  in_Glossy_0.roughness = reflection.roughness;
  in_Refraction_1.N = refraction.N;
  in_Refraction_1.roughness = refraction.roughness;
  in_Refraction_1.ior = refraction.ior;

  CLOSURE_EVAL_FUNCTION_2(GlassBSDF, Glossy, Refraction);

  Closure closure = CLOSURE_DEFAULT;
  closure.radiance += out_Refraction_1.radiance * refraction.color * refraction.weight;
  if (!output_ssr(reflection)) {
    closure.radiance += out_Glossy_0.radiance * reflection.color * reflection.weight;
  }
  return closure;
#endif
}

/* Dielectric BSDF */
CLOSURE_EVAL_FUNCTION_DECLARE_2(DielectricBSDF, Diffuse, Glossy)
Closure closure_eval(ClosureDiffuse diffuse, ClosureReflection reflection)
{
#if defined(DO_SPLIT_CLOSURE_EVAL)
  Closure closure = closure_eval(diffuse);
  Closure closure_reflection = closure_eval(reflection);
  closure.radiance += closure_reflection.radiance;
  return closure;
#else
  /* Glue with the old system. */
  CLOSURE_VARS_DECLARE_2(Diffuse, Glossy);

  /* WORKAROUND: This is to avoid regression in 3.2 and avoid messing with EEVEE-Next. */
  in_common.occlusion = (diffuse.sss_radius.g == -1.0) ? diffuse.sss_radius.r : 1.0;
  in_Diffuse_0.N = diffuse.N;
  in_Diffuse_0.albedo = diffuse.color;
  in_Glossy_1.N = reflection.N;
  in_Glossy_1.roughness = reflection.roughness;

  CLOSURE_EVAL_FUNCTION_2(DielectricBSDF, Diffuse, Glossy);

  Closure closure = CLOSURE_DEFAULT;
  if (!output_sss(diffuse, out_Diffuse_0)) {
    closure.radiance += out_Diffuse_0.radiance * diffuse.color * diffuse.weight;
  }
  if (!output_ssr(reflection)) {
    closure.radiance += out_Glossy_1.radiance * reflection.color * reflection.weight;
  }
  return closure;
#endif
}

/* Specular BSDF */
CLOSURE_EVAL_FUNCTION_DECLARE_3(SpecularBSDF, Diffuse, Glossy, Glossy)
Closure closure_eval(ClosureDiffuse diffuse,
                     ClosureReflection reflection,
                     ClosureReflection clearcoat)
{
#if defined(DO_SPLIT_CLOSURE_EVAL)
  Closure closure = closure_eval(diffuse);
  Closure closure_reflection = closure_eval(reflection);
  Closure closure_clearcoat = closure_eval(clearcoat, false);
  closure.radiance += closure_reflection.radiance + closure_clearcoat.radiance;
  return closure;
#else
  /* Glue with the old system. */
  CLOSURE_VARS_DECLARE_3(Diffuse, Glossy, Glossy);

  /* WORKAROUND: This is to avoid regression in 3.2 and avoid messing with EEVEE-Next. */
  in_common.occlusion = (diffuse.sss_radius.g == -1.0) ? diffuse.sss_radius.r : 1.0;
  in_Diffuse_0.N = diffuse.N;
  in_Diffuse_0.albedo = diffuse.color;
  in_Glossy_1.N = reflection.N;
  in_Glossy_1.roughness = reflection.roughness;
  in_Glossy_2.N = clearcoat.N;
  in_Glossy_2.roughness = clearcoat.roughness;

  CLOSURE_EVAL_FUNCTION_3(SpecularBSDF, Diffuse, Glossy, Glossy);

  Closure closure = CLOSURE_DEFAULT;
  if (!output_sss(diffuse, out_Diffuse_0)) {
    closure.radiance += out_Diffuse_0.radiance * diffuse.color * diffuse.weight;
  }
  closure.radiance += out_Glossy_2.radiance * clearcoat.color * clearcoat.weight;
  if (!output_ssr(reflection)) {
    closure.radiance += out_Glossy_1.radiance * reflection.color * reflection.weight;
  }
  return closure;
#endif
}

/* Principled BSDF */
CLOSURE_EVAL_FUNCTION_DECLARE_4(PrincipledBSDF, Diffuse, Glossy, Glossy, Refraction)
Closure closure_eval(ClosureDiffuse diffuse,
                     ClosureReflection reflection,
                     ClosureReflection clearcoat,
                     ClosureRefraction refraction)
{
#if defined(DO_SPLIT_CLOSURE_EVAL)
  Closure closure = closure_eval(diffuse);
  Closure closure_reflection = closure_eval(reflection);
  Closure closure_clearcoat = closure_eval(clearcoat, false);
  Closure closure_refraction = closure_eval(refraction);
  closure.radiance += closure_reflection.radiance + closure_clearcoat.radiance +
                      closure_refraction.radiance;
  return closure;
#else
  /* Glue with the old system. */
  CLOSURE_VARS_DECLARE_4(Diffuse, Glossy, Glossy, Refraction);

  in_Diffuse_0.N = diffuse.N;
  in_Diffuse_0.albedo = diffuse.color;
  in_Glossy_1.N = reflection.N;
  in_Glossy_1.roughness = reflection.roughness;
  in_Glossy_2.N = clearcoat.N;
  in_Glossy_2.roughness = clearcoat.roughness;
  in_Refraction_3.N = refraction.N;
  in_Refraction_3.roughness = refraction.roughness;
  in_Refraction_3.ior = refraction.ior;

  CLOSURE_EVAL_FUNCTION_4(PrincipledBSDF, Diffuse, Glossy, Glossy, Refraction);

  Closure closure = CLOSURE_DEFAULT;
  closure.radiance += out_Glossy_2.radiance * clearcoat.color * clearcoat.weight;
  closure.radiance += out_Refraction_3.radiance * refraction.color * refraction.weight;
  if (!output_sss(diffuse, out_Diffuse_0)) {
    closure.radiance += out_Diffuse_0.radiance * diffuse.color * diffuse.weight;
  }
  if (!output_ssr(reflection)) {
    closure.radiance += out_Glossy_1.radiance * reflection.color * reflection.weight;
  }
  return closure;
#endif
}

CLOSURE_EVAL_FUNCTION_DECLARE_2(PrincipledBSDFMetalClearCoat, Glossy, Glossy)
Closure closure_eval(ClosureReflection reflection, ClosureReflection clearcoat)
{
#if defined(DO_SPLIT_CLOSURE_EVAL)
  Closure closure = closure_eval(clearcoat);
  Closure closure_reflection = closure_eval(reflection);
  closure.radiance += closure_reflection.radiance;
  return closure;
#else
  /* Glue with the old system. */
  CLOSURE_VARS_DECLARE_2(Glossy, Glossy);

  in_Glossy_0.N = reflection.N;
  in_Glossy_0.roughness = reflection.roughness;
  in_Glossy_1.N = clearcoat.N;
  in_Glossy_1.roughness = clearcoat.roughness;

  CLOSURE_EVAL_FUNCTION_2(PrincipledBSDFMetalClearCoat, Glossy, Glossy);

  Closure closure = CLOSURE_DEFAULT;
  closure.radiance += out_Glossy_1.radiance * clearcoat.color * clearcoat.weight;
  if (!output_ssr(reflection)) {
    closure.radiance += out_Glossy_0.radiance * reflection.color * reflection.weight;
  }
  return closure;
#endif
}

/* Not supported for surface shaders. */
Closure closure_eval(ClosureVolumeScatter volume_scatter)
{
  return CLOSURE_DEFAULT;
}
Closure closure_eval(ClosureVolumeAbsorption volume_absorption)
{
  return CLOSURE_DEFAULT;
}
Closure closure_eval(ClosureVolumeScatter volume_scatter,
                     ClosureVolumeAbsorption volume_absorption,
                     ClosureEmission emission)
{
  return CLOSURE_DEFAULT;
}

/* Not implemented yet. */
Closure closure_eval(ClosureHair hair)
{
  return CLOSURE_DEFAULT;
}

vec4 closure_to_rgba(Closure closure)
{
  return vec4(closure.radiance, 1.0 - saturate(avg(closure.transmittance)));
}

float calc_self_shadows_only(LightData ld, vec3 P, vec4 l_vector)
{
  // float vis = light_attenuation(ld, l_vector, lightGroups);
  float vis = 1.0;
  if (ld.l_shadowid >= 0.0 && vis > 0.001) {
    if (ld.l_type == SUN) {
      vis *= sample_cascade_shadow(int(ld.l_shadowid), P, false);
    }
    else {
      vis *= sample_cube_shadow(int(ld.l_shadowid), P, false);
    }
  }
  return vis;
}

/* Use default (Material) light groups */
void calc_shader_info(vec3 position,
                      vec3 normal,
                      out vec4 half_light,
                      out float shadows,
                      out float self_shadows,
                      out vec4 ambient)
{
  calc_shader_info(position,
                   normal,
                   lightGroups,
                   lightGroupShadows,
                   half_light,
                   shadows,
                   self_shadows,
                   ambient);
}

/* Use custom (Per-Node) light groups */
void calc_shader_info(vec3 position,
                      vec3 normal,
                      ivec4 light_groups,
                      ivec4 light_group_shadows,
                      out vec4 half_light,
                      out float shadows,
                      out float self_shadows,
                      out vec4 ambient)
{
  ClosureEvalCommon cl_common = closure_Common_eval_init(CLOSURE_INPUT_COMMON_DEFAULT);
  cl_common.P = position;
  cl_common.light_groups = light_groups;
  cl_common.light_group_shadows = light_group_shadows;
  vec3 n_n = normalize(normal);

  float shadow_accum = 0.0;
  float self_shadow_accum = 0.0;
  float light_accum = 0.0;
  half_light = vec4(0.0);

  for (int i = 0; i < laNumLight && i < MAX_LIGHT; i++) {
    ClosureLightData light = closure_light_eval_init(cl_common, i);
    LightData ld = light.data;
    if ((ld.light_group_bits.x & light_groups.x) == 0 &&
        (ld.light_group_bits.y & light_groups.y) == 0 &&
        (ld.light_group_bits.z & light_groups.z) == 0 &&
        (ld.light_group_bits.w & light_groups.w) == 0) {
      continue;
    }

    if (!((ld.light_group_bits.x & light_group_shadows.x) == 0 &&
          (ld.light_group_bits.y & light_group_shadows.y) == 0 &&
          (ld.light_group_bits.z & light_group_shadows.z) == 0 &&
          (ld.light_group_bits.w & light_group_shadows.w) == 0)) {
      float light_fac = max(ld.l_color.x, max(ld.l_color.y, ld.l_color.z)) * ld.l_diff;
      shadow_accum += (1 - light.vis * light.contact_shadow) * light_fac;
      self_shadow_accum += (1 - calc_self_shadows_only(light.data, position, light.L)) * light_fac;
      light_accum += light_fac;
    }

    float radiance = light_diffuse(light.data, n_n, cl_common.V, light.L);
    half_light += vec4(light.data.l_color * light.data.l_diff * radiance, 0.0);
  }

  shadows = (1 - (shadow_accum / max(light_accum, 1)));
  self_shadows = (1 - (self_shadow_accum / max(light_accum, 1)));
  ambient = vec4(probe_evaluate_world_diff(n_n), 1.0);
}

Closure closure_add(inout Closure cl1, inout Closure cl2)
{
  Closure cl;
  cl.radiance = cl1.radiance + cl2.radiance;
  cl.transmittance = cl1.transmittance + cl2.transmittance;
  cl.holdout = cl1.holdout + cl2.holdout;
  /* Make sure each closure is only added once to the result. */
  cl1 = CLOSURE_DEFAULT;
  cl2 = CLOSURE_DEFAULT;
  return cl;
}

Closure closure_mix(inout Closure cl1, inout Closure cl2, float fac)
{
  /* Weights have already been applied. */
  return closure_add(cl1, cl2);
}
