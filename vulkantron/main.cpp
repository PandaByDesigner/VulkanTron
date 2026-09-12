#include "classic_bridge.h"
#include "renderer.hpp"
#include "scene.hpp"
#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
struct Options {
    bool demo = false, validation = false, fixed = false, overview = false;
    bool self_test = false, help = false, version = false, exercise_window = false;
    unsigned frames = 0, seed = 12313, width = 1280, height = 720;
    fs::path assets, shaders, capture;
};

unsigned number(const std::string& value, unsigned maximum, bool zero = false) {
    unsigned result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() ||
        result > maximum || (!zero && !result))
        throw std::runtime_error("Invalid numeric argument: " + value);
    return result;
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (++i == argc) throw std::runtime_error("Missing value for " + arg);
            return argv[i];
        };
        if (arg == "--help" || arg == "-h") o.help = true;
        else if (arg == "--version") o.version = true;
        else if (arg == "--self-test") o.self_test = true;
        else if (arg == "--demo") o.demo = true;
        else if (arg == "--validation") o.validation = true;
        else if (arg == "--fixed-step") o.fixed = true;
        else if (arg == "--overview") o.overview = true;
        else if (arg == "--exercise-window") o.exercise_window = true;
        else if (arg == "--frames") o.frames = number(value(), 1000000);
        else if (arg == "--seed") o.seed = number(value(), 0xffffffffU, true);
        else if (arg == "--width") o.width = number(value(), 8192);
        else if (arg == "--height") o.height = number(value(), 8192);
        else if (arg == "--assets") o.assets = value();
        else if (arg == "--shaders") o.shaders = value();
        else if (arg == "--capture") o.capture = value();
        else throw std::runtime_error("Unknown argument: " + arg);
    }
    if (o.fixed && !o.frames) throw std::runtime_error("--fixed-step requires a bounded --frames run");
    if (!o.capture.empty() && !o.frames) throw std::runtime_error("--capture requires --frames (captures the final frame)");
    if (o.exercise_window && o.frames < 160) throw std::runtime_error("--exercise-window requires at least 160 --frames");
    return o;
}

void help() {
    std::cout << "VulkanTron " VT_VERSION " — direct Vulkan development slice\n"
        "Usage: vulkantron [--demo] [--validation] [--overview]\n"
        "                  [--frames N] [--fixed-step] [--seed N]\n"
        "                  [--width N --height N] [--capture NEW.png]\n"
        "                  [--assets DIR] [--shaders DIR]\n"
        "                  [--exercise-window (bounded resize/fullscreen check)]\n"
        "       vulkantron --self-test | --help | --version\n\n"
        "Left/right or A/D: turn  Shift: boost  Space: pause\n"
        "R: reset round  F10: classic camera  Tab: overview  F11: fullscreen\n"
        "F12: exclusive PNG screenshot in current directory  Escape: quit\n"
        "This scaffold uses original simulation and cycle geometry. Full texture,\n"
        "lighting, menu, audio and multiplayer presentation parity is still pending.\n"
        "Preferences are not read or written. The OpenGL reference remains ./gltron.\n";
}

struct ClassicSession {
    ClassicSession(unsigned seed, bool demo) {
        if (!vt_classic_init(seed, demo)) throw std::runtime_error("Classic state initialization failed");
    }
    ~ClassicSession() { vt_classic_shutdown(); }
};

const VTSnapshot& snapshot() {
    const auto* state = vt_classic_snapshot();
    if (!state) throw std::runtime_error("Classic snapshot allocation or validation failed");
    return *state;
}

void check_frame(const vt::Frame& frame) {
    if (frame.vertices.empty() || frame.vertices.size() % 3)
        throw std::runtime_error("Scene has no complete triangle geometry");
    for (float x : frame.view_projection)
        if (!std::isfinite(x)) throw std::runtime_error("Non-finite camera projection");
    for (const auto& v : frame.vertices) {
        for (float x : v.position) if (!std::isfinite(x)) throw std::runtime_error("Non-finite geometry");
        for (float x : v.color) if (!std::isfinite(x)) throw std::runtime_error("Non-finite color");
    }
}

