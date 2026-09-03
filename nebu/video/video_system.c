#include "Nebu_video.h"
#include "input/nebu_input_system.h"

#include "SDL.h"
#include "SDL_opengl.h"

#if SDL_MAJOR_VERSION >= 2
static SDL_Window *window;
static SDL_GLContext gl_context;
static int input_grabbed;
static int pointer_hidden;
static int relative_mouse_mode;
static int pointer_x;
static int pointer_y;

static void updateRelativeMouseMode(void) {
  int desired = window != NULL && input_grabbed && pointer_hidden;
  int was_relative = relative_mouse_mode;

  if(desired != relative_mouse_mode) {
    if(SDL_SetRelativeMouseMode(desired ? SDL_TRUE : SDL_FALSE) < 0)
      fprintf(stderr, "[system] Couldn't change relative mouse mode: %s\n",
              SDL_GetError());
    relative_mouse_mode = SDL_GetRelativeMouseMode() == SDL_TRUE;
  }
  SystemInputSetRelativeMouseMode(relative_mouse_mode);

  if(was_relative && !relative_mouse_mode && window != NULL)
    SDL_WarpMouseInWindow(window, pointer_x, pointer_y);
}
#else
static SDL_Surface *screen;
#endif
static int width, height;
static int flags;
static int fullscreen;
extern int video_initialized;

void SystemSwapBuffers() {
#if SDL_MAJOR_VERSION >= 2
  if(window != NULL)
    SDL_GL_SwapWindow(window);
#else
  SDL_GL_SwapBuffers();
#endif
}

void SystemInitWindow(int x, int y, int w, int h) {
  (void)x;
  (void)y;
  width = w;
  height = h;
}

void SystemInitDisplayMode(int f, unsigned char full) {
  int zdepth;

  flags = f;
  fullscreen = full;
  if(!video_initialized) {
    if(SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
      fprintf(stderr, "[system] can't initialize Video: %s\n", SDL_GetError());
      exit(1); /* OK: critical, no visual */
    }
  }
  if(flags & SYSTEM_DOUBLE)
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1);

#if SDL_MAJOR_VERSION >= 2
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                      SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif

  if(flags & SYSTEM_32_BIT) {
    zdepth = 24;
  } else {
    zdepth = 16;
  }
  if(flags & SYSTEM_DEPTH)
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, zdepth);
  if(flags & SYSTEM_STENCIL)
     SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8);
  else 
     SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 0);
  video_initialized = 1;
}

void SystemSetGamma(float red, float green, float blue) {
#if SDL_MAJOR_VERSION >= 2
  Uint16 red_ramp[256];
  Uint16 green_ramp[256];
  Uint16 blue_ramp[256];

  if(window == NULL)
    return;

  SDL_CalculateGammaRamp(red, red_ramp);
  SDL_CalculateGammaRamp(green, green_ramp);
  SDL_CalculateGammaRamp(blue, blue_ramp);
  SDL_SetWindowGammaRamp(window, red_ramp, green_ramp, blue_ramp);
#else
  SDL_SetGamma(red, green, blue);
#endif
}

int SystemCreateWindow(char *name) {
#if SDL_MAJOR_VERSION >= 2
  Uint32 window_flags = SDL_WINDOW_OPENGL;

  if(fullscreen & SYSTEM_FULLSCREEN)
    window_flags |= SDL_WINDOW_FULLSCREEN;

  window = SDL_CreateWindow("GLtron", SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, width, height,
                            window_flags);
  if(window == NULL) {
    fprintf(stderr, "[system] Couldn't create GL window: %s\n",
            SDL_GetError());
    return -1;
  }

  gl_context = SDL_GL_CreateContext(window);
  if(gl_context == NULL || SDL_GL_MakeCurrent(window, gl_context) != 0) {
    fprintf(stderr, "[system] Couldn't create GL context: %s\n",
            SDL_GetError());
    if(gl_context != NULL) {
      SDL_GL_DeleteContext(gl_context);
      gl_context = NULL;
    }
    SDL_DestroyWindow(window);
    window = NULL;
    return -1;
  }
#else
  int f = SDL_OPENGL;
  if(fullscreen & SYSTEM_FULLSCREEN)
    f |= SDL_FULLSCREEN;
  if( (screen = SDL_SetVideoMode( width, height, 0, f )) == NULL ) {
    fprintf(stderr, "[system] Couldn't set GL mode: %s\n", SDL_GetError());
    exit(1); /* OK: critical, no visual */
  }
  SDL_WM_SetCaption("GLtron", "");
#endif
  (void)name;
  glClearColor(0,0,0,0);
  glClear(GL_COLOR_BUFFER_BIT);
  SystemSwapBuffers();
  return 1;
}

