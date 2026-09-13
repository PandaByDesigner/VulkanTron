// Native opt-in failure/retry gate. No user preferences or game assets used.
#include "renderer.hpp"
#include "faithful_frame.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    SDL_Window* window = nullptr;
    try {
        if (argc != 3) throw std::runtime_error("Usage: bloom-native-test SHADERS NEW_TEMP_DIR");
        const std::filesystem::path source = argv[1], staged = argv[2];
        if (!std::filesystem::create_directory(staged))
            throw std::runtime_error("Test directory must be new");
        for (const char* name : {"faithful.vert.spv","faithful.frag.spv","bloom.vert.spv"})
            std::filesystem::copy_file(source/name, staged/name);
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("VulkanTron bloom lifecycle check",640,480,SDL_WINDOW_VULKAN);
        if (!window) throw std::runtime_error(SDL_GetError());
        vt::Renderer renderer(window,staged,true);
        vt::FaithfulFrame frame;
        vt::ClearCommand clear;
        clear.mask = 0x4000 | 0x0100 | 0x0400;
        clear.color = {0.1f,0.2f,0.3f,1};
        frame.commands.emplace_back(clear);
        frame.commands.emplace_back(vt::BloomCommand{{0,0,640,480},0.28f});
        for (int retry=0; retry<3; ++retry) {
            bool rejected = false;
            try { renderer.draw(frame); }
            catch (const std::exception& error) {
                const std::string message = error.what();
                if (message.find("bloom.frag.spv") == std::string::npos) throw;
                rejected = true;
            }
            if (!rejected) throw std::runtime_error("Missing bloom shader was not rejected");
        }
        std::filesystem::copy_file(source/"bloom.frag.spv", staged/"bloom.frag.spv");
        std::vector<std::uint8_t> rgb;
        if (!renderer.draw(frame,&rgb) || rgb.empty())
            throw std::runtime_error("Bloom failed to recover after shader restoration");
        renderer.shutdown();
        if (renderer.stats().validation_errors != 0)
            throw std::runtime_error("Bloom retry or shutdown produced validation errors");
        SDL_DestroyWindow(window); window = nullptr; SDL_Quit();
        std::cout << "BLOOM_NATIVE_OK missing_shader_retries=3 recovery=1 validation_errors=0\n";
        return 0;
    } catch (const std::exception& error) {
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        std::cerr << "BLOOM_NATIVE_FAILED " << error.what() << '\n';
        return 1;
    }
}
