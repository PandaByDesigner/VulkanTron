#include "Nebu_video.h"
#include "input/nebu_input_system.h"
#ifdef GLTRON_USE_SDL3
#include <SDL3/SDL.h>
#else
#include "SDL.h"
#endif
#ifdef GLTRON_USE_SDL3
#include <SDL3/SDL_opengl.h>
#else
#include "SDL_opengl.h"
#endif
#include <limits.h>
#include <stdint.h>
#ifdef GLTRON_DIRECT_VULKAN
#include "faithful_platform.h"
#endif

static int width, height;
static int drawable_width, drawable_height;
static int flags;
static int fullscreen;
static void (*reshape_callback)(int, int);
static void (*capture_callback)(void);
extern int video_initialized;

#if SDL_MAJOR_VERSION >= 2
static SDL_Window *window;
#ifndef GLTRON_DIRECT_VULKAN
static SDL_GLContext gl_context;
#endif
static int input_grabbed;
static int pointer_hidden;
static int relative_mouse_mode;
static int pointer_x;
static int pointer_y;
#if SDL_MAJOR_VERSION >= 3
static int resize_pending;
static Uint64 resize_requested_at;
#endif

static void updateRelativeMouseMode(void) {
  int desired = window != NULL && input_grabbed && pointer_hidden;
  int was_relative = relative_mouse_mode;
  if(desired != relative_mouse_mode) {
#if SDL_MAJOR_VERSION >= 3
    if(!SDL_SetWindowRelativeMouseMode(window, desired != 0))
#else
    if(SDL_SetRelativeMouseMode(desired ? SDL_TRUE : SDL_FALSE) < 0)
#endif
      fprintf(stderr, "[system] Couldn't change relative mouse mode: %s\n",
              SDL_GetError());
#if SDL_MAJOR_VERSION >= 3
    relative_mouse_mode = SDL_GetWindowRelativeMouseMode(window);
#else
    relative_mouse_mode = SDL_GetRelativeMouseMode() == SDL_TRUE;
#endif
  }
  SystemInputSetRelativeMouseMode(relative_mouse_mode);
  if(was_relative && !relative_mouse_mode && window != NULL)
    SDL_WarpMouseInWindow(window, pointer_x, pointer_y);
}
#else
static SDL_Surface *screen;
#endif

void SystemGetDrawableSize(int *w, int *h) {
  int dw = width, dh = height;
#if SDL_MAJOR_VERSION >= 3
  if(window != NULL && !SDL_GetWindowSizeInPixels(window, &dw, &dh))
    dw = dh = 0;
#elif SDL_MAJOR_VERSION >= 2
  if(window != NULL)
    SDL_GL_GetDrawableSize(window, &dw, &dh);
#endif
  if(w != NULL) *w = dw;
  if(h != NULL) *h = dh;
}

void SystemGetWindowSize(int *w, int *h) {
  int lw = width, lh = height;
#if SDL_MAJOR_VERSION >= 2
  if(window != NULL)
    SDL_GetWindowSize(window, &lw, &lh);
#endif
  if(w != NULL) *w = lw;
  if(h != NULL) *h = lh;
}

int SystemIsFullscreen(void) {
#if SDL_MAJOR_VERSION >= 2
  if(window != NULL)
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
#endif
  return (fullscreen & SYSTEM_FULLSCREEN) != 0;
}

static void notifyReshape(void) {
  int w, h;
#if SDL_MAJOR_VERSION >= 3
  /* An exposed/fullscreen event can precede an unfinished native resize.
   * Keep its intermediate dimensions out of the saved window preferences. */
  if(resize_pending)
    return;
#endif
  SystemGetDrawableSize(&w, &h);
  /* Minimized windows may have no drawable. Keep the last usable layout. */
  if(w <= 0 || h <= 0)
    return;
  drawable_width = w;
  drawable_height = h;
  if(reshape_callback != NULL)
    reshape_callback(w, h);
  SystemPostRedisplay();
}

