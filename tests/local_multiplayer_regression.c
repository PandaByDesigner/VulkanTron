#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/gltron.h"

#define TEST_GRID_SIZE 200
#define TEST_SPEED 12.0f
#define TEST_SEED 12313U
#define AUTO_DISPLAY 3

typedef struct ExpectedViewport {
  int x;
  int y;
  int width;
  int height;
} ExpectedViewport;

static const ExpectedViewport classic_viewports[3][4] = {
  { { 0, 0, 800, 600 }, { 0, 0, 0, 0 },
    { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
  { { 25, 12, 750, 288 }, { 25, 312, 750, 288 },
    { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
  { { 25, 25, 350, 262 }, { 400, 25, 350, 262 },
    { 25, 312, 350, 263 }, { 400, 312, 350, 263 } }
};

static int configured_ai[PLAYERS] = {
  AI_NONE, AI_NONE, AI_NONE, AI_NONE
};
static int configured_display_type;
static unsigned int fake_elapsed_time;
static int exit_loop_code = -1;

static void fail(const char *message) {
  fprintf(stderr, "FAIL: %s\n", message);
  exit(EXIT_FAILURE);
}

static float settingValue(const char *name) {
  if(strcmp(name, "speed") == 0) return TEST_SPEED;
  if(strcmp(name, "grid_size") == 0) return TEST_GRID_SIZE;
  if(strcmp(name, "erase_crashed") == 0) return 1.0f;
  if(strcmp(name, "booster_on") == 0) return 0.0f;
  if(strcmp(name, "booster_min") == 0) return 1.0f;
  if(strcmp(name, "booster_max") == 0) return 6.5f;
  if(strcmp(name, "booster_use") == 0) return 1.0f;
  if(strcmp(name, "booster_decrease") == 0) return 0.8f;
  if(strcmp(name, "booster_regenerate") == 0) return 0.4f;
  if(strcmp(name, "wall_accel_on") == 0) return 0.0f;
  if(strcmp(name, "wall_accel_limit") == 0) return 20.0f;
  if(strcmp(name, "wall_accel_use") == 0) return 1.0f;
  if(strcmp(name, "wall_accel_decrease") == 0) return 0.8f;
  if(strcmp(name, "display_type") == 0) return configured_display_type;
  if(strcmp(name, "ai_player1") == 0) return configured_ai[0];
  if(strcmp(name, "ai_player2") == 0) return configured_ai[1];
  if(strcmp(name, "ai_player3") == 0) return configured_ai[2];
  if(strcmp(name, "ai_player4") == 0) return configured_ai[3];

  fprintf(stderr, "missing test setting: %s\n", name);
  exit(EXIT_FAILURE);
}

float getSettingf(const char *name) {
  return settingValue(name);
}

int getSettingi(const char *name) {
  return (int) settingValue(name);
}

void setSettingi(const char *name, int value) {
  if(strcmp(name, "display_type") != 0)
    fail("production display selection changed an unrelated setting");
  configured_display_type = value;
}

unsigned int SystemGetElapsedTime(void) {
  return fake_elapsed_time;
}

void SystemExitLoop(int code) {
  exit_loop_code = code;
}

void Audio_CrashPlayer(int player) {
  (void) player;
}

void Audio_StopEngine(int player) {
  (void) player;
}

void displayMessage(outloc_e where, const char *format, ...) {
  (void) where;
  (void) format;
}

void initCamera(Camera *camera, Data *data, int type) {
  (void) data;
  memset(camera, 0, sizeof(*camera));
  camera->type.type = type;
}

void doCameraMovement(void) { }
void doRecognizerMovement(void) { }

int scripting_GetGlobal(const char *global, const char *format, ...) {
  (void) global;
  (void) format;
  return 0;
}

void scripting_GetFloatArrayResult(float *values, int count) {
  int i;

  for(i = 0; i < count; i++)
    values[i] = 0.0f;
}

static int nearlyEqual(float left, float right) {
  return fabsf(left - right) < 0.0001f;
}

static void setAiRoles(const int roles[PLAYERS]) {
  int player;

  for(player = 0; player < PLAYERS; player++) {
    configured_ai[player] = roles[player];
    game->player[player].ai->active = roles[player];
  }
}

static void expectViewport(int player, int layout, int pane) {
  const Visual *actual = &gPlayerVisuals[player].display;
  const ExpectedViewport *expected = &classic_viewports[layout][pane];

  if(actual->w != 800 || actual->h != 600 ||
     actual->vp_x != expected->x || actual->vp_y != expected->y ||
     actual->vp_w != expected->width || actual->vp_h != expected->height)
    fail("production player viewport differs from classic 800x600 topology");
}

static void expectSelection(int layout, const int *players, int count) {
  int visible[PLAYERS] = { 0, 0, 0, 0 };
  int pane;
  int player;

  if(gViewportType != layout)
    fail("production viewport type does not match selected topology");

  for(pane = 0; pane < count; pane++) {
    player = players[pane];
    if(player < 0 || player >= PLAYERS || visible[player])
      fail("production viewport selection contains an invalid player");
    if(viewport_content[pane] != player)
      fail("production viewport-to-player ordering changed");
    visible[player] = 1;
    expectViewport(player, layout, pane);
  }

  for(player = 0; player < PLAYERS; player++) {
    if(gPlayerVisuals[player].display.onScreen != visible[player])
      fail("production on-screen player flags disagree with pane selection");
  }
}

static void verifyExplicitTopologies(void) {
  static const int arbitrary_roles[PLAYERS] = {
    AI_NONE, AI_HUMAN, AI_COMPUTER, AI_NONE
  };
  static const int single_players[] = { 0 };
  static const int split_players[] = { 0, 1 };
  static const int four_players[] = { 0, 1, 2, 3 };

  setAiRoles(arbitrary_roles);

  changeDisplay(VP_SINGLE);
  expectSelection(VP_SINGLE, single_players, 1);
  if(configured_display_type != VP_SINGLE)
    fail("explicit single topology was not retained in settings");

  changeDisplay(VP_SPLIT);
  expectSelection(VP_SPLIT, split_players, 2);
  if(configured_display_type != VP_SPLIT)
    fail("explicit stacked topology was not retained in settings");

  changeDisplay(VP_FOURWAY);
  expectSelection(VP_FOURWAY, four_players, 4);
  if(configured_display_type != VP_FOURWAY)
    fail("explicit four-way topology was not retained in settings");

  configured_display_type = VP_SPLIT;
  changeDisplay(-1);
  expectSelection(VP_SPLIT, split_players, 2);

  printf("PASS: production explicit single, stacked, and four-way selection\n");
}

static void verifyAutomaticTopologies(void) {
  static const int no_humans[PLAYERS] = {
    AI_NONE, AI_NONE, AI_NONE, AI_NONE
  };
  static const int one_noncontiguous_human[PLAYERS] = {
    AI_NONE, AI_NONE, AI_HUMAN, AI_NONE
  };
  static const int two_noncontiguous_humans[PLAYERS] = {
    AI_COMPUTER, AI_HUMAN, AI_COMPUTER, AI_HUMAN
  };
  static const int three_humans_with_gap[PLAYERS] = {
    AI_HUMAN, AI_NONE, AI_HUMAN, AI_HUMAN
  };
  static const int four_humans[PLAYERS] = {
    AI_HUMAN, AI_HUMAN, AI_HUMAN, AI_HUMAN
  };
  static const int player_zero[] = { 0 };
  static const int player_two[] = { 2 };
  static const int players_one_three[] = { 1, 3 };
  static const int all_players[] = { 0, 1, 2, 3 };

  setAiRoles(no_humans);
  changeDisplay(AUTO_DISPLAY);
  expectSelection(VP_SINGLE, player_zero, 1);

  setAiRoles(one_noncontiguous_human);
  changeDisplay(AUTO_DISPLAY);
  expectSelection(VP_SINGLE, player_two, 1);

  setAiRoles(two_noncontiguous_humans);
  changeDisplay(AUTO_DISPLAY);
  expectSelection(VP_SPLIT, players_one_three, 2);

  changeDisplay(VP_FOURWAY);
  configured_display_type = AUTO_DISPLAY;
  changeDisplay(-1);
  expectSelection(VP_SPLIT, players_one_three, 2);
  if(configured_display_type != AUTO_DISPLAY)
    fail("stored automatic display preference was not retained");

  setAiRoles(three_humans_with_gap);
  changeDisplay(AUTO_DISPLAY);
  expectSelection(VP_FOURWAY, all_players, 4);

  setAiRoles(four_humans);
  changeDisplay(AUTO_DISPLAY);
  expectSelection(VP_FOURWAY, all_players, 4);

  if(configured_display_type != AUTO_DISPLAY)
    fail("automatic display preference was replaced by its resolved topology");

  printf("PASS: production automatic player-to-pane selection, including gaps\n");
}

static void setPlayerTrack(int player, float x, float y, int direction) {
  Data *data = game->player[player].data;

  data->dir = direction;
  data->last_dir = direction;
  data->turn_time = 0;
  data->speed = TEST_SPEED;
  data->booster = 6.5f;
  data->boost_enabled = 0;
  data->trail_height = TRAIL_HEIGHT;
  data->trailOffset = 0;
  data->trails[0].vStart.v[0] = x;
  data->trails[0].vStart.v[1] = y;
  data->trails[0].vDirection.v[0] = 0.0f;
  data->trails[0].vDirection.v[1] = 0.0f;
}

static int trailIsContinuous(const Data *data) {
  int segment;

  for(segment = 1; segment <= data->trailOffset; segment++) {
    const segment2 *previous = &data->trails[segment - 1];
    const segment2 *current = &data->trails[segment];
    float previous_x = previous->vStart.v[0] + previous->vDirection.v[0];
    float previous_y = previous->vStart.v[1] + previous->vDirection.v[1];

    if(!nearlyEqual(previous_x, current->vStart.v[0]) ||
       !nearlyEqual(previous_y, current->vStart.v[1]))
      return 0;
  }

  return 1;
}

static void getPlayerPosition(int player, float *x, float *y) {
  getPositionFromData(x, y, game->player[player].data);
}

static int eventQueueIsEmpty(void) {
  return game2->events.data == NULL && game2->events.next == NULL;
}

static int runPhysicsAt(unsigned int current, unsigned int dt) {
  game2->time.lastFrame = game2->time.current;
  game2->time.current = current;
  game2->time.dt = dt;
  return Game_PhysicsStep((int) dt);
}

static void verifyTwoHumanGameplay(void) {
  static const int two_humans[PLAYERS] = {
    AI_NONE, AI_HUMAN, AI_NONE, AI_HUMAN
  };
  static const int visible_humans[] = { 1, 3 };
  Data *first;
  Data *second;
  GameEvent *crash;
  float first_x;
  float first_y;
  float second_x;
  float second_y;
  float first_after_y;
  float second_after_x;
  int player;

  setAiRoles(two_humans);
  fake_elapsed_time = 0;
  exit_loop_code = -1;
  gSettingsCache.ai_level = 2;
  gSettingsCache.camType = CAM_TYPE_CIRCLING;
  gSettingsCache.fast_finish = 0;
  resetScores();
  tsrand(TEST_SEED);
  initData();
  changeDisplay(AUTO_DISPLAY);
  expectSelection(VP_SPLIT, visible_humans, 2);

  first = game->player[1].data;
  second = game->player[3].data;
  setPlayerTrack(1, 40.0f, 60.0f, 3);
  setPlayerTrack(3, 160.0f, 140.0f, 0);
  game->running = 2;

  game2->time.current = 100;
  createEvent(1, EVENT_TURN_LEFT);
  createEvent(3, EVENT_TURN_RIGHT);
  if(runPhysicsAt(100, 20) != 0 || !eventQueueIsEmpty())
    fail("independent first turns did not execute in one physics quantum");

  getPlayerPosition(1, &first_x, &first_y);
  getPlayerPosition(3, &second_x, &second_y);
  if(first->trailOffset != 1 || first->dir != 2 || first->last_dir != 3 ||
     first->turn_time != 100 || !trailIsContinuous(first) ||
     !nearlyEqual(first_x, 40.0f) || first_y <= 60.0f ||
     second->trailOffset != 1 || second->dir != 1 || second->last_dir != 0 ||
     second->turn_time != 100 || !trailIsContinuous(second) ||
     second_x >= 160.0f || !nearlyEqual(second_y, 140.0f))
    fail("two-human first turns no longer produce independent trails");

  first_after_y = first_y;
  second_after_x = second_x;
  game2->time.current = 120;
  createEvent(1, EVENT_TURN_RIGHT);
  createEvent(3, EVENT_TURN_LEFT);
  if(runPhysicsAt(120, 20) != 0 || !eventQueueIsEmpty())
    fail("independent return turns did not execute in one physics quantum");

  getPlayerPosition(1, &first_x, &first_y);
  getPlayerPosition(3, &second_x, &second_y);
  if(first->trailOffset != 2 || first->dir != 3 || first->last_dir != 2 ||
     first->turn_time != 120 || !trailIsContinuous(first) ||
     first_x <= 40.0f || !nearlyEqual(first_y, first_after_y) ||
     second->trailOffset != 2 || second->dir != 0 || second->last_dir != 1 ||
     second->turn_time != 120 || !trailIsContinuous(second) ||
     !nearlyEqual(second_x, second_after_x) || second_y >= 140.0f)
    fail("two-human return turns or trail ownership changed");

  /* Start a controlled crash phase after proving both independent trails. */
  first->dir = 1;
  first->last_dir = 1;
  first->trailOffset = 0;
  first->trails[0].vStart.v[0] = 0.5f;
  first->trails[0].vStart.v[1] = 80.0f;
  first->trails[0].vDirection.v[0] = 0.0f;
  first->trails[0].vDirection.v[1] = 0.0f;
  first->speed = TEST_SPEED;
  first->trail_height = TRAIL_HEIGHT;
  second->trail_height = 0.0f;

  if(runPhysicsAt(200, 20) != 0 || game2->events.data == NULL ||
     game2->events.next == NULL || game2->events.next->next != NULL)
    fail("controlled human wall contact did not queue exactly one crash");

  crash = (GameEvent *) game2->events.data;
  getPlayerPosition(1, &first_x, &first_y);
  if(crash->type != EVENT_CRASH || crash->player != 1 ||
     crash->timestamp != 200 || !nearlyEqual(crash->x, 0.0f) ||
     !nearlyEqual(crash->y, 80.0f) || !nearlyEqual(first_x, 0.0f) ||
     !nearlyEqual(first_y, 80.0f))
    fail("controlled human crash metadata or clipping changed");

  if(runPhysicsAt(220, 20) != 0 || !eventQueueIsEmpty())
    fail("controlled human crash did not process on the following quantum");

  if(!nearlyEqual(first->speed, SPEED_CRASHED) || first->score != 0 ||
     second->score != 1 || game->running != 2 ||
     !nearlyEqual(gPlayerVisuals[1].exp_radius, 0.2f) ||
     exit_loop_code != -1)
    fail("two-human crash ownership, scoring, or animation timing changed");
  for(player = 0; player < PLAYERS; player++) {
    if(player != 3 && game->player[player].data->score != 0)
      fail("crash score leaked to an inactive or crashed player");
  }
  expectSelection(VP_SPLIT, visible_humans, 2);

  printf("PASS: production two-human turns, trails, crash, and scoring state\n");
}

static void freeGameState(void) {
  int player;

  clearEventQueue();
  for(player = 0; player < game->players; player++) {
    free(game->player[player].data->trails);
    free(game->player[player].data);
    free(game->player[player].ai);
    free(game->player[player].camera);
  }
  free(game->player);
  free(gPlayerVisuals);
}

int main(void) {
  static Visual screen;

  memset(&screen, 0, sizeof(screen));
  screen.w = 800;
  screen.h = 600;
  screen.vp_w = 800;
  screen.vp_h = 600;
  gScreen = &screen;

  gPlayerVisuals = (PlayerVisual *)
    calloc(MAX_PLAYERS, sizeof(*gPlayerVisuals));
  if(gPlayerVisuals == NULL)
    return EXIT_FAILURE;

  initGameStructures();
  verifyExplicitTopologies();
  verifyAutomaticTopologies();
  verifyTwoHumanGameplay();
  freeGameState();

  return EXIT_SUCCESS;
}
