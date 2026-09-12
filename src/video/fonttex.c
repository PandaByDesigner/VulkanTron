#include "video/video.h"
#include "filesystem/path.h"
#include "Nebu_filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

// #include <GL/gl.h>
#define NO_SDL_GLEXT
#ifdef GLTRON_USE_SDL3
#include <SDL3/SDL_opengl.h>
#else
#include "SDL_opengl.h"
#endif

#define FTX_ERR "[FontTex error]: "

/* Return failure on EOF, overlong records and truncated metadata. */
static int getLine(char *buf, int size, file_handle file) {
  while(file_gets(file, buf, size) != NULL) {
    char *start = buf;
    size_t length = strlen(buf);
    if(length == (size_t)size - 1 && buf[length - 1] != '\n')
      return 0;
    while(*start != '\0' && isspace((unsigned char)*start)) ++start;
    if(*start == '\0' || *start == '#') continue;
    memmove(buf, start, strlen(start) + 1);
    length = strlen(buf);
    while(length > 0 && isspace((unsigned char)buf[length - 1]))
      buf[--length] = '\0';
    return 1;
  }
  return 0;
}

static int parseNumbers(const char *line, int *values, int count) {
  int i;
  for(i = 0; i < count; ++i) {
    char *end;
    long value;
    errno = 0;
    value = strtol(line, &end, 10);
    if(line == end || errno == ERANGE || value < INT_MIN || value > INT_MAX)
      return 0;
    values[i] = (int)value;
    line = end;
  }
  while(isspace((unsigned char)*line)) ++line;
  return *line == '\0';
}

FontTex *ftxLoadFont(const char *filename) {
  char *path;
  file_handle file;
  char buf[100];
  char **textures = NULL;
  int values[3], i, cells;
  FontTex *ftx = NULL;

  if(filename == NULL)
    return NULL;
  path = getPath(PATH_DATA, filename);
  if(path == NULL)
    return NULL;
  file = file_open(path, "r");
  free(path);
  if(file == NULL)
    return NULL;
  ftx = calloc(1, sizeof(*ftx));
  if(ftx == NULL || !getLine(buf, sizeof(buf), file) ||
     !parseNumbers(buf, values, 3)) goto fail;
  ftx->nTextures = values[0];
  ftx->texwidth = values[1];
  ftx->width = values[2];
  if(ftx->nTextures <= 0 || ftx->nTextures > 256 || ftx->texwidth <= 0 ||
     ftx->texwidth > 16384 || ftx->width <= 0 ||
     ftx->width > ftx->texwidth || ftx->texwidth % ftx->width != 0)
    goto fail;
  if(!getLine(buf, sizeof(buf), file) || !parseNumbers(buf, values, 2))
    goto fail;
  ftx->lower = values[0];
  ftx->upper = values[1];
  cells = ftx->texwidth / ftx->width;
  if(ftx->lower < 0 || ftx->upper > 255 || ftx->upper < ftx->lower ||
     (size_t)ftx->upper - ftx->lower + 2 >
       (size_t)ftx->nTextures * cells * cells)
    goto fail;
  if(!getLine(buf, sizeof(buf), file)) goto fail;
  ftx->fontname = malloc(strlen(buf) + 1);
  textures = calloc((size_t)ftx->nTextures, sizeof(*textures));
  if(ftx->fontname == NULL || textures == NULL) goto fail;
  strcpy(ftx->fontname, buf);
  for(i = 0; i < ftx->nTextures; ++i) {
    if(!getLine(buf, sizeof(buf), file)) goto fail;
    textures[i] = malloc(strlen(buf) + 1);
    if(textures[i] == NULL) goto fail;
    strcpy(textures[i], buf);
  }
  /* Validate all metadata before creating GPU resources. */
  ftx->texID = calloc((size_t)ftx->nTextures, sizeof(*ftx->texID));
  if(ftx->texID == NULL) goto fail;
  glGenTextures(ftx->nTextures, ftx->texID);
  for(i = 0; i < ftx->nTextures; ++i) {
    glBindTexture(GL_TEXTURE_2D, ftx->texID[i]);
    if(!loadTextureChecked(textures[i], GL_RGBA)) goto fail;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  }
  for(i = 0; i < ftx->nTextures; ++i) free(textures[i]);
  free(textures);
  file_close(file);
  return ftx;

fail:
  fprintf(stderr, FTX_ERR "invalid or unavailable font '%s'\n", filename);
  if(textures != NULL) {
    for(i = 0; i < ftx->nTextures; ++i) free(textures[i]);
    free(textures);
  }
  ftxUnloadFont(ftx);
  file_close(file);
  return NULL;
}