int SystemHandleWindowEvent(const void *event_data) {
#if SDL_MAJOR_VERSION >= 2
  const SDL_Event *event = event_data;
  if(window == NULL || event == NULL)
    return 0;
#if SDL_MAJOR_VERSION >= 3
  if(event->type < SDL_EVENT_WINDOW_FIRST ||
     event->type > SDL_EVENT_WINDOW_LAST ||
     event->window.windowID != SDL_GetWindowID(window))
    return 0;
  switch(event->type) {
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return 1;
  case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
  case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
    fullscreen = SystemIsFullscreen() ? SYSTEM_FULLSCREEN : 0;
    notifyReshape();
    break;
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
  case SDL_EVENT_WINDOW_RESIZED:
    if(resize_pending) {
      int event_width, event_height;
      if(event->window.timestamp < resize_requested_at)
        break;
      if(event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        SystemGetDrawableSize(&event_width, &event_height);
      else
        SystemGetWindowSize(&event_width, &event_height);
      /* Ignore queued dimensions superseded by another native resize. */
      if(event_width <= 0 || event_height <= 0 ||
         event_width != event->window.data1 || event_height != event->window.data2)
        break;
      resize_pending = 0;
    }
    notifyReshape();
    break;
  case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
  case SDL_EVENT_WINDOW_RESTORED:
  case SDL_EVENT_WINDOW_EXPOSED:
    notifyReshape();
    break;
  }
#else
  if(event->type != SDL_WINDOWEVENT ||
     event->window.windowID != SDL_GetWindowID(window))
    return 0;
  switch(event->window.event) {
  case SDL_WINDOWEVENT_CLOSE: return 1;
  case SDL_WINDOWEVENT_SIZE_CHANGED:
  case SDL_WINDOWEVENT_RESIZED:
  case SDL_WINDOWEVENT_RESTORED:
  case SDL_WINDOWEVENT_EXPOSED:
    notifyReshape();
    break;
  }
#endif
#else
  (void)event_data;
#endif
  return 0;
}

void SystemCaptureNextFrame(void (*capture)(void)) {
  capture_callback = capture;
  SystemPostRedisplay();
}

void SystemSwapBuffers(void) {
  if(capture_callback != NULL) {
    void (*capture)(void) = capture_callback;
    capture_callback = NULL;
    capture();
  }
#ifdef GLTRON_DIRECT_VULKAN
  if(window != NULL)
    VT_FaithfulSwap();
#elif SDL_MAJOR_VERSION >= 2
  if(window != NULL)
    SDL_GL_SwapWindow(window);
#else
  SDL_GL_SwapBuffers();
#endif
}

void SystemInitWindow(int x, int y, int w, int h) {
  (void)x;
  (void)y;
  width = w > 0 ? w : 1280;
  height = h > 0 ? h : 720;
}

void SystemInitDisplayMode(int f, unsigned char full) {
  flags = f;
  fullscreen = full;
  if(!video_initialized) {
#if SDL_MAJOR_VERSION >= 3
    if(!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
#else
    if(SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
#endif
      fprintf(stderr, "[system] can't initialize video: %s\n", SDL_GetError());
      exit(1);
    }
  }
#ifndef GLTRON_DIRECT_VULKAN
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, (flags & SYSTEM_DOUBLE) != 0);
#if SDL_MAJOR_VERSION >= 2
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  /* A compatibility renderer needs legacy GL, not a 3.2 core profile. */
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
#endif
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,
                     (flags & SYSTEM_DEPTH) ? ((flags & SYSTEM_32_BIT) ? 24 : 16) : 0);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, (flags & SYSTEM_STENCIL) ? 8 : 0);
#endif /* OpenGL context attributes */
  video_initialized = 1;
}

