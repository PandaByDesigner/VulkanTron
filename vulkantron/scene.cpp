#include "scene.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace vt {
namespace {
constexpr std::size_t kFrameVertices = 1024 * 1024;
constexpr std::size_t kMeshVertices = 200000;
constexpr std::size_t kFileBytes = 32 * 1024 * 1024;
constexpr float kPi = 3.14159265358979323846f;
struct V3 { float x, y, z; };
using Color = std::array<float, 4>;
using Matrix = std::array<float, 16>;
constexpr Color kCycleColors[4] = {
    {1, .55f, .14f, 1}, {.75f, .02f, .02f, 1},
    {.12f, .52f, .60f, 1}, {.8f, .8f, .8f, 1}
};
constexpr Color kTrailColors[4] = {
    {1, .85f, .14f, 1}, {.75f, .02f, .02f, 1},
    {.12f, .52f, .60f, 1}, {.7f, .7f, .7f, 1}
};
V3 operator+(V3 a, V3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
V3 operator*(V3 a, float b) { return {a.x*b, a.y*b, a.z*b}; }
float dot(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
V3 cross(V3 a, V3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
V3 normalized(V3 a) {
    const float n = std::sqrt(dot(a, a));
    if (!std::isfinite(n) || n < 1e-8f) throw std::runtime_error("Scene camera has no usable direction");
    return a * (1/n);
}
void finite(float value, const char* label, float maximum = 10000000) {
    if (!std::isfinite(value) || std::abs(value) > maximum)
        throw std::runtime_error(std::string("Invalid scene ") + label);
}
void finite(V3 value, const char* label) {
    finite(value.x, label); finite(value.y, label); finite(value.z, label);
}

// Files and individual records are bounded before allocating or tokenizing them.
class Lines {
public:
    explicit Lines(const std::filesystem::path& path) : path_(path), file_(path, std::ios::binary) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || !file_)
            fail("cannot open regular asset file");
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > kFileBytes) fail("asset exceeds 32 MiB limit");
    }
    bool next(std::istringstream& input) {
        char buffer[4098];
        if (!file_.getline(buffer, sizeof(buffer))) {
            if (file_.eof() && file_.gcount() == 0) return false;
            if (!file_.eof()) fail("record exceeds 4096 bytes or read failed");
        }
        ++line_;
        const auto bytes = static_cast<std::size_t>(file_.gcount());
        total_ += bytes;
        if (total_ > kFileBytes || bytes > 4097) fail("asset record limit exceeded");
        // gcount includes a consumed newline, but does not include the NUL terminator.
        const std::size_t length = bytes - (!file_.eof() && bytes ? 1 : 0);
        std::string text(buffer, length);
        if (text.find('\0') != std::string::npos) fail("NUL in text asset");
        const auto comment = text.find('#');
        if (comment != std::string::npos) text.resize(comment);
        input.clear(); input.str(text); input.imbue(std::locale::classic());
        return true;
    }
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(path_.string() + ":" + std::to_string(line_) + ": " + message);
    }
private:
    std::filesystem::path path_;
    std::ifstream file_;
    std::size_t line_ = 0, total_ = 0;
};
void end_record(std::istringstream& input, const Lines& lines) {
    std::string extra;
    if (input >> extra) lines.fail("unexpected trailing fields");
}
float number(std::istringstream& input, const Lines& lines, float limit = 100000) {
    float value;
    if (!(input >> value) || !std::isfinite(value) || std::abs(value) > limit)
        lines.fail("invalid or unbounded numeric field");
    return value;
}
std::string name(std::istringstream& input, const Lines& lines) {
    std::string value;
    if (!(input >> value) || value.size() > 128) lines.fail("missing or overlong name");
    return value;
}
struct Material { Color diffuse = {1, 1, 0, 1}; bool hull = false; };
using Materials = std::unordered_map<std::string, Material>;
void read_materials(const std::filesystem::path& path, Materials& materials) {
    Lines lines(path);
    std::istringstream input;
    Material* current = nullptr;
    while (lines.next(input)) {
        std::string command;
        if (!(input >> command)) continue;
        if (command == "newmtl") {
            const auto label = name(input, lines);
            end_record(input, lines);
            if (materials.size() >= 256) lines.fail("too many materials");
            auto entry = materials.emplace(label, Material{});
            if (!entry.second) lines.fail("duplicate material: " + label);
            current = &entry.first->second;
            current->hull = label == "Hull";
        } else if (command == "Kd") {
            if (!current) lines.fail("Kd before newmtl");
            for (int i = 0; i < 3; ++i) {
                current->diffuse[i] = number(input, lines, 1);
                if (current->diffuse[i] < 0) lines.fail("negative diffuse color");
            }
            end_record(input, lines);
        }
        // Bootstrap uses diffuse color only: ambient/specular/maps are not rendered.
    }
}
std::size_t index(const std::string& token, std::size_t count, const Lines& lines) {
    long long value = 0;
    const auto parsed = std::from_chars(token.data(), token.data()+token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data()+token.size() || !value)
        lines.fail("invalid OBJ index");
    if (value < 0) {
        if (value < -static_cast<long long>(count)) lines.fail("OBJ index out of range");
        return static_cast<std::size_t>(static_cast<long long>(count)+value);
    }
    if (static_cast<unsigned long long>(value) > count) lines.fail("OBJ index out of range");
    return static_cast<std::size_t>(value-1);
}
std::size_t face_index(const std::string& token, std::size_t positions,
                       std::size_t texcoords, std::size_t normals, const Lines& lines) {
    const auto slash = token.find('/');
    if (slash == std::string::npos) return index(token, positions, lines);
    const auto result = index(token.substr(0, slash), positions, lines);
    const auto second = token.find('/', slash+1);
    if (second == std::string::npos) {
        index(token.substr(slash+1), texcoords, lines);
    } else {
        if (token.find('/', second+1) != std::string::npos) lines.fail("too many OBJ index components");
        if (second > slash+1) index(token.substr(slash+1, second-slash-1), texcoords, lines);
        index(token.substr(second+1), normals, lines);
    }
    return result;
}
void check_polygon(const std::vector<V3>& face, const Lines& lines) {
    if (face.size() == 3) return; // Retain original triangles, including degenerate ones.
    V3 normal{};
    for (std::size_t i = 0; i < face.size(); ++i)
        normal = normal + cross(face[i], face[(i+1)%face.size()]);
    const float length = std::sqrt(dot(normal, normal));
    if (!std::isfinite(length) || length < 1e-7f) lines.fail("degenerate polygon");
    normal = normal * (1/length);
    float scale = 1;
    for (const auto p : face) scale = std::max(scale, std::sqrt(dot(p-face[0], p-face[0])));
    for (std::size_t i = 0; i < face.size(); ++i) {
        if (std::abs(dot(face[i]-face[0], normal)) > scale*1e-4f)
            lines.fail("nonplanar polygon is unsupported");
        // Every other point must lie inside each directed edge, rejecting concave
        // and self-intersecting polygons instead of silently producing a bad fan.
        const V3 edge = face[(i+1)%face.size()]-face[i];
        for (const auto p : face)
            if (dot(cross(edge, p-face[i]), normal) < -scale*scale*1e-5f)
                lines.fail("nonconvex polygon is unsupported");
    }
}
Vertex vertex(V3 p, Color c) { return {{p.x,p.y,p.z}, {c[0],c[1],c[2],c[3]}}; }
void quad(std::vector<Vertex>& out, V3 a, V3 b, V3 c, V3 d, Color color) {
    for (auto p : {a,b,c,a,c,d}) out.push_back(vertex(p,color));
}
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k) out[col*4+row] += a[k*4+row]*b[col*4+k];
    return out;
}
Matrix camera(V3 eye, V3 target, float aspect, float fov, float near_plane, float far_plane) {
    finite(eye, "camera eye"); finite(target, "camera target");
    const V3 forward = normalized(target-eye);
    V3 right = cross(forward, {0,0,1});
    if (dot(right,right) < 1e-8f) right = cross(forward, {0,1,0});
    right = normalized(right);
    const V3 up = cross(right, forward);
    const Matrix view = {
        right.x,up.x,-forward.x,0, right.y,up.y,-forward.y,0,
        right.z,up.z,-forward.z,0, -dot(right,eye),-dot(up,eye),dot(forward,eye),1
    };
    const float cotangent = 1/std::tan(fov*kPi/360);
    Matrix projection{};
    projection[0] = cotangent/aspect;
    projection[5] = -cotangent;
    projection[10] = far_plane/(near_plane-far_plane);
    projection[11] = -1;
    projection[14] = far_plane*near_plane/(near_plane-far_plane);
    const auto result = multiply(projection, view);
    for (float value : result)
        if (!std::isfinite(value)) throw std::runtime_error("Invalid scene camera projection");
    return result;
}
V3 rotate(V3 p, int direction) {
    switch (direction) {
    case 0: return p;                      // Original mesh points toward -Y.
    case 1: return {p.y,-p.x,p.z};          // -90 degrees
    case 2: return {-p.x,-p.y,p.z};         // -180 degrees
    case 3: return {-p.y,p.x,p.z};          // +90 degrees
    default: throw std::runtime_error("Invalid classic player direction");
    }
}
} // namespace

