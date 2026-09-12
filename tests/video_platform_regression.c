/* Native display gate: production window lifecycle and screenshot code.
 * This opens only its own temporary windows and never loads user preferences. */
#include "video/video.h"
#include "filesystem/path.h"
#include "input/nebu_input_system.h"
#ifdef GLTRON_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <errno.h>
#include <limits.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int video_initialized;
static int reshape_count, reshape_width, reshape_height;
static int redisplay_count, relative_mode, anchor_x, anchor_y;
static int capture_count;
static int monitor_windowed_restore, stale_fullscreen_reshape;
static char artifact_directory[PATH_MAX];
static SDL_Window *test_window, *foreign_window;
static const int display_flags = SYSTEM_RGBA | SYSTEM_DOUBLE | SYSTEM_DEPTH;
static const unsigned char texture_pixels[12] = {
  12, 34, 56, 78, 90, 123, 210, 156, 87, 42, 73, 201
};
static const unsigned char quadrant_colors[4][3] = {
  {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}
};

void SystemPostRedisplay(void) { redisplay_count++; }
void SystemInputSetRelativeMouseMode(int enabled) { relative_mode = enabled; }
void SystemInputSetMouseAnchor(int x, int y) { anchor_x = x; anchor_y = y; }
int fileExists(const char *path) {
  struct stat info;
  return path != NULL && stat(path, &info) == 0;
}
char *getPossiblePath(int location, const char *filename) {
  char *result;
  size_t size;
  if(location != PATH_SNAPSHOTS || filename == NULL) return NULL;
  size = strlen(artifact_directory) + strlen(filename) + 2;
  result = malloc(size);
  if(result != NULL) snprintf(result, size, "%s/%s", artifact_directory, filename);
  return result;
}

static int fail(const char *message, int line) {
  fprintf(stderr, "FAIL line %d: %s (SDL: %s)\n", line, message, SDL_GetError());
  return 1;
}
#define CHECK(test, message) do { if(!(test)) return fail(message, __LINE__); } while(0)

static void reshape(int w, int h) {
  int logical_width, logical_height;
  reshape_count++;
  reshape_width = w;
  reshape_height = h;
  /* Production reshape persists logical dimensions whenever fullscreen is
   * false. Catch a callback that would save the previous fullscreen size. */
  if(monitor_windowed_restore && !SystemIsFullscreen()) {
    SystemGetWindowSize(&logical_width, &logical_height);
    if(abs(logical_width - 257) > 1 || abs(logical_height - 193) > 1)
      stale_fullscreen_reshape = 1;
  }
}
static void captureFrame(void) { capture_count++; }

static void pumpWindowEvents(void) {
  SDL_Event event;
  while(SDL_PollEvent(&event)) SystemHandleWindowEvent(&event);
}
static void settleWindow(void) {
  int i;
#if SDL_MAJOR_VERSION >= 3
  SDL_SyncWindow(test_window);
#endif
  for(i = 0; i < 50; i++) {
    pumpWindowEvents();
    SDL_Delay(10);
  }
}

static int checkTexture(GLuint texture, SDL_GLContext context) {
  unsigned char pixels[12];
  CHECK(SDL_GL_GetCurrentContext() == context,
        "window transition replaced the OpenGL context");
  CHECK(glIsTexture(texture), "window transition lost a texture object");
  glBindTexture(GL_TEXTURE_2D, texture);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
  CHECK(memcmp(pixels, texture_pixels, sizeof(pixels)) == 0,
        "window transition changed texture contents");
  CHECK(glGetError() == GL_NO_ERROR, "texture verification raised an OpenGL error");
  return 0;
}

static int checkDimensions(void) {
  int width, height, actual_width, actual_height;
  SystemGetDrawableSize(&width, &height);
#if SDL_MAJOR_VERSION >= 3
  CHECK(SDL_GetWindowSizeInPixels(test_window, &actual_width, &actual_height),
        "could not query the actual drawable size");
#else
  SDL_GL_GetDrawableSize(test_window, &actual_width, &actual_height);
#endif
  CHECK(width > 0 && height > 0 && width == actual_width && height == actual_height,
        "drawable dimensions disagree with the native surface");
  CHECK(reshape_count > 0 && reshape_width == width && reshape_height == height,
        "reshape callback did not receive drawable pixels");
  SystemGetWindowSize(&width, &height);
  SDL_GetWindowSize(test_window, &actual_width, &actual_height);
  CHECK(width == actual_width && height == actual_height,
        "logical window size disagrees with native SDL size");
  return 0;
}