void SystemSetGamma(float red, float green, float blue) {
#if SDL_MAJOR_VERSION >= 3
  /* This legacy entry point has no game callers. SDL3 removed global gamma
   * ramps; do not change the desktop's gamma to emulate an unused feature. */
  (void)red; (void)green; (void)blue;
#elif SDL_MAJOR_VERSION >= 2
  Uint16 r[256], g[256], b[256];
  if(window != NULL) {
    SDL_CalculateGammaRamp(red, r);
    SDL_CalculateGammaRamp(green, g);
    SDL_CalculateGammaRamp(blue, b);
    SDL_SetWindowGammaRamp(window, r, g, b);
  }
#else
  SDL_SetGamma(red, green, blue);
#endif
}

int SystemCreateWindow(char *name) {
#if SDL_MAJOR_VERSION >= 3
  SDL_WindowFlags window_flags =
#ifdef GLTRON_DIRECT_VULKAN
                                 SDL_WINDOW_VULKAN |
#else
                                 SDL_WINDOW_OPENGL |
#endif
                                 SDL_WINDOW_RESIZABLE |
                                 SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if(SystemIsFullscreen()) window_flags |= SDL_WINDOW_FULLSCREEN;
  window = SDL_CreateWindow(name, width, height, window_flags);
#elif SDL_MAJOR_VERSION >= 2
  Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                        SDL_WINDOW_ALLOW_HIGHDPI;
  if(SystemIsFullscreen()) window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  window = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            width, height, window_flags);
#else
  int f = SDL_OPENGL;
  if(SystemIsFullscreen()) f |= SDL_FULLSCREEN;
  screen = SDL_SetVideoMode(width, height, 0, f);
  if(screen == NULL) {
    fprintf(stderr, "[system] Couldn't set GL mode: %s\n", SDL_GetError());
    return -1;
  }
  SDL_WM_SetCaption(name, "");
#endif
#if SDL_MAJOR_VERSION >= 2
  if(window == NULL) {
    fprintf(stderr, "[system] Couldn't create GL window: %s\n", SDL_GetError());
    return -1;
  }
#ifdef GLTRON_DIRECT_VULKAN
  if(!VT_FaithfulInitialize(window)) {
    SDL_DestroyWindow(window);
    window = NULL;
    return -1;
  }
#else
  gl_context = SDL_GL_CreateContext(window);
#if SDL_MAJOR_VERSION >= 3
  if(gl_context == NULL || !SDL_GL_MakeCurrent(window, gl_context)) {
#else
  if(gl_context == NULL || SDL_GL_MakeCurrent(window, gl_context) != 0) {
#endif
    fprintf(stderr, "[system] Couldn't create GL context: %s\n", SDL_GetError());
    if(gl_context != NULL) {
#if SDL_MAJOR_VERSION >= 3
      SDL_GL_DestroyContext(gl_context);
#else
      SDL_GL_DeleteContext(gl_context);
#endif
      gl_context = NULL;
    }
    SDL_DestroyWindow(window);
    window = NULL;
    return -1;
  }
#endif
#endif /* renderer initialization */
  SystemGetDrawableSize(&drawable_width, &drawable_height);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  SystemSwapBuffers();
  return 1;
}