void self_test(const Options& o) {
    ClassicSession session(o.seed, true);
    vt::Scene scene(o.assets);
    if (!scene.cycle_vertices()) throw std::runtime_error("Original cycle mesh is empty");
    const auto initial = vt_classic_state_hash();
    for (int i = 0; i < 100; ++i) vt_classic_step(20);
    const auto advanced = vt_classic_state_hash();
    if (initial == advanced) throw std::runtime_error("Production simulation did not advance");
    for (float aspect : {4.0f/3.0f, 16.0f/9.0f, 9.0f/16.0f}) {
        check_frame(scene.frame(snapshot(), aspect, true));
        check_frame(scene.frame(snapshot(), aspect, false));
    }
    if (advanced != vt_classic_state_hash()) throw std::runtime_error("Rendering mutated simulation");
    vt_classic_reset(o.seed, true);
    if (initial != vt_classic_state_hash()) throw std::runtime_error("Reset did not restore initial state");
    for (int i = 0; i < 100; ++i) vt_classic_step(20);
    if (advanced != vt_classic_state_hash()) throw std::runtime_error("Repeated simulation differs");
    std::cout << "VT_HEADLESS_OK cycle_vertices=" << scene.cycle_vertices()
              << " state_hash=" << std::hex << advanced << std::dec << '\n';
}

std::string title(const VTSnapshot& s, bool paused, bool demo) {
    std::string out = "VulkanTron — direct Vulkan prototype | ";
    if (paused) out += "PAUSED — Space to resume";
    else if (!s.running) out += "ROUND OVER — R to restart";
    else out += demo ? "AI demonstration" : "A/D or arrows: turn · Shift: boost";
    return out;
}