static int checkWindowLifecycle(void) {
  SDL_Event event;
  GLuint texture;
  SDL_GLContext context = SDL_GL_GetCurrentContext();
  int before;
  int fullscreen_native;
  int logical_width, logical_height;
  const int desired_width = 257, desired_height = 193;
  CHECK(context != NULL && test_window != NULL, "native OpenGL window is missing");
  CHECK(SDL_GetWindowFlags(test_window) & SDL_WINDOW_RESIZABLE,
        "native window is not resizable");
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB,
               GL_UNSIGNED_BYTE, texture_pixels);
  CHECK(glGetError() == GL_NO_ERROR, "could not initialize reference texture");
  SystemReshapeFunc(reshape);
  CHECK(redisplay_count > 0, "registering reshape did not request a redraw");
  CHECK(checkDimensions() == 0, "initial window dimensions failed");
  CHECK(SystemApplyWindow(desired_width, desired_height, display_flags, 0),
        "window resize was rejected");
  settleWindow();
  SystemGetWindowSize(&logical_width, &logical_height);
  printf("Resize request: %dx%d; actual logical size: %dx%d\n",
         desired_width, desired_height, logical_width, logical_height);
  /* A fractional-scale compositor can round the requested logical size by a
   * pixel. Validate the actual surface and a narrow rounding tolerance. */
  CHECK(abs(logical_width - desired_width) <= 1 &&
        abs(logical_height - desired_height) <= 1,
        "native compositor did not apply the requested windowed resize");
  CHECK(checkDimensions() == 0 && checkTexture(texture, context) == 0,
        "resize did not preserve renderer state or drawable reshape");
  CHECK(SystemApplyWindow(desired_width, desired_height, display_flags, SYSTEM_FULLSCREEN),
        "borderless fullscreen request was rejected");
  settleWindow();
  fullscreen_native = (SDL_GetWindowFlags(test_window) & SDL_WINDOW_FULLSCREEN) != 0;
  CHECK(SystemIsFullscreen() && fullscreen_native,
        "fullscreen state does not match native SDL state");
  CHECK(checkDimensions() == 0 && checkTexture(texture, context) == 0,
        "fullscreen did not preserve renderer state or drawable reshape");
  monitor_windowed_restore = 1;
  CHECK(SystemApplyWindow(desired_width, desired_height, display_flags, 0),
        "return from fullscreen was rejected");
  settleWindow();
  monitor_windowed_restore = 0;
  CHECK(!stale_fullscreen_reshape,
        "fullscreen restoration exposed stale fullscreen dimensions to settings persistence");
  CHECK(!SystemIsFullscreen() &&
        !(SDL_GetWindowFlags(test_window) & SDL_WINDOW_FULLSCREEN),
        "window remained fullscreen after restoring windowed mode");
  SystemGetWindowSize(&logical_width, &logical_height);
  CHECK(abs(logical_width - desired_width) <= 1 &&
        abs(logical_height - desired_height) <= 1,
        "fullscreen roundtrip did not restore requested logical dimensions");
  CHECK(checkDimensions() == 0 && checkTexture(texture, context) == 0,
        "fullscreen roundtrip lost texture or reshape state");
  CHECK(!SystemApplyWindow(0, 193, display_flags, 0) &&
        !SystemApplyWindow(257, -1, display_flags, 0) &&
        !SystemApplyWindow(257, 193, display_flags | SYSTEM_STENCIL, 0),
        "invalid dimensions or context-changing display mode were accepted in place");
#if SDL_MAJOR_VERSION >= 3
  foreign_window = SDL_CreateWindow("GLTron foreign event fixture", 32, 32, SDL_WINDOW_HIDDEN);
#else
  foreign_window = SDL_CreateWindow("GLTron foreign event fixture", 0, 0, 32, 32, SDL_WINDOW_HIDDEN);
#endif
  CHECK(foreign_window != NULL, "could not create isolated foreign-window fixture");
  before = reshape_count;
  memset(&event, 0, sizeof(event));
#if SDL_MAJOR_VERSION >= 3
  event.type = SDL_EVENT_WINDOW_RESIZED;