int SystemApplyWindow(int w, int h, int new_flags, unsigned char full) {
#if SDL_MAJOR_VERSION >= 2
  if(window == NULL || new_flags != flags || w <= 0 || h <= 0)
    return 0;
#if SDL_MAJOR_VERSION >= 3
  {
    int requested_fullscreen = (full & SYSTEM_FULLSCREEN) != 0;
    int actual_fullscreen = SystemIsFullscreen();
    int actual_width, actual_height;
    int pixel_width, pixel_height;
    int synchronized = 1;
    Uint64 requested_at = SDL_GetTicksNS();
    /* SDL3 window requests can return before the compositor applies them.
     * In particular, an immediate reshape after leaving fullscreen would
     * save the old fullscreen size as the user's windowed preference. */
    /* A previous size request may have been declined by a tiling compositor.
     * Do not synchronize that request again merely to retain the same mode. */
    if(actual_fullscreen != requested_fullscreen) {
      if(!SDL_SetWindowFullscreen(window, requested_fullscreen != 0)) {
        fprintf(stderr, "[system] Fullscreen request failed: %s\n", SDL_GetError());
        fullscreen = SystemIsFullscreen() ? SYSTEM_FULLSCREEN : 0;
        return 0;
      }
      synchronized = SDL_SyncWindow(window);
    }
    actual_fullscreen = SystemIsFullscreen();
    if(actual_fullscreen != requested_fullscreen) {
      fprintf(stderr, "[system] Window system declined fullscreen state %d\n",
              requested_fullscreen);
      fullscreen = actual_fullscreen ? SYSTEM_FULLSCREEN : 0;
      return 0;
    }
    if(!requested_fullscreen) {
      if(!SDL_SetWindowSize(window, w, h)) {
        fprintf(stderr, "[system] Window resize request failed: %s\n", SDL_GetError());
        fullscreen = 0;
        return 0;
      }
      /* SDL_SyncWindow returns false on timeout, including when the window
       * manager declines a size. That does not invalidate the GL context. */
      synchronized = SDL_SyncWindow(window);
    }
    if(!SDL_GetWindowSize(window, &actual_width, &actual_height) ||
       !SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height) ||
       actual_width <= 0 || actual_height <= 0 || pixel_width <= 0 || pixel_height <= 0) {
      fprintf(stderr, "[system] Could not query a usable native window after resize: %s\n",
              SDL_GetError());
      fullscreen = actual_fullscreen ? SYSTEM_FULLSCREEN : 0;
      return 0;
    }
    resize_pending = !synchronized;
    if(resize_pending) {
      resize_requested_at = requested_at;
      fprintf(stderr, "[system] Window size request %dx%d remains pending or constrained; "
                      "retaining context at native %dx%d\n",
              w, h, actual_width, actual_height);
    } else if(!requested_fullscreen) {
      /* Tiling, minimum-size rules and fractional scale can constrain a
       * request. Persist the completed native size, never an intermediate
       * fullscreen drawable, and make larger constraints visible in logs. */
      if(abs(actual_width - w) > 1 || abs(actual_height - h) > 1)
        fprintf(stderr, "[system] Window system constrained requested %dx%d to %dx%d\n",
                w, h, actual_width, actual_height);
    }
  }
#else
  if(SDL_SetWindowFullscreen(window, (full & SYSTEM_FULLSCREEN) ?
                             SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0)
    return 0;
  if(!(full & SYSTEM_FULLSCREEN))
    SDL_SetWindowSize(window, w, h);
#endif
  width = w;
  height = h;
  fullscreen = full;
  notifyReshape();
  return 1;
#else
  (void)w; (void)h; (void)new_flags; (void)full;
  return 0;
#endif
}

void SystemDestroyWindow(int id) {
  reshape_callback = NULL;
  capture_callback = NULL;
#if SDL_MAJOR_VERSION >= 3
  resize_pending = 0;
  resize_requested_at = 0;
#endif
#if SDL_MAJOR_VERSION >= 2
  if(relative_mouse_mode) {
#if SDL_MAJOR_VERSION >= 3
    SDL_SetWindowRelativeMouseMode(window, false);
#else
    SDL_SetRelativeMouseMode(SDL_FALSE);
#endif
  }
  relative_mouse_mode = input_grabbed = pointer_hidden = 0;
  SystemInputSetRelativeMouseMode(0);
#ifdef GLTRON_DIRECT_VULKAN
  VT_FaithfulShutdown();
#else
  if(gl_context != NULL) {
#if SDL_MAJOR_VERSION >= 3
    SDL_GL_DestroyContext(gl_context);
#else
    SDL_GL_DeleteContext(gl_context);
#endif
    gl_context = NULL;
  }
#endif /* renderer shutdown */
  if(window != NULL) {
    SDL_DestroyWindow(window);
    window = NULL;
  }
#endif
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  video_initialized = 0;
  (void)id;
}

void SystemGrabInput(void) {
#if SDL_MAJOR_VERSION >= 2
  input_grabbed = 1;
  if(window != NULL) {
#if SDL_MAJOR_VERSION >= 3
    SDL_SetWindowMouseGrab(window, true);
#else
    SDL_SetWindowGrab(window, SDL_TRUE);
#endif
    updateRelativeMouseMode();
  }
#else
  SDL_WM_GrabInput(SDL_GRAB_ON);
#endif
}

