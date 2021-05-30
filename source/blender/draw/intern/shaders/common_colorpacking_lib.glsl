/*****************************************/ /* TODO - rename */
/* 2 component packing */ 
/* http://emmettmcquinn.com/blog/graphics/2012/11/07/float-packing.html */

const float c_precision = 4096.0; //4096 (12bit * 2)
const float c_precisionp1 = c_precision + 1.0;
const float range = 0.1;
const float range_inverse = 10.0;

vec4 packVec4(vec4 color_a, vec4 color_b) {
	vec4 packed_color = vec4(0.0);
	/* Clamp inputs*/
	color_a *= range;
	color_a = clamp(color_a, 0.0, 1.0);
    color_b *= range;
    color_b = clamp(color_b, 0.0, 1.0);
	/* Pack RGBA */
	packed_color.r = floor(color_a.r * c_precision + 0.5) + floor(color_b.r * c_precision + 0.5) * c_precisionp1;
    packed_color.g = floor(color_a.g * c_precision + 0.5) + floor(color_b.g * c_precision + 0.5) * c_precisionp1;
    packed_color.b = floor(color_a.b * c_precision + 0.5) + floor(color_b.b * c_precision + 0.5) * c_precisionp1;
    packed_color.a = floor(color_a.a * c_precision + 0.5) + floor(color_b.a * c_precision + 0.5) * c_precisionp1;

    return packed_color.rgba;
}

vec3 packVec3(vec3 color_a, vec3 color_b) {
	vec3 packed_color = vec3(0.0);
	/* Clamp inputs*/
	color_a *= range;
	color_a = clamp(color_a, 0.0, 1.0);
    color_b *= range;
    color_b = clamp(color_b, 0.0, 1.0);
	/* Pack RGB */
	packed_color.r = floor(color_a.r * c_precision + 0.5) + floor(color_b.r * c_precision + 0.5) * c_precisionp1;
    packed_color.g = floor(color_a.g * c_precision + 0.5) + floor(color_b.g * c_precision + 0.5) * c_precisionp1;
    packed_color.b = floor(color_a.b * c_precision + 0.5) + floor(color_b.b * c_precision + 0.5) * c_precisionp1;

    return packed_color.rgb;
}

float packFloat(float color_a, float color_b) {
	float packed_color = 0.0;
	/* Clamp inputs*/
	color_a *= range;
	color_a = clamp(color_a, 0.0, 1.0);
    color_b *= range;
    color_b = clamp(color_b, 0.0, 1.0);
	/* Pack float */
	packed_color = floor(color_a * c_precision + 0.5) + floor(color_b * c_precision + 0.5) * c_precisionp1;

    return packed_color;
}

vec4 unpackVec4(vec4 packedColor, out vec4 color_a, out vec4 color_b){
	vec4 color;
	color_a.r = mod(packedColor.r, c_precisionp1) / c_precision;
	color_b.r = mod(floor(packedColor.r / c_precisionp1), c_precisionp1) / c_precision;
    color_a.g = mod(packedColor.g, c_precisionp1) / c_precision;
	color_b.g = mod(floor(packedColor.g / c_precisionp1), c_precisionp1) / c_precision;
    color_a.b = mod(packedColor.b, c_precisionp1) / c_precision;
	color_b.b = mod(floor(packedColor.b / c_precisionp1), c_precisionp1) / c_precision;
    color_a.a = mod(packedColor.a, c_precisionp1) / c_precision;
	color_b.a = mod(floor(packedColor.a / c_precisionp1), c_precisionp1) / c_precision;
	/* Restore range */
	color_a *= range_inverse;
	color_b *= range_inverse;

	return vec4(0.0);
}
