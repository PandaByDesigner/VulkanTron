#include "faithful_platform.h"
#include "renderer.hpp"
#include "faithful_frame.hpp"
#include "fixed_function.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::unique_ptr<vt::Renderer> renderer;
std::vector<std::uint8_t> readback;
bool rendered = false;
bool failed = false;
unsigned int validation_errors = 0;
std::uint64_t adapter_errors = 0;
std::string default_config, default_screenshots;

bool enabled(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] && std::strcmp(value, "0") != 0;
}

std::filesystem::path shader_directory() {
    if (const char* path = std::getenv("VULKANTRON_SHADER_DIR"); path && path[0])
        return path;
    const char* base = SDL_GetBasePath();
    if (!base) throw std::runtime_error(SDL_GetError());
    auto adjacent = std::filesystem::path(base) / "vulkantron-shaders";
    if (std::filesystem::exists(adjacent / "faithful.vert.spv")) return adjacent;
    // CMake's native verification tools live at the build root, above bin.
    auto test_adjacent = std::filesystem::path(base) / "bin/vulkantron-shaders";
    if (std::filesystem::exists(test_adjacent / "faithful.vert.spv")) return test_adjacent;
    return std::filesystem::path(base) / "../share/vulkantron/shaders";
}

int capture_pixels(int x, int y, int width, int height,
                   unsigned format, unsigned type, void* output) {
    try {
        if (!renderer || !output || width < 0 || height < 0 || x < 0 || y < 0 ||
            format != GL_RGB || type != GL_UNSIGNED_BYTE) return 0;
        if (!rendered) {
            if (!renderer->draw(vt::faithful_frame(), &readback)) return 0;
            rendered = true;
        }
        const auto& stats = renderer->stats();
        if (std::uint64_t(x) + width > stats.width ||
            std::uint64_t(y) + height > stats.height ||
            readback.size() != std::size_t(stats.width) * stats.height * 3) return 0;
        auto* destination = static_cast<unsigned char*>(output);
        for (int row = 0; row < height; ++row)
            std::memcpy(destination + std::size_t(row) * width * 3,
                        readback.data() + (std::size_t(y + row) * stats.width + x) * 3,
                        std::size_t(width) * 3);
        return 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[VulkanTron] readback failed: %s\n", error.what());
        failed = true;
        return 0;
    }
}
}

extern "C" int VT_FaithfulInitialize(SDL_Window* window) {
    try {
        if (renderer) throw std::runtime_error("renderer already initialized");
        vt::faithful_reset();
        rendered = false;
        readback.clear();
        renderer = std::make_unique<vt::Renderer>(window, shader_directory(),
                                                 enabled("VULKANTRON_VALIDATION"));
        const auto& stats = renderer->stats();
        VT_SetFramebufferInfo(stats.depth_bits, stats.stencil_bits,
                              stats.max_texture_size, stats.device.c_str());
        VT_SetReadbackCallback(capture_pixels);
        std::fprintf(stderr, "[VulkanTron] direct Vulkan on %s\n",
                     renderer->stats().device.c_str());
        return 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[VulkanTron] initialization failed: %s\n", error.what());
        renderer.reset();
        return 0;
    }
}

extern "C" void VT_FaithfulSwap() {
    try {
        if (!renderer) throw std::runtime_error("swap without a Vulkan renderer");
        if (!rendered) renderer->draw(vt::faithful_frame());
        rendered = false;
        readback.clear();
        vt::faithful_end_frame();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[VulkanTron] rendering failed: %s\n", error.what());
        failed = true;
        VT_FaithfulShutdown();
        std::exit(EXIT_FAILURE);
    }
}

extern "C" int VT_FaithfulShutdown() {
    VT_SetReadbackCallback(nullptr);
    if (renderer) {
        try {
            renderer->shutdown();
        } catch (const std::exception& error) {
            std::fprintf(stderr, "[VulkanTron] shutdown failed: %s\n", error.what());
            failed = true;
        }
        const auto stats = renderer->stats();
        validation_errors += stats.validation_errors;
        adapter_errors += vt::faithful_error_count();
        renderer.reset();
        readback.clear();
        rendered = false;
        try { vt::faithful_reset(); }
        catch (const std::exception& error) {
            std::fprintf(stderr, "[VulkanTron] recorder cleanup failed: %s\n", error.what());
            failed = true;
        }
        std::fprintf(stderr, "VT_FAITHFUL_STATS backend=direct-vulkan device=\"%s\" "
                     "frames=%llu drawable=%ux%u validation_errors=%u adapter_errors=%llu failed=%d\n",
                     stats.device.c_str(), static_cast<unsigned long long>(stats.frames),
                     stats.width, stats.height, validation_errors,
                     static_cast<unsigned long long>(adapter_errors), failed ? 1 : 0);
    }
    return !failed && validation_errors == 0 && adapter_errors == 0;
}

extern "C" unsigned int VT_FaithfulValidationErrors() {
    return validation_errors + (renderer ? renderer->stats().validation_errors : 0);
}

extern "C" const char* VT_FaithfulDefaultDirectory(int screenshots) {
    try {
        std::string& result = screenshots ? default_screenshots : default_config;
        if (!result.empty()) return result.c_str();
        const char* home = std::getenv("HOME");
        if (!home || !home[0]) throw std::runtime_error("HOME is not set");
        const char* xdg = std::getenv(screenshots ? "XDG_STATE_HOME" : "XDG_CONFIG_HOME");
        std::filesystem::path base = xdg && xdg[0] && std::filesystem::path(xdg).is_absolute()
            ? std::filesystem::path(xdg)
            : std::filesystem::path(home) / (screenshots ? ".local/state" : ".config");
        auto directory = base / "vulkantron";
        if (screenshots) directory /= "screenshots";
        std::filesystem::create_directories(directory);
        result = directory.string();
        return result.c_str();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[VulkanTron] cannot create private state directory: %s\n",
                     error.what());
        return nullptr;
    }
}