Scene::Scene(const std::filesystem::path& asset_root) {
    const auto path = asset_root / "data/lightcycle-high.obj";
    Lines lines(path);
    std::istringstream input;
    std::vector<V3> positions;
    std::size_t normals = 0, texcoords = 0, libraries = 0;
    Materials materials;
    Material material;
    float low_z = std::numeric_limits<float>::max(), high_z = -low_z;
    while (lines.next(input)) {
        std::string command;
        if (!(input >> command)) continue;
        if (command == "v" || command == "vn") {
            const V3 p = {number(input,lines), number(input,lines), number(input,lines)};
            end_record(input, lines);
            if (command == "v") {
                if (positions.size() >= 100000) lines.fail("too many OBJ positions");
                positions.push_back(p);
                low_z = std::min(low_z,p.z); high_z = std::max(high_z,p.z);
            } else if (++normals > 200000) lines.fail("too many OBJ normals");
        } else if (command == "vt") {
            number(input,lines);
            int components = 1;
            while (input >> std::ws && !input.eof()) {
                if (++components > 3) lines.fail("too many texture coordinate components");
                number(input,lines);
            }
            if (++texcoords > 200000) lines.fail("too many texture coordinates");
        } else if (command == "mtllib") {
            const auto filename = name(input,lines);
            end_record(input,lines);
            if (++libraries > 8) lines.fail("too many material libraries");
            if (filename == "." || filename == ".." || filename.find_first_of("/\\:") != std::string::npos)
                lines.fail("material library must be a filename beside the OBJ");
            read_materials(path.parent_path()/filename, materials);
        } else if (command == "usemtl") {
            const auto label = name(input,lines);
            end_record(input,lines);
            const auto found = materials.find(label);
            if (found == materials.end()) lines.fail("unknown material: " + label);
            material = found->second;
        } else if (command == "f") {
            std::vector<V3> face;
            std::string token;
            while (input >> token) {
                if (face.size() == 64) lines.fail("face exceeds 64 corners");
                face.push_back(positions[face_index(token,positions.size(),texcoords,normals,lines)]);
            }
            if (face.size() < 3) lines.fail("face needs at least three corners");
            check_polygon(face,lines);
            const std::size_t added = (face.size()-2)*3;
            if (added > kMeshVertices-cycle_.size()) lines.fail("cycle mesh exceeds geometry budget");
            Color color = material.diffuse;
            // Internal-only marker preserves Hull's per-player material override.
            color[3] = material.hull ? -1.0f : 1.0f;
            for (std::size_t i = 1; i+1 < face.size(); ++i)
                for (auto p : {face[0],face[i],face[i+1]}) cycle_.push_back(vertex(p,color));
        } else if (command != "g" && command != "o" && command != "s") {
            lines.fail("unsupported OBJ record: " + command);
        }
    }
    if (cycle_.empty()) lines.fail("cycle asset contains no triangles");
    // Original drawModel lifts by half the bounding-box height, without rescaling.
    const float lift = (high_z-low_z)*.5f;
    for (auto& v : cycle_) v.position[2] += lift;
}

