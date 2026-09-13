#ifdef NDEBUG
#undef NDEBUG
#endif
/* The same production-game fixture is linked to GL and Vulkan. All game/UI
 * time comes from --wrap=SystemGetElapsedTime; SDL timing stays real for native
 * window completion. No screenshots or state are resized/aligned for parity.
 * Fixture-controlled positions below exercise hard-to-reach effects; movement,
 * AI, turns, collisions, camera, menus and drawing remain production code. */
#include "game/gltron.h"
#include "game/init.h"
#include "base/switchCallbacks.h"
#include "filesystem/path.h"
#include "video/display_layout.h"
#include "video/video.h"
#include <SDL3/SDL.h>
#ifdef GLTRON_DIRECT_VULKAN
#include "faithful_platform.h"
#endif
#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern lua_State *L;
extern void reshape(int width, int height);
static unsigned int fixture_time = 1000;
static unsigned int capture_count, fullscreen_enters, fullscreen_leaves;
static unsigned int drawable_events, minimized_events, restored_events;
static const char *capture_directory, *selected_artpack, *active_artpack;
static char capture_path[4096];
static FILE *manifest;
static SDL_Window *fixture_window;
static float initial_camera_defaults[CAM_COUNT][3];
unsigned int __wrap_SystemGetElapsedTime(void) { return fixture_time; }
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
#ifndef GLTRON_DIRECT_VULKAN
  SDL_GLContext context = SDL_GL_GetCurrentContext();
  assert(context != NULL);
#endif
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
#ifndef GLTRON_DIRECT_VULKAN
  assert(SDL_GL_GetCurrentContext() == context);
#endif
  printf("PASS: selected %s assets: %d GPU textures, four font atlases, exact paths/dimensions and classic glyph metrics\n",
         artpack, count);
  active_artpack = artpack;
}

static void hashU32(uint64_t *hash, uint32_t value) {
  int byte;
  for(byte = 0; byte < 4; byte++) {
    *hash ^= value & 255U;
    *hash *= UINT64_C(1099511628211);
    value >>= 8;
  }
}
static void hashFloat(uint64_t *hash, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  hashU32(hash, bits);
}
static uint64_t stateHash(int visual) {
  uint64_t hash = UINT64_C(14695981039346656037);
  List *event;
  int i, j, axis;
  hashU32(&hash, game->players);
  hashU32(&hash, game->running);
  hashU32(&hash, game->winner);
  hashU32(&hash, game->pauseflag);
  hashFloat(&hash, game2->rules.speed);
  hashU32(&hash, game2->rules.grid_size);
  hashU32(&hash, game2->rules.eraseCrashed);
  hashU32(&hash, game2->time.current);
  for(i = 0; i < game->players; i++) {
    Data *d = game->player[i].data;
    AI *ai = game->player[i].ai;
    hashU32(&hash, d->dir); hashU32(&hash, d->last_dir);
    hashU32(&hash, d->score); hashU32(&hash, d->turn_time);
    hashFloat(&hash, d->speed); hashFloat(&hash, d->booster);
    hashU32(&hash, d->boost_enabled); hashFloat(&hash, d->trail_height);
    hashU32(&hash, d->trailOffset);
    for(j = 0; j <= d->trailOffset; j++) {
      for(axis = 0; axis < 2; axis++) {
        hashFloat(&hash, d->trails[j].vStart.v[axis]);
        hashFloat(&hash, d->trails[j].vDirection.v[axis]);
      }
    }
    hashU32(&hash, ai->active); hashU32(&hash, ai->tdiff);
    hashU32(&hash, ai->lasttime);
    if(visual) {
      Camera *camera = game->player[i].camera;
      PlayerVisual *v = &gPlayerVisuals[i];
      hashU32(&hash, camera->type.type);
      for(j = 0; j < 3; j++) {
        hashFloat(&hash, camera->cam[j]); hashFloat(&hash, camera->target[j]);
      }
      for(j = 0; j < 4; j++) {
        hashFloat(&hash, camera->movement[j]);
        hashFloat(&hash, v->pColorDiffuse[j]);
        hashFloat(&hash, v->pColorSpecular[j]); hashFloat(&hash, v->pColorAlpha[j]);
      }
      hashU32(&hash, v->spoke_time); hashU32(&hash, v->spoke_state);
      hashFloat(&hash, v->exp_radius); hashFloat(&hash, v->impact_radius);
      hashU32(&hash, v->display.vp_x); hashU32(&hash, v->display.vp_y);
      hashU32(&hash, v->display.vp_w); hashU32(&hash, v->display.vp_h);
      hashU32(&hash, v->display.onScreen);
    }
  }
  for(event = &game2->events; event->next; event = event->next) {
    GameEvent *e = event->data;
    hashU32(&hash, e->type); hashU32(&hash, e->player);
    hashU32(&hash, e->timestamp); hashFloat(&hash, e->x); hashFloat(&hash, e->y);
  }
  if(visual) {
    hashU32(&hash, fixture_time); hashU32(&hash, gViewportType);
    hashU32(&hash, gSettingsCache.use_stencil);
    hashU32(&hash, gSettingsCache.alpha_trails);
    hashU32(&hash, gSettingsCache.lod);
    hashU32(&hash, gSettingsCache.show_floor_texture);
    hashU32(&hash, gSettingsCache.show_skybox);
    hashU32(&hash, gSettingsCache.light_cycles);
    hashFloat(&hash, gSettingsCache.fov); hashFloat(&hash, gSettingsCache.znear);
    hashFloat(&hash, gSettingsCache.map_ratio_w); hashFloat(&hash, gSettingsCache.map_ratio_h);
  }
  return hash;
}

