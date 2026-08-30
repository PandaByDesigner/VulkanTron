#include "base/nebu_system.h"

#include "SDL.h"
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

  fprintf(stderr, "[system] shutting down SDL now\n");
  SDL_Quit();
  fprintf(stderr, "[system] exiting application\n");
  exit(EXIT_SUCCESS);
}

unsigned int SystemGetElapsedTime() {
  /* fprintf(stderr, "%d\n", SDL_GetTicks()); */
  return SDL_GetTicks();
}

int SystemMainLoop() {
  SDL_Event event;
  
	return_code = -1;
  while(return_code == -1) {
    while(SDL_PollEvent(&event) && current) {
			switch(event.type) {
			case SDL_KEYDOWN:
			case SDL_KEYUP:
			case SDL_JOYAXISMOTION:
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
			case SDL_MOUSEBUTTONUP:
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEMOTION:
				SystemHandleInput(&event);
				break;
			case SDL_QUIT:
				SystemExit();
				break;
			default:
				/* ignore event */
				break;
      }
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