void SystemDestroyWindow(int id) {
  /* quit the video subsytem
	 * otherwise SDL can't create a new context on win32, if the stencil
	 * bits change 
	 */
	/* there used to be some problems (memory leaks, unprober driver unloading)
	 * caused by this, but I can't remember what they where
	 */

#if SDL_MAJOR_VERSION >= 2
  if(relative_mouse_mode)
    SDL_SetRelativeMouseMode(SDL_FALSE);
  relative_mouse_mode = 0;
  input_grabbed = 0;
  SystemInputSetRelativeMouseMode(0);
  if(gl_context != NULL) {
    SDL_GL_DeleteContext(gl_context);
    gl_context = NULL;
  }
  if(window != NULL) {
    SDL_DestroyWindow(window);
    window = NULL;
  }
#endif
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  video_initialized = 0;
  (void)id;
}

void SystemGrabInput() {
#if SDL_MAJOR_VERSION >= 2
  input_grabbed = 1;
  if(window != NULL) {
    SDL_SetWindowGrab(window, SDL_TRUE);
    updateRelativeMouseMode();
  }
#else
  SDL_WM_GrabInput(SDL_GRAB_ON);
#endif
}

void SystemUngrabInput() {
#if SDL_MAJOR_VERSION >= 2
  input_grabbed = 0;
  if(window != NULL) {
    updateRelativeMouseMode();
    SDL_SetWindowGrab(window, SDL_FALSE);
  }
#else
  SDL_WM_GrabInput(SDL_GRAB_OFF);
#endif
}

void SystemWarpPointer(int x, int y) {
#if SDL_MAJOR_VERSION >= 2
  pointer_x = x;
  pointer_y = y;
  SystemInputSetMouseAnchor(x, y);
  if(window != NULL && !relative_mouse_mode)
    SDL_WarpMouseInWindow(window, x, y);
#else
  SDL_WarpMouse(x, y);
#endif
}

void SystemHidePointer() {
#if SDL_MAJOR_VERSION >= 2
  pointer_hidden = 1;
#endif
  SDL_ShowCursor(SDL_DISABLE);
#if SDL_MAJOR_VERSION >= 2
  updateRelativeMouseMode();
#endif
}

void SystemUnhidePointer() {
#if SDL_MAJOR_VERSION >= 2
  pointer_hidden = 0;
  updateRelativeMouseMode();
#endif
  SDL_ShowCursor(SDL_ENABLE);
}

void SystemReshapeFunc(void(*reshape)(int, int)) {
  (void)reshape;
}

int SystemWriteBMP(char *filename, int x, int y, unsigned char *pixels) {
  /* this code is shamelessly stolen from Ray Kelm, but I believe he
     put it in the public domain */
  SDL_Surface *temp;
  int i;

  temp = SDL_CreateRGBSurface(SDL_SWSURFACE, x, y, 24,
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
			      0x000000FF, 0x0000FF00, 0x00FF0000, 0
#else
			      0x00FF0000, 0x0000FF00, 0x000000FF, 0
#endif
			      );

  if (temp == NULL)
    return -1;

  for (i = 0; i < y; i++)
    memcpy(((char *) temp->pixels) + temp->pitch * i, 
	   pixels + 3 * x * (y - i - 1), x * 3);

  SDL_SaveBMP(temp, filename);
  SDL_FreeSurface(temp);
  return 0;
}
