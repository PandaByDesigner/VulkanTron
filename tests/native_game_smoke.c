#ifdef NDEBUG
#undef NDEBUG
#endif
/* Real SDL/OpenGL integration. Requires explicit isolated config/output dirs. */
#include "game/gltron.h"
#include "game/init.h"
#include "base/switchCallbacks.h"
#include "filesystem/path.h"
#include "video/video.h"
#ifdef GLTRON_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern lua_State *L;
static char capture_path[4096];
static int captures;

static void pngDimensions(const char *path, int *width, int *height) {
  png_image image;
  memset(&image, 0, sizeof(image));
  image.version = PNG_IMAGE_VERSION;
  assert(png_image_begin_read_from_file(&image, path));
  assert(image.width > 0 && image.width <= INT_MAX &&
         image.height > 0 && image.height <= INT_MAX);
  *width = (int)image.width;
  *height = (int)image.height;
  png_image_free(&image);
}

static void verifyLoadedTexture(const char *artpack, const char *filename,
                                GLuint texture_id) {
  char expected_path[4096];
  char *path = getArtPath(artpack, filename);
  GLint bound, width, height, maximum;
  int source_width, source_height;
  struct stat status;
  assert(snprintf(expected_path, sizeof(expected_path), "%s/%s/%s",
                  getDirectory(PATH_ART), artpack, filename) < (int)sizeof(expected_path));
  /* A missing or malformed faithful asset must fail this gate even though
   * production can safely fall back to the corresponding original. */
  assert(path != NULL && strcmp(path, expected_path) == 0);
  assert(stat(path, &status) == 0 && S_ISREG(status.st_mode));
  pngDimensions(path, &source_width, &source_height);
  if(strcmp(artpack, "faithful") == 0) {
    char *original = getArtPath("default", filename);
    int original_width, original_height;
    assert(original != NULL);
    pngDimensions(original, &original_width, &original_height);
    assert(original_width <= INT_MAX / 4 && original_height <= INT_MAX / 4);
    assert(source_width == original_width * 4 && source_height == original_height * 4);
    free(original);
  }
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
  assert(maximum > 0);
  /* The renderer drops oversized mip levels on a smaller GPU. */
  while(source_width > maximum || source_height > maximum) {
    source_width = source_width > 1 ? source_width / 2 : 1;
    source_height = source_height > 1 ? source_height / 2 : 1;
  }
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
  assert(glIsTexture(texture_id));
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
  assert(width == source_width && height == source_height);
  glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
  assert(glGetError() == GL_NO_ERROR);
  free(path);
}

static void verifyLoadedFont(const char *artpack, const char *name, FontTex *font) {
  int i;
  assert(font != NULL && font->texID != NULL && strcmp(font->fontname, name) == 0);
  /* Upscaling atlas pixels must not change the classic 8x8 glyph layout,
   * character range, or the metrics used by the menu/HUD renderer. */
  assert(font->nTextures == 2 && font->texwidth == 256 && font->width == 32 &&
         font->lower == 32 && font->upper == 126);
  for(i = 0; i < font->nTextures; i++) {
    char filename[128];
    assert(snprintf(filename, sizeof(filename), "%s.%d.png", name, i) < (int)sizeof(filename));
    verifyLoadedTexture(artpack, filename, font->texID[i]);
  }
}