#else
  event.type = SDL_WINDOWEVENT;
  event.window.event = SDL_WINDOWEVENT_RESIZED;
#endif
  event.window.windowID = SDL_GetWindowID(foreign_window);
  event.window.data1 = 17;
  event.window.data2 = 19;
  CHECK(!SystemHandleWindowEvent(&event) && reshape_count == before,
        "foreign window resize affected the game window");
#if SDL_MAJOR_VERSION >= 3
  event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
#else
  event.window.event = SDL_WINDOWEVENT_CLOSE;
#endif
  CHECK(!SystemHandleWindowEvent(&event), "foreign window close requested game shutdown");
  event.window.windowID = SDL_GetWindowID(test_window);
  CHECK(SystemHandleWindowEvent(&event), "own-window close was not recognized");
  CHECK(!SystemHandleWindowEvent(NULL), "NULL native window event was not ignored");
  SDL_DestroyWindow(foreign_window);
  foreign_window = NULL;

  /* Simulate fullscreen changed outside the game settings path. The public
   * state query must reflect native flags even before queued events dispatch. */
#if SDL_MAJOR_VERSION >= 3
  CHECK(SDL_SetWindowFullscreen(test_window, true) && SDL_SyncWindow(test_window),
        "external native fullscreen request failed");
#else
  CHECK(SDL_SetWindowFullscreen(test_window, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0,
        "external native fullscreen request failed");
#endif
  CHECK(SystemIsFullscreen(), "fullscreen query ignored external native state");
  settleWindow();
  CHECK(checkDimensions() == 0 && checkTexture(texture, context) == 0,
        "external fullscreen changed drawable reshape or GL resources");
#if SDL_MAJOR_VERSION >= 3
  before = reshape_count;
  memset(&event, 0, sizeof(event));
  event.type = SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
  event.window.windowID = SDL_GetWindowID(test_window);
  SystemHandleWindowEvent(&event);
  CHECK(reshape_count == before + 1, "native fullscreen-enter event omitted reshape");
  CHECK(SDL_SetWindowFullscreen(test_window, false) && SDL_SyncWindow(test_window) &&
        SDL_SetWindowSize(test_window, desired_width, desired_height) && SDL_SyncWindow(test_window),
        "external native fullscreen restore failed");
#else
  CHECK(SDL_SetWindowFullscreen(test_window, 0) == 0,
        "external native fullscreen restore failed");
  SDL_SetWindowSize(test_window, desired_width, desired_height);
#endif
  CHECK(!SystemIsFullscreen(), "fullscreen query retained external fullscreen after restore");
  settleWindow();
#if SDL_MAJOR_VERSION >= 3
  before = reshape_count;
  event.type = SDL_EVENT_WINDOW_LEAVE_FULLSCREEN;
  SystemHandleWindowEvent(&event);
  CHECK(reshape_count == before + 1, "native fullscreen-leave event omitted reshape");
#endif
  CHECK(checkDimensions() == 0 && checkTexture(texture, context) == 0,
        "external fullscreen restore changed drawable reshape or GL resources");
  glDeleteTextures(1, &texture);
  return 0;
}

static int nativeRelativeMode(void) {
#if SDL_MAJOR_VERSION >= 3
  return SDL_GetWindowRelativeMouseMode(test_window);
#else
  return SDL_GetRelativeMouseMode() == SDL_TRUE;
#endif
}
static int cursorVisible(void) {
#if SDL_MAJOR_VERSION >= 3
  return SDL_CursorVisible();
#else
  return SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE;
#endif
}
static int checkMouseAndFrameHooks(void) {
  int before = redisplay_count;
  SystemUnhidePointer();
  SystemUngrabInput();
  CHECK(cursorVisible() && !relative_mode && !nativeRelativeMode(),
        "initial visible pointer retained relative mode");
  SystemWarpPointer(100, 100);
  CHECK(anchor_x == 100 && anchor_y == 100,
        "classic camera anchor did not retain logical coordinates");
  SystemHidePointer();
  CHECK(!cursorVisible() && !relative_mode && !nativeRelativeMode(),
        "hiding an ungrabbed pointer enabled relative input");
  SystemGrabInput();
  CHECK(relative_mode && nativeRelativeMode(),
        "hidden grabbed pointer did not enable relative input");
  SystemWarpPointer(101, 103);
  CHECK(anchor_x == 101 && anchor_y == 103,
        "relative camera anchor was scaled into drawable coordinates");
  SystemUnhidePointer();
  CHECK(cursorVisible() && !relative_mode && !nativeRelativeMode(),
        "showing a grabbed pointer did not disable relative input");
  SystemHidePointer();
  CHECK(relative_mode && nativeRelativeMode(), "rehiding did not restore relative input");
  SystemUngrabInput();
  CHECK(!relative_mode && !nativeRelativeMode(), "ungrabbing did not release relative input");
  SystemUnhidePointer();
  SystemCaptureNextFrame(captureFrame);
  CHECK(redisplay_count > before && capture_count == 0,
        "capture request ran before a completed frame or omitted redraw");
  SystemSwapBuffers();
  SystemSwapBuffers();
  CHECK(capture_count == 1, "capture hook did not run exactly once before swap");
  return 0;
}