void ftxUnloadFont(FontTex *ftx) {
  if(ftx == NULL) return;
  if(ftx->texID != NULL) glDeleteTextures(ftx->nTextures, ftx->texID);
  free(ftx->texID);
  free(ftx->fontname);
  free(ftx);
}

static int color_base = 48;
static int colors = 8;
enum {
  white = 0,
  black,
  red,
  orange,
  yellow,
  lt_green,
  green,
  blue_green
};

unsigned char color_codes[][3] = {
  { 255, 255,255 },
  { 0, 0, 0 },
  { 255, 0, 0 },
  { 255, 128, 0 },
  { 255, 255, 0 },
  { 128, 255, 0 },
  { 0, 255, 0 },
  { 0, 255, 128 }
};

/*
0     48    white       65535  65536  65535  FFFFFF
1     49    black           0      0      0  000000

2     50    red         65535      0      0  FF0000
3     51    orange      65535  32768      0  FF8000
4     52    yellow      65535  65535      0  FFFF00
5     53    lt green    32768  65535      0  80FF00
6     54    green           0  65535      0  00FF00
7     55    blue green      0  65535  32768  00FF80
8     56    cyan            0  65535  65535  00FFFF
9     57    lt blue         0  32768  65535  0080FF
:     58    blue            0      0  65535  0000FF
;     59    purple      32768      0  65535  8000FF
< 60    magenta     65535      0  65535  FF00FF
=     61    purple red  65535      0  32768  FF0080

> 62    lt gray     49152  49152  49152  C0C0C0
?     63    dk gray     16384  16384  16384  404040

@     64    -           32768      0      0  800000
A     65    |           32768  16384      0  804000
B     66    |           32768  32768      0  808000
C     67    | darker    16384  32768      0  408000
D     68    | versions      0  32768      0  008000
E     69    | of            0  32768  16384  008040
F     70    | colors        0  32768  32768  008080
G     71    | 50..61        0  16384  32768  004080
H     72    |               0      0  32768  000080
I     73    |           16384      0  32768  400080
J     74    |           32768      0  32768  800080
K     75    -           32768      0  16384  800040
*/

void ftxRenderString(FontTex *ftx, const char *string, int len) {
  int i;
  int bound = -1;
  int index;
  
  int tex;
  int w;
  float cw;
  float cx, cy;

  if(ftx == NULL || string == NULL || len <= 0 || ftx->width <= 0 ||
     ftx->texwidth < ftx->width || ftx->texID == NULL)
    return;
  w = ftx->texwidth / ftx->width;
  cw = (float)ftx->width / (float)ftx->texwidth;

  for(i = 0; i < len; i++) {
    if(string[i] == 3) { /* color code */
      i++;
      if(i >= len) return;
      if((unsigned char)string[i] < color_base ||
         (unsigned char)string[i] >= color_base + colors) continue;
      glColor3ubv(color_codes[(unsigned char)string[i] - color_base]);
      continue;
    }
      
    /* find out which texture it's in */
    /* TODO(4): find out why the +1 is necessary */
    if((unsigned char)string[i] < ftx->lower ||
       (unsigned char)string[i] > ftx->upper) continue;
    index = (unsigned char)string[i] - ftx->lower + 1;
    tex = index / (w * w);
    if(tex < 0 || tex >= ftx->nTextures) continue;
    /* bind texture */
    if(tex != bound) {
      glBindTexture(GL_TEXTURE_2D, ftx->texID[tex]);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
      bound = tex;
    }
    /* find texture coordinates */
    index = index % (w * w);
    cx = (float)(index % w) / (float)w;
    cy = (float)(index / w) / (float)w;
    /* draw quad */
    /* fprintf(stderr, FTX_ERR "coords: tex %d (%.2f, %.2f), %.2f\n", */
    /*     bound, cx, cy, cw); */

    glBegin(GL_QUADS);
    glTexCoord2f(cx, 1 - cy - cw);
    glVertex2f(i, 0);
    glTexCoord2f(cx + cw, 1 - cy - cw);
    glVertex2f(i + 1, 0);
    glTexCoord2f(cx + cw, 1 - cy);
    glVertex2f(i + 1, 1);
    glTexCoord2f(cx, 1 - cy);
    glVertex2f(i, 1);
    glEnd();
  }
  /* checkGLError("FontTex.c ftxRenderString\n"); */
}
