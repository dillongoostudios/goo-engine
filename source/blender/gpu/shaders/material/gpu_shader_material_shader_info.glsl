
void node_shader_info(float3 position, float3 normal,
out float4 half_light, out float shadows, out float self_shadows, out float4 ambient, out float half_lambert)
{
#ifndef EEVEE_ENGINE
    return;
#else
    // Implicitly included by eevee shader linking.
    calc_shader_info(position, normal, half_light, shadows, self_shadows, ambient, half_lambert);
#endif
}

void node_shader_info_light_groups(float3 position, float3 normal, float4 light_groups, float4 light_group_shadows,
out float4 half_light, out float shadows, out float self_shadows, out float4 ambient, out float half_lambert)
{
#ifndef EEVEE_ENGINE
    return;
#else
    // Implicitly included by eevee shader linking.
    calc_shader_info(position, normal, floatBitsToInt(light_groups), floatBitsToInt(light_group_shadows), half_light, shadows, self_shadows, ambient, half_lambert);
#endif
}