static void renderQuadrants(int width, int height) {
  int left = width / 2, bottom = height / 2, quadrant;
  glDrawBuffer(GL_BACK);
  glViewport(0, 0, width, height);
  glDisable(GL_DITHER);
  glEnable(GL_SCISSOR_TEST);
  for(quadrant = 0; quadrant < 4; quadrant++) {
    int right = quadrant & 1, top = quadrant >> 1;
    glScissor(right ? left : 0, top ? bottom : 0,
              right ? width - left : left, top ? height - bottom : bottom);
    glClearColor(quadrant_colors[quadrant][0] / 255.0f,
                 quadrant_colors[quadrant][1] / 255.0f,
                 quadrant_colors[quadrant][2] / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  glDisable(GL_SCISSOR_TEST);
  glFinish();
}

static int compareQuadrants(const unsigned char *pixels, int width, int height,
                            size_t pitch) {
  int x, y;
  for(y = 0; y < height; y++) {
    for(x = 0; x < width; x++) {
      int quadrant = (y < height - height / 2 ? 2 : 0) + (x >= width / 2);
      if(memcmp(pixels + (size_t)y * pitch + (size_t)x * 3,
                quadrant_colors[quadrant], 3) != 0) {
        fprintf(stderr, "Wrong RGB pixel at (%d,%d) in %dx%d image\n", x, y, width, height);
        return 1;
      }
    }
  }
  return 0;
}

static int checkPng(const char *path, int width, int height) {
  png_image image;
  unsigned char *pixels;
  int mismatch;
  memset(&image, 0, sizeof(image));
  image.version = PNG_IMAGE_VERSION;
  CHECK(png_image_begin_read_from_file(&image, path), "PNG decoder could not open screenshot");
  CHECK(image.width == (png_uint_32)width && image.height == (png_uint_32)height,
        "PNG dimensions do not match the drawable");
  image.format = PNG_FORMAT_RGB;
  pixels = malloc(PNG_IMAGE_SIZE(image));
  CHECK(pixels != NULL, "could not allocate PNG verification pixels");
  CHECK(png_image_finish_read(&image, NULL, pixels, 0, NULL), "PNG pixel decode failed");
  mismatch = compareQuadrants(pixels, width, height, (size_t)width * 3);
  png_image_free(&image);
  free(pixels);
  CHECK(!mismatch, "PNG channel order, vertical orientation, or row packing changed");
  return 0;
}

static int checkBmp(const char *path, int width, int height) {
  SDL_Surface *source = SDL_LoadBMP(path);
  SDL_Surface *rgb;
  int mismatch;
  CHECK(source != NULL, "BMP decoder could not open screenshot");
#if SDL_MAJOR_VERSION >= 3
  rgb = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGB24);
  SDL_DestroySurface(source);
#else
  rgb = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGB24, 0);
  SDL_FreeSurface(source);
#endif
  CHECK(rgb != NULL, "BMP RGB conversion failed");
  CHECK(rgb->w == width && rgb->h == height, "BMP dimensions changed");
  mismatch = compareQuadrants(rgb->pixels, width, height, (size_t)rgb->pitch);
#if SDL_MAJOR_VERSION >= 3
  SDL_DestroySurface(rgb);
#else
  SDL_FreeSurface(rgb);
#endif
  CHECK(!mismatch, "BMP channel order, vertical orientation, or odd row packing changed");
  return 0;
}

