#version 450
layout(set=0,binding=0) uniform sampler2D image_texture;
layout(location=0) in vec4 primary_front;
layout(location=1) in vec4 primary_back;
layout(location=2) in vec2 uv;
layout(location=3) in float fog_z;
layout(location=0) out vec4 fragment_color;
layout(push_constant) uniform State {
    vec4 fog_color;
    vec4 fog_params;
    uvec4 modes;
    uvec4 wraps;
} state;
void main() {
    // Fixed-function colors and texture bytes operate in the original numeric
    // color space. The UNORM offscreen target avoids implicit sRGB conversion.
    vec4 color = clamp(gl_FrontFacing ? primary_front : primary_back, 0.0, 1.0);
    if (state.modes.x != 0) {
        vec2 gradient_x = dFdx(uv), gradient_y = dFdy(uv);
        vec2 coordinate = uv;
        // Legacy GL_CLAMP clamps the coordinate, then blends border texels at
        // the edge. CLAMP_TO_EDGE deliberately does not have that border mix.
        if (state.wraps.x == 0x2900u) coordinate.x = clamp(coordinate.x, 0.0, 1.0);
        if (state.wraps.y == 0x2900u) coordinate.y = clamp(coordinate.y, 0.0, 1.0);
        vec4 texel = textureGrad(image_texture, coordinate, gradient_x, gradient_y);
        bool alpha_only = state.modes.z == 0x1906u;
        bool has_alpha = state.modes.z == 0x1908u || state.modes.z == 0x190au || alpha_only;
        if (state.modes.y == 0x1e01u) { // REPLACE
            if (!alpha_only) color.rgb = texel.rgb;
            if (has_alpha) color.a = texel.a;
        } else if (state.modes.y == 0x2101u) { // DECAL
            color.rgb = has_alpha ? mix(color.rgb, texel.rgb, texel.a) : texel.rgb;
        } else { // MODULATE, validated by the backend
            if (!alpha_only) color.rgb *= texel.rgb;
            if (has_alpha) color.a *= texel.a;
        }
    }
    if (state.modes.w != 0) {
        float factor;
        if (state.modes.w == 0x2601u) { // LINEAR
            float span = state.fog_params.y - state.fog_params.x;
            factor = span == 0.0 ? (fog_z < state.fog_params.y ? 1.0 : 0.0) :
                     (state.fog_params.y - fog_z) / span;
        } else if (state.modes.w == 0x0801u) { // EXP2
            float distance = state.fog_params.z * fog_z;
            factor = exp(-distance * distance);
        } else {
            factor = exp(-state.fog_params.z * fog_z);
        }
        color.rgb = mix(state.fog_color.rgb, color.rgb, clamp(factor, 0.0, 1.0));
    }
    fragment_color = color;
}
