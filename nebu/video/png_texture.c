#include "video/nebu_png_texture.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <png.h>

#define ERR_PREFIX "[load_png_texture] "
#define MAX_TEXTURE_BYTES ((size_t)256 * 1024 * 1024)
#define MAX_TEXTURE_DIMENSION 16384

/* Bound allocations before multiplying file-provided dimensions. */
static int texture_size(int width, int height, int channels, size_t *bytes) {
  size_t row;
  if(width <= 0 || height <= 0 || width > MAX_TEXTURE_DIMENSION ||
     height > MAX_TEXTURE_DIMENSION || (channels != 3 && channels != 4))
    return 0;
  row = (size_t)width * (size_t)channels;
  if((size_t)height > MAX_TEXTURE_BYTES / row)
    return 0;
  *bytes = row * (size_t)height;
  return 1;
}

static void user_read_data(png_structp png_ptr, png_bytep data,
                           png_size_t length) {
  FILE *file = (FILE*)png_get_io_ptr(png_ptr);
  if(fread(data, 1, length, file) != length)
    png_error(png_ptr, "truncated PNG data");
}

png_texture* load_png_texture(char *filename) {
  /* Heap-owned mutable state remains valid across libpng's longjmp. */
  struct ReadState {
    png_texture tex;
    png_byte **rows;
  } *state;
  png_structp png_ptr;
  png_infop info_ptr;
  FILE *file;
  png_uint_32 width, height, i;
  int bitdepth, color_type;
  size_t bytes;
  png_texture *result;

  if(filename == NULL || (file = fopen(filename, "rb")) == NULL) {
    fprintf(stderr, ERR_PREFIX "can't open file %s\n",
            filename != NULL ? filename : "(null)");
    return NULL;
  }
  state = calloc(1, sizeof(*state));
  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if(state == NULL || png_ptr == NULL) {
    free(state);
    if(png_ptr != NULL) png_destroy_read_struct(&png_ptr, NULL, NULL);
    fclose(file);
    return NULL;
  }
  info_ptr = png_create_info_struct(png_ptr);
  if(info_ptr == NULL) {
    png_destroy_read_struct(&png_ptr, NULL, NULL);
    free(state);
    fclose(file);
    return NULL;
  }
  if(setjmp(png_jmpbuf(png_ptr))) {
    free(state->rows);
    free(state->tex.data);
    free(state);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(file);
    return NULL;
  }

  png_set_user_limits(png_ptr, MAX_TEXTURE_DIMENSION, MAX_TEXTURE_DIMENSION);
  png_set_read_fn(png_ptr, file, user_read_data);
  png_read_info(png_ptr, info_ptr);
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bitdepth, &color_type,
               NULL, NULL, NULL);
  if(bitdepth != 8 || (color_type != PNG_COLOR_TYPE_RGB &&
                       color_type != PNG_COLOR_TYPE_RGB_ALPHA))
    png_error(png_ptr, "GLTron textures require 8-bit RGB or RGBA PNG data");
  state->tex.width = (int)width;
  state->tex.height = (int)height;
  state->tex.channels = color_type == PNG_COLOR_TYPE_RGB ? 3 : 4;
  if(!texture_size(state->tex.width, state->tex.height,
                    state->tex.channels, &bytes))
    png_error(png_ptr, "PNG dimensions exceed GLTron's texture limits");

  state->tex.data = malloc(bytes);
  state->rows = malloc((size_t)height * sizeof(*state->rows));
  if(state->tex.data == NULL || state->rows == NULL)
    png_error(png_ptr, "out of memory reading texture");
  for(i = 0; i < height; ++i)
    state->rows[i] = state->tex.data + (size_t)(height - i - 1) * width *
                                      state->tex.channels;
  /* Keep the original byte layout and bottom-to-top row orientation. */
  png_read_image(png_ptr, state->rows);
  png_read_end(png_ptr, NULL);
  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
  fclose(file);
  free(state->rows);
  result = malloc(sizeof(*result));
  if(result != NULL)
    *result = state->tex;
  else
    free(state->tex.data);
  free(state);
  return result;
}

void unload_png_texture(png_texture *tex) {
  if(tex != NULL) {
    free(tex->data);
    free(tex);
  }
}

png_texture* mipmap_png_texture(png_texture *source, int level,
                                int clamp_u, int clamp_v) {
  png_texture *mip;
  int channel, x, y, fx, fy;
  size_t bytes;
  (void)clamp_u;
  (void)clamp_v;
  if(level != 1 || source == NULL || source->data == NULL ||
     !texture_size(source->width, source->height, source->channels, &bytes))
    return NULL;
  mip = malloc(sizeof(*mip));
  if(mip == NULL)
    return NULL;
  mip->channels = source->channels;
  fx = source->width > 1 ? 2 : 1;
  fy = source->height > 1 ? 2 : 1;
  mip->width = source->width / fx;
  mip->height = source->height / fy;
  texture_size(mip->width, mip->height, mip->channels, &bytes);
  mip->data = malloc(bytes);
  if(mip->data == NULL) {
    free(mip);
    return NULL;
  }
  /* Retain the classic box filter's integer rounding, including 1xN images. */
  for(channel = 0; channel < mip->channels; ++channel) {
    for(y = 0; y < mip->height; ++y) {
      for(x = 0; x < mip->width; ++x) {
        size_t src = (size_t)source->channels *
                     (fx * x + (size_t)fy * y * source->width) + channel;
        size_t right = (size_t)(fx - 1) * source->channels;
        size_t down = (size_t)(fy - 1) * source->width * source->channels;
        mip->data[channel + (size_t)mip->channels * (x + y * mip->width)] =
          (source->data[src] + source->data[src + right] +
           source->data[src + right + down] + source->data[src + down]) / 4;
      }
    }
  }
  return mip;
}