void SystemUngrabInput(void) {
#if SDL_MAJOR_VERSION >= 2
  input_grabbed = 0;
  if(window != NULL) {
    updateRelativeMouseMode();
#if SDL_MAJOR_VERSION >= 3
    SDL_SetWindowMouseGrab(window, false);
#else
    SDL_SetWindowGrab(window, SDL_FALSE);
#endif
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

void SystemHidePointer(void) {
#if SDL_MAJOR_VERSION >= 2
  pointer_hidden = 1;
#endif
#if SDL_MAJOR_VERSION >= 3
  SDL_HideCursor();
#else
  SDL_ShowCursor(SDL_DISABLE);
#endif
#if SDL_MAJOR_VERSION >= 2
  updateRelativeMouseMode();
#endif
}

void SystemUnhidePointer(void) {
#if SDL_MAJOR_VERSION >= 2
  pointer_hidden = 0;
  updateRelativeMouseMode();
#endif
#if SDL_MAJOR_VERSION >= 3
  SDL_ShowCursor();
#else
  SDL_ShowCursor(SDL_ENABLE);
#endif
}

void SystemReshapeFunc(void (*reshape)(int, int)) {
  reshape_callback = reshape;
  notifyReshape();
}

static void bmpWriteLE32(unsigned char *bytes, uint32_t value) {
  int byte;
  for(byte = 0; byte < 4; byte++)
    bytes[byte] = (unsigned char)(value >> (byte * 8));
}

int SystemWriteBMP(char *filename, int x, int y, unsigned char *pixels) {
  unsigned char header[54] = { 'B', 'M' };
  unsigned char *row;
  size_t stride, image_size;
  FILE *output;
  int i, column, failed = 0;
  if(filename == NULL || pixels == NULL || x <= 0 || y <= 0 ||
     x > (INT_MAX - 3) / 3)
    return -1;
  stride = ((size_t)x * 3 + 3) & ~(size_t)3;
  /* BMP's file-size field is uint32, including its 54-byte header. */
  if(stride > UINT32_MAX - sizeof(header) ||
     (size_t)y > (UINT32_MAX - sizeof(header)) / stride)
    return -1;
  image_size = stride * (size_t)y;
  row = calloc(stride, 1);
  if(row == NULL) return -1;
  /* The exclusive create is the authority: simultaneous game instances must
   * never truncate an existing screenshot between a path check and a save. */
  output = fopen(filename, "wbx");
  if(output == NULL) {
    free(row);
    return -1;
  }
  bmpWriteLE32(header + 2, (uint32_t)(sizeof(header) + image_size));
  bmpWriteLE32(header + 10, sizeof(header));
  bmpWriteLE32(header + 14, 40); /* BITMAPINFOHEADER */
  bmpWriteLE32(header + 18, (uint32_t)x);
  bmpWriteLE32(header + 22, (uint32_t)y); /* positive: rows start at bottom */
  header[26] = 1; /* one color plane */
  header[28] = 24;
  bmpWriteLE32(header + 34, (uint32_t)image_size);
  bmpWriteLE32(header + 38, 2835); /* pixels per meter, nominal 72 DPI */
  bmpWriteLE32(header + 42, 2835);
  if(fwrite(header, 1, sizeof(header), output) != sizeof(header)) failed = 1;
  for(i = 0; i < y && !failed; i++) {
    const unsigned char *source = pixels + (size_t)i * x * 3;
    for(column = 0; column < x; column++) {
      row[column * 3] = source[column * 3 + 2];
      row[column * 3 + 1] = source[column * 3 + 1];
      row[column * 3 + 2] = source[column * 3];
    }
    if(fwrite(row, 1, stride, output) != stride) failed = 1;
  }
  if(fclose(output) != 0) failed = 1;
  free(row);
  if(failed) {
    remove(filename);
    return -1;
  }
  return 0;
}