/* External user input is excluded from deterministic fixtures. Window events
 * still go through the actual platform handler, including deferred reshapes. */
static void pumpWindow(void) {
  SDL_Event event;
  while(SDL_PollEvent(&event)) {
    assert(event.type != SDL_EVENT_QUIT);
    if(event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) fullscreen_enters++;
    if(event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) fullscreen_leaves++;
    if(event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) drawable_events++;
    if(event.type == SDL_EVENT_WINDOW_MINIMIZED) minimized_events++;
    if(event.type == SDL_EVENT_WINDOW_RESTORED) restored_events++;
    assert(!SystemHandleWindowEvent(&event));
  }
  gInput.mouse1 = gInput.mouse2 = gInput.mousex = gInput.mousey = 0;
}

static void settleWindow(int fullscreen) {
  Uint64 deadline = SDL_GetTicksNS() + UINT64_C(5000000000);
  Uint64 stable_since = SDL_GetTicksNS();
  int previous_w = -1, previous_h = -1;
  do {
    int w, h;
    pumpWindow();
    SystemGetDrawableSize(&w, &h);
    if(w != previous_w || h != previous_h) {
      previous_w = w; previous_h = h;
      stable_since = SDL_GetTicksNS();
    }
    if(w > 0 && h > 0 && SystemIsFullscreen() == fullscreen &&
       SDL_GetTicksNS() - stable_since >= UINT64_C(150000000)) {
      /* Refresh the existing production layout even if a compositor supplied
       * its final dimensions through an event that preceded the reshape hook. */
      reshape(w, h);
      return;
    }
    SDL_Delay(5);
  } while(SDL_GetTicksNS() < deadline);
  assert(!"native window did not settle");
}

static void setWindow(int width, int height, int fullscreen) {
  setSettingi("width", width); setSettingi("height", height);
  setSettingi("windowMode", fullscreen ? 0 : 1);
  assert(applyWindowSettings());
  settleWindow(fullscreen);
}

static void capturePixels(void) {
  assert(VideoCaptureScreenshot(capture_path, 1) == 0);
  /* A second save must reject an existing file and retain it. The runner
   * separately checks image bytes and complete PNG decoding. */
  assert(VideoCaptureScreenshot(capture_path, 1) != 0);
  capture_count++;
}

static void captureScene(const char *name, const char *kind) {
  uint64_t gameplay, visual_before, visual_after;
  unsigned int dt = game2->time.dt;
  int logical_w, logical_h, pixel_w, pixel_h;
  game2->time.dt = 0;
  pumpWindow();
  gameplay = stateHash(0);
  visual_before = stateHash(1);
  assert(snprintf(capture_path, sizeof(capture_path), "%s/%s.png", capture_directory, name) < (int)sizeof(capture_path));
  SystemCaptureNextFrame(capturePixels);
  current->display();
  assert(glGetError() == GL_NO_ERROR);
  assert(stateHash(0) == gameplay);
  visual_after = stateHash(1);
  SystemGetWindowSize(&logical_w, &logical_h);
  pngDimensions(capture_path, &pixel_w, &pixel_h);
  assert(pixel_w == gScreen->w && pixel_h == gScreen->h);
  fprintf(manifest, "{\"scene\":\"%s\",\"kind\":\"%s\",\"artpack\":\"%s\",\"width\":%d,\"height\":%d,\"logical_width\":%d,\"logical_height\":%d,\"time_ms\":%u,\"gameplay_hash\":\"%016" PRIx64 "\",\"visual_before\":\"%016" PRIx64 "\",\"visual_after\":\"%016" PRIx64 "\"}\n",
          name, kind, active_artpack, pixel_w, pixel_h, logical_w, logical_h,
          game2->time.current, gameplay, visual_before, visual_after);
  assert(fflush(manifest) == 0);
  printf("FAITHFUL_CAPTURE %s %dx%d state=%016" PRIx64 "\n", name, pixel_w, pixel_h, gameplay);
  game2->time.dt = dt;
}

