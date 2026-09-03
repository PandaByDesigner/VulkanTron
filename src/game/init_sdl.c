#include "game/init.h"
#include "input/nebu_input_system.h"

#include "SDL.h"
#include <stdlib.h>

int video_initialized = 0;

void audioInit(void) {
#ifdef GLTRON_NO_SOUND
  return;
#else
  if(SDL_Init(SDL_INIT_AUDIO) < 0 ){
    fprintf(stderr, "Couldn't initialize SDL audio: %s\n", SDL_GetError());
    /* FIXME: disable sound system */
  }
#endif
}

void videoInit(void) {
  if(SDL_Init(SDL_INIT_VIDEO) < 0 ) {
    fprintf(stderr, "Couldn't initialize SDL video: %s\n", SDL_GetError());
    exit(1); /* OK: critical, no visual */
  }
  else video_initialized = 1;
}

void inputInit(void) {
  SystemInputInit();
}