static void selectAndVerifyArtpack(const char *artpack) {
  char *loaded_artpack = NULL;
  char expected_marker[4096];
  char *marker;
  int i, j, count = 0;
  SDL_GLContext context = SDL_GL_GetCurrentContext();
  assert(context != NULL);
  lua_getglobal(L, "settings");
  lua_pushstring(L, "current_artpack");
  lua_pushstring(L, artpack);
  lua_settable(L, -3);
  lua_pop(L, 1);
  /* Exercise the same enumeration selection and registered reload callback
   * used by the Video menu, rather than merely checking a setting string. */
  assert(scripting_RunChecked("setupArtpacks(); c_reloadArtpack()") == 0);
  scripting_GetGlobal("settings", "current_artpack", NULL);
  assert(scripting_GetStringResult(&loaded_artpack) == 0);
  assert(strcmp(loaded_artpack, artpack) == 0);
  free(loaded_artpack);
  marker = getArtPath(artpack, "artpack.lua");
  assert(snprintf(expected_marker, sizeof(expected_marker), "%s/%s/artpack.lua",
                  getDirectory(PATH_ART), artpack) < (int)sizeof(expected_marker));
  assert(marker != NULL && strcmp(marker, expected_marker) == 0);
  free(marker);
  for(i = 0; i < n_textures; i++) {
    for(j = 0; j < textures[i].count; j++) {
      char filename[128];
      int length = textures[i].count == 1 ?
        snprintf(filename, sizeof(filename), "%s%s", textures[i].name, TEX_SUFFIX) :
        snprintf(filename, sizeof(filename), "%s%d%s", textures[i].name, j, TEX_SUFFIX);
      assert(length >= 0 && length < (int)sizeof(filename));
      verifyLoadedTexture(artpack, filename, gScreen->textures[textures[i].id + j]);
      count++;
    }
  }
  verifyLoadedFont(artpack, "babbage", guiFtx);
  verifyLoadedFont(artpack, "xenotron", gameFtx);
  assert(SDL_GL_GetCurrentContext() == context);
  printf("PASS: selected %s assets: %d GPU textures, four font atlases, exact paths/dimensions and classic glyph metrics\n",
         artpack, count);
}

static void capture(void) {
  if(VideoCaptureScreenshot(capture_path, 1) != 0) {
    fprintf(stderr, "FAIL: native capture %s\n", capture_path);
    exit(2);
  }
  captures++;
}

static void captureNamed(const char *directory, const char *name) {
  int logical_width, logical_height;
  snprintf(capture_path, sizeof(capture_path), "%s/%s.png", directory, name);
  SystemCaptureNextFrame(capture);
  current->display();
  assert(glGetError() == GL_NO_ERROR);
  SystemGetWindowSize(&logical_width, &logical_height);
  printf("CAPTURE %s pixels=%dx%d window=%dx%d\n", name, gScreen->w,
         gScreen->h, logical_width, logical_height);
}

static void pump(void) {
  SDL_Event event;
  while(SDL_PollEvent(&event)) {
    if(SystemHandleWindowEvent(&event)) {
      fprintf(stderr, "FAIL: verification window closed early\n");
      exit(3);
    }
    SystemHandleInputEvent(&event);
  }
}

static void playFrames(int frames) {
  int i;
  for(i = 0; i < frames; i++) {
    pump();
    if(current->idle != NULL) current->idle();
    current->display();
    assert(glGetError() == GL_NO_ERROR);
    SDL_Delay(16);
  }
}