static void frames(int count) {
  int i;
  assert(current == &gameCallbacks);
  for(i = 0; i < count; i++) {
    pumpWindow();
    fixture_time += 20;
    current->idle();
    current->display();
    assert(glGetError() == GL_NO_ERROR);
  }
}

static void resetRound(int layout, int camera, int humans) {
  int i;
  const char *names[] = {"ai_player1", "ai_player2", "ai_player3", "ai_player4"};
  for(i = 0; i < 4; i++) setSettingi(names[i], i < humans ? AI_HUMAN : AI_COMPUTER);
  setSettingi("display_type", layout);
  setSettingi("camType", camera);
  setSettingi("fast_finish", 0);
  updateSettingsCache();
  memcpy(cam_defaults, initial_camera_defaults, sizeof(initial_camera_defaults));
  for(i = 0; i < game->players; i++) {
    memset(game->player[i].ai, 0, sizeof(*game->player[i].ai));
    memset(game->player[i].camera, 0, sizeof(*game->player[i].camera));
  }
  memset(&game2->time, 0, sizeof(game2->time));
  tsrand(12313);
  resetScores();
  initData();
  resetRecognizer();
  changeDisplay(-1);
  setCallback("game");
  consoleInit();
}

static void playerKey(const char *action, int down) {
  int key;
  assert(scripting_RunFormatChecked("return settings.keys[1].%s", action) == 0);
  assert(scripting_GetStrictIntegerResult(&key) == 0);
  keyGame(down ? SYSTEM_KEYSTATE_DOWN : SYSTEM_KEYSTATE_UP, key, 0, 0);
}

static void menu(const char *name, const char *capture_name) {
  setCallback("gui");
  assert(scripting_RunFormatChecked("Menu.current = '%s'; Menu.active = 1", name) == 0);
  captureScene(capture_name, "menu");
}

