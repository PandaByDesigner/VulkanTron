/*
  gltron
  Copyright (C) 1999 by Andreas Umbach <marvin@dataway.ch>
*/
#ifdef GLTRON_USE_SDL3
#include <SDL3/SDL.h>
#else
#include "SDL.h"
#endif
#if SDL_MAJOR_VERSION >= 3
#include <SDL3/SDL_main.h>
#endif

#include "game/init.h"
#include "filesystem/path.h"
#include "base/util.h"
#include <stdio.h>
#include <string.h>
#ifdef GLTRON_CMAKE_BUILD
#include "gltron_build_info.h"
#endif

int main(int argc, char *argv[] ) {
  int i;
  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--version") == 0) {
#ifdef GLTRON_CMAKE_BUILD
      printf("%s %s (%s; %s; audio %s)\n", GLTRON_BUILD_NAME,
             GLTRON_BUILD_VERSION, GLTRON_BUILD_REVISION,
             GLTRON_BUILD_PLATFORM, GLTRON_BUILD_AUDIO);
#else
      printf("GLTron %s\n", VERSION);
#endif
      return 0;
    }
    if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("GLTron Faithful Remaster\nUsage: %s [options]\n\n"
             "  -i            Start in a window\n"
             "  -1 ... -9     Choose a classic or widescreen resolution\n"
             "  -4 / -8 / -9  800x600 / 1280x720 / 1920x1080\n"
             "  -s            Mute music and effects\n"
             "  -F / -c       Hide FPS / AI labels\n"
             "  --version     Print build information\n"
             "  --help        Show this help\n\n"
             "In game: F5 saves preferences; F11 saves BMP; F12 saves PNG.\n"
             "Screen options are available in the Video menu.\n\n"
             "Optional directories: GLTRON_DATA_DIR, GLTRON_CONFIG_DIR,\n"
             "GLTRON_SCREENSHOT_DIR. Existing .gltronrc bindings are retained.\n",
             argv[0]);
      return 0;
    }
  }
	initSubsystems(argc, (const char **) argv);
	runScript(PATH_SCRIPTS, "main.lua");
  return 0;
}