int main(int argc, char **argv) {
  const char *config = getenv("GLTRON_CONFIG_DIR");
  const char *output = getenv("GLTRON_SCREENSHOT_DIR");
  const char *options[] = { argv[0], "-i", "-4" };
  const char *artpack = argc > 1 ? argv[1] : "default";
  int i;
  SDL_Event quit_event;
  SDL_GLContext initial_context;
  if(config == NULL || output == NULL || config[0] == '\0' || output[0] == '\0' ||
     strcmp(config, getenv("HOME") != NULL ? getenv("HOME") : ".") == 0) {
    fprintf(stderr, "Set isolated GLTRON_CONFIG_DIR and GLTRON_SCREENSHOT_DIR.\n");
    return 2;
  }
  initSubsystems(3, options);
  initial_context = SDL_GL_GetCurrentContext();
  assert(initial_context != NULL);
  selectAndVerifyArtpack("default");
  if(strcmp(artpack, "default") != 0)
    selectAndVerifyArtpack(artpack);
  printf("NATIVE SDL %d driver %s OpenGL %s renderer %s\n", SDL_MAJOR_VERSION,
         SDL_GetCurrentVideoDriver(),
         (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));
  setCallback("gui");
  playFrames(3);
  captureNamed(output, "menu");
  for(i = 0; i < 3; i++) {
    const char *names[] = { "single", "split", "fourway" };
    setSettingi("ai_player1", AI_HUMAN);
    setSettingi("ai_player2", i >= 1 ? AI_HUMAN : AI_COMPUTER);
    setSettingi("ai_player3", i >= 2 ? AI_HUMAN : AI_COMPUTER);
    setSettingi("ai_player4", i >= 2 ? AI_HUMAN : AI_COMPUTER);
    setSettingi("display_type", i);
    setSettingi("width", i == 0 ? 800 : 1281);
    setSettingi("height", i == 0 ? 600 : 721);
    assert(applyWindowSettings());
    initData();
    changeDisplay(-1);
    setCallback("game");
    playFrames(12);
    assert(SDL_GL_GetCurrentContext() == initial_context);
    captureNamed(output, names[i]);
    setCallback("pause");
    captureNamed(output, i == 0 ? "pause-single" : i == 1 ? "pause-split" : "pause-fourway");
  }
  setSettingi("display_type", 0);
  setSettingi("ai_player2", AI_COMPUTER);
  setSettingi("ai_player3", AI_COMPUTER);
  setSettingi("ai_player4", AI_COMPUTER);
  for(i = 0; i < 4; i++) {
    const char *names[] = { "camera-circling", "camera-follow", "camera-cockpit", "camera-mouse" };
    setSettingi("camType", i);
    updateSettingsCache();
    initData();
    changeDisplay(-1);
    setCallback("game");
    playFrames(12);
    captureNamed(output, names[i]);
  }
  /* Exercise the production GL index path beyond the old fixed mesh/trail
   * allocations. Keep the accumulated geometry for fullscreen captures too. */
  {
    Data *data = game->player[0].data;
    GameEvent turn;
    memset(&turn, 0, sizeof(turn));
    turn.player = 0;
    for(i = 0; i < 1100; i++) {
      data->trails[data->trailOffset].vDirection.v[0] = dirsX[data->dir] * 0.1f;
      data->trails[data->trailOffset].vDirection.v[1] = dirsY[data->dir] * 0.1f;
      doTurn(&turn, i % 2 ? 3 : 1);
    }
    assert(data->trailOffset >= 1100 && data->trailCapacity > data->trailOffset);
    setCallback("pause");
    playFrames(3);
    captureNamed(output, "long-trail");
  }
  setSettingi("windowMode", 0);
  assert(applyWindowSettings());
  playFrames(3);
  assert(SystemIsFullscreen());
  captureNamed(output, "fullscreen");
  setSettingi("windowMode", 1);
  assert(applyWindowSettings());
  playFrames(3);
  assert(!SystemIsFullscreen());
  assert(SDL_GL_GetCurrentContext() == initial_context);
  captureNamed(output, "restored");
  if(strcmp(artpack, "default") != 0) {
    Data original_player;
    memcpy(&original_player, game->player[0].data, sizeof(original_player));
    selectAndVerifyArtpack("default");
    captureNamed(output, "artpack-default-restored");
    selectAndVerifyArtpack(artpack);
    captureNamed(output, "artpack-selected-restored");
    assert(SDL_GL_GetCurrentContext() == initial_context);
    assert(memcmp(&original_player, game->player[0].data, sizeof(original_player)) == 0);
  }
  /* Exercise the real serializers twice; both writes stay in the test dir. */
  for(i = 0; i < 2; i++) {
    char *path = getPossiblePath(PATH_PREFERENCES, RC_NAME);
    struct stat status;
    setSettingi("smoke_roundtrip", i + 19);
    saveSettings();
    assert(path != NULL && stat(path, &status) == 0 && status.st_size > 0);
    setSettingi("smoke_roundtrip", -1);
    assert(scripting_RunFileChecked(path) == 0);
    assert(getSettingi("smoke_roundtrip") == i + 19);
    free(path);
  }
  assert(captures == (strcmp(artpack, "default") == 0 ? 14 : 16));
  printf("PASS: native menu/play/pause/multiplayer/cameras/fullscreen, %d complete-frame captures, two saved settings roundtrips\n", captures);
  shutdownDisplay(gScreen);
  memset(&quit_event, 0, sizeof(quit_event));
#ifdef GLTRON_USE_SDL3
  quit_event.type = SDL_EVENT_QUIT;
  assert(SDL_PushEvent(&quit_event));
#else
  quit_event.type = SDL_QUIT;
  assert(SDL_PushEvent(&quit_event) == 1);
#endif
  SystemMainLoop();
  return 0;
}
