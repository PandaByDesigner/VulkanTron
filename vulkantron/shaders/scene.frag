#version 450
layout(location = 0) in vec4 vertex_color;
layout(location = 0) out vec4 fragment_color;
layout(push_constant) uniform Scene {
    mat4 view_projection;
    uint encode_srgb;
} scene;
vec3 linear_to_srgb(vec3 value) {
    value = clamp(value, 0.0, 1.0);
    return mix(1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055,
               value * 12.92, lessThanEqual(value, vec3(0.0031308)));
}
void main() {
    // SRGB attachments encode automatically; UNORM presentation needs this.
    vec3 rgb = scene.encode_srgb != 0 ? linear_to_srgb(vertex_color.rgb) : vertex_color.rgb;
    fragment_color = vec4(rgb, clamp(vertex_color.a, 0.0, 1.0));
}
