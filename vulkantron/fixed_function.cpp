#include "fixed_function.h"
#include "faithful_frame.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace {
using Vec = std::array<float, 4>;
using Mat = std::array<float, 16>;
constexpr std::size_t max_vertices = 4 * 1024 * 1024;
constexpr std::size_t max_commands = 65536;
constexpr std::size_t max_texture_bytes = 256 * 1024 * 1024;
Mat identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
Mat multiply(const Mat &a, const Mat &b) {
    Mat r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
    return r;
}
Vec transform(const Mat &m, const Vec &v) {
    Vec r{};
    for (int row = 0; row < 4; ++row)
        for (int c = 0; c < 4; ++c)
            r[row] += m[c * 4 + row] * v[c];
    return r;
}
float dot3(const Vec &a, const Vec &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec unit(Vec v) {
    const float n = std::sqrt(dot3(v, v));
    if (n > 0 && std::isfinite(n))
        for (int i = 0; i < 3; ++i)
            v[i] /= n;
    else
        v = {0, 0, 0, 0};
    return v;
}
float clamp(float x) { return std::isfinite(x) ? std::clamp(x, 0.f, 1.f) : 0.f; }
Vec bounded(Vec v) {
    for (auto &x : v)
        x = clamp(x);
    return v;
}
struct Material {
    Vec ambient{.2f, .2f, .2f, 1}, diffuse{.8f, .8f, .8f, 1}, specular{0, 0, 0, 1}, emission{0, 0, 0, 1};
    float shininess = 0;
};
struct Light {
    bool enabled = false;
    Vec position{0, 0, 1, 0}, ambient{0, 0, 0, 1}, diffuse{0, 0, 0, 1}, specular{0, 0, 0, 1};
    float constant = 1, linear = 0, quadratic = 0;
};
struct Array {
    bool enabled = false;
    int size = 3;
    GLenum type = GL_FLOAT;
    int stride = 0;
    const void *pointer = nullptr;
};
struct PixelStore {
    int alignment = 4, row_length = 0, skip_rows = 0, skip_pixels = 0;
};
struct ClientState {
    Array vertex, normal, color, uv;
    PixelStore pack, unpack;
};
struct State {
    vt::FaithfulFrame frame;
    vt::DrawState draw;
    Vec color{1, 1, 1, 1}, normal{0, 0, 1, 0}, uv{0, 0, 0, 1};
    Vec light_ambient{.2f, .2f, .2f, 1};
    std::array<Light, 8> lights;
    std::array<Material, 2> materials;
    std::array<std::vector<Mat>, 3> matrices;
    Mat combined = identity();
    bool combined_dirty = true;
    int matrix_mode = 0;
    bool lighting = false, normalize = false, two_side = false, local_viewer = false, color_material = false;
    GLenum color_material_face = GL_FRONT_AND_BACK, color_material_mode = GL_AMBIENT_AND_DIFFUSE;
    GLenum shade = GL_SMOOTH, error = GL_NO_ERROR, primitive = GL_TRIANGLES, read_buffer = GL_BACK;
    bool begun = false;
    std::vector<vt::ClipVertex> immediate;
    ClientState client;
    std::vector<std::pair<GLbitfield, ClientState>> client_stack;
    std::map<GLuint, std::shared_ptr<vt::Texture>> textures;
    std::set<GLuint> reserved;
    GLuint next_texture = 1, bound_texture = 0;
    std::uint64_t revision = 0, error_count = 0;
    int depth_bits = 0, stencil_bits = 0, max_texture_size = 4096;
    std::string renderer = "VulkanTron fixed-function recorder";
    Vec clear_color{0, 0, 0, 0};
    float clear_depth = 1;
    unsigned clear_stencil = 0;
    VTReadbackCallback readback = nullptr;
    std::string fatal;
    State() {
        for (auto &stack : matrices)
            stack.push_back(identity());
        lights[0].diffuse = lights[0].specular = {1, 1, 1, 1};
        client.normal.size = 3;
        client.color.size = 4;
        client.uv.size = 4;
        auto zero = std::make_shared<vt::Texture>();
        textures.emplace(0, zero);
    }
};
State s;
void error(GLenum value) {
    ++s.error_count;
    if (s.error == GL_NO_ERROR)
        s.error = value;
}
bool outside() {
    if (s.begun) {
        error(GL_INVALID_OPERATION);
        return false;
    }
    return true;
}
void resource_error(const char *message) {
    error(GL_OUT_OF_MEMORY);
    s.fatal = message;
}
Mat &matrix() {
    s.combined_dirty = true;
    return s.matrices[s.matrix_mode].back();
}
void apply_material(Material &m, GLenum mode, const Vec &value) {
    switch (mode) {
    case GL_AMBIENT:
        m.ambient = value;
        break;
    case GL_DIFFUSE:
        m.diffuse = value;
        break;
    case GL_SPECULAR:
        m.specular = value;
        break;
    case GL_EMISSION:
        m.emission = value;
        break;
    case GL_AMBIENT_AND_DIFFUSE:
        m.ambient = m.diffuse = value;
        break;
    default:
        error(GL_INVALID_ENUM);
        break;
    }
}
void color_material() {
    if (!s.color_material)
        return;
    if (s.color_material_face != GL_BACK)
        apply_material(s.materials[0], s.color_material_mode, s.color);
    if (s.color_material_face != GL_FRONT)
        apply_material(s.materials[1], s.color_material_mode, s.color);
}
Vec normal_transform(Vec n) {
    // Inverse transpose of the modelview's upper 3x3, not the forward matrix.
    const auto &m = s.matrices[0].back();
    const double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(determinant) < 1e-30)
        return {0, 0, 0, 0}; // Singular shadow matrices are unlit in production.
    Vec result{static_cast<float>(((e * i - f * h) * n[0] + (f * g - d * i) * n[1] + (d * h - e * g) * n[2]) /
                                  determinant),
               static_cast<float>(((c * h - b * i) * n[0] + (a * i - c * g) * n[1] + (b * g - a * h) * n[2]) /
                                  determinant),
               static_cast<float>(((b * f - c * e) * n[0] + (c * d - a * f) * n[1] + (a * e - b * d) * n[2]) /
                                  determinant),
               0};
    return s.normalize ? unit(result) : result;
}
Vec lighting(const Vec &eye, Vec n, const Material &material) {
    Vec result = material.emission;
    for (int c = 0; c < 3; ++c)
        result[c] += material.ambient[c] * s.light_ambient[c];
    const Vec viewer = s.local_viewer ? unit(Vec{-eye[0], -eye[1], -eye[2], 0}) : Vec{0, 0, 1, 0};
    for (const auto &light : s.lights) {
        if (!light.enabled)
            continue;
        Vec direction = light.position;
        float attenuation = 1;
        if (light.position[3] != 0) {
            for (int c = 0; c < 3; ++c)
                direction[c] = light.position[c] / light.position[3] - eye[c];
            const float distance = std::sqrt(dot3(direction, direction));
            const float denominator =
                light.constant + light.linear * distance + light.quadratic * distance * distance;
            attenuation = denominator > 0 ? 1 / denominator : 1;
        }
        direction = unit(direction);
        const float ndotl = std::max(0.f, dot3(n, direction));
        Vec half_vector =
            unit(Vec{direction[0] + viewer[0], direction[1] + viewer[1], direction[2] + viewer[2], 0});
        const float ndoth = std::max(0.f, dot3(n, half_vector));
        const float specular = ndotl > 0 ? std::pow(ndoth, material.shininess) : 0;
        for (int c = 0; c < 3; ++c)
            result[c] += attenuation * (material.ambient[c] * light.ambient[c] +
                                        material.diffuse[c] * light.diffuse[c] * ndotl +
                                        material.specular[c] * light.specular[c] * specular);
    }
    result[3] = material.diffuse[3];
    return bounded(result);
}
vt::ClipVertex make_vertex(Vec position) {
    const Vec eye = transform(s.matrices[0].back(), position);
    if (s.combined_dirty) {
        s.combined = multiply(s.matrices[1].back(), s.matrices[0].back());
        s.combined_dirty = false;
    }
    const Vec clip = transform(s.combined, position);
    const Vec tex = transform(s.matrices[2].back(), s.uv);
    const Vec n = normal_transform(s.normal);
    Vec front = s.lighting ? lighting(eye, n, s.materials[0]) : bounded(s.color);
    Vec back = front;
    if (s.lighting && s.two_side)
        back = lighting(eye, Vec{-n[0], -n[1], -n[2], 0}, s.materials[1]);
    vt::ClipVertex v{};
    v.position[0] = clip[0];
    v.position[1] = -clip[1];
    v.position[2] = (clip[2] + clip[3]) * .5f;
    v.position[3] = clip[3];
    v.gl_clip_z = clip[2];
    std::copy(front.begin(), front.end(), v.front_color);
    std::copy(back.begin(), back.end(), v.back_color);
    // Production uses 2D affine texture coordinates; no projective texture Q.
    v.uv[0] = tex[0];
    v.uv[1] = tex[1];
    v.fog_distance = std::abs(eye[2]);
    return v;
}
bool primitive(GLenum mode) { return mode <= GL_POLYGON; }
vt::ClipVertex interpolate(const vt::ClipVertex &a, const vt::ClipVertex &b, float t) {
    vt::ClipVertex r{};
    for (int i = 0; i < 4; ++i) {
        r.position[i] = a.position[i] + t * (b.position[i] - a.position[i]);
        r.front_color[i] = a.front_color[i] + t * (b.front_color[i] - a.front_color[i]);
        r.back_color[i] = a.back_color[i] + t * (b.back_color[i] - a.back_color[i]);
    }
    for (int i = 0; i < 2; ++i)
        r.uv[i] = a.uv[i] + t * (b.uv[i] - a.uv[i]);
    r.fog_distance = a.fog_distance + t * (b.fog_distance - a.fog_distance);
    r.gl_clip_z = a.gl_clip_z + t * (b.gl_clip_z - a.gl_clip_z);
    return r;
}
std::vector<vt::ClipVertex> clip_polygon(std::vector<vt::ClipVertex> polygon) {
    // Sutherland-Hodgman in homogeneous Vulkan clip coordinates. Polygon-line
    // mode is converted after clipping, keeping original quad boundaries rather
    // than exposing the artificial diagonal of its filled triangulation.
    for (int plane = 0; plane < 6 && !polygon.empty(); ++plane) {
        auto distance = [&](const vt::ClipVertex &v) {
            const auto *p = v.position;
            switch (plane) {
            case 0:
                return p[3] + p[0];
            case 1:
                return p[3] - p[0];
            case 2:
                return p[3] + p[1];
            case 3:
                return p[3] - p[1];
            case 4:
                return p[2];
            default:
                return p[3] - p[2];
            }
        };
        std::vector<vt::ClipVertex> clipped;
        auto previous = polygon.back();
        float pd = distance(previous);
        for (const auto &current : polygon) {
            const float cd = distance(current);
            if ((pd >= 0) != (cd >= 0))
                clipped.push_back(interpolate(previous, current, pd / (pd - cd)));
            if (cd >= 0)
                clipped.push_back(current);
            previous = current;
            pd = cd;
        }
        polygon = std::move(clipped);
    }
    return polygon;
}
void wire_polygon(vt::DrawBatch &batch, const std::vector<vt::ClipVertex> &source,
                  std::vector<std::size_t> ids, std::size_t provoking) {
    std::vector<vt::ClipVertex> polygon;
    polygon.reserve(ids.size());
    for (auto index : ids) {
        auto v = source[index];
        if (s.shade == GL_FLAT) {
            std::copy(std::begin(source[provoking].front_color), std::end(source[provoking].front_color),
                      v.front_color);
            std::copy(std::begin(source[provoking].back_color), std::end(source[provoking].back_color),
                      v.back_color);
        }
        polygon.push_back(v);
    }
    polygon = clip_polygon(std::move(polygon));
    if (polygon.size() < 3)
        return;
    double area = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto *a = polygon[i].position;
        const auto *b = polygon[(i + 1) % polygon.size()].position;
        if (a[3] == 0 || b[3] == 0)
            return;
        area += (static_cast<double>(a[0]) * b[1] - static_cast<double>(b[0]) * a[1]) /
                (static_cast<double>(a[3]) * b[3]);
    }
    const bool front = s.draw.front_face == GL_CCW ? area < 0 : area > 0; // Clip Y has already flipped.
    if (s.draw.cull && (s.draw.cull_face == GL_FRONT_AND_BACK || (front && s.draw.cull_face == GL_FRONT) ||
                        (!front && s.draw.cull_face == GL_BACK)))
        return;
    if (s.frame.vertices + batch.vertices.size() + polygon.size() * 2 > max_vertices) {
        resource_error("Wireframe exceeds recording limit");
        return;
    }
    for (auto &v : polygon) {
        if (!front)
            std::copy(std::begin(v.back_color), std::end(v.back_color), v.front_color);
        std::copy(std::begin(v.front_color), std::end(v.front_color), v.back_color);
    }
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        batch.vertices.push_back(polygon[i]);
        batch.vertices.push_back(polygon[(i + 1) % polygon.size()]);
    }
}
void record(GLenum mode, const std::vector<vt::ClipVertex> &source) {
    if (source.empty())
        return;
    if (s.frame.commands.size() >= max_commands) {
        resource_error("Faithful frame exceeds 65536-command recording limit");
        return;
    }
    vt::DrawBatch batch;
    batch.state = s.draw;
    if (s.draw.texture_enabled)
        batch.state.texture = s.textures.at(s.bound_texture);
    auto append = [&](std::initializer_list<std::size_t> ids, std::size_t provoking) {
        if (batch.vertices.size() + ids.size() + s.frame.vertices > max_vertices) {
            resource_error("Faithful frame exceeds 4194304-vertex recording limit");
            return;
        }
        for (auto id : ids) {
            auto v = source[id];
            if (s.shade == GL_FLAT) {
                std::copy(std::begin(source[provoking].front_color), std::end(source[provoking].front_color),
                          v.front_color);
                std::copy(std::begin(source[provoking].back_color), std::end(source[provoking].back_color),
                          v.back_color);
            }
            batch.vertices.push_back(v);
        }
    };
    const auto count = source.size();
    if (s.draw.polygon_mode == GL_LINE && (mode == GL_QUADS || mode == GL_QUAD_STRIP || mode == GL_POLYGON)) {
        batch.primitive = vt::Primitive::LineList;
        batch.state.polygon_mode = GL_FILL;
        batch.state.cull = false;
        batch.state.polygon_offset = false;
        if (mode == GL_QUADS)
            for (std::size_t i = 3; i < count; i += 4)
                wire_polygon(batch, source, {i - 3, i - 2, i - 1, i}, i);
        else if (mode == GL_QUAD_STRIP)
            for (std::size_t i = 3; i < count; i += 2)
                wire_polygon(batch, source, {i - 3, i - 2, i, i - 1}, i);
        else {
            std::vector<std::size_t> ids;
            ids.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
                ids.push_back(i);
            wire_polygon(batch, source, std::move(ids), 0);
        }
        if (!s.fatal.empty())
            return;
        if (!batch.vertices.empty()) {
            s.frame.vertices += batch.vertices.size();
            s.frame.commands.emplace_back(std::move(batch));
        }
        return;
    }
    switch (mode) {
    case GL_POINTS:
        batch.primitive = vt::Primitive::PointList;
        for (std::size_t i = 0; i < count; ++i)
            append({i}, i);
        break;
    case GL_LINES:
        batch.primitive = vt::Primitive::LineList;
        for (std::size_t i = 1; i < count; i += 2)
            append({i - 1, i}, i);
        break;
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        batch.primitive = vt::Primitive::LineList;
        for (std::size_t i = 1; i < count; ++i)
            append({i - 1, i}, i);
        if (mode == GL_LINE_LOOP && count > 1)
            append({count - 1, 0}, 0);
        break;
    case GL_TRIANGLES:
        for (std::size_t i = 2; i < count; i += 3)
            append({i - 2, i - 1, i}, i);
        break;
    case GL_TRIANGLE_STRIP:
        for (std::size_t i = 2; i < count; ++i) {
            if (i % 2)
                append({i - 1, i - 2, i}, i);
            else
                append({i - 2, i - 1, i}, i);
        }
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        for (std::size_t i = 2; i < count; ++i)
            append({0, i - 1, i}, mode == GL_POLYGON ? 0 : i);
        break;
    case GL_QUADS:
        for (std::size_t i = 3; i < count; i += 4) {
            append({i - 3, i - 2, i - 1}, i);
            append({i - 3, i - 1, i}, i);
        }
        break;
    case GL_QUAD_STRIP:
        for (std::size_t i = 3; i < count; i += 2) {
            append({i - 3, i - 2, i}, i);
            append({i - 3, i, i - 1}, i);
        }
        break;
    default:
        error(GL_INVALID_ENUM);
        return;
    }
    if (!s.fatal.empty())
        return;
    if (!batch.vertices.empty()) {
        s.frame.vertices += batch.vertices.size();
        s.frame.commands.emplace_back(std::move(batch));
    }
}
bool *capability(GLenum cap) {
    if (cap >= GL_LIGHT0 && cap <= GL_LIGHT7)
        return &s.lights[cap - GL_LIGHT0].enabled;
    switch (cap) {
    case GL_LIGHTING:
        return &s.lighting;
    case GL_NORMALIZE:
        return &s.normalize;
    case GL_COLOR_MATERIAL:
        return &s.color_material;
    case GL_DEPTH_TEST:
        return &s.draw.depth_test;
    case GL_CULL_FACE:
        return &s.draw.cull;
    case GL_BLEND:
        return &s.draw.blend;
    case GL_STENCIL_TEST:
        return &s.draw.stencil;
    case GL_TEXTURE_2D:
        return &s.draw.texture_enabled;
    case GL_FOG:
        return &s.draw.fog;
    case GL_POLYGON_OFFSET_FILL:
        return &s.draw.polygon_offset;
    case GL_LINE_SMOOTH:
        return &s.draw.line_smooth;
    case GL_SCISSOR_TEST:
        return &s.draw.scissor_test;
    default:
        error(GL_INVALID_ENUM);
        return nullptr;
    }
}
Array *array(GLenum cap) {
    switch (cap) {
    case GL_VERTEX_ARRAY:
        return &s.client.vertex;
    case GL_NORMAL_ARRAY:
        return &s.client.normal;
    case GL_COLOR_ARRAY:
        return &s.client.color;
    case GL_TEXTURE_COORD_ARRAY:
        return &s.client.uv;
    default:
        error(GL_INVALID_ENUM);
        return nullptr;
    }
}
std::size_t type_size(GLenum type) {
    switch (type) {
    case GL_FLOAT:
        return 4;
    case GL_DOUBLE:
        return 8;
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
        return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        return 2;
    case GL_INT:
    case GL_UNSIGNED_INT:
        return 4;
    default:
        return 0;
    }
}
template <class T> T scalar(const unsigned char *p) {
    T v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
float component(const unsigned char *p, GLenum type, bool normalized) {
    switch (type) {
    case GL_FLOAT:
        return scalar<float>(p);
    case GL_DOUBLE:
        return static_cast<float>(scalar<double>(p));
    case GL_UNSIGNED_BYTE:
        return scalar<unsigned char>(p) * (normalized ? 1.f / 255 : 1);
    case GL_BYTE:
        return normalized ? std::max(-1.f, scalar<signed char>(p) / 127.f) : scalar<signed char>(p);
    case GL_UNSIGNED_SHORT:
        return scalar<unsigned short>(p) * (normalized ? 1.f / 65535 : 1);
    case GL_SHORT:
        return normalized ? std::max(-1.f, scalar<short>(p) / 32767.f) : scalar<short>(p);
    case GL_UNSIGNED_INT:
        return normalized ? static_cast<float>(scalar<unsigned>(p) / 4294967295.0)
                          : static_cast<float>(scalar<unsigned>(p));
    case GL_INT:
        return normalized ? std::max(-1.f, static_cast<float>(scalar<int>(p) / 2147483647.0))
                          : static_cast<float>(scalar<int>(p));
    default:
        return 0;
    }
}
Vec array_value(const Array &a, std::size_t index, bool normalized, Vec defaults) {
    const auto size = type_size(a.type);
    const auto stride = a.stride ? static_cast<std::size_t>(a.stride) : size * a.size;
    const auto *data = static_cast<const unsigned char *>(a.pointer) + index * stride;
    for (int i = 0; i < a.size; ++i)
        defaults[i] = component(data + i * size, a.type, normalized);
    return defaults;
}
void array_pointer(Array &a, int size, GLenum type, int stride, const void *pointer) {
    if (!outside())
        return;
    if (size < 1 || size > 4 || stride < 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (!type_size(type)) {
        error(GL_INVALID_ENUM);
        return;
    }
    a.size = size;
    a.type = type;
    a.stride = stride;
    a.pointer = pointer;
}
vt::Texture *edit_texture() {
    auto &texture = s.textures.at(s.bound_texture);
    if (texture.use_count() > 1)
        texture = std::make_shared<vt::Texture>(*texture);
    texture->revision = ++s.revision;
    return texture.get();
}
std::size_t texture_bytes(const vt::Texture &texture) {
    std::size_t size = 0;
    for (const auto &level : texture.levels)
        size += level.rgba.size();
    return size;
}
std::size_t pixel_stride(int width, int channels, const PixelStore &store) {
    const auto row = static_cast<std::size_t>(store.row_length ? store.row_length : width) * channels;
    return (row + store.alignment - 1) & ~static_cast<std::size_t>(store.alignment - 1);
}
} // namespace

namespace vt {
const FaithfulFrame &faithful_frame() {
    if (!s.fatal.empty())
        throw std::runtime_error(s.fatal);
    if (s.begun)
        throw std::runtime_error("Incomplete production glBegin/glEnd command");
    s.frame.textures.clear();
    s.frame.textures.reserve(s.textures.size());
    for (const auto &entry : s.textures)
        if (!entry.second->levels.empty() && entry.second->levels[0].width && entry.second->levels[0].height)
            s.frame.textures.push_back(entry.second);
    return s.frame;
}
std::uint64_t faithful_error_count() { return s.error_count; }
void faithful_end_frame() {
    s.frame = {};
    s.immediate.clear();
}
void faithful_reset() {
    const auto callback = s.readback;
    s = State{};
    s.readback = callback;
}
} // namespace vt

extern "C" {
void VT_SetReadbackCallback(VTReadbackCallback callback) { s.readback = callback; }
GLenum vt_glGetError(void) {
    const auto result = s.error;
    s.error = GL_NO_ERROR;
    return result;
}
void vt_glBegin(GLenum mode) {
    if (!outside())
        return;
    if (!primitive(mode)) {
        error(GL_INVALID_ENUM);
        return;
    }
    s.begun = true;
    s.primitive = mode;
    s.immediate.clear();
}
void vt_glEnd(void) {
    if (!s.begun) {
        error(GL_INVALID_OPERATION);
        return;
    }
    s.begun = false;
    record(s.primitive, s.immediate);
    s.immediate.clear();
}
void vt_glVertex3f(GLfloat x, GLfloat y, GLfloat z) {
    if (!s.begun) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (s.immediate.size() >= max_vertices) {
        resource_error("Immediate primitive exceeds recording limit");
        return;
    }
    s.immediate.push_back(make_vertex({x, y, z, 1}));
}
void vt_glVertex3fv(const GLfloat *v) {
    if (v)
        vt_glVertex3f(v[0], v[1], v[2]);
    else
        error(GL_INVALID_VALUE);
}
void vt_glVertex3d(GLdouble x, GLdouble y, GLdouble z) {
    vt_glVertex3f(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
}
void vt_glVertex3i(GLint x, GLint y, GLint z) {
    vt_glVertex3f(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
}
void vt_glVertex2f(GLfloat x, GLfloat y) { vt_glVertex3f(x, y, 0); }
void vt_glVertex2d(GLdouble x, GLdouble y) { vt_glVertex3d(x, y, 0); }
void vt_glVertex2i(GLint x, GLint y) { vt_glVertex3i(x, y, 0); }
void vt_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    s.color = {r, g, b, a};
    color_material();
}
void vt_glColor4fv(const GLfloat *v) {
    if (v)
        vt_glColor4f(v[0], v[1], v[2], v[3]);
    else
        error(GL_INVALID_VALUE);
}
void vt_glColor3f(GLfloat r, GLfloat g, GLfloat b) { vt_glColor4f(r, g, b, 1); }
void vt_glColor3fv(const GLfloat *v) {
    if (v)
        vt_glColor3f(v[0], v[1], v[2]);
    else
        error(GL_INVALID_VALUE);
}
void vt_glColor3d(GLdouble r, GLdouble g, GLdouble b) {
    vt_glColor3f(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
}
void vt_glColor3ubv(const GLubyte *v) {
    if (v)
        vt_glColor3f(v[0] / 255.f, v[1] / 255.f, v[2] / 255.f);
    else
        error(GL_INVALID_VALUE);
}
void vt_glNormal3fv(const GLfloat *v) {
    if (v)
        s.normal = {v[0], v[1], v[2], 0};
    else
        error(GL_INVALID_VALUE);
}
void vt_glTexCoord2f(GLfloat u, GLfloat v) { s.uv = {u, v, 0, 1}; }
void vt_glTexCoord2fv(const GLfloat *v) {
    if (v)
        vt_glTexCoord2f(v[0], v[1]);
    else
        error(GL_INVALID_VALUE);
}
void vt_glTexCoord2i(GLint u, GLint v) { vt_glTexCoord2f(static_cast<float>(u), static_cast<float>(v)); }
void vt_glEnable(GLenum cap) {
    if (!outside())
        return;
    if (auto *value = capability(cap)) {
        *value = true;
        if (cap == GL_COLOR_MATERIAL)
            color_material();
    }
}
void vt_glDisable(GLenum cap) {
    if (!outside())
        return;
    if (auto *value = capability(cap))
        *value = false;
}
GLboolean vt_glIsEnabled(GLenum cap) {
    if (!outside())
        return GL_FALSE;
    auto *value = capability(cap);
    return value && *value ? GL_TRUE : GL_FALSE;
}
void vt_glMatrixMode(GLenum mode) {
    if (!outside())
        return;
    if (mode == GL_MODELVIEW)
        s.matrix_mode = 0;
    else if (mode == GL_PROJECTION)
        s.matrix_mode = 1;
    else if (mode == GL_TEXTURE)
        s.matrix_mode = 2;
    else
        error(GL_INVALID_ENUM);
}
void vt_glLoadIdentity(void) {
    if (outside())
        matrix() = identity();
}
void vt_glLoadMatrixf(const GLfloat *m) {
    if (!outside())
        return;
    if (m)
        std::copy(m, m + 16, matrix().begin());
    else
        error(GL_INVALID_VALUE);
}
void vt_glMultMatrixf(const GLfloat *m) {
    if (!outside())
        return;
    if (!m) {
        error(GL_INVALID_VALUE);
        return;
    }
    Mat value;
    std::copy(m, m + 16, value.begin());
    matrix() = multiply(matrix(), value);
}
void vt_glPushMatrix(void) {
    if (!outside())
        return;
    auto &stack = s.matrices[s.matrix_mode];
    if (stack.size() >= 64) {
        error(GL_STACK_OVERFLOW);
        return;
    }
    stack.push_back(stack.back());
}
void vt_glPopMatrix(void) {
    if (!outside())
        return;
    auto &stack = s.matrices[s.matrix_mode];
    if (stack.size() == 1) {
        error(GL_STACK_UNDERFLOW);
        return;
    }
    stack.pop_back();
    s.combined_dirty = true;
}
void vt_glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    Mat m = identity();
    m[12] = x;
    m[13] = y;
    m[14] = z;
    vt_glMultMatrixf(m.data());
}
void vt_glScalef(GLfloat x, GLfloat y, GLfloat z) {
    Mat m = identity();
    m[0] = x;
    m[5] = y;
    m[10] = z;
    vt_glMultMatrixf(m.data());
}
void vt_glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z) {
    const double length = std::sqrt(x * x + y * y + z * z);
    if (length == 0)
        return;
    x /= length;
    y /= length;
    z /= length;
    const double a = angle * 3.14159265358979323846 / 180, c = std::cos(a), q = std::sin(a), t = 1 - c;
    Mat m = {static_cast<float>(t * x * x + c),
             static_cast<float>(t * x * y + q * z),
             static_cast<float>(t * x * z - q * y),
             0,
             static_cast<float>(t * x * y - q * z),
             static_cast<float>(t * y * y + c),
             static_cast<float>(t * y * z + q * x),
             0,
             static_cast<float>(t * x * z + q * y),
             static_cast<float>(t * y * z - q * x),
             static_cast<float>(t * z * z + c),
             0,
             0,
             0,
             0,
             1};
    vt_glMultMatrixf(m.data());
}
void vt_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) { vt_glRotated(angle, x, y, z); }
void vt_glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
    if (l == r || b == t || n == f) {
        error(GL_INVALID_VALUE);
        return;
    }
    Mat m = identity();
    m[0] = static_cast<float>(2 / (r - l));
    m[5] = static_cast<float>(2 / (t - b));
    m[10] = static_cast<float>(-2 / (f - n));
    m[12] = static_cast<float>(-(r + l) / (r - l));
    m[13] = static_cast<float>(-(t + b) / (t - b));
    m[14] = static_cast<float>(-(f + n) / (f - n));
    vt_glMultMatrixf(m.data());
}
void vt_glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
    if (l == r || b == t || n == f || n <= 0 || f <= 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    Mat m{};
    m[0] = static_cast<float>(2 * n / (r - l));
    m[5] = static_cast<float>(2 * n / (t - b));
    m[8] = static_cast<float>((r + l) / (r - l));
    m[9] = static_cast<float>((t + b) / (t - b));
    m[10] = static_cast<float>(-(f + n) / (f - n));
    m[11] = -1;
    m[14] = static_cast<float>(-2 * f * n / (f - n));
    vt_glMultMatrixf(m.data());
}
void vt_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    if (!outside())
        return;
    if (w < 0 || h < 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    s.draw.viewport = {x, y, w, h};
}
void vt_glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    if (!outside())
        return;
    if (w < 0 || h < 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    s.draw.scissor = {x, y, w, h};
}
void vt_glShadeModel(GLenum mode) {
    if (!outside())
        return;
    if (mode != GL_FLAT && mode != GL_SMOOTH)
        error(GL_INVALID_ENUM);
    else
        s.shade = mode;
}
void vt_glColorMaterial(GLenum face, GLenum mode) {
    if (!outside())
        return;
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) ||
        (mode != GL_AMBIENT && mode != GL_DIFFUSE && mode != GL_AMBIENT_AND_DIFFUSE && mode != GL_SPECULAR &&
         mode != GL_EMISSION)) {
        error(GL_INVALID_ENUM);
        return;
    }
    s.color_material_face = face;
    s.color_material_mode = mode;
    color_material();
}
void vt_glMaterialfv(GLenum face, GLenum name, const GLfloat *params) {
    if (!params) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (name == GL_SHININESS) {
        vt_glMaterialf(face, name, params[0]);
        return;
    }
    const Vec value{params[0], params[1], params[2], params[3]};
    if (face != GL_BACK)
        apply_material(s.materials[0], name, value);
    if (face != GL_FRONT)
        apply_material(s.materials[1], name, value);
}
void vt_glMaterialf(GLenum face, GLenum name, GLfloat value) {
    if (name != GL_SHININESS || (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK)) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (value < 0 || value > 128 || !std::isfinite(value)) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (face != GL_BACK)
        s.materials[0].shininess = value;
    if (face != GL_FRONT)
        s.materials[1].shininess = value;
}
void vt_glLightfv(GLenum light, GLenum name, const GLfloat *params) {
    if (!outside())
        return;
    if (light < GL_LIGHT0 || light > GL_LIGHT7) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!params) {
        error(GL_INVALID_VALUE);
        return;
    }
    auto &l = s.lights[light - GL_LIGHT0];
    if (name == GL_CONSTANT_ATTENUATION || name == GL_LINEAR_ATTENUATION ||
        name == GL_QUADRATIC_ATTENUATION) {
        if (params[0] < 0) {
            error(GL_INVALID_VALUE);
            return;
        }
        if (name == GL_CONSTANT_ATTENUATION)
            l.constant = params[0];
        else if (name == GL_LINEAR_ATTENUATION)
            l.linear = params[0];
        else
            l.quadratic = params[0];
        return;
    }
    const Vec value{params[0], params[1], params[2], params[3]};
    switch (name) {
    case GL_POSITION:
        l.position = transform(s.matrices[0].back(), value);
        break;
    case GL_AMBIENT:
        l.ambient = value;
        break;
    case GL_DIFFUSE:
        l.diffuse = value;
        break;
    case GL_SPECULAR:
        l.specular = value;
        break;
    default:
        error(GL_INVALID_ENUM);
    }
}
void vt_glLightModelfv(GLenum name, const GLfloat *params) {
    if (!outside())
        return;
    if (!params) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (name == GL_LIGHT_MODEL_AMBIENT)
        s.light_ambient = {params[0], params[1], params[2], params[3]};
    else
        vt_glLightModeli(name, static_cast<int>(params[0]));
}
void vt_glLightModeli(GLenum name, GLint param) {
    if (!outside())
        return;
    if (name == GL_LIGHT_MODEL_TWO_SIDE)
        s.two_side = param != 0;
    else if (name == GL_LIGHT_MODEL_LOCAL_VIEWER)
        s.local_viewer = param != 0;
    else
        error(GL_INVALID_ENUM);
}
void vt_glEnableClientState(GLenum cap) {
    if (!outside())
        return;
    if (auto *a = array(cap))
        a->enabled = true;
}
void vt_glDisableClientState(GLenum cap) {
    if (!outside())
        return;
    if (auto *a = array(cap))
        a->enabled = false;
}
void vt_glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    if (size < 2 || size > 4) {
        error(GL_INVALID_VALUE);
        return;
    }
    array_pointer(s.client.vertex, size, type, stride, pointer);
}
void vt_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) {
    array_pointer(s.client.normal, 3, type, stride, pointer);
}
void vt_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    if (size < 3 || size > 4) {
        error(GL_INVALID_VALUE);
        return;
    }
    array_pointer(s.client.color, size, type, stride, pointer);
}
void vt_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    array_pointer(s.client.uv, size, type, stride, pointer);
}
void vt_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    if (!outside())
        return;
    if (!primitive(mode)) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (count < 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (!count || !s.client.vertex.enabled)
        return;
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!indices || !s.client.vertex.pointer || (s.client.normal.enabled && !s.client.normal.pointer) ||
        (s.client.color.enabled && !s.client.color.pointer) ||
        (s.client.uv.enabled && !s.client.uv.pointer)) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (static_cast<std::size_t>(count) > max_vertices) {
        resource_error("Client-array draw exceeds recording limit");
        return;
    }
    std::vector<vt::ClipVertex> values;
    values.reserve(static_cast<std::size_t>(count));
    const auto *p = static_cast<const unsigned char *>(indices);
    const auto saved_color = s.color, saved_normal = s.normal, saved_uv = s.uv;
    for (int i = 0; i < count; ++i) {
        const std::size_t index = type == GL_UNSIGNED_BYTE ? p[i]
                                  : type == GL_UNSIGNED_SHORT
                                      ? scalar<unsigned short>(p + 2 * i)
                                      : scalar<unsigned>(p + 4 * static_cast<std::size_t>(i));
        // The production arrays are host pointers, as in GL 1.x. Their allocation
        // size is unknown, but unreasonable indices are rejected before arithmetic.
        if (index > max_vertices) {
            error(GL_INVALID_VALUE);
            break;
        }
        if (s.client.color.enabled) {
            s.color = array_value(s.client.color, index, true, {0, 0, 0, 1});
            color_material();
        }
        if (s.client.normal.enabled)
            s.normal = array_value(s.client.normal, index, true, {0, 0, 1, 0});
        if (s.client.uv.enabled)
            s.uv = array_value(s.client.uv, index, false, {0, 0, 0, 1});
        values.push_back(make_vertex(array_value(s.client.vertex, index, false, {0, 0, 0, 1})));
    }
    record(mode, values);
    // GL leaves enabled-array current attributes unspecified. Keep prior current
    // attributes deterministically; every production user resets them explicitly.
    s.color = saved_color;
    s.normal = saved_normal;
    s.uv = saved_uv;
    color_material();
}
void vt_glDepthMask(GLboolean flag) {
    if (outside())
        s.draw.depth_write = flag != 0;
}
void vt_glDepthFunc(GLenum func) {
    if (!outside())
        return;
    if (func < GL_NEVER || func > GL_ALWAYS)
        error(GL_INVALID_ENUM);
    else
        s.draw.depth_func = func;
}
void vt_glCullFace(GLenum face) {
    if (!outside())
        return;
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK)
        error(GL_INVALID_ENUM);
    else
        s.draw.cull_face = face;
}
void vt_glFrontFace(GLenum face) {
    if (!outside())
        return;
    if (face != GL_CW && face != GL_CCW)
        error(GL_INVALID_ENUM);
    else
        s.draw.front_face = face;
}
void vt_glBlendFunc(GLenum src, GLenum dst) {
    if (!outside())
        return;
    auto valid = [](GLenum value) {
        return value == GL_ZERO || value == GL_ONE ||
               (value >= GL_SRC_COLOR && value <= GL_SRC_ALPHA_SATURATE);
    };
    if (!valid(src) || !valid(dst) || dst == GL_SRC_ALPHA_SATURATE) {
        error(GL_INVALID_ENUM);
        return;
    }
    s.draw.blend_src = src;
    s.draw.blend_dst = dst;
}
void vt_glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    if (!outside())
        return;
    if (func < GL_NEVER || func > GL_ALWAYS) {
        error(GL_INVALID_ENUM);
        return;
    }
    s.draw.stencil_func = func;
    s.draw.stencil_ref = std::clamp(ref, 0, 255);
    s.draw.stencil_read_mask = mask;
}
void vt_glStencilMask(GLuint mask) {
    if (outside())
        s.draw.stencil_write_mask = mask;
}
void vt_glStencilOp(GLenum fail, GLenum zfail, GLenum pass) {
    if (!outside())
        return;
    auto valid = [](GLenum v) {
        return v == GL_KEEP || v == GL_ZERO || v == GL_REPLACE || v == GL_INCR || v == GL_DECR ||
               v == GL_INVERT;
    };
    if (!valid(fail) || !valid(zfail) || !valid(pass)) {
        error(GL_INVALID_ENUM);
        return;
    }
    s.draw.stencil_fail = fail;
    s.draw.stencil_depth_fail = zfail;
    s.draw.stencil_pass = pass;
}
void vt_glPolygonOffset(GLfloat factor, GLfloat units) {
    if (outside()) {
        s.draw.polygon_offset_factor = factor;
        s.draw.polygon_offset_units = units;
    }
}
void vt_glPolygonMode(GLenum face, GLenum mode) {
    if (!outside())
        return;
    if (face != GL_FRONT_AND_BACK || (mode != GL_FILL && mode != GL_LINE && mode != GL_POINT)) {
        error(GL_INVALID_ENUM);
        return;
    }
    s.draw.polygon_mode = mode;
}
void vt_glPointSize(GLfloat size) {
    if (!outside())
        return;
    if (size <= 0 || !std::isfinite(size))
        error(GL_INVALID_VALUE);
    else
        s.draw.point_size = size;
}
void vt_glLineWidth(GLfloat size) {
    if (!outside())
        return;
    if (size <= 0 || !std::isfinite(size))
        error(GL_INVALID_VALUE);
    else
        s.draw.line_width = size;
}
void vt_glFogfv(GLenum name, const GLfloat *params) {
    if (!outside())
        return;
    if (!params) {
        error(GL_INVALID_VALUE);
        return;
    }
    switch (name) {
    case GL_FOG_COLOR:
        s.draw.fog_color = bounded({params[0], params[1], params[2], params[3]});
        break;
    case GL_FOG_START:
        s.draw.fog_start = params[0];
        break;
    case GL_FOG_END:
        s.draw.fog_end = params[0];
        break;
    case GL_FOG_DENSITY:
        if (params[0] < 0)
            error(GL_INVALID_VALUE);
        else
            s.draw.fog_density = params[0];
        break;
    case GL_FOG_MODE:
        if (params[0] != GL_LINEAR && params[0] != GL_EXP && params[0] != GL_EXP2)
            error(GL_INVALID_ENUM);
        else
            s.draw.fog_mode = static_cast<GLenum>(params[0]);
        break;
    default:
        error(GL_INVALID_ENUM);
    }
}
void vt_glFogi(GLenum name, GLint value) {
    const GLfloat f = static_cast<float>(value);
    if (name == GL_FOG_COLOR) {
        error(GL_INVALID_ENUM);
        return;
    }
    vt_glFogfv(name, &f);
}
void vt_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    if (outside())
        s.clear_color = bounded({r, g, b, a});
}
void vt_glClearDepth(GLclampd value) {
    if (outside())
        s.clear_depth = clamp(static_cast<float>(value));
}
void vt_glClearStencil(GLint value) {
    if (outside())
        s.clear_stencil = static_cast<unsigned>(value);
}
void vt_glClear(GLbitfield mask) {
    if (!outside())
        return;
    if (mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (!mask)
        return;
    if (s.frame.commands.size() >= max_commands) {
        resource_error("Faithful frame exceeds 65536-command recording limit");
        return;
    }
    vt::ClearCommand clear;
    clear.mask = mask;
    clear.color = s.clear_color;
    clear.depth = s.clear_depth;
    clear.stencil = s.clear_stencil;
    clear.depth_write = s.draw.depth_write;
    clear.stencil_write_mask = s.draw.stencil_write_mask;
    clear.scissor_test = s.draw.scissor_test;
    clear.scissor = s.draw.scissor;
    s.frame.commands.emplace_back(clear);
}
void vt_glGenTextures(GLsizei count, GLuint *names) {
    if (!outside())
        return;
    if (count < 0 || (!names && count)) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (static_cast<std::size_t>(count) + s.reserved.size() + s.textures.size() > 4096) {
        resource_error("Texture registry exceeds 4096-name limit");
        return;
    }
    for (int i = 0; i < count; ++i) {
        while (s.reserved.count(s.next_texture) || s.textures.count(s.next_texture) || !s.next_texture)
            ++s.next_texture;
        names[i] = s.next_texture++;
        s.reserved.insert(names[i]);
    }
}
void vt_glBindTexture(GLenum target, GLuint id) {
    if (!outside())
        return;
    if (target != GL_TEXTURE_2D) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!s.textures.count(id)) {
        if (s.textures.size() >= 4096) {
            resource_error("Texture registry exceeds 4096-object limit");
            return;
        }
        auto t = std::make_shared<vt::Texture>();
        t->id = id;
        t->revision = ++s.revision;
        s.textures.emplace(id, std::move(t));
    }
    s.bound_texture = id;
    s.reserved.erase(id);
}
void vt_glDeleteTextures(GLsizei count, const GLuint *names) {
    if (!outside())
        return;
    if (count < 0 || (!names && count)) {
        error(GL_INVALID_VALUE);
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (!names[i])
            continue;
        s.reserved.erase(names[i]);
        s.textures.erase(names[i]);
        if (s.bound_texture == names[i])
            s.bound_texture = 0;
    }
}
GLboolean vt_glIsTexture(GLuint name) {
    if (!outside())
        return GL_FALSE;
    return name && s.textures.count(name) ? GL_TRUE : GL_FALSE;
}
void vt_glTexParameteri(GLenum target, GLenum name, GLint value) {
    if (!outside())
        return;
    if (target != GL_TEXTURE_2D) {
        error(GL_INVALID_ENUM);
        return;
    }
    bool valid = false;
    if (name == GL_TEXTURE_MIN_FILTER)
        valid = value == GL_NEAREST || value == GL_LINEAR ||
                (value >= GL_NEAREST_MIPMAP_NEAREST && value <= GL_LINEAR_MIPMAP_LINEAR);
    else if (name == GL_TEXTURE_MAG_FILTER)
        valid = value == GL_NEAREST || value == GL_LINEAR;
    else if (name == GL_TEXTURE_WRAP_S || name == GL_TEXTURE_WRAP_T)
        valid = value == GL_REPEAT || value == GL_CLAMP || value == 0x812F;
    else {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!valid) {
        error(GL_INVALID_ENUM);
        return;
    }
    auto *texture = edit_texture();
    if (name == GL_TEXTURE_MIN_FILTER)
        texture->min_filter = static_cast<unsigned>(value);
    else if (name == GL_TEXTURE_MAG_FILTER)
        texture->mag_filter = static_cast<unsigned>(value);
    else if (name == GL_TEXTURE_WRAP_S)
        texture->wrap_s = static_cast<unsigned>(value);
    else
        texture->wrap_t = static_cast<unsigned>(value);
}
void vt_glTexParameterf(GLenum target, GLenum name, GLfloat value) {
    if (name == GL_TEXTURE_MAX_ANISOTROPY_EXT) {
        if (!outside())
            return;
        if (target != GL_TEXTURE_2D) {
            error(GL_INVALID_ENUM);
            return;
        }
        if (value < 1 || !std::isfinite(value)) {
            error(GL_INVALID_VALUE);
            return;
        }
        edit_texture()->anisotropy = value;
    } else
        vt_glTexParameteri(target, name, static_cast<int>(value));
}
void vt_glTexParameterfv(GLenum target, GLenum name, const GLfloat *value) {
    if (!value) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (name == GL_TEXTURE_BORDER_COLOR) {
        if (!outside())
            return;
        if (target != GL_TEXTURE_2D) {
            error(GL_INVALID_ENUM);
            return;
        }
        edit_texture()->border_color = bounded({value[0], value[1], value[2], value[3]});
    } else
        vt_glTexParameterf(target, name, value[0]);
}
void vt_glTexEnvi(GLenum target, GLenum name, GLint value) {
    if (!outside())
        return;
    if (target != GL_TEXTURE_ENV || name != GL_TEXTURE_ENV_MODE ||
        (value != GL_MODULATE && value != GL_DECAL && value != GL_REPLACE)) {
        error(GL_INVALID_ENUM);
        return;
    }
    s.draw.texture_env = static_cast<unsigned>(value);
}
void vt_glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height, GLint border,
                     GLenum format, GLenum type, const GLvoid *pixels) {
    if (!outside())
        return;
    if (target != GL_TEXTURE_2D || type != GL_UNSIGNED_BYTE) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || level > 15 || width < 0 || height < 0 || width > 16384 || height > 16384 ||
        border != 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    int channels = 0;
    switch (format) {
    case GL_RGB:
        channels = 3;
        break;
    case GL_RGBA:
        channels = 4;
        break;
    case GL_ALPHA:
    case GL_LUMINANCE:
        channels = 1;
        break;
    case GL_LUMINANCE_ALPHA:
        channels = 2;
        break;
    default:
        error(GL_INVALID_ENUM);
        return;
    }
    if (internal == 3)
        internal = GL_RGB;
    if (internal == 4)
        internal = GL_RGBA;
    if (internal != GL_RGB && internal != GL_RGBA && internal != GL_ALPHA && internal != GL_LUMINANCE &&
        internal != GL_LUMINANCE_ALPHA) {
        error(GL_INVALID_VALUE);
        return;
    }
    const auto bytes = static_cast<std::size_t>(width) * height * 4;
    std::size_t live = 0;
    for (const auto &entry : s.textures)
        live += texture_bytes(*entry.second);
    const auto &old = s.textures.at(s.bound_texture)->levels;
    if (static_cast<std::size_t>(level) < old.size())
        live -= old[level].rgba.size();
    if (bytes > max_texture_bytes || live > max_texture_bytes - bytes) {
        resource_error("Texture uploads exceed 256 MiB registry limit");
        return;
    }
    vt::TextureLevel image;
    image.width = static_cast<unsigned>(width);
    image.height = static_cast<unsigned>(height);
    image.rgba.resize(bytes, 0);
    if (pixels) {
        const auto stride = pixel_stride(width, channels, s.client.unpack);
        const auto offset = static_cast<std::size_t>(s.client.unpack.skip_rows) * stride +
                            static_cast<std::size_t>(s.client.unpack.skip_pixels) * channels;
        const auto *data = static_cast<const unsigned char *>(pixels) + offset;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const auto *src =
                    data + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * channels;
                auto *dst = image.rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
                dst[0] = dst[1] = dst[2] = 255;
                dst[3] = 255;
                if (format == GL_RGB || format == GL_RGBA) {
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    if (format == GL_RGBA)
                        dst[3] = src[3];
                } else if (format == GL_ALPHA)
                    dst[3] = src[0];
                else {
                    dst[0] = dst[1] = dst[2] = src[0];
                    if (format == GL_LUMINANCE_ALPHA)
                        dst[3] = src[1];
                }
                if (internal == GL_RGB || internal == GL_LUMINANCE)
                    dst[3] = 255;
                if (internal == GL_ALPHA)
                    dst[0] = dst[1] = dst[2] = 255;
                if (internal == GL_LUMINANCE || internal == GL_LUMINANCE_ALPHA)
                    dst[1] = dst[2] = dst[0];
            }
    }
    auto *texture = edit_texture();
    if (level == 0)
        texture->base_format = static_cast<unsigned>(internal);
    if (texture->levels.size() <= static_cast<std::size_t>(level))
        texture->levels.resize(static_cast<std::size_t>(level) + 1);
    texture->levels[level] = std::move(image);
}
void vt_glGetTexLevelParameteriv(GLenum target, GLint level, GLenum name, GLint *value) {
    if (!outside())
        return;
    if (!value) {
        error(GL_INVALID_VALUE);
        return;
    }
    *value = 0;
    if (target != GL_TEXTURE_2D) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || level > 15) {
        error(GL_INVALID_VALUE);
        return;
    }
    const auto &texture = *s.textures.at(s.bound_texture);
    if (name != GL_TEXTURE_WIDTH && name != GL_TEXTURE_HEIGHT && name != GL_TEXTURE_INTERNAL_FORMAT) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (static_cast<std::size_t>(level) >= texture.levels.size())
        return;
    if (name == GL_TEXTURE_WIDTH)
        *value = static_cast<int>(texture.levels[level].width);
    else if (name == GL_TEXTURE_HEIGHT)
        *value = static_cast<int>(texture.levels[level].height);
    else
        *value = static_cast<int>(texture.base_format);
}
void vt_glPixelStorei(GLenum name, GLint value) {
    if (!outside())
        return;
    PixelStore *store = nullptr;
    int field = 0;
    switch (name) {
    case GL_PACK_ALIGNMENT:
        store = &s.client.pack;
        break;
    case GL_UNPACK_ALIGNMENT:
        store = &s.client.unpack;
        break;
    case GL_PACK_ROW_LENGTH:
        store = &s.client.pack;
        field = 1;
        break;
    case GL_UNPACK_ROW_LENGTH:
        store = &s.client.unpack;
        field = 1;
        break;
    case GL_PACK_SKIP_ROWS:
        store = &s.client.pack;
        field = 2;
        break;
    case GL_UNPACK_SKIP_ROWS:
        store = &s.client.unpack;
        field = 2;
        break;
    case GL_PACK_SKIP_PIXELS:
        store = &s.client.pack;
        field = 3;
        break;
    case GL_UNPACK_SKIP_PIXELS:
        store = &s.client.unpack;
        field = 3;
        break;
    default:
        error(GL_INVALID_ENUM);
        return;
    }
    if (value < 0 || value > 1048576 ||
        (field == 0 && value != 1 && value != 2 && value != 4 && value != 8)) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (field == 0)
        store->alignment = value;
    else if (field == 1)
        store->row_length = value;
    else if (field == 2)
        store->skip_rows = value;
    else
        store->skip_pixels = value;
}
void vt_glPushClientAttrib(GLbitfield mask) {
    if (!outside())
        return;
    if (mask != GL_CLIENT_ALL_ATTRIB_BITS &&
        (mask & ~(GL_CLIENT_PIXEL_STORE_BIT | GL_CLIENT_VERTEX_ARRAY_BIT))) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (s.client_stack.size() >= 16) {
        error(GL_STACK_OVERFLOW);
        return;
    }
    s.client_stack.emplace_back(mask, s.client);
}
void vt_glPopClientAttrib(void) {
    if (!outside())
        return;
    if (s.client_stack.empty()) {
        error(GL_STACK_UNDERFLOW);
        return;
    }
    const auto saved = s.client_stack.back();
    s.client_stack.pop_back();
    if (saved.first & GL_CLIENT_PIXEL_STORE_BIT) {
        s.client.pack = saved.second.pack;
        s.client.unpack = saved.second.unpack;
    }
    if (saved.first & GL_CLIENT_VERTEX_ARRAY_BIT) {
        s.client.vertex = saved.second.vertex;
        s.client.normal = saved.second.normal;
        s.client.color = saved.second.color;
        s.client.uv = saved.second.uv;
    }
}
void vt_glReadBuffer(GLenum mode) {
    if (!outside())
        return;
    if (mode != GL_BACK && mode != GL_FRONT) {
        error(GL_INVALID_ENUM);
        return;
    }
    s.read_buffer = mode;
}
void vt_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                     GLvoid *output) {
    if (!outside())
        return;
    if (format != GL_RGB && format != GL_RGBA) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (type != GL_UNSIGNED_BYTE) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0 || width > 16384 || height > 16384 || (!output && width && height)) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (!width || !height)
        return;
    if (!s.readback) {
        error(GL_INVALID_OPERATION);
        return;
    }
    const auto bytes = static_cast<std::size_t>(width) * height * 3;
    if (bytes > max_texture_bytes) {
        resource_error("Readback exceeds 256 MiB limit");
        return;
    }
    std::vector<unsigned char> rgb(bytes);
    if (!s.readback(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data())) {
        error(GL_INVALID_OPERATION);
        return;
    }
    const int channels = format == GL_RGB ? 3 : 4;
    const auto stride = pixel_stride(width, channels, s.client.pack);
    const auto offset = static_cast<std::size_t>(s.client.pack.skip_rows) * stride +
                        static_cast<std::size_t>(s.client.pack.skip_pixels) * channels;
    auto *data = static_cast<unsigned char *>(output) + offset;
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width; ++col) {
            const auto *src = rgb.data() + (static_cast<std::size_t>(row) * width + col) * 3;
            auto *dst =
                data + static_cast<std::size_t>(row) * stride + static_cast<std::size_t>(col) * channels;
            std::memcpy(dst, src, 3);
            if (channels == 4)
                dst[3] = 255;
        }
}
void vt_glRasterPos2i(GLint, GLint) {
    if (!outside())
        return; /* Only a commented-out production call; no bitmap path is used. */
}
void VT_SetFramebufferInfo(int depth_bits, int stencil_bits, int max_texture_size, const char *renderer) {
    s.depth_bits = std::max(0, depth_bits);
    s.stencil_bits = std::max(0, stencil_bits);
    if (max_texture_size > 0)
        s.max_texture_size = std::min(16384, max_texture_size);
    if (renderer)
        s.renderer = renderer;
}
void vt_glGetIntegerv(GLenum name, GLint *value) {
    if (!outside())
        return;
    if (!value) {
        error(GL_INVALID_VALUE);
        return;
    }
    *value = 0;
    switch (name) {
    case GL_VIEWPORT:
        std::copy(s.draw.viewport.begin(), s.draw.viewport.end(), value);
        return;
    case GL_SCISSOR_BOX:
        std::copy(s.draw.scissor.begin(), s.draw.scissor.end(), value);
        return;
    case GL_MAX_TEXTURE_SIZE:
        *value = s.max_texture_size;
        break;
    case GL_MAX_LIGHTS:
        *value = 8;
        break;
    case GL_MAX_MODELVIEW_STACK_DEPTH:
    case GL_MAX_PROJECTION_STACK_DEPTH:
    case GL_MAX_TEXTURE_STACK_DEPTH:
        *value = 64;
        break;
    case GL_MODELVIEW_STACK_DEPTH:
        *value = static_cast<int>(s.matrices[0].size());
        break;
    case GL_PROJECTION_STACK_DEPTH:
        *value = static_cast<int>(s.matrices[1].size());
        break;
    case GL_TEXTURE_STACK_DEPTH:
        *value = static_cast<int>(s.matrices[2].size());
        break;
    case GL_MATRIX_MODE:
        *value = s.matrix_mode == 0 ? GL_MODELVIEW : s.matrix_mode == 1 ? GL_PROJECTION : GL_TEXTURE;
        break;
    case GL_TEXTURE_BINDING_2D:
        *value = static_cast<int>(s.bound_texture);
        break;
    case GL_READ_BUFFER:
        *value = static_cast<int>(s.read_buffer);
        break;
    case GL_DRAW_BUFFER:
        *value = GL_BACK;
        break;
    case GL_DOUBLEBUFFER:
        *value = GL_TRUE;
        break;
    case GL_DEPTH_BITS:
        *value = s.depth_bits;
        break;
    case GL_STENCIL_BITS:
        *value = s.stencil_bits;
        break;
    case GL_RED_BITS:
    case GL_GREEN_BITS:
    case GL_BLUE_BITS:
    case GL_ALPHA_BITS:
        *value = 8;
        break;
    case GL_PACK_ALIGNMENT:
        *value = s.client.pack.alignment;
        break;
    case GL_UNPACK_ALIGNMENT:
        *value = s.client.unpack.alignment;
        break;
    case GL_PACK_ROW_LENGTH:
        *value = s.client.pack.row_length;
        break;
    case GL_UNPACK_ROW_LENGTH:
        *value = s.client.unpack.row_length;
        break;
    case GL_PACK_SKIP_ROWS:
        *value = s.client.pack.skip_rows;
        break;
    case GL_UNPACK_SKIP_ROWS:
        *value = s.client.unpack.skip_rows;
        break;
    case GL_PACK_SKIP_PIXELS:
        *value = s.client.pack.skip_pixels;
        break;
    case GL_UNPACK_SKIP_PIXELS:
        *value = s.client.unpack.skip_pixels;
        break;
    case GL_SHADE_MODEL:
        *value = static_cast<int>(s.shade);
        break;
    case GL_DEPTH_WRITEMASK:
        *value = s.draw.depth_write;
        break;
    case GL_DEPTH_FUNC:
        *value = static_cast<int>(s.draw.depth_func);
        break;
    case GL_BLEND_SRC:
        *value = static_cast<int>(s.draw.blend_src);
        break;
    case GL_BLEND_DST:
        *value = static_cast<int>(s.draw.blend_dst);
        break;
    case GL_CULL_FACE_MODE:
        *value = static_cast<int>(s.draw.cull_face);
        break;
    case GL_FRONT_FACE:
        *value = static_cast<int>(s.draw.front_face);
        break;
    case GL_STENCIL_REF:
        *value = s.draw.stencil_ref;
        break;
    case GL_STENCIL_VALUE_MASK:
        *value = static_cast<int>(s.draw.stencil_read_mask);
        break;
    case GL_STENCIL_WRITEMASK:
        *value = static_cast<int>(s.draw.stencil_write_mask);
        break;
    case GL_LIGHT_MODEL_TWO_SIDE:
        *value = s.two_side;
        break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
        *value = s.local_viewer;
        break;
    case GL_POLYGON_MODE:
        value[0] = value[1] = static_cast<int>(s.draw.polygon_mode);
        break;
    case GL_VERTEX_ARRAY:
    case GL_NORMAL_ARRAY:
    case GL_COLOR_ARRAY:
    case GL_TEXTURE_COORD_ARRAY:
        *value = array(name)->enabled;
        break;
    default:
        if (auto *enabled = capability(name))
            *value = *enabled;
        break;
    }
}
void vt_glGetFloatv(GLenum name, GLfloat *value) {
    if (!outside())
        return;
    if (!value) {
        error(GL_INVALID_VALUE);
        return;
    }
    const Mat *m = nullptr;
    if (name == GL_MODELVIEW_MATRIX)
        m = &s.matrices[0].back();
    else if (name == GL_PROJECTION_MATRIX)
        m = &s.matrices[1].back();
    else if (name == GL_TEXTURE_MATRIX)
        m = &s.matrices[2].back();
    if (m) {
        std::copy(m->begin(), m->end(), value);
        return;
    }
    switch (name) {
    case GL_CURRENT_COLOR:
        std::copy(s.color.begin(), s.color.end(), value);
        return;
    case GL_CURRENT_NORMAL:
        std::copy(s.normal.begin(), s.normal.begin() + 3, value);
        return;
    case GL_CURRENT_TEXTURE_COORDS:
        std::copy(s.uv.begin(), s.uv.end(), value);
        return;
    case GL_LIGHT_MODEL_AMBIENT:
        std::copy(s.light_ambient.begin(), s.light_ambient.end(), value);
        return;
    case GL_COLOR_CLEAR_VALUE:
        std::copy(s.clear_color.begin(), s.clear_color.end(), value);
        return;
    case GL_DEPTH_CLEAR_VALUE:
        *value = s.clear_depth;
        return;
    case GL_POINT_SIZE:
        *value = s.draw.point_size;
        return;
    case GL_LINE_WIDTH:
        *value = s.draw.line_width;
        return;
    case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
        *value = 1;
        return;
    default:
        break;
    }
    GLint values[4]{};
    vt_glGetIntegerv(name, values);
    const int count = name == GL_VIEWPORT || name == GL_SCISSOR_BOX ? 4 : name == GL_POLYGON_MODE ? 2 : 1;
    for (int i = 0; i < count; ++i)
        value[i] = static_cast<float>(values[i]);
}
void vt_glGetDoublev(GLenum name, GLdouble *value) {
    if (!value) {
        error(GL_INVALID_VALUE);
        return;
    }
    GLfloat values[16]{};
    vt_glGetFloatv(name, values);
    const int count = name == GL_MODELVIEW_MATRIX || name == GL_PROJECTION_MATRIX || name == GL_TEXTURE_MATRIX
                          ? 16
                      : name == GL_VIEWPORT || name == GL_SCISSOR_BOX || name == GL_CURRENT_COLOR ||
                              name == GL_CURRENT_TEXTURE_COORDS || name == GL_LIGHT_MODEL_AMBIENT ||
                              name == GL_COLOR_CLEAR_VALUE
                          ? 4
                      : name == GL_CURRENT_NORMAL ? 3
                      : name == GL_POLYGON_MODE   ? 2
                                                  : 1;
    for (int i = 0; i < count; ++i)
        value[i] = values[i];
}
void vt_glGetBooleanv(GLenum name, GLboolean *value) {
    if (!value) {
        error(GL_INVALID_VALUE);
        return;
    }
    GLint values[4]{};
    vt_glGetIntegerv(name, values);
    const int count = name == GL_VIEWPORT || name == GL_SCISSOR_BOX ? 4 : name == GL_POLYGON_MODE ? 2 : 1;
    for (int i = 0; i < count; ++i)
        value[i] = values[i] ? GL_TRUE : GL_FALSE;
}
const GLubyte *vt_glGetString(GLenum name) {
    if (!outside())
        return nullptr;
    const char *result = nullptr;
    switch (name) {
    case GL_VENDOR:
        result = "VulkanTron";
        break;
    case GL_RENDERER:
        result = s.renderer.c_str();
        break;
    case GL_VERSION:
        result = "VulkanTron game-specific adapter (direct Vulkan 1.3)";
        break;
    case GL_EXTENSIONS:
        result = "";
        break;
    default:
        error(GL_INVALID_ENUM);
        return nullptr;
    }
    return reinterpret_cast<const GLubyte *>(result);
}
} // extern C