static void glyphDisplay(void) {
  int font, row;
  glClearColor(.05f, .05f, .05f, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  rasonly(gScreen);
  glColor4f(1, 1, 1, 1);
  for(font = 0; font < 2; font++) {
    FontTex *atlas = font ? gameFtx : guiFtx;
    for(row = 0; row < 5; row++) {
      char text[21];
      int column;
      for(column = 0; column < 19; column++) text[column] = (char)(32 + row * 19 + column);
      text[19] = 0;
      drawText(atlas, 24, gScreen->h - 40 - (font * 5 + row) * 42, 24, text);
    }
  }
  SystemSwapBuffers();
}

static void clusteredPlayers(void) {
  static const float positions[4][2] = {{360,360},{368,352},{352,344},{360,340}};
  int i;
  resetRound(0, CAM_FOLLOW, 4);
  for(i = 0; i < 4; i++) {
    Data *data = game->player[i].data;
    data->dir = data->last_dir = i;
    data->trails[0].vStart.v[0] = positions[i][0];
    data->trails[0].vStart.v[1] = positions[i][1];
    data->trails[0].vDirection.v[0] = data->trails[0].vDirection.v[1] = 0;
    initCamera(game->player[i].camera, data, CAM_FOLLOW);
  }
  frames(8);
}

static void recognizerScene(void) {
  vec2 position, velocity;
  Camera *camera;
  resetRound(0, CAM_FOLLOW, 1);
  getRecognizerPositionVelocity(&position, &velocity);
  camera = game->player[0].camera;
  /* Put the original recognizer and its projected shadow in a substantial
   * part of the frame. Ordinary driving cameras can leave it off-screen. */
  camera->cam[0] = position.v[0] - 75;
  camera->cam[1] = position.v[1] - 100;
  camera->cam[2] = RECOGNIZER_HEIGHT + 90;
  camera->target[0] = position.v[0];
  camera->target[1] = position.v[1];
  camera->target[2] = RECOGNIZER_HEIGHT * .5f;
  assert(gSettingsCache.show_recognizer && gSettingsCache.use_stencil);
  setCallback("pause");
  captureScene("recognizer", "effects");
}

static void longTrail(int segments) {
  Data *data;
  GameEvent turn;
  int i;
  resetRound(0, CAM_FOLLOW, 1);
  data = game->player[0].data;
  memset(&turn, 0, sizeof(turn)); turn.player = 0;
  for(i = 0; i < segments; i++) {
    data->trails[data->trailOffset].vDirection.v[0] = dirsX[data->dir] * .1f;
    data->trails[data->trailOffset].vDirection.v[1] = dirsY[data->dir] * .1f;
    doTurn(&turn, i % 2 ? TURN_LEFT : TURN_RIGHT);
  }
  assert(data->trailOffset == segments && data->trailCapacity > segments);
  game2->time.dt = 0;
  doCameraMovement();
  setCallback("pause");
}

static int compareDouble(const void *left, const void *right) {
  double a = *(const double *)left, b = *(const double *)right;
  return (a > b) - (a < b);
}

/* Diagnostic wall time only: includes CPU drawing, submission and any present
 * wait. It is not a GPU timestamp, and different swap policies affect it. */
static void warmedPresentationTiming(void) {
  double samples[32], ordered[32];
  Uint64 frequency = SDL_GetPerformanceFrequency();
  uint64_t before = stateHash(0);
  unsigned int previous_dt = game2->time.dt;
  char path[4096];
  FILE *output;
  int i;
  assert(frequency > 0);
  game2->time.dt = 0;
  for(i = 0; i < 8; i++) current->display();
  for(i = 0; i < 32; i++) {
    Uint64 start = SDL_GetPerformanceCounter();
    current->display();
    samples[i] = (double)(SDL_GetPerformanceCounter() - start) * 1000.0 / (double)frequency;
  }
  assert(glGetError() == GL_NO_ERROR && stateHash(0) == before);
  game2->time.dt = previous_dt;
  memcpy(ordered, samples, sizeof(ordered));
  qsort(ordered, 32, sizeof(ordered[0]), compareDouble);
  assert(snprintf(path, sizeof(path), "%s/timing.json", capture_directory) < (int)sizeof(path));
  output = fopen(path, "wx"); assert(output);
  fprintf(output, "{\"metric\":\"display_and_present_wall_ms\",\"scene\":\"restored-window\",\"warmup\":8,\"samples\":32,\"median_ms\":%.6f,\"p95_ms\":%.6f,\"width\":%d,\"height\":%d,\"values_ms\":[",
          (ordered[15] + ordered[16]) / 2, ordered[30], gScreen->w, gScreen->h);
  for(i = 0; i < 32; i++) fprintf(output, "%s%.6f", i ? "," : "", samples[i]);
  fprintf(output, "]}\n"); assert(fclose(output) == 0);
  printf("FAITHFUL_TIMING display_and_present_wall_ms median=%.3f p95=%.3f samples=32\n",
         (ordered[15] + ordered[16]) / 2, ordered[30]);
}

int main(int argc, char **argv) {
#ifdef GLTRON_DIRECT_VULKAN
  const char *config = getenv("VULKANTRON_CONFIG_DIR");
#else
  const char *config = getenv("GLTRON_CONFIG_DIR");
#endif
  const char *options[] = {argv[0], "-i", "-4"};
  char manifest_path[4096];
  SDL_Window **windows;
  int window_count, i, minimize_supported = 0, visibility_fallback = 0;
  GLint stencil_bits, depth_bits;
  SDL_GLContext original_context = NULL;
  assert(argc == 2 && (strcmp(argv[1], "default") == 0 || strcmp(argv[1], "faithful") == 0));
  selected_artpack = argv[1];
#ifdef GLTRON_DIRECT_VULKAN
  capture_directory = getenv("VULKANTRON_SCREENSHOT_DIR");
#else
  capture_directory = getenv("GLTRON_SCREENSHOT_DIR");
#endif
  assert(config && config[0] && capture_directory && capture_directory[0]);
  assert(!getenv("HOME") || strcmp(config, getenv("HOME")) != 0);
  assert(snprintf(manifest_path, sizeof(manifest_path), "%s/scenes.jsonl", capture_directory) < (int)sizeof(manifest_path));
  manifest = fopen(manifest_path, "wx");
  assert(manifest);
  initSubsystems(3, options);
  assert(SystemGetElapsedTime() == 1000); /* Detect a missing linker clock wrapper. */
  windows = SDL_GetWindows(&window_count);
  assert(windows && window_count == 1);
  fixture_window = windows[0]; SDL_free(windows);
#ifdef GLTRON_DIRECT_VULKAN
  assert(SDL_GL_GetCurrentContext() == NULL);
#else
  original_context = SDL_GL_GetCurrentContext(); assert(original_context);
#endif
  (void)original_context;
  glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);
  glGetIntegerv(GL_DEPTH_BITS, &depth_bits);
  assert(stencil_bits >= 8); /* Runner must seed use_stencil before context creation. */
  assert(depth_bits >= 16);
  printf("FAITHFUL_BACKEND %s renderer=%s stencil=%d depth=%d\n",
#ifdef GLTRON_DIRECT_VULKAN
         "vulkan",
#else
         "opengl",
#endif
         glGetString(GL_RENDERER), stencil_bits, depth_bits);
  memcpy(initial_camera_defaults, cam_defaults, sizeof(initial_camera_defaults));
  setSettingi("show_console", 1);
  setSettingi("show_fps", 0);
  setSettingi("show_scores", 1);
  setSettingi("show_ai_status", 1);
  selectAndVerifyArtpack(selected_artpack);
  resetRound(0, CAM_FOLLOW, 1);
  setCallback("gui");
  settleWindow(0);
  menu("RootMenu", "menu-root");
  current->keyboard(SYSTEM_KEYSTATE_DOWN, SYSTEM_KEY_RETURN, 0, 0);
  captureScene("menu-game", "menu");
  menu("VideoMenu", "menu-video");
  menu("DetailsMenu", "menu-details");
  menu("AudioMenu", "menu-audio");
  menu("Player1_KeyMenu", "menu-keys");
  setCallback("configure"); captureScene("configure-key", "menu");
  {
    Callbacks glyphs = guiCallbacks;
    glyphs.display = glyphDisplay;
    SystemRegisterCallbacks(&glyphs);
    captureScene("font-glyphs", "fonts");
    SystemRegisterCallbacks(&guiCallbacks);
  }
  setCallback("credits"); fixture_time += 5000; captureScene("credits", "menu");

  for(i = 0; i < 3; i++) {
    const char *name[] = {"single", "split", "fourway"};
    char paused[64];
    setWindow(i ? 960 : 800, i ? 540 : 600, 0);
    resetRound(i, CAM_FOLLOW, i == 0 ? 1 : i == 1 ? 2 : 4);
    frames(8);
    playerKey("left", 1); frames(4);
    playerKey("right", 1); playerKey("boost", 1); frames(6);
    playerKey("boost", 0); frames(4);
    consoleAddLine("Faithful renderer: identical production game state");
    captureScene(name[i], "world");
    setCallback("pause");
    snprintf(paused, sizeof(paused), "pause-%s", name[i]);
    captureScene(paused, "world");
  }
  setWindow(800, 600, 0);
  for(i = 0; i < 4; i++) {
    const char *name[] = {"camera-circling", "camera-follow", "camera-cockpit", "camera-mouse"};
    resetRound(0, i, 1); frames(12); captureScene(name[i], "world");
  }
  resetRound(0, CAM_FOLLOW, 0); frames(40); captureScene("ai-hud", "world");
  recognizerScene();
  clusteredPlayers();
  captureScene("effects-stencil", "effects");
  setSettingi("use_stencil", 0); updateSettingsCache();
  captureScene("effects-shadows-simple", "effects");
  setSettingi("use_stencil", 1); setSettingi("alpha_trails", 1); updateSettingsCache();
  captureScene("effects-transparent-trails", "effects");
  assert(scripting_RunChecked("video.settings.show_floor_texture = 0") == 0);
  updateSettingsCache(); assert(!gSettingsCache.show_floor_texture);
  captureScene("floor-grid-fog", "effects");
  assert(scripting_RunChecked("video.settings.show_floor_texture = 1") == 0);
  updateSettingsCache(); assert(gSettingsCache.show_floor_texture);
  createEvent(1, EVENT_CRASH); frames(6);
  assert(game->player[1].data->speed < 0 && gPlayerVisuals[1].exp_radius > 0);
  captureScene("crash-early", "effects");
  frames(24); captureScene("crash-late", "effects");
  createEvent(0, EVENT_STOP);
  frames(1); assert(game->winner == 0 && game->pauseflag == PAUSE_GAME_FINISHED);
  setCallback("pause"); captureScene("winner", "world");
  setCallback("game"); createEvent(0, EVENT_STOP);
  /* The draw-result fixture uses the production no-winner event payload.
   * createEvent itself requires a valid player to record the event position. */
  ((GameEvent *)game2->events.data)->player = PLAYERS;
  frames(1); assert(game->winner == -2);
  setCallback("pause"); captureScene("draw-result", "world");
  for(i = 0; i < 4; i++) {
    const int segments[] = {155, 243, 999, 2005};
    char name[64]; longTrail(segments[i]);
    snprintf(name, sizeof(name), "trail-%d", segments[i]);
    captureScene(name, "world");
  }
  {
    uint64_t before = stateHash(0);
    selectAndVerifyArtpack(strcmp(selected_artpack, "default") == 0 ? "faithful" : "default");
    captureScene("artpack-alternate", "world");
    selectAndVerifyArtpack(selected_artpack);
    captureScene("artpack-restored", "world");
    assert(stateHash(0) == before);
  }
  setWindow(961, 541, 0); captureScene("resized-odd", "world");
  assert(SDL_MinimizeWindow(fixture_window));
  {
    Uint64 end = SDL_GetTicksNS() + UINT64_C(3000000000);
    do { pumpWindow(); SDL_Delay(5); }
    while(!(SDL_GetWindowFlags(fixture_window) & SDL_WINDOW_MINIMIZED) && SDL_GetTicksNS() < end);
    minimize_supported = (SDL_GetWindowFlags(fixture_window) & SDL_WINDOW_MINIMIZED) != 0;
  }
  if(minimize_supported) {
    assert(SDL_RestoreWindow(fixture_window));
  } else {
    /* Some compositors (including tested Hyprland/XWayland) ignore native
     * minimize requests in both renderers. Record that unsupported operation,
     * and independently verify actual visibility loss/restoration. Do not
     * present the hide/show result as proof that minimization worked. */
    fprintf(stderr, "FAITHFUL_MINIMIZE_UNSUPPORTED: compositor did not minimize the OpenGL/Vulkan window\n");
    assert(SDL_HideWindow(fixture_window)); pumpWindow();
    assert(SDL_GetWindowFlags(fixture_window) & SDL_WINDOW_HIDDEN);
    assert(SDL_ShowWindow(fixture_window));
    visibility_fallback = 1;
  }
  settleWindow(0);
  assert(!(SDL_GetWindowFlags(fixture_window) & SDL_WINDOW_MINIMIZED));
  assert(!(SDL_GetWindowFlags(fixture_window) & SDL_WINDOW_HIDDEN));
  captureScene("restored-visibility", "world");
  setWindow(961, 541, 1); captureScene("fullscreen", "world");
  setWindow(800, 600, 0); captureScene("restored-window", "world");
  warmedPresentationTiming();
  assert(fullscreen_enters && fullscreen_leaves && drawable_events);
  if(minimize_supported) assert(minimized_events && restored_events);
#ifndef GLTRON_DIRECT_VULKAN
  assert(SDL_GL_GetCurrentContext() == original_context);
#endif
  for(i = 0; i < 2; i++) {
    char *path = getPossiblePath(PATH_PREFERENCES, RC_NAME);
    struct stat status;
    setSettingi("faithful_roundtrip", i + 71);
    saveSettings();
    assert(path && stat(path, &status) == 0 && status.st_size > 0);
    setSettingi("faithful_roundtrip", -1);
    assert(scripting_RunFileChecked(path) == 0);
    assert(getSettingi("faithful_roundtrip") == i + 71);
    free(path);
  }
  assert(capture_count == 39);
  assert(fclose(manifest) == 0);
  shutdownDisplay(gScreen);
#ifdef GLTRON_DIRECT_VULKAN
  assert(VT_FaithfulShutdown());
  assert(VT_FaithfulValidationErrors() == 0);
#endif
  printf("FAITHFUL_SMOKE_OK captures=%u settings_roundtrips=2 fullscreen=%u/%u minimize=%u/%u drawable_events=%u minimize_supported=%d visibility_fallback=%d\n",
         capture_count, fullscreen_enters, fullscreen_leaves, minimized_events, restored_events,
         drawable_events, minimize_supported, visibility_fallback);
  SystemExit();
  return 0;
}
