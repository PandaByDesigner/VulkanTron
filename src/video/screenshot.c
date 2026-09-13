#include "video/video.h"
#include "filesystem/path.h"
#include "Nebu_filesystem.h"

#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef GLTRON_DIRECT_VULKAN
#define SCREENSHOT_PREFIX "vulkantron"
#else
#define SCREENSHOT_PREFIX "gltron"
#endif

typedef struct {
  int width, height;
  unsigned char *pixmap;
} screenshot_info_t;

static int pending_png, pending_bmp;

void VideoCancelPendingScreenshots(void) {
  pending_png = pending_bmp = 0;
  SystemCaptureNextFrame(NULL);
}

static int writePixmapToPng(const screenshot_info_t *image, const char *filename) {
  png_structp png;
  png_infop info;
  FILE *output = fopen(filename, "wbx");
  int row;
  if(output == NULL)
    return -1;
  png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if(png == NULL) {
    fclose(output);
    remove(filename);
    return -1;
  }
  info = png_create_info_struct(png);
  if(info == NULL) {
    png_destroy_write_struct(&png, NULL);
    fclose(output);
    remove(filename);
    return -1;
  }
  if(setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(output);
    remove(filename);
    return -1;
  }
  png_init_io(png, output);
  png_set_IHDR(png, info, image->width, image->height, 8, PNG_COLOR_TYPE_RGB,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  for(row = image->height - 1; row >= 0; row--)
    png_write_row(png, image->pixmap + (size_t)row * image->width * 3);
  png_write_end(png, info);
  png_destroy_write_struct(&png, &info);
  if(fclose(output) != 0) {
    remove(filename);
    return -1;
  }
  return 0;
}

static char *getNextFilename(const char *suffix, int *start_at) {
  char *path = NULL;
  char filename[256];
  do {
    free(path);
    (*start_at)++;
    snprintf(filename, sizeof(filename), "%s-%s-%d%s", SCREENSHOT_PREFIX,
              VERSION, *start_at, suffix);
    path = getPossiblePath(PATH_SNAPSHOTS, filename);
  } while(path != NULL && fileExists(path));
  return path;
}

static int captureScreenToPixmap(screenshot_info_t *image) {
  GLint old_read_buffer;
  GLenum error;
  size_t bytes;
  SystemGetDrawableSize(&image->width, &image->height);
  image->pixmap = NULL;
  if(image->width <= 0 || image->height <= 0 ||
     (size_t)image->width > SIZE_MAX / 3 ||
     (size_t)image->height > SIZE_MAX / ((size_t)image->width * 3))
    return -1;
  bytes = (size_t)image->width * image->height * 3;
  image->pixmap = malloc(bytes);
  if(image->pixmap == NULL)
    return -1;
  glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
  glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, image->width, image->height, GL_RGB, GL_UNSIGNED_BYTE,
                image->pixmap);
  error = glGetError();
  glReadBuffer(old_read_buffer);
  glPopClientAttrib();
  if(error != GL_NO_ERROR) {
    fprintf(stderr, "[screenshot] OpenGL capture error: %u\n", error);
    free(image->pixmap);
    image->pixmap = NULL;
    return -1;
  }
  return 0;
}

/* Call only on a completed back buffer. Public for the native rendering gate. */
int VideoCaptureScreenshot(const char *filename, int png_format) {
  screenshot_info_t image;
  int result;
  if(filename == NULL || fileExists(filename) || captureScreenToPixmap(&image) != 0)
    return -1;
  if(png_format)
    result = writePixmapToPng(&image, filename);
  else
    result = SystemWriteBMP((char *)filename, image.width, image.height, image.pixmap);
  free(image.pixmap);
  return result;
}

static void captureRequestedFrame(void) {
  static int last_png, last_bmp;
  char *path;
  int format;
  for(format = 0; format < 2; format++) {
    if(!(format ? pending_png : pending_bmp))
      continue;
    path = getNextFilename(format ? ".png" : ".bmp", format ? &last_png : &last_bmp);
    if(path != NULL) {
      if(VideoCaptureScreenshot(path, format) == 0)
        fprintf(stderr, "Screenshot written to %s\n", path);
      else
        fprintf(stderr, "Error writing screenshot %s\n", path);
      free(path);
    }
  }
  pending_png = pending_bmp = 0;
}

void doPngScreenShot(Visual *display) {
  (void)display;
  pending_png = 1;
  SystemCaptureNextFrame(captureRequestedFrame);
}

void doBmpScreenShot(Visual *display) {
  (void)display;
  pending_bmp = 1;
  SystemCaptureNextFrame(captureRequestedFrame);
}
