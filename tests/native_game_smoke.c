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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern lua_State *L;
static char capture_path[4096];
static int captures;

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
  char *loaded_artpack = NULL;
  int i;
  SDL_Event quit_event;
  SDL_GLContext initial_context;
  if(config == NULL || output == NULL || config[0] == '\0' || output[0] == '\0' ||
     strcmp(config, getenv("HOME") != NULL ? getenv("HOME") : ".") == 0) {
    fprintf(stderr, "Set isolated GLTRON_CONFIG_DIR and GLTRON_SCREENSHOT_DIR.\n");
    return 2;
  }
  initSubsystems(3, options);
  lua_getglobal(L, "settings");
  lua_pushstring(L, "current_artpack");
  lua_pushstring(L, artpack);
  lua_settable(L, -3);
  lua_pop(L, 1);
  reloadArt();
  scripting_GetGlobal("settings", "current_artpack", NULL);
  assert(scripting_GetStringResult(&loaded_artpack) == 0);
  assert(strcmp(loaded_artpack, artpack) == 0);
  free(loaded_artpack);
  initial_context = SDL_GL_GetCurrentContext();
  assert(initial_context != NULL);
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
  assert(captures == 14);
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
