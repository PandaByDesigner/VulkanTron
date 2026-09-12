#include "game/init.h"
#include "input/nebu_input_system.h"

#ifdef GLTRON_USE_SDL3
#include <SDL3/SDL.h>
#else
#include "SDL.h"
#endif
#include <stdlib.h>
#include <stdio.h>

int video_initialized = 0;

void audioInit(void) {
#ifdef GLTRON_NO_SOUND
  return;
#else
#if SDL_MAJOR_VERSION >= 3
  if(!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
#else
  if(SDL_Init(SDL_INIT_AUDIO) < 0 ){
#endif
    fprintf(stderr, "Couldn't initialize SDL audio: %s\n", SDL_GetError());
    /* FIXME: disable sound system */
  }
#endif
}

void videoInit(void) {
#if SDL_MAJOR_VERSION >= 3
  if(!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
#else
  if(SDL_Init(SDL_INIT_VIDEO) < 0 ) {
#endif
    fprintf(stderr, "Couldn't initialize SDL video: %s\n", SDL_GetError());
    exit(1); /* OK: critical, no visual */
  }
  else video_initialized = 1;
}

void inputInit(void) {
  SystemInputInit();
}
