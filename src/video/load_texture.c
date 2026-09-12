#include "video/video.h"
#include "filesystem/path.h"

#include "Nebu_scripting.h"

void freeTextureData(texture *tex) {
  unload_png_texture(tex);
}

static texture* tryLoadTextureData(const char *filename) {
  texture *tex = NULL;
  char *path;
  char *artpack = NULL;
  if(filename == NULL || filename[0] == '\0')
    return NULL;
  scripting_GetGlobal("settings", "current_artpack", NULL);
  scripting_GetStringResult(&artpack);
  path = getArtPath(artpack != NULL ? artpack : "default", filename);
  if(path != NULL) {
    tex = LOAD_TEX(path);
    free(path);
  }
  /* A malformed optional override must not hide a working original asset. */
  if(tex == NULL && artpack != NULL && strcmp(artpack, "default") != 0) {
    path = getArtPath("default", filename);
    if(path != NULL) {
      tex = LOAD_TEX(path);
      free(path);
    }
  }
  free(artpack);
  return tex;
}

texture* loadTextureData(const char *filename) {
  texture *tex = tryLoadTextureData(filename);
  if(tex == NULL) {
    fprintf(stderr, "fatal: failed to load texture %s\n",
            filename != NULL ? filename : "(null)");
    exit(1); /* Critical: both selected and original assets are unavailable. */
  }
  return tex;
}

int loadTextureChecked(const char *filename, int format) {
  texture *tex;
  GLint internal;
  GLint maxSize = 0;
  int level = 0;

  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
  if(maxSize < 1)
    return 0;
  tex = tryLoadTextureData(filename);
  if(tex == NULL)
    return 0;
  internal = tex->channels == 3 ? GL_RGB : GL_RGBA;
  if(format == GL_DONT_CARE)
    format = internal;
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  for(;;) {
    texture *next;
    if(tex->width <= maxSize && tex->height <= maxSize) {
      glTexImage2D(GL_TEXTURE_2D, level++, format, tex->width, tex->height,
                    0, internal, GL_UNSIGNED_BYTE, tex->data);
    }
    if(tex->width == 1 && tex->height == 1)
      break;
    next = mipmap_png_texture(tex, 1, 0, 0);
    freeTextureData(tex);
    tex = next;
    if(tex == NULL)
      return 0;
  }
  freeTextureData(tex);
  return 1;
}

void loadTexture(const char *filename, int format) {
  if(!loadTextureChecked(filename, format)) {
    fprintf(stderr, "fatal: failed to upload texture %s\n",
            filename != NULL ? filename : "(null)");
    exit(1);
  }
}
