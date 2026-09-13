#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "game/init.h"
#include "base/util.h"
#include "filesystem/path.h"
#include "faithful_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int i, forwarded = 1;
    const char **options = calloc((size_t)argc + 1, sizeof(*options));
    if(options == NULL) return EXIT_FAILURE;
    options[0] = argv[0];
    for(i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--version") == 0) {
            printf("VulkanTron %s (direct Vulkan; SDL3; faithful GLTron 0.70 gameplay)\n", VT_VERSION);
            free(options);
            return 0;
        }
        if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("VulkanTron %s\nUsage: %s [options]\n\n"
                   "  -i            Start in a window\n"
                   "  -1 ... -9     Choose a classic or widescreen resolution\n"
                   "  -4 / -8 / -9  800x600 / 1280x720 / 1920x1080\n"
                   "  -s            Mute music and effects\n"
                   "  -F / -c       Hide FPS / AI labels\n"
                   "  --validation  Enable Vulkan validation layers\n"
                   "  --obsidian    Select the Obsidian arena and original soundtrack\n"
                   "  --version     Print build information\n"
                   "  --help        Show this help\n\n"
                   "Use the original menus to configure controls, audio, artpacks,\n"
                   "cameras, and local multiplayer. F5 saves settings; F11 saves BMP;\n"
                   "F12 saves PNG. VulkanTron has its own preferences and screenshots.\n\n"
                   "Optional directories: VULKANTRON_DATA_DIR, VULKANTRON_CONFIG_DIR,\n"
                   "VULKANTRON_SCREENSHOT_DIR, VULKANTRON_SHADER_DIR.\n",
                   VT_VERSION, argv[0]);
            free(options);
            return 0;
        }
        if(strcmp(argv[i], "--obsidian") == 0) {
            if(SDL_setenv_unsafe("VULKANTRON_PRESENTATION", "obsidian", 1) != 0) {
                free(options);
                return EXIT_FAILURE;
            }
        } else if(strcmp(argv[i], "--validation") == 0) {
            if(!SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "VULKANTRON_VALIDATION", "1", true)) {
                fprintf(stderr, "Cannot enable validation: %s\n", SDL_GetError());
                free(options);
                return EXIT_FAILURE;
            }
            /* The platform reads process environment, as does the Vulkan loader. */
            if(SDL_setenv_unsafe("VULKANTRON_VALIDATION", "1", 1) != 0) {
                free(options);
                return EXIT_FAILURE;
            }
        } else options[forwarded++] = argv[i];
    }
    SDL_SetAppMetadata("VulkanTron", VT_VERSION, "io.github.PandaByDesigner.VulkanTron");
    initSubsystems(forwarded, options);
    free(options);
    runScript(PATH_SCRIPTS, "main.lua");
    return VT_FaithfulShutdown() ? EXIT_SUCCESS : EXIT_FAILURE;
}
