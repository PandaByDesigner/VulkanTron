#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace vt {
struct FaithfulFrame;
struct Vertex {
    float position[3];
    float color[4];
};

struct Frame {
    std::vector<Vertex> vertices;
    // Column-major Vulkan clip coordinates: depth 0..1; projection flips Y.
    std::array<float, 16> view_projection{};
};

struct RenderStats {
    std::string device;
    std::uint32_t width = 0, height = 0;
    std::uint32_t depth_bits = 0, stencil_bits = 0, max_texture_size = 0;
    std::uint64_t frames = 0;
    std::uint32_t validation_errors = 0;
};

class Renderer {
public:
    Renderer(SDL_Window* window, const std::filesystem::path& shaders, bool validation);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    // Returns false when the drawable cannot currently be rendered. A nonempty
    // capture path writes this complete frame, exclusively (never overwrites).
    bool draw(const Frame& frame, const std::filesystem::path& capture = {});
    // Execute the recorded classic frame exactly once, then present it. Optional
    // readback is complete framebuffer RGB in the game's bottom-up row order.
    bool draw(const FaithfulFrame& frame, std::vector<std::uint8_t>* bottom_up_rgb = nullptr);
    void wait_idle();
    // Idempotent finalization; stats remain readable, including cleanup errors.
    void shutdown();
    const RenderStats& stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
