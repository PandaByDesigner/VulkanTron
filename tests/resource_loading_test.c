#include "video/video.h"
#include "filesystem/path.h"
#include "Nebu_scripting.h"
#include <assert.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

static const char *repo_dir, *fixture_dir;
static const char *selected_pack = "default";
static const char *font_definition;
static int max_texture_size = 4096;
static int generated, deleted, uploads, glyphs, colors;
static GLuint next_id = 1;
static uint64_t draw_hash;
FontTex *gameFtx = NULL, *guiFtx = NULL;

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t bytes) {
  const unsigned char *p = data;
  size_t i;
  for(i = 0; i < bytes; ++i) {
    hash ^= p[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static char *path_join(const char *parent, const char *name) {
  size_t size = strlen(parent) + strlen(name) + 2;
  char *path = malloc(size);
  assert(path != NULL);
  snprintf(path, size, "%s/%s", parent, name);
  return path;
}

char *getPath(int location, const char *filename) {
  char *path;
  assert(location == PATH_DATA);
  if(strcmp(filename, "fonts.txt") == 0 && font_definition != NULL)
    return strdup(font_definition);
  path = path_join(fixture_dir, filename);
  if(access(path, R_OK) == 0) return path;
  free(path);
  path = path_join(repo_dir, "data");
  {
    char *result = path_join(path, filename);
    free(path);
    if(access(result, R_OK) == 0) return result;
    free(result);
  }
  return NULL;
}

char *getArtPath(const char *artpack, const char *filename) {
  char *directory, *path;
  if(strcmp(artpack, "fixture") == 0) {
    path = path_join(fixture_dir, filename);
    if(access(path, R_OK) == 0) return path;
    free(path);
  }
  directory = path_join(repo_dir, "art/default");
  path = path_join(directory, filename);
  free(directory);
  if(access(path, R_OK) == 0) return path;
  free(path);
  return NULL;
}

int scripting_GetGlobal(const char *global, const char *name, ...) {
  (void)global; (void)name;
  return 0;
}
int scripting_GetStringResult(char **result) {
  *result = strdup(selected_pack);
  return 0;
}

void APIENTRY glGenTextures(GLsizei count, GLuint *ids) {
  int i;
  generated += count;
  for(i = 0; i < count; ++i) ids[i] = next_id++;
}
void APIENTRY glDeleteTextures(GLsizei count, const GLuint *ids) {
  int i;
  for(i = 0; i < count; ++i) assert(ids[i] > 0);
  deleted += count;
}
void APIENTRY glBindTexture(GLenum target, GLuint id) {
  assert(target == GL_TEXTURE_2D && id > 0 && id < next_id);
}
void APIENTRY glGetIntegerv(GLenum name, GLint *value) {
  assert(name == GL_MAX_TEXTURE_SIZE);
  *value = max_texture_size;
}
void APIENTRY glPixelStorei(GLenum name, GLint value) {
  assert(name == GL_UNPACK_ALIGNMENT && value == 1);
}
void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internal,
                           GLsizei width, GLsizei height, GLint border,
                           GLenum format, GLenum type, const GLvoid *pixels) {
  assert(target == GL_TEXTURE_2D && level >= 0 && border == 0);
  assert(internal == GL_RGB || internal == GL_RGBA);
  assert(format == GL_RGB || format == GL_RGBA);
  assert(type == GL_UNSIGNED_BYTE && pixels != NULL);
  assert(width > 0 && height > 0 && width <= max_texture_size &&
         height <= max_texture_size);
  ++uploads;
}
void APIENTRY glTexParameteri(GLenum target, GLenum key, GLint value) {
  (void)key; (void)value;
  assert(target == GL_TEXTURE_2D);
}
void APIENTRY glTexEnvi(GLenum target, GLenum key, GLint value) {
  assert(target == GL_TEXTURE_ENV && key == GL_TEXTURE_ENV_MODE &&
         value == GL_MODULATE);
}
void APIENTRY glBegin(GLenum mode) { assert(mode == GL_QUADS); ++glyphs; }
void APIENTRY glEnd(void) {}
void APIENTRY glTexCoord2f(GLfloat x, GLfloat y) {
  draw_hash = hash_bytes(draw_hash, &x, sizeof(x));
  draw_hash = hash_bytes(draw_hash, &y, sizeof(y));
}
void APIENTRY glVertex2f(GLfloat x, GLfloat y) {
  draw_hash = hash_bytes(draw_hash, &x, sizeof(x));
  draw_hash = hash_bytes(draw_hash, &y, sizeof(y));
}
void APIENTRY glColor3ubv(const GLubyte *color) {
  draw_hash = hash_bytes(draw_hash, color, 3);
  ++colors;
}

static void write_text(const char *name, const char *text) {
  char *path = path_join(fixture_dir, name);
  FILE *file = fopen(path, "wb");
  assert(file != NULL);
  assert(fwrite(text, 1, strlen(text), file) == strlen(text));
  assert(fclose(file) == 0);
  free(path);
}

static void write_png(const char *name, int width, int height, int interlace) {
  char *path = path_join(fixture_dir, name);
  FILE *file = fopen(path, "wb");
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_infop info = png_create_info_struct(png);
  png_bytep *rows = malloc((size_t)height * sizeof(*rows));
  unsigned char *data = malloc((size_t)width * height * 4);
  int x, y;
  assert(file != NULL && png != NULL && info != NULL && rows != NULL && data != NULL);
  assert(setjmp(png_jmpbuf(png)) == 0);
  for(y = 0; y < height; ++y) {
    rows[y] = data + (size_t)y * width * 4;
    for(x = 0; x < width; ++x) {
      rows[y][4*x] = (unsigned char)(x + y*3);
      rows[y][4*x+1] = (unsigned char)(x*5 + y);
      rows[y][4*x+2] = (unsigned char)(x ^ y);
      rows[y][4*x+3] = (unsigned char)(255 - x - y);
    }
  }
  png_init_io(png, file);
  png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
               interlace, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  png_write_image(png, rows);
  png_write_end(png, NULL);
  png_destroy_write_struct(&png, &info);
  assert(fclose(file) == 0);
  free(rows); free(data); free(path);
}

static uint64_t texture_chain(const char *name) {
  char *directory = path_join(repo_dir, "art/default");
  char *path = path_join(directory, name);
  png_texture *tex = load_png_texture(path);
  uint64_t hash = UINT64_C(14695981039346656037);
  free(directory); free(path);
  assert(tex != NULL);
  for(;;) {
    png_texture *next;
    hash = hash_bytes(hash, tex->data,
                      (size_t)tex->width * tex->height * tex->channels);
    if(tex->width == 1 && tex->height == 1) break;
    next = mipmap_png_texture(tex, 1, 0, 0);
    unload_png_texture(tex);
    tex = next;
    assert(tex != NULL);
  }
  unload_png_texture(tex);
  return hash;
}

static void check_originals(int record) {
  static const char *const names[] = {
    "babbage.0.png", "babbage.1.png", "gltron.png", "gltron_bitmap.png",
    "gltron_floor.png", "gltron_impact.png", "gltron_logo.png", "gltron_trail.png",
    "gltron_traildecal.png", "gltron_wall_1.png", "gltron_wall_2.png",
    "gltron_wall_3.png", "gltron_wall_4.png", "skybox0.png", "skybox1.png",
    "skybox2.png", "skybox3.png", "skybox4.png", "skybox5.png", "test.bitmap.png",
    "xenotron.0.png", "xenotron.1.png"
  };
  /* Canonical hashes include level zero and every classic box-filter mip. */
  static const uint64_t expected[] = {
    UINT64_C(0x3c24abbb82853d3d),
    UINT64_C(0x7ea0be8c0f4c1551),
    UINT64_C(0x04393d1775d2bcea),
    UINT64_C(0x6d0043ac51a33bde),
    UINT64_C(0xbf9b91d0aaf31303),
    UINT64_C(0x9208a6833e1b17f3),
    UINT64_C(0x0fe6bc326c71e814),
    UINT64_C(0x759f731acc8a299e),
    UINT64_C(0x72e70ee76a55d555),
    UINT64_C(0x46097d2d66405651),
    UINT64_C(0xefc8b9fffd638e76),
    UINT64_C(0x7108bd88846ba80e),
    UINT64_C(0x02b928a06d903847),
    UINT64_C(0x4e2fd09b7fde72c7),
    UINT64_C(0x4e2fd09b7fde72c7),
    UINT64_C(0x4e2fd09b7fde72c7),
    UINT64_C(0x4e2fd09b7fde72c7),
    UINT64_C(0x4e2fd09b7fde72c7),
    UINT64_C(0x4e2fd09b7fde72c7),
    UINT64_C(0xf3162345c59cab6a),
    UINT64_C(0x7f6dc2322082ba78),
    UINT64_C(0xcb1c4502f2fb1b7f)
  };
  size_t i;
  for(i = 0; i < sizeof(names)/sizeof(names[0]); ++i) {
    uint64_t hash = texture_chain(names[i]);
    if(record) printf("%s %016llx\n", names[i], (unsigned long long)hash);
    else assert(i < sizeof(expected)/sizeof(expected[0]) && hash == expected[i]);
  }
}

static uint64_t font_trace(FontTex *font) {
  char text[95];
  int i;
  for(i = 0; i < 95; ++i) text[i] = (char)(32 + i);
  draw_hash = UINT64_C(14695981039346656037);
  glyphs = 0;
  ftxRenderString(font, text, sizeof(text));
  assert(glyphs == 95);
  return draw_hash;
}

static void check_font_geometry(int record) {
  FontTex *font = ftxLoadFont("babbage.ftx");
  uint64_t hash;
  assert(font != NULL && font->texwidth == 256 && font->width == 32);
  assert(font->lower == 32 && font->upper == 126 && font->nTextures == 2);
  hash = font_trace(font);
  if(record) printf("classic_font_draw %016llx\n", (unsigned long long)hash);
  else assert(hash == UINT64_C(0xa0c2faee52894d55));
  ftxUnloadFont(font);
}

static void write_header_png(const char *name, unsigned width, unsigned height,
                              unsigned depth, unsigned color) {
  static const unsigned char signature[8] = {137,80,78,71,13,10,26,10};
  unsigned char ihdr[17] = {'I','H','D','R'};
  unsigned char length[4] = {0,0,0,13}, crc_bytes[4];
  char *path = path_join(fixture_dir, name);
  FILE *file = fopen(path, "wb");
  uint32_t crc;
  int i;
  assert(file != NULL);
  for(i = 0; i < 4; ++i) {
    ihdr[4+i] = (unsigned char)(width >> (24-8*i));
    ihdr[8+i] = (unsigned char)(height >> (24-8*i));
  }
  ihdr[12] = depth; ihdr[13] = color;
  crc = (uint32_t)crc32(0, ihdr, sizeof(ihdr));
  for(i = 0; i < 4; ++i) crc_bytes[i] = (unsigned char)(crc >> (24-8*i));
  assert(fwrite(signature, 1, sizeof(signature), file) == sizeof(signature));
  assert(fwrite(length, 1, sizeof(length), file) == sizeof(length));
  assert(fwrite(ihdr, 1, sizeof(ihdr), file) == sizeof(ihdr));
  assert(fwrite(crc_bytes, 1, sizeof(crc_bytes), file) == sizeof(crc_bytes));
  assert(fwrite("\0\0\0\0IDAT", 1, 8, file) == 8);
  assert(fclose(file) == 0);
  free(path);
}

static void check_bad_pngs(void) {
  char *path;
  png_texture *tex;
  int i, before;
  DIR *directory;
  struct dirent *entry;
  write_text("empty.png", "");
  write_text("invalid.png", "not a PNG file");
  write_header_png("wide.png", 16385, 1, 8, PNG_COLOR_TYPE_RGBA);
  write_header_png("large.png", 16384, 8192, 8, PNG_COLOR_TYPE_RGBA);
  write_header_png("gray.png", 2, 2, 8, PNG_COLOR_TYPE_GRAY);
  write_header_png("deep.png", 2, 2, 16, PNG_COLOR_TYPE_RGB);
  write_png("small.png", 2, 2, PNG_INTERLACE_NONE);
  write_png("interlaced.png", 8, 8, PNG_INTERLACE_ADAM7);
  path = path_join(fixture_dir, "small.png");
  tex = load_png_texture(path);
  assert(tex != NULL && tex->width == 2 && tex->height == 2 && tex->channels == 4);
  assert(tex->data[0] == 3 && tex->data[1] == 1 && tex->data[2] == 1);
  assert(tex->data[8] == 0 && tex->data[9] == 0 && tex->data[10] == 0);
  unload_png_texture(tex);
  {
    FILE *file = fopen(path, "rb");
    unsigned char bytes[64];
    char *bad = path_join(fixture_dir, "truncated.png");
    FILE *out = fopen(bad, "wb");
    assert(file != NULL && out != NULL && fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    assert(fwrite(bytes, 1, sizeof(bytes), out) == sizeof(bytes));
    fclose(file); fclose(out); free(bad);
  }
  free(path);
  path = path_join(fixture_dir, "interlaced.png");
  tex = load_png_texture(path);
  assert(tex != NULL && tex->data[0] == 21 && tex->data[1] == 7);
  unload_png_texture(tex); free(path);
  /* Repeated libpng longjmp paths must release descriptors and allocations. */
  directory = opendir("/proc/self/fd");
  assert(directory != NULL); before = 0;
  while((entry = readdir(directory)) != NULL) ++before;
  closedir(directory);
  for(i = 0; i < 32; ++i) {
    const char *names[] = {"empty.png", "invalid.png", "truncated.png",
                           "wide.png", "large.png", "gray.png", "deep.png"};
    size_t j;
    for(j = 0; j < sizeof(names)/sizeof(names[0]); ++j) {
      path = path_join(fixture_dir, names[j]);
      assert(load_png_texture(path) == NULL);
      free(path);
    }
  }
  directory = opendir("/proc/self/fd");
  assert(directory != NULL); i = 0;
  while((entry = readdir(directory)) != NULL) ++i;
  closedir(directory);
  assert(i == before);
  unload_png_texture(NULL);
  assert(mipmap_png_texture(NULL, 1, 0, 0) == NULL);
  {
    unsigned char pixel[4] = {12, 34, 56, 78};
    png_texture source = {1, 1, 4, pixel};
    png_texture *mip = mipmap_png_texture(&source, 1, 0, 0);
    assert(mip != NULL && memcmp(pixel, mip->data, sizeof(pixel)) == 0);
    unload_png_texture(mip);
    assert(mipmap_png_texture(&source, 2, 0, 0) == NULL);
    source.width = 0;
    assert(mipmap_png_texture(&source, 1, 0, 0) == NULL);
    source.width = 2147483647;
    assert(mipmap_png_texture(&source, 1, 0, 0) == NULL);
  }
}

static void check_bad_fonts(void) {
  static const char *const invalid[] = {
    "", "# only comments\n\n", "2 256 32\n", "2 256 32\n32 126\n",
    "2 256 32\n32 126\nname\n", "2 256 32\n32 126\nname\nbabbage.0.png\n",
    "-1 256 32\n", "2 256 0\n", "2 256 512\n", "2 255 32\n",
    "2147483648 256 32\n", "999999999999999999999999999 256 32\n",
    "2 256 32\n-1 126\n", "2 256 32\n32 256\n", "2 256 32\n126 32\n",
    "1 32 32\n32 126\n", "2 256 32 unexpected\n"
  };
  size_t i;
  int before = generated - deleted;
  for(i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
    write_text("bad.ftx", invalid[i]);
    assert(ftxLoadFont("bad.ftx") == NULL);
    assert(generated - deleted == before);
  }
  write_text("bad.ftx", "2 256 32\n32 126\nname\nbabbage.0.png\nmissing.png\n");
  assert(ftxLoadFont("bad.ftx") == NULL);
  assert(generated - deleted == before);
  assert(ftxLoadFont("missing.ftx") == NULL);
  ftxUnloadFont(NULL);
  {
    FontTex *font = ftxLoadFont("xenotron.ftx");
    const char text[] = {3, 0, 3, 127, (char)255, 1, 'A', 3, '0', 'B', 3};
    assert(font != NULL);
    glyphs = colors = 0;
    ftxRenderString(font, text, sizeof(text));
    assert(glyphs == 2 && colors == 1);
    ftxUnloadFont(font);
  }
}

static void check_reload_and_hd(void) {
  char *definition = path_join(fixture_dir, "fonts.txt");
  FontTex *oldgame, *oldgui, *hd;
  int i;
  write_text("fonts.txt", "game: xenotron.ftx\nmenu: babbage.ftx\n");
  initFonts();
  assert(gameFtx != NULL && guiFtx != NULL);
  oldgame = gameFtx; oldgui = guiFtx;
  font_definition = definition;
  write_text("fonts.txt", "game: missing.ftx\nmenu: babbage.ftx\n");
  initFonts();
  assert(gameFtx == oldgame && guiFtx == oldgui && generated - deleted == 4);
  write_text("fonts.txt", "game: xenotron.ftx\nmenu: missing.ftx\n");
  initFonts();
  assert(gameFtx == oldgame && guiFtx == oldgui && generated - deleted == 4);
  {
    char overlong[400];
    memset(overlong, 'a', sizeof(overlong));
    memcpy(overlong, "game: ", 6); overlong[sizeof(overlong)-1] = 0;
    write_text("fonts.txt", overlong);
    initFonts();
    assert(gameFtx == oldgame && guiFtx == oldgui && generated - deleted == 4);
  }
  write_text("fonts.txt", "game: xenotron.ftx\nmenu: babbage.ftx\n");
  for(i = 0; i < 32; ++i) {
    initFonts();
    assert(generated - deleted == 4);
  }
  deleteFonts(); deleteFonts();
  assert(gameFtx == NULL && guiFtx == NULL && generated == deleted);
  free(definition); font_definition = NULL;

  /* Pixel resolution is independent of the unchanged logical 8x8 glyph grid. */
  write_png("hd.png", 1024, 1024, PNG_INTERLACE_NONE);
  write_text("hd.ftx", "2 256 32\n32 126\nfixture\nhd.png\nhd.png\n");
  selected_pack = "fixture";
  hd = ftxLoadFont("hd.ftx");
  assert(hd != NULL && hd->texwidth == 256 && hd->width == 32);
  assert(font_trace(hd) == UINT64_C(0xa0c2faee52894d55));
  ftxUnloadFont(hd);
  /* Bad optional texture falls back to default and still loads successfully. */
  write_text("babbage.0.png", "broken override");
  hd = ftxLoadFont("babbage.ftx");
  assert(hd != NULL); ftxUnloadFont(hd);
  selected_pack = "default";
  max_texture_size = 64;
  uploads = 0;
  assert(loadTextureChecked("babbage.0.png", GL_RGBA));
  assert(uploads == 7); /*64,32,16,8,4,2,1*/
  max_texture_size = 0;
  assert(!loadTextureChecked("babbage.0.png", GL_RGBA));
  max_texture_size = 4096;
  assert(generated == deleted);
}

int main(int argc, char **argv) {
  int record;
  assert(argc == 3 || argc == 4);
  repo_dir = argv[1]; fixture_dir = argv[2];
  record = argc == 4 && strcmp(argv[3], "--record") == 0;
  check_originals(record);
  check_font_geometry(record);
  if(record) return 0;
  check_bad_pngs();
  check_bad_fonts();
  check_reload_and_hd();
  puts("PASS: original PNG/mipmap bytes and font geometry; malformed assets; font reload lifetime; HD atlas metric parity");
  return 0;
}
