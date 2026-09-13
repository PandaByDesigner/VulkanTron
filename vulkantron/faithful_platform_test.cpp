// Exercise the production platform and recorder with an in-memory Renderer.
// No SDL window, Vulkan device, user preferences or screenshot files are created.
#include "faithful_frame.hpp"
#include "faithful_platform.h"
#include "fixed_function.h"
#include "renderer.hpp"
#include <iostream>
#include <stdexcept>

static int draws = 0, shutdowns = 0;
static bool drawable = true;
namespace vt {
struct Renderer::Impl {
    RenderStats stats;
    bool shutdown = false;
    Impl() {
        stats.device = "fake-review-device";
        stats.width = 4;
        stats.height = 3;
        stats.depth_bits = 24;
        stats.stencil_bits = 8;
        stats.max_texture_size = 4096;
    }
};
Renderer::Renderer(SDL_Window *, const std::filesystem::path &, bool) : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() {
    shutdown();
}
bool Renderer::draw(const FaithfulFrame &frame, std::vector<std::uint8_t> *out) {
    if (!drawable)
        return false;
    if (frame.commands.empty())
        throw std::runtime_error("review expected recorded frame");
    ++draws;
    ++impl_->stats.frames;
    if (out) {
        out->resize(36);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 4; ++x) {
                auto i = (y * 4 + x) * 3;
                (*out)[i] = x;
                (*out)[i + 1] = y;
                (*out)[i + 2] = 250;
            }
    }
    return true;
}
void Renderer::shutdown() {
    if (!impl_->shutdown) {
        impl_->shutdown = true;
        ++shutdowns;
    }
}
const RenderStats &Renderer::stats() const {
    return impl_->stats;
}
} // namespace vt
static void require(bool b, const char *msg) {
    if (!b)
        throw std::runtime_error(msg);
}
int main() {
    try {
        require(VT_FaithfulInitialize(reinterpret_cast<SDL_Window *>(1)), "initialize failed");
        glClear(GL_COLOR_BUFFER_BIT);
        unsigned char first[36]{};
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, 4, 3, GL_RGB, GL_UNSIGNED_BYTE, first);
        require(draws == 1 && first[0] == 0 && first[1] == 0 && first[2] == 250 && first[33] == 3 &&
                    first[34] == 2,
                "complete bottom-up RGB changed");
        unsigned char second[16]{};
        glReadPixels(1, 1, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, second);
        require(draws == 1 && second[0] == 1 && second[1] == 1 && second[3] == 255 && second[8] == 1 &&
                    second[9] == 2,
                "second-format crop did not reuse complete frame");
        VT_FaithfulSwap();
        require(draws == 1 && vt::faithful_frame().commands.empty(), "swap rerendered captured frame");
        glClear(GL_COLOR_BUFFER_BIT);
        VT_FaithfulSwap();
        require(draws == 2, "following frame did not render");
        drawable = false;
        glClear(GL_COLOR_BUFFER_BIT);
        VT_FaithfulSwap();
        require(draws == 2 && vt::faithful_frame().commands.empty(),
                "minimized frame retained stale commands");
        drawable = true;
        glClear(GL_COLOR_BUFFER_BIT);
        VT_FaithfulSwap();
        require(draws == 3, "restored drawable did not render fresh frame");
        require(glGetError() == GL_NO_ERROR && vt::faithful_error_count() == 0,
                "platform raised adapter error");
        require(VT_FaithfulShutdown() && VT_FaithfulShutdown() && shutdowns == 1,
                "shutdown was not idempotent");
        require(VT_FaithfulInitialize(reinterpret_cast<SDL_Window *>(1)), "context recreation failed");
        glClear(GL_COLOR_BUFFER_BIT);
        VT_FaithfulSwap();
        require(draws == 4 && VT_FaithfulShutdown() && shutdowns == 2, "recreated context lifecycle failed");
        require(VT_FaithfulValidationErrors() == 0, "Post-shutdown validation status changed");
        std::cout << "VT_PLATFORM_CPU_REVIEW_OK draw_once rgb_rgba_crop swap_cache minimize_restore recreate "
                     "shutdown\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