static int checkScreenshots(void) {
  char png_path[PATH_MAX], bmp_path[PATH_MAX], odd_bmp_path[PATH_MAX];
  GLint pack_alignment, pack_row_length, pack_skip_rows, pack_skip_pixels, read_buffer;
  int width, height, x, y;
  unsigned char odd_pixels[7 * 5 * 3];
  char *queued_png, *queued_bmp, *next_png, *next_bmp;
  SystemGetDrawableSize(&width, &height);
  CHECK(snprintf(png_path, sizeof(png_path), "%s/native.png", artifact_directory) < (int)sizeof(png_path) &&
        snprintf(bmp_path, sizeof(bmp_path), "%s/native.bmp", artifact_directory) < (int)sizeof(bmp_path) &&
        snprintf(odd_bmp_path, sizeof(odd_bmp_path), "%s/odd-row.bmp", artifact_directory) < (int)sizeof(odd_bmp_path),
        "artifact directory is too long");
  renderQuadrants(width, height);
  CHECK(glGetError() == GL_NO_ERROR, "reference frame rendering failed");
  glPixelStorei(GL_PACK_ALIGNMENT, 8);
  glPixelStorei(GL_PACK_ROW_LENGTH, width + 11);
  glPixelStorei(GL_PACK_SKIP_ROWS, 3);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 5);
  glReadBuffer(GL_FRONT);
  CHECK(VideoCaptureScreenshot(png_path, 1) == 0, "native PNG capture failed");
  CHECK(VideoCaptureScreenshot(bmp_path, 0) == 0, "native BMP capture failed");
  glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &pack_row_length);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &pack_skip_rows);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &pack_skip_pixels);
  glGetIntegerv(GL_READ_BUFFER, &read_buffer);
  CHECK(pack_alignment == 8 && pack_row_length == width + 11 &&
        pack_skip_rows == 3 && pack_skip_pixels == 5 && read_buffer == GL_FRONT,
        "screenshot capture changed GL packing or read-buffer state");
  CHECK(checkPng(png_path, width, height) == 0 && checkBmp(bmp_path, width, height) == 0,
        "native screenshot pixel comparison failed");
  CHECK(VideoCaptureScreenshot(png_path, 1) != 0 &&
        VideoCaptureScreenshot(bmp_path, 0) != 0,
        "screenshot capture overwrote an existing file");
  CHECK(checkPng(png_path, width, height) == 0 && checkBmp(bmp_path, width, height) == 0,
        "refused overwrite still changed screenshot pixels");
  /* This remains odd even when a HiDPI surface has an even pixel width. */
  CHECK(!fileExists(odd_bmp_path), "odd-row fixture already exists");
  for(y = 0; y < 5; y++) for(x = 0; x < 7; x++) {
    int quadrant = (y >= 5 / 2 ? 2 : 0) + (x >= 7 / 2);
    memcpy(odd_pixels + (y * 7 + x) * 3, quadrant_colors[quadrant], 3);
  }
  CHECK(SystemWriteBMP(odd_bmp_path, 7, 5, odd_pixels) == 0 &&
        checkBmp(odd_bmp_path, 7, 5) == 0,
        "odd-width RGB input did not survive padded BMP rows");
  CHECK(SystemWriteBMP(odd_bmp_path, 7, 5, odd_pixels) == -1 &&
        checkBmp(odd_bmp_path, 7, 5) == 0,
        "direct BMP writer overwrote an existing image");
  CHECK(SystemWriteBMP(NULL, 7, 5, odd_pixels) == -1 &&
        SystemWriteBMP(odd_bmp_path, -1, 5, odd_pixels) == -1 &&
        SystemWriteBMP(odd_bmp_path, INT_MAX, 5, odd_pixels) == -1 &&
        SystemWriteBMP(odd_bmp_path, 7, 5, NULL) == -1,
        "invalid BMP dimensions or pointers were accepted");
  queued_png = getPossiblePath(PATH_SNAPSHOTS, "gltron-" VERSION "-1.png");
  queued_bmp = getPossiblePath(PATH_SNAPSHOTS, "gltron-" VERSION "-1.bmp");
  next_png = getPossiblePath(PATH_SNAPSHOTS, "gltron-" VERSION "-2.png");
  next_bmp = getPossiblePath(PATH_SNAPSHOTS, "gltron-" VERSION "-2.bmp");
  CHECK(queued_png && queued_bmp && next_png && next_bmp,
        "could not allocate queued screenshot paths");
  CHECK(!fileExists(queued_png) && !fileExists(queued_bmp) &&
        !fileExists(next_png) && !fileExists(next_bmp),
        "queued screenshot fixtures already exist");
  doPngScreenShot(NULL);
  doBmpScreenShot(NULL);
  CHECK(!fileExists(queued_png) && !fileExists(queued_bmp),
        "screenshot request captured before the completed backbuffer");
  SystemSwapBuffers();
  CHECK(checkPng(queued_png, width, height) == 0 &&
        checkBmp(queued_bmp, width, height) == 0,
        "queued PNG/BMP capture did not preserve the completed frame");
  SystemSwapBuffers();
  CHECK(!fileExists(next_png) && !fileExists(next_bmp),
        "queued screenshot request was not consumed after one frame");
  /* A context restart cancels the queued callback and its format flags.
   * Requesting another format afterwards must not revive the canceled BMP. */
  doBmpScreenShot(NULL);
  VideoCancelPendingScreenshots();
  doPngScreenShot(NULL);
  CHECK(!fileExists(next_png) && !fileExists(next_bmp),
        "replacement screenshot request captured before the completed frame");
  renderQuadrants(width, height);
  SystemSwapBuffers();
  CHECK(checkPng(next_png, width, height) == 0 && !fileExists(next_bmp),
        "canceling queued BMP left a ghost capture in the later PNG request");
  SystemSwapBuffers();
  CHECK(!fileExists(next_bmp), "canceled BMP reappeared on a later frame");
  puts("PASS: canceled BMP request stays canceled when the next screenshot is PNG");
  free(queued_png); free(queued_bmp); free(next_png); free(next_bmp);
  printf("PASS: screenshots decode exact RGB quadrants at %dx%d drawable pixels; odd BMP rows and GL state preserved\n",
         width, height);
  return 0;
}

