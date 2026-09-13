#version 450
layout(set=0,binding=0) uniform sampler2D scene;
layout(location=0) out vec4 color;
layout(push_constant) uniform Bloom {
    vec4 bounds; // Min/max pixel centers, in normalized framebuffer coordinates.
    vec4 params; // inverse width/height, radius in pixels, strength
} bloom;
vec3 light(vec2 uv) {
    vec3 value = texture(scene, clamp(uv, bloom.bounds.xy, bloom.bounds.zw)).rgb;
    float peak = max(value.r, max(value.g, value.b));
    return value * smoothstep(0.50, 0.96, peak);
}
void main() {
    vec2 uv = gl_FragCoord.xy * bloom.params.xy;
    vec3 glow = vec3(0.0);
    float total = 0.0;
    // Soft local scatter of bright surfaces. This pass runs before text/HUD,
    // clips and clamps to one player view, and keeps the original sharp core.
    for (int y=-2; y<=2; ++y) for (int x=-2; x<=2; ++x) {
        float weight = exp(-0.6 * float(x*x+y*y));
        vec2 delta = vec2(x,y) * bloom.params.xy * bloom.params.z;
        glow += light(uv + delta) * weight;
        total += weight;
    }
    color = vec4(glow * (bloom.params.w / total), 0.0);
}
