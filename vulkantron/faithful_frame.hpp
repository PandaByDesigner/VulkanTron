#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace vt {
// Game-specific fixed-function recording. Enum fields retain the GL numeric
// tokens from the production draw calls; this header has no OpenGL dependency.
struct ClipVertex {
    float position[4]; // Vulkan clip coordinates: Y flipped, Z=(GL Z+W)/2.
    float front_color[4];
    float back_color[4];
    float uv[2];
    float fog_distance; // Absolute eye-space Z, before projection.
    float gl_clip_z; // Original clip Z for optional native -W..W depth clipping.
};
enum class Primitive { TriangleList, LineList, PointList };
struct TextureLevel {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba; // Original upload row order; never flipped.
};
struct Texture {
    std::uint32_t id = 0;
    std::uint64_t revision = 0;
    std::uint32_t base_format = 0x1908; // RGBA or original RGB/LUMINANCE/ALPHA.
    std::array<float,4> border_color = {0,0,0,0};
    std::vector<TextureLevel> levels;
    std::uint32_t min_filter = 0x2702, mag_filter = 0x2601; // NEAREST_MIPMAP_LINEAR / LINEAR
    std::uint32_t wrap_s = 0x2901, wrap_t = 0x2901; // REPEAT
    float anisotropy = 1;
};
struct DrawState {
    std::array<int,4> viewport = {0,0,1,1}; // GL bottom-left pixel coordinates.
    std::array<int,4> scissor = {0,0,1,1};
    bool scissor_test = false;
    bool depth_test = false, depth_write = true;
    std::uint32_t depth_func = 0x0201; // LESS
    bool cull = false;
    std::uint32_t cull_face = 0x0405, front_face = 0x0901; // BACK / CCW in GL coordinates
    bool blend = false;
    std::uint32_t blend_src = 1, blend_dst = 0;
    bool stencil = false;
    std::uint32_t stencil_func = 0x0207, stencil_read_mask = ~0u, stencil_write_mask = ~0u;
    int stencil_ref = 0;
    std::uint32_t stencil_fail = 0x1E00, stencil_depth_fail = 0x1E00, stencil_pass = 0x1E00;
    bool polygon_offset = false;
    float polygon_offset_factor = 0, polygon_offset_units = 0;
    std::uint32_t polygon_mode = 0x1B02; // FILL; production changes FRONT_AND_BACK together.
    float line_width = 1, point_size = 1;
    bool line_smooth = false;
    bool texture_enabled = false;
    std::shared_ptr<const Texture> texture;
    std::uint32_t texture_env = 0x2100; // MODULATE (or DECAL in production).
    bool fog = false;
    std::uint32_t fog_mode = 0x0800; // EXP default; production selects LINEAR.
    float fog_start = 0, fog_end = 1, fog_density = 1;
    std::array<float,4> fog_color = {0,0,0,0};
};
struct DrawBatch {
    Primitive primitive = Primitive::TriangleList;
    DrawState state;
    std::vector<ClipVertex> vertices;
};
struct ClearCommand {
    std::uint32_t mask = 0; // COLOR_BUFFER_BIT / DEPTH_BUFFER_BIT / STENCIL_BUFFER_BIT.
    std::array<float,4> color = {0,0,0,0};
    float depth = 1;
    std::uint32_t stencil = 0;
    bool depth_write = true;
    std::uint32_t stencil_write_mask = ~0u;
    bool scissor_test = false;
    std::array<int,4> scissor = {0,0,1,1};
};
using FaithfulCommand = std::variant<ClearCommand,DrawBatch>;
struct FaithfulFrame {
    std::vector<FaithfulCommand> commands;
    // Full live registry, cheap immutable snapshots. Draws also retain a version
    // if the same texture is subsequently edited/deleted in the same frame.
    std::vector<std::shared_ptr<const Texture>> textures;
    std::uint64_t vertices = 0;
};
const FaithfulFrame& faithful_frame();
std::uint64_t faithful_error_count(); // Includes errors drained by production glGetError.
void faithful_end_frame(); // Drop commands, preserve all current GL state/textures.
void faithful_reset();     // Fresh context, including default matrices and materials.
}