/* Run before SDL starts threads: both children race to create the same path.
 * Exactly one may succeed, and the file must contain one complete RGB image. */
static int checkConcurrentBmp(void) {
  int pipes[2], i, status, successes = 0;
  pid_t writers[2];
  char path[PATH_MAX];
  unsigned char expected[3] = {0, 0, 0};
  SDL_Surface *surface, *rgb;
  CHECK(snprintf(path, sizeof(path), "%s/concurrent.bmp", artifact_directory) < (int)sizeof(path),
        "concurrent BMP path is too long");
  CHECK(!fileExists(path), "concurrent BMP fixture already exists");
  CHECK(pipe(pipes) == 0, "could not synchronize concurrent BMP writers");
  for(i = 0; i < 2; i++) {
    writers[i] = fork();
    CHECK(writers[i] >= 0, "could not create concurrent BMP writer");
    if(writers[i] == 0) {
      unsigned char pixels[7 * 5 * 3];
      char start;
      int pixel;
      close(pipes[1]);
      if(read(pipes[0], &start, 1) != 1) _exit(3);
      close(pipes[0]);
      for(pixel = 0; pixel < 7 * 5; pixel++) {
        pixels[pixel * 3] = i == 0 ? 255 : 0;
        pixels[pixel * 3 + 1] = i == 1 ? 255 : 0;
        pixels[pixel * 3 + 2] = 0;
      }
      _exit(SystemWriteBMP(path, 7, 5, pixels) == 0 ? 0 : 2);
    }
  }
  close(pipes[0]);
  CHECK(write(pipes[1], "go", 2) == 2, "could not release concurrent writers");
  close(pipes[1]);
  for(i = 0; i < 2; i++) {
    CHECK(waitpid(writers[i], &status, 0) == writers[i] && WIFEXITED(status),
          "concurrent BMP writer did not exit cleanly");
    CHECK(WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 2,
          "concurrent BMP writer failed before exclusive creation");
    if(WEXITSTATUS(status) == 0) { successes++; expected[i] = 255; }
  }
  CHECK(successes == 1, "concurrent BMP writers did not have exactly one winner");
  surface = SDL_LoadBMP(path);
  CHECK(surface != NULL, "concurrent BMP result could not be decoded");
#if SDL_MAJOR_VERSION >= 3
  rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGB24);
  SDL_DestroySurface(surface);
