
void node_shader_info(vec3 position, vec3 normal,
out vec4 half_light, out float shadows, out float self_shadows, out vec4 ambient)
{
    // Implicitly included by eevee shader linking.
    calc_shader_info(position, normal, half_light, shadows, self_shadows, ambient);
}

void node_shader_info_light_groups(vec3 position, vec3 normal, vec4 light_groups, vec4 light_group_shadows,
out vec4 half_light, out float shadows, out float self_shadows, out vec4 ambient)
{
    // Implicitly included by eevee shader linking.
    calc_shader_info(position, normal, floatBitsToInt(light_groups), floatBitsToInt(light_group_shadows), half_light, shadows, self_shadows, ambient);
}
