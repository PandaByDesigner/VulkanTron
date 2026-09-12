#include "base/nebu_system.h"
#include "input/nebu_input_system.h"
#include "video/nebu_video_system.h"

#ifdef GLTRON_USE_SDL3
#include <SDL3/SDL.h>
#else
#include "SDL.h"
#endif
#include <stdio.h>
#include <stdlib.h>

Callbacks *current = 0;
static int return_code = -1;
static int redisplay = 0;
static int shutting_down = 0;
static SystemShutdownCallback shutdown_callback = NULL;

void SystemSetShutdownCallback(SystemShutdownCallback callback) {
  shutdown_callback = callback;
}

void SystemExit() {
  if(shutting_down)
    exit(EXIT_SUCCESS);

  shutting_down = 1;
  if(shutdown_callback != NULL)
    shutdown_callback();

  SystemInputShutdown();
  fprintf(stderr, "[system] shutting down SDL now\n");
  SDL_Quit();
  fprintf(stderr, "[system] exiting application\n");
  exit(EXIT_SUCCESS);
}

unsigned int SystemGetElapsedTime() {
  /* fprintf(stderr, "%d\n", SDL_GetTicks()); */
  return (unsigned int)SDL_GetTicks();
}

void SystemDelay(unsigned int milliseconds) {
  SDL_Delay(milliseconds);
}

int SystemMainLoop() {
  SDL_Event event;
  
	return_code = -1;
  while(return_code == -1) {
    while(SDL_PollEvent(&event) && current) {
#if SDL_MAJOR_VERSION >= 3
      int quit_requested = (event.type == SDL_EVENT_QUIT);
#else
      int quit_requested = (event.type == SDL_QUIT);
#endif
      if(SystemHandleWindowEvent(&event))
        quit_requested = 1;

      if(quit_requested)
        SystemExit();

      SystemHandleInputEvent(&event);
    }
    if(redisplay) {
      current->display();
      redisplay = 0;
    } else
      current->idle();
  }
	if(current->exit)
		(current->exit)();
	return return_code;
}
  
void SystemRegisterCallbacks(Callbacks *cb) {
  current = cb;
}

void SystemExitLoop(int value) {
	return_code = value;
}

void SystemPostRedisplay() {
  redisplay = 1;
}