int run(Options o) {
    if (o.help) { help(); return 0; }
    if (o.version) { std::cout << "VulkanTron " VT_VERSION " (direct Vulkan 1.3; SDL3 platform)\n"; return 0; }
    const char* base = SDL_GetBasePath();
    if (!base) throw std::runtime_error(SDL_GetError());
    const fs::path bin(base);
    if (o.assets.empty()) {
        const auto installed = bin.parent_path() / "share/gltron";
        o.assets = fs::is_directory(installed / "art/default") ? installed : fs::path(VT_SOURCE_ROOT);
    }
    if (o.shaders.empty()) {
        const auto local = bin / "vulkantron-shaders";
        o.shaders = fs::is_directory(local) ? local : bin.parent_path() / "share/vulkantron/shaders";
    }
    if (o.self_test) { self_test(o); return 0; }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) throw std::runtime_error(SDL_GetError());
    struct SDLSession { ~SDLSession() { SDL_Quit(); } } sdl_session;
    SDL_SetHint(SDL_HINT_APP_ID, "vulkantron");
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("VulkanTron — direct Vulkan prototype", o.width, o.height,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY), SDL_DestroyWindow);
    if (!window) throw std::runtime_error(SDL_GetError());
    ClassicSession classic(o.seed, o.demo);
    vt::Scene scene(o.assets);
    vt::Renderer renderer(window.get(), o.shaders, o.validation);
    bool quit = false, paused = false, fullscreen = false;
    std::string previous_title;
    fs::path next_capture;
    auto last = std::chrono::steady_clock::now();
    double accumulator = 0;
    const auto began = last;
    unsigned window_stage = 0;
    unsigned fullscreen_enters = 0, fullscreen_leaves = 0, drawable_changes = 0;
    bool fixed_tick_pending = true;
    while (!quit && (!o.frames || renderer.stats().frames < o.frames)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) ++fullscreen_enters;
            if (event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) ++fullscreen_leaves;
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) ++drawable_changes;
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) quit = true;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && !o.frames) { paused = true; vt_classic_boost(0); }
            if (event.type == SDL_EVENT_KEY_UP &&
                (event.key.key == SDLK_LSHIFT || event.key.key == SDLK_RSHIFT)) vt_classic_boost(0);
            if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) continue;
            switch (event.key.key) {
            case SDLK_ESCAPE: quit = true; break;
            case SDLK_LEFT: case SDLK_A: if (!paused && !o.demo) vt_classic_turn(0); break;
            case SDLK_RIGHT: case SDLK_D: if (!paused && !o.demo) vt_classic_turn(1); break;
            case SDLK_LSHIFT: case SDLK_RSHIFT: if (!paused && !o.demo) vt_classic_boost(1); break;
            case SDLK_SPACE: if (!o.frames) { paused = !paused; vt_classic_boost(0); } break;
            case SDLK_R: if (!o.frames) { vt_classic_reset(o.seed, o.demo); paused = false; accumulator = 0; } break;
            case SDLK_TAB: o.overview = !o.overview; break;
            case SDLK_F10: vt_classic_cycle_camera(); break;
            case SDLK_F11:
                fullscreen = !fullscreen;
                if (!SDL_SetWindowFullscreen(window.get(), fullscreen)) throw std::runtime_error(SDL_GetError());
                break;
            case SDLK_F12:
                next_capture = "vulkantron-" + std::to_string(SDL_GetTicksNS()) + ".png";
                break;
            default: break;
            }
        }
        if (quit) break;
        if (o.exercise_window) {
            const auto rendered = renderer.stats().frames;
            if (!window_stage && rendered >= 40) {
                if (!SDL_SetWindowSize(window.get(), 1000, 650)) throw std::runtime_error(SDL_GetError());
                ++window_stage;
            } else if (window_stage == 1 && rendered >= 80) {
                if (!SDL_SetWindowFullscreen(window.get(), true)) throw std::runtime_error(SDL_GetError());
                ++window_stage;
            } else if (window_stage == 2 && rendered >= 120) {
                if (!SDL_SetWindowFullscreen(window.get(), false) ||
                    !SDL_SetWindowSize(window.get(), o.width, o.height)) throw std::runtime_error(SDL_GetError());
                ++window_stage;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - last).count();
        last = now;
        if (!paused) {
            if (o.fixed) {
                if (fixed_tick_pending) { vt_classic_step(20); fixed_tick_pending = false; }
            } else {
                accumulator += std::min(elapsed, 0.160);
                while (accumulator + 1e-9 >= 0.020) {
                    vt_classic_step(20);
                    accumulator -= 0.020;
                }
            }
        }
        const auto& state = snapshot();
        const auto new_title = title(state, paused, o.demo);
        if (new_title != previous_title) {
            SDL_SetWindowTitle(window.get(), new_title.c_str()); previous_title = new_title;
        }
        int width = 0, height = 0;
        if (!SDL_GetWindowSizeInPixels(window.get(), &width, &height)) throw std::runtime_error(SDL_GetError());
        if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED)) {
            SDL_Delay(20);
        } else {
            auto frame = scene.frame(state, static_cast<float>(width) / height, o.overview);
            fs::path capture = next_capture;
            if (!o.capture.empty() && renderer.stats().frames + 1 == o.frames) capture = o.capture;
            if (renderer.draw(frame, capture)) {
                fixed_tick_pending = true;
                if (!capture.empty()) {
                    std::cout << "VT_CAPTURE " << fs::absolute(capture) << '\n'; next_capture.clear();
                }
            }
        }
        if (o.frames && std::chrono::duration<double>(now - began).count() > 120)
            throw std::runtime_error("Bounded run exceeded its 120-second deadline");
    }
    renderer.shutdown();
    const auto& stats = renderer.stats();
    std::cout << "VT_STATS backend=direct-vulkan device=\"" << stats.device << "\" frames=" << stats.frames
              << " drawable=" << stats.width << 'x' << stats.height
              << " validation_errors=" << stats.validation_errors
              << " window_requests=" << window_stage
              << " fullscreen_events=" << fullscreen_enters << '/' << fullscreen_leaves
              << " drawable_changes=" << drawable_changes
              << " state_hash=" << std::hex << vt_classic_state_hash() << std::dec
              << " cycle_vertices=" << scene.cycle_vertices() << '\n';
    if (o.frames && stats.frames != o.frames) throw std::runtime_error("Bounded run did not render the requested frame count");
    if (o.exercise_window && (!fullscreen_enters || !fullscreen_leaves || !drawable_changes))
        throw std::runtime_error("Window check did not observe fullscreen entry, exit, and drawable changes");
    return stats.validation_errors ? 1 : 0;
}
}

int main(int argc, char** argv) {
    try { return run(parse(argc, argv)); }
    catch (const std::exception& e) { std::cerr << "VulkanTron: " << e.what() << '\n'; return 1; }
}
