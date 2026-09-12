#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 vertex_color;
layout(push_constant) uniform Scene {
    mat4 view_projection;
    uint encode_srgb;
} scene;
void main() {
    gl_Position = scene.view_projection * vec4(position, 1.0);
    vertex_color = color;
}