Frame Scene::frame(const VTSnapshot& state, float aspect, bool overview) const {
    finite(aspect,"aspect ratio",10000);
    finite(state.grid_size,"grid size",100000);
    if (aspect <= 0 || state.grid_size < 1) throw std::runtime_error("Scene requires positive aspect and arena size");
    // Fail explicitly instead of truncating a production trail or allocating without bound.
    if (state.trail_count > kFrameVertices/6)
        throw std::runtime_error("Scene trail geometry exceeds 1048576-vertex rendering budget");
    if (state.trail_count && !state.trails) throw std::runtime_error("Missing classic trail snapshot");
    std::size_t visible_trails = 0, live_players = 0;
    for (std::size_t i = 0; i < state.trail_count; ++i) {
        const auto& t = state.trails[i];
        for (float x : t.start) finite(x,"trail start");
        for (float x : t.end) finite(x,"trail end");
        finite(t.height,"trail height");
        if (t.player < 0 || t.player >= 4) throw std::runtime_error("Invalid classic trail owner");
        if (t.height > 0 && (t.start[0] != t.end[0] || t.start[1] != t.end[1])) ++visible_trails;
    }
    for (const auto& player : state.players) {
        finite(player.x,"player X"); finite(player.y,"player Y");
        if (player.direction < 0 || player.direction > 3) throw std::runtime_error("Invalid classic player direction");
        if (player.alive) ++live_players;
    }
    const float g = state.grid_size;
    const auto grid_lines = static_cast<std::size_t>(std::floor(g/20))+1;
    const std::size_t required = 30 + grid_lines*12 + visible_trails*6 + live_players*cycle_.size();
    if (required > kFrameVertices)
        throw std::runtime_error("Scene exceeds 1048576-vertex rendering budget; simulation was not changed");
    Frame out;
    out.vertices.reserve(required);
    const auto& p0 = state.players[0];
    const V3 eye = overview ? V3{g*.5f,-g*.6f,g*1.2f} : V3{p0.eye[0],p0.eye[1],p0.eye[2]};
    const V3 target = overview ? V3{g*.5f,g*.5f,0} : V3{p0.target[0],p0.target[1],p0.target[2]};
    // Overview is far above the arena: scale its near plane to preserve depth
    // precision for the floor grid. The gameplay camera keeps the original .5.
    out.view_projection = camera(eye,target,aspect,overview ? 55.0f : 105.0f,
                                 overview ? g*.05f : .5f,g*6.5f);

    // First Vulkan slice: original geometry and logical dimensions, with the
    // classic untextured grid. Textured walls/floor, smooth/specular lighting,
    // transparent trails, turn lean, reflections, explosions and HUD are pending.
    quad(out.vertices,{0,0,0},{g,0,0},{g,g,0},{0,g,0},{.005f,.005f,.005f,1});
    constexpr float half_line = .08f, line_z = .015f;
    for (std::size_t i = 0; i < grid_lines; ++i) {
        const float point = static_cast<float>(i)*20;
        const float a = std::max(0.0f,point-half_line), b = std::min(g,point+half_line);
        quad(out.vertices,{a,0,line_z},{b,0,line_z},{b,g,line_z},{a,g,line_z},{.65f,.65f,.65f,1});
        quad(out.vertices,{0,a,line_z},{g,a,line_z},{g,b,line_z},{0,b,line_z},{.65f,.65f,.65f,1});
    }
    const float h = g*.2f; // Classic stretch_textures wall height: grid_size / 240 * 48.
    const Color wall = {.055f,.065f,.09f,1};
    quad(out.vertices,{0,0,0},{0,0,h},{g,0,h},{g,0,0},wall);
    quad(out.vertices,{g,0,0},{g,0,h},{g,g,h},{g,g,0},wall);
    quad(out.vertices,{g,g,0},{g,g,h},{0,g,h},{0,g,0},wall);
    quad(out.vertices,{0,g,0},{0,g,h},{0,0,h},{0,0,0},wall);
    for (std::size_t i = 0; i < state.trail_count; ++i) {
        const auto& t = state.trails[i];
        if (t.height <= 0 || (t.start[0] == t.end[0] && t.start[1] == t.end[1])) continue;
        quad(out.vertices,{t.start[0],t.start[1],0},{t.end[0],t.end[1],0},
             {t.end[0],t.end[1],t.height},{t.start[0],t.start[1],t.height},kTrailColors[t.player]);
    }
    const V3 light = normalized({.35f,-.45f,.82f});
    for (int player = 0; player < 4; ++player) {
        const auto& p = state.players[player];
        if (!p.alive) continue;
        for (std::size_t i = 0; i < cycle_.size(); i += 3) {
            V3 triangle[3];
            for (int j = 0; j < 3; ++j) {
                const auto& v = cycle_[i+j];
                triangle[j] = rotate({v.position[0],v.position[1],v.position[2]},p.direction);
            }
            const V3 normal = cross(triangle[1]-triangle[0],triangle[2]-triangle[0]);
            const float norm = std::sqrt(dot(normal,normal));
            const float shade = .4f + (norm > 1e-8f ? .6f*std::max(0.0f,dot(normal,light)/norm) : 0);
            for (int j = 0; j < 3; ++j) {
                const auto& v = cycle_[i+j];
                Color c = v.color[3] < 0 ? kCycleColors[player] : Color{v.color[0],v.color[1],v.color[2],1};
                for (int channel = 0; channel < 3; ++channel) c[channel] *= shade;
                out.vertices.push_back(vertex(triangle[j]+V3{p.x,p.y,0},c));
            }
        }
    }
    return out;
}
} // namespace vt