#else
  rgb = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB24, 0);
  SDL_FreeSurface(surface);
#endif
  CHECK(rgb != NULL && rgb->w == 7 && rgb->h == 5, "concurrent BMP result is incomplete");
  for(i = 0; i < 7 * 5; i++)
    CHECK(memcmp((unsigned char *)rgb->pixels + (i / 7) * rgb->pitch + (i % 7) * 3,
                 expected, 3) == 0, "concurrent BMP output mixed the writers' pixels");
#if SDL_MAJOR_VERSION >= 3
  SDL_DestroySurface(rgb);
#else
  SDL_FreeSurface(rgb);
#endif
  puts("PASS: concurrent BMP creation has one winner and preserves complete RGB pixels");
  return 0;
}

int main(int argc, char **argv) {
  int result = 1;
  char template_path[] = "/tmp/gltron-video-platform.XXXXXX";
  const char *directory;
  const char *filenames[] = { "native.png", "native.bmp", "odd-row.bmp", "concurrent.bmp",
                             "gltron-" VERSION "-1.png", "gltron-" VERSION "-1.bmp",
                             "gltron-" VERSION "-2.png" };
  size_t i;
  if(argc > 2) {
    fprintf(stderr, "usage: %s [existing-empty-artifact-directory]\n", argv[0]);
    return 2;
  }
  directory = argc == 2 ? argv[1] : mkdtemp(template_path);
  if(directory == NULL || strlen(directory) >= sizeof(artifact_directory)) {
    fprintf(stderr, "Cannot create test artifact directory\n");
    return 1;
  }
  strcpy(artifact_directory, directory);
  if(checkConcurrentBmp()) goto cleanup;
  /* Ask X11 tiling compositors to float only this disposable test window so
   * requested logical sizes can be exercised without desktop rule changes. */
  SDL_SetHint(SDL_HINT_X11_WINDOW_TYPE, "_NET_WM_WINDOW_TYPE_DIALOG");
  SystemInitWindow(0, 0, 321, 241);
  SystemInitDisplayMode(display_flags, 0);
  if(SystemCreateWindow("GLTron native display regression") < 0) goto cleanup;
  test_window = SDL_GL_GetCurrentWindow();
  printf("Native SDL%d display driver: %s; GL renderer: %s\n", SDL_MAJOR_VERSION,
         SDL_GetCurrentVideoDriver(), glGetString(GL_RENDERER));
  /* A tiling Wayland compositor may require the harness to float this exact
   * test window. Allow up to five seconds for that external placement; never
   * modify desktop configuration or relax the native resize assertions. */
  if(getenv("GLTRON_NATIVE_PLACEMENT_WAIT_MS") != NULL) {
    long wait_ms = strtol(getenv("GLTRON_NATIVE_PLACEMENT_WAIT_MS"), NULL, 10);
    if(wait_ms > 5000) wait_ms = 5000;
    while(wait_ms > 0) {
      pumpWindowEvents();
      SDL_Delay(10);
      wait_ms -= 10;
    }
    SDL_RestoreWindow(test_window);
    settleWindow();
  }
  if(checkWindowLifecycle() || checkMouseAndFrameHooks() || checkScreenshots()) goto cleanup;
  result = 0;
cleanup:
  if(foreign_window != NULL) SDL_DestroyWindow(foreign_window);
  if(video_initialized) {
    SystemUnhidePointer();
    SystemUngrabInput();
    SystemDestroyWindow(1);
  }
  if(relative_mode || video_initialized || SDL_GL_GetCurrentContext() != NULL) {
    fprintf(stderr, "FAIL: window teardown left renderer or input state active\n");
    result = 1;
  }
  SDL_Quit();
  if(result == 0 && argc == 1) {
    for(i = 0; i < sizeof(filenames) / sizeof(filenames[0]); i++) {
      char path[PATH_MAX];
      if(snprintf(path, sizeof(path), "%s/%s", artifact_directory, filenames[i]) < (int)sizeof(path))
        unlink(path);
    }
    rmdir(artifact_directory);
  } else {
    fprintf(stderr, "Native regression artifacts: %s\n", artifact_directory);
  }
  if(result == 0) printf("PASS: SDL%d resize, native/external fullscreen, texture/context lifetime, drawable reshape, mouse state, frame hook, and teardown\n", SDL_MAJOR_VERSION);
  return result;
}
