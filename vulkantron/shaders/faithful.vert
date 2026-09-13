#version 450
layout(location=0) in vec4 clip_position;
layout(location=1) in vec4 front_color;
layout(location=2) in vec4 back_color;
layout(location=3) in vec2 texture_coordinate;
layout(location=4) in float fog_distance;
layout(location=5) in float original_clip_z;
layout(location=0) out vec4 primary_front;
layout(location=1) out vec4 primary_back;
layout(location=2) out vec2 uv;
layout(location=3) out float fog_z;
layout(push_constant) uniform State {
    vec4 fog_color;
    vec4 fog_params; // start, end, density, point size
    uvec4 modes; // texture enabled, environment, base format, fog mode (0=off)
    uvec4 wraps;
} state;
void main() {
    gl_Position = clip_position;
    // The optional native clip-depth extension applies GL's depth viewport
    // transform after perspective division, avoiding an early rounded remap.
    if (state.wraps.z != 0) gl_Position.z = original_clip_z;
    gl_PointSize = state.fog_params.w;
    primary_front = front_color;
    primary_back = back_color;
    uv = texture_coordinate;
    fog_z = fog_distance;
}
