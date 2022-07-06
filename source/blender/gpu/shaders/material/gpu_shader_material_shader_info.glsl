void node_shader_info(vec3 position, vec3 normal,
    out vec4 half_light, out float shadows, out float self_shadows, out vec4 ambient)
{
    calc_shader_info(position, normal, half_light, shadows, self_shadows, ambient);
}
