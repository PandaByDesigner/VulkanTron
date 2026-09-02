#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/gltron.h"
#include "game/timesystem.h"

#define TEST_SEED 12313U
#define TEST_GRID_SIZE 200
#define TEST_SPEED 12.0f
#define TEST_MAX_FRAMES 12000U
#define FNV_OFFSET UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)

typedef struct ExpectedRound {
  const char *name;
  unsigned int frames;
  unsigned int time;
  int winner;
  uint64_t hash;
} ExpectedRound;

typedef struct RoundResult {
  unsigned int frames;
  unsigned int time;
  int winner;
  uint64_t hash;
} RoundResult;

static const ExpectedRound expected_fixed_20 = {
  "fixed-20", 324U, 6480U, 1, UINT64_C(0x76d51ba3de0d7b09)
};

static const ExpectedRound expected_fixed_10 = {
  "fixed-10", 759U, 7590U, 3, UINT64_C(0x611551b67aed43dd)
};

static const ExpectedRound expected_variable = {
  "variable-7-13-41-3-22", 631U, 10843U, 3,
  UINT64_C(0xc2b9c4b61a32f180)
};

static unsigned int fake_elapsed_time;
static int exit_loop_code = -1;
static uint64_t state_hash = FNV_OFFSET;
static int configured_ai[PLAYERS] = {
  AI_COMPUTER, AI_COMPUTER, AI_COMPUTER, AI_COMPUTER
};
static int configured_booster_on;

static float settingValue(const char *name) {
  if(strcmp(name, "speed") == 0) return TEST_SPEED;
  if(strcmp(name, "grid_size") == 0) return TEST_GRID_SIZE;
  if(strcmp(name, "erase_crashed") == 0) return 1.0f;
  if(strcmp(name, "booster_on") == 0) return configured_booster_on;
  if(strcmp(name, "booster_min") == 0) return 1.0f;
  if(strcmp(name, "booster_max") == 0) return 6.5f;
  if(strcmp(name, "booster_use") == 0) return 1.0f;
  if(strcmp(name, "booster_decrease") == 0) return 0.8f;
  if(strcmp(name, "booster_regenerate") == 0) return 0.4f;
  if(strcmp(name, "wall_accel_on") == 0) return 0.0f;
  if(strcmp(name, "wall_accel_limit") == 0) return 20.0f;
  if(strcmp(name, "wall_accel_use") == 0) return 1.0f;
  if(strcmp(name, "wall_accel_decrease") == 0) return 0.8f;
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

static void hashByte(uint64_t *hash, unsigned int value) {
  *hash ^= value & 0xffU;
  *hash *= FNV_PRIME;
}

static void hashU32(uint64_t *hash, uint32_t value) {
  int i;
  for(i = 0; i < 4; i++) {
    hashByte(hash, value);
    value >>= 8;
  }
}

static void hashFloat(uint64_t *hash, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  hashU32(hash, bits);
}

static void hashSimulation(uint64_t *hash) {
  List *node;
  int i;

  hashFloat(hash, game2->rules.speed);
  hashU32(hash, (uint32_t) game2->rules.eraseCrashed);
  hashU32(hash, (uint32_t) game2->rules.grid_size);
  hashU32(hash, (uint32_t) game->running);
  hashU32(hash, (uint32_t) game->winner);
  hashU32(hash, (uint32_t) game->pauseflag);

  for(i = 0; i < game->players; i++) {
    Data *data = game->player[i].data;
    AI *ai = game->player[i].ai;
    PlayerVisual *visual = &gPlayerVisuals[i];
    int segment;

    hashU32(hash, (uint32_t) data->dir);
    hashU32(hash, (uint32_t) data->last_dir);
    hashU32(hash, (uint32_t) data->score);
    hashFloat(hash, data->speed);
    hashFloat(hash, data->booster);
    hashU32(hash, (uint32_t) data->boost_enabled);
    hashFloat(hash, data->trail_height);
    hashU32(hash, data->turn_time);
    hashU32(hash, (uint32_t) data->trailOffset);

    for(segment = 0; segment <= data->trailOffset; segment++) {
      hashFloat(hash, data->trails[segment].vStart.v[0]);
      hashFloat(hash, data->trails[segment].vStart.v[1]);
      hashFloat(hash, data->trails[segment].vDirection.v[0]);
      hashFloat(hash, data->trails[segment].vDirection.v[1]);
    }

    hashU32(hash, (uint32_t) ai->active);
    hashU32(hash, (uint32_t) ai->tdiff);
    hashU32(hash, ai->lasttime);
    hashU32(hash, visual->spoke_time);
    hashU32(hash, (uint32_t) visual->spoke_state);
    hashFloat(hash, visual->impact_radius);
    hashFloat(hash, visual->exp_radius);
  }

  for(node = &game2->events; node->next != NULL; node = node->next) {
    GameEvent *event = (GameEvent *) node->data;
    hashU32(hash, (uint32_t) event->type);
    hashU32(hash, (uint32_t) event->player);
    hashFloat(hash, event->x);
    hashFloat(hash, event->y);
    hashU32(hash, event->timestamp);
  }
}

static void hashFrame(unsigned int frame) {
  hashU32(&state_hash, frame);
  hashU32(&state_hash, game2->time.current);
  hashU32(&state_hash, game2->time.dt);
  hashSimulation(&state_hash);
}

static uint64_t simulationHash(void) {
  uint64_t hash = FNV_OFFSET;
  hashSimulation(&hash);
  return hash;
}

static int nearlyEqual(float left, float right) {
  return fabsf(left - right) < 0.0001f;
}

static void poisonRoundState(void) {
  int i;

  for(i = 0; i < game->players; i++) {
    game->player[i].ai->active = -12345;
    game->player[i].ai->lasttime = UINT32_MAX;
    game->player[i].data->turn_time = UINT32_MAX;
    gPlayerVisuals[i].spoke_time = UINT32_MAX;
    gPlayerVisuals[i].spoke_state = -12345;
    gPlayerVisuals[i].impact_radius = 12345.0f;
    gPlayerVisuals[i].exp_radius = 12345.0f;
  }
}

static void assertInitialState(void) {
  static const int expected_directions[PLAYERS] = { 3, 3, 2, 1 };
  static const float expected_positions[PLAYERS][2] = {
    { 150.0f, 100.0f },
    { 100.0f, 150.0f },
    { 100.0f, 50.0f },
    { 50.0f, 100.0f }
  };
  int i;

  for(i = 0; i < PLAYERS; i++) {
    float x;
    float y;
    float expected_explosion =
      configured_ai[i] == AI_NONE ? EXP_RADIUS_MAX : 0.0f;
    Data *data = game->player[i].data;
    AI *ai = game->player[i].ai;
    PlayerVisual *visual = &gPlayerVisuals[i];

    getPositionFromIndex(&x, &y, i);
    if(ai->active != configured_ai[i] ||
       data->dir != expected_directions[i] ||
       data->last_dir != expected_directions[i] ||
       data->turn_time != 0 || ai->lasttime != 0 ||
       visual->spoke_time != 0 || visual->spoke_state != 0 ||
       !nearlyEqual(visual->impact_radius, 0.0f) ||
       !nearlyEqual(visual->exp_radius, expected_explosion) ||
       !nearlyEqual(x, expected_positions[i][0]) ||
       !nearlyEqual(y, expected_positions[i][1])) {
      fprintf(stderr, "unexpected deterministic reset state for player %d\n", i);
      exit(EXIT_FAILURE);
    }
  }
}

static void setupRound(const int ai[PLAYERS], int fast_finish) {
  int i;

  if(game2->events.data != NULL || game2->events.next != NULL) {
    fprintf(stderr, "event queue was not empty before round reset\n");
    exit(EXIT_FAILURE);
  }

  for(i = 0; i < PLAYERS; i++)
    configured_ai[i] = ai[i];

  fake_elapsed_time = 0;
  exit_loop_code = -1;
  state_hash = FNV_OFFSET;
  configured_booster_on = 0;
  gSettingsCache.ai_level = 2;
  gSettingsCache.camType = CAM_TYPE_CIRCLING;
  gSettingsCache.fast_finish = fast_finish;

  resetScores();
  poisonRoundState();
  tsrand(TEST_SEED);
  initData();
  assertInitialState();
  hashFrame(0);
}

static RoundResult runRound(const char *name, const int *frame_times,
                            unsigned int frame_time_count) {
  static const int all_computer[PLAYERS] = {
    AI_COMPUTER, AI_COMPUTER, AI_COMPUTER, AI_COMPUTER
  };
  RoundResult result;
  unsigned int frame = 0;

  setupRound(all_computer, 0);

  while(exit_loop_code == -1 && frame < TEST_MAX_FRAMES) {
    int elapsed = frame_times[frame % frame_time_count];
    fake_elapsed_time += (unsigned int) elapsed;
    Time_Idle();
    Game_Idle();
    frame++;
    hashFrame(frame);
  }

  if(exit_loop_code != RETURN_GAME_END) {
    fprintf(stderr, "%s did not terminate within %u frames\n",
            name, TEST_MAX_FRAMES);
    exit(EXIT_FAILURE);
  }
  if(game2->events.data != NULL || game2->events.next != NULL) {
    fprintf(stderr, "%s left a dangling event queue after STOP\n", name);
    exit(EXIT_FAILURE);
  }

  result.frames = frame;
  result.time = game2->time.current;
  result.winner = game->winner;
  result.hash = state_hash;
  return result;
}

static void verifyRound(const ExpectedRound *expected, RoundResult actual) {
  if(actual.frames != expected->frames ||
     actual.time != expected->time ||
     actual.winner != expected->winner ||
     actual.hash != expected->hash) {
    fprintf(stderr,
            "%s behavior changed: frames=%u time=%u winner=%d state=%016llx\n",
            expected->name, actual.frames, actual.time, actual.winner,
            (unsigned long long) actual.hash);
    exit(EXIT_FAILURE);
  }

  printf("PASS: %s frames=%u time=%u winner=%d state=%016llx\n",
         expected->name, actual.frames, actual.time, actual.winner,
         (unsigned long long) actual.hash);
}

static uint64_t runSchedulerBatch(const int ai[PLAYERS], int fast_finish,
                                  unsigned int current, unsigned int dt) {
  setupRound(ai, fast_finish);
  game2->time.lastFrame = 0;
  game2->time.current = current;
  game2->time.dt = dt;
  Game_Idle();

  if(exit_loop_code != -1) {
    fprintf(stderr, "scheduler equivalence batch ended the round\n");
    exit(EXIT_FAILURE);
  }
  if(game2->events.data != NULL || game2->events.next != NULL) {
    fprintf(stderr, "scheduler equivalence batch left queued events\n");
    exit(EXIT_FAILURE);
  }
  return simulationHash();
}

static void verifyFastFinishScheduling(void) {
  static const int all_computer[PLAYERS] = {
    AI_COMPUTER, AI_COMPUTER, AI_COMPUTER, AI_COMPUTER
  };
  static const int one_human[PLAYERS] = {
    AI_HUMAN, AI_COMPUTER, AI_COMPUTER, AI_COMPUTER
  };
  uint64_t fast_all_ai = runSchedulerBatch(all_computer, 1, 20, 20);
  uint64_t regular_80 = runSchedulerBatch(all_computer, 0, 20, 80);
  uint64_t fast_with_human = runSchedulerBatch(one_human, 1, 20, 20);
  uint64_t regular_20 = runSchedulerBatch(one_human, 0, 20, 20);

  if(fast_all_ai != regular_80) {
    fprintf(stderr, "fast-finish factor no longer matches four physics steps\n");
    exit(EXIT_FAILURE);
  }
  if(fast_with_human != regular_20) {
    fprintf(stderr, "live human no longer suppresses fast finish\n");
    exit(EXIT_FAILURE);
  }

  printf("PASS: production fast-finish factor and live-human gate\n");
}

static void verifyInactivePlayerReset(void) {
  static const int one_inactive[PLAYERS] = {
    AI_NONE, AI_COMPUTER, AI_COMPUTER, AI_COMPUTER
  };

  setupRound(one_inactive, 0);
  printf("PASS: production active and inactive player-visual reset branches\n");
}

static void verifyQueuedEventReset(void) {
  static const int all_computer[PLAYERS] = {
    AI_COMPUTER, AI_COMPUTER, AI_COMPUTER, AI_COMPUTER
  };

  setupRound(all_computer, 0);
  createEvent(0, EVENT_TURN_LEFT);
  if(game2->events.data == NULL || game2->events.next == NULL) {
    fprintf(stderr, "queued-event reset test could not create its event\n");
    exit(EXIT_FAILURE);
  }

  poisonRoundState();
  tsrand(TEST_SEED);
  initData();
  assertInitialState();
  if(game2->events.data != NULL || game2->events.next != NULL) {
    fprintf(stderr, "round reset left a queued event or sentinel node\n");
    exit(EXIT_FAILURE);
  }

  printf("PASS: production round reset clears pending event storage\n");
}

static int runPhysicsAt(unsigned int current, unsigned int dt) {
  game2->time.lastFrame = game2->time.current;
  game2->time.current = current;
  game2->time.dt = dt;
  return Game_PhysicsStep((int) dt);
}

static int eventQueueIsEmpty(void) {
  return game2->events.data == NULL && game2->events.next == NULL;
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

static void verifyScriptedHumanTurns(void) {
  static const int two_humans[PLAYERS] = {
    AI_HUMAN, AI_HUMAN, AI_NONE, AI_NONE
  };
  Data *data;
  GameEvent *first;
  GameEvent *second;
  float initial_x;
  float initial_y;
  float before_x;
  float before_y;
  float final_x;
  float final_y;

  setupRound(two_humans, 0);
  data = game->player[0].data;
  /* Isolate turn/event semantics from the legacy same-origin wall test. */
  data->trail_height = 0.0f;
  getPositionFromData(&initial_x, &initial_y, data);

  game2->time.current = 100;
  createEvent(0, EVENT_TURN_LEFT);
  createEvent(0, EVENT_TURN_RIGHT);

  if(game2->events.data == NULL || game2->events.next == NULL ||
     game2->events.next->data == NULL ||
     game2->events.next->next == NULL ||
     game2->events.next->next->next != NULL) {
    fprintf(stderr, "scripted turns did not create two queued events\n");
    exit(EXIT_FAILURE);
  }

  first = (GameEvent *) game2->events.data;
  second = (GameEvent *) game2->events.next->data;
  if(first->type != EVENT_TURN_LEFT || second->type != EVENT_TURN_RIGHT ||
     first->player != 0 || second->player != 0 ||
     first->timestamp != 100 || second->timestamp != 100 ||
     !nearlyEqual(first->x, initial_x) ||
     !nearlyEqual(first->y, initial_y) ||
     !nearlyEqual(second->x, initial_x) ||
     !nearlyEqual(second->y, initial_y)) {
    fprintf(stderr, "scripted human turn queue order or metadata changed\n");
    exit(EXIT_FAILURE);
  }

  if(runPhysicsAt(100, 20) != 0 || !eventQueueIsEmpty()) {
    fprintf(stderr, "scripted human turns did not drain normally\n");
    exit(EXIT_FAILURE);
  }
  getPositionFromData(&final_x, &final_y, data);
  if(data->trailOffset != 2 || data->dir != 3 || data->last_dir != 2 ||
     data->turn_time != 100 || !trailIsContinuous(data) ||
     !nearlyEqual(data->trails[1].vDirection.v[0], 0.0f) ||
     !nearlyEqual(data->trails[1].vDirection.v[1], 0.0f) ||
     final_x <= initial_x || !nearlyEqual(final_y, initial_y)) {
    fprintf(stderr, "same-tick human turn ordering or trail continuity changed\n");
    exit(EXIT_FAILURE);
  }

  before_x = final_x;
  before_y = final_y;
  game2->time.current = 120;
  createEvent(0, EVENT_TURN_LEFT);
  if(runPhysicsAt(120, 20) != 0 || !eventQueueIsEmpty()) {
    fprintf(stderr, "follow-up human turn did not drain normally\n");
    exit(EXIT_FAILURE);
  }
  getPositionFromData(&final_x, &final_y, data);
  if(data->trailOffset != 3 || data->dir != 2 || data->last_dir != 3 ||
     data->turn_time != 120 || !trailIsContinuous(data) ||
     !nearlyEqual(final_x, before_x) || final_y <= before_y ||
     exit_loop_code != -1) {
    fprintf(stderr, "follow-up human turn geometry changed\n");
    exit(EXIT_FAILURE);
  }

  printf("PASS: production scripted human turn order and trail continuity\n");
}

static void verifyWallCollisionTiming(void) {
  static const int two_humans[PLAYERS] = {
    AI_HUMAN, AI_HUMAN, AI_NONE, AI_NONE
  };
  Data *crashing;
  Data *survivor;
  GameEvent *event;
  float x;
  float y;

  setupRound(two_humans, 0);
  crashing = game->player[0].data;
  survivor = game->player[1].data;

  crashing->dir = 1;
  crashing->last_dir = 1;
  crashing->turn_time = 0;
  crashing->trailOffset = 0;
  crashing->trails[0].vStart.v[0] = 0.5f;
  crashing->trails[0].vStart.v[1] = 100.0f;
  crashing->trails[0].vDirection.v[0] = 0.0f;
  crashing->trails[0].vDirection.v[1] = 0.0f;
  crashing->speed = TEST_SPEED;
  crashing->trail_height = TRAIL_HEIGHT;

  /* Keep a live scoring opponent while removing its trail as an obstacle. */
  survivor->trail_height = 0.0f;

  if(runPhysicsAt(200, 20) != 0 || game2->events.data == NULL ||
     game2->events.next == NULL || game2->events.next->next != NULL) {
    fprintf(stderr, "wall contact did not queue exactly one crash event\n");
    exit(EXIT_FAILURE);
  }

  event = (GameEvent *) game2->events.data;
  getPositionFromData(&x, &y, crashing);
  if(event->type != EVENT_CRASH || event->player != 0 ||
     event->timestamp != 200 || !nearlyEqual(event->x, 0.0f) ||
     !nearlyEqual(event->y, 100.0f) || !nearlyEqual(x, 0.0f) ||
     !nearlyEqual(y, 100.0f) || !nearlyEqual(crashing->speed, TEST_SPEED) ||
     crashing->score != 0 || survivor->score != 0 || game->running != 2) {
    fprintf(stderr, "wall crash was not clipped and deferred as expected\n");
    exit(EXIT_FAILURE);
  }

  if(runPhysicsAt(220, 20) != 0 || !eventQueueIsEmpty()) {
    fprintf(stderr, "queued wall crash did not process on the next step\n");
    exit(EXIT_FAILURE);
  }
  if(!nearlyEqual(crashing->speed, SPEED_CRASHED) ||
     crashing->score != 0 || survivor->score != 1 || game->running != 2 ||
     !nearlyEqual(gPlayerVisuals[0].exp_radius, 0.2f) ||
     !nearlyEqual(crashing->trail_height, 3.43f) ||
     exit_loop_code != -1) {
    fprintf(stderr, "wall crash score or animation timing changed\n");
    exit(EXIT_FAILURE);
  }

  printf("PASS: production wall collision clipping, event, and score timing\n");
}

static void verifyBoosterUseAndRecovery(void) {
  static const int two_humans[PLAYERS] = {
    AI_HUMAN, AI_HUMAN, AI_NONE, AI_NONE
  };
  Data *data;

  setupRound(two_humans, 0);
  configured_booster_on = 1;
  data = game->player[0].data;
  data->boost_enabled = 1;

  if(runPhysicsAt(20, 20) != 0 ||
     !nearlyEqual(data->booster, 6.48f) ||
     !nearlyEqual(data->speed, 12.02f) || !eventQueueIsEmpty()) {
    fprintf(stderr, "booster use no longer drains fuel and raises speed\n");
    exit(EXIT_FAILURE);
  }

  data->boost_enabled = 0;
  if(runPhysicsAt(40, 20) != 0 ||
     !nearlyEqual(data->booster, 6.488f) ||
     !nearlyEqual(data->speed, 12.004f) || !eventQueueIsEmpty()) {
    fprintf(stderr, "booster release no longer regenerates and decelerates\n");
    exit(EXIT_FAILURE);
  }

  if(runPhysicsAt(60, 20) != 0 ||
     !nearlyEqual(data->booster, 6.496f) ||
     !nearlyEqual(data->speed, TEST_SPEED) || !eventQueueIsEmpty()) {
    fprintf(stderr, "booster recovery no longer clamps at base speed\n");
    exit(EXIT_FAILURE);
  }

  if(runPhysicsAt(80, 20) != 0 ||
     !nearlyEqual(data->booster, 6.5f) ||
     !nearlyEqual(data->speed, TEST_SPEED) || !eventQueueIsEmpty() ||
     exit_loop_code != -1) {
    fprintf(stderr, "booster recovery no longer caps at maximum fuel\n");
    exit(EXIT_FAILURE);
  }

  configured_booster_on = 0;
  printf("PASS: production booster use, release, recovery, and clamps\n");
}

static void freeGameState(void) {
  int i;

  for(i = 0; i < game->players; i++) {
    free(game->player[i].data->trails);
    free(game->player[i].data);
    free(game->player[i].ai);
    free(game->player[i].camera);
  }
  free(game->player);
  free(gPlayerVisuals);
}

int main(void) {
  static const int fixed_20[] = { 20 };
  static const int fixed_10[] = { 10 };
  static const int variable[] = { 7, 13, 41, 3, 22 };

  gPlayerVisuals = (PlayerVisual *)
    malloc(MAX_PLAYERS * sizeof(*gPlayerVisuals));
  if(gPlayerVisuals == NULL)
    return EXIT_FAILURE;

  initGameStructures();
  resetScores();

  verifyRound(&expected_fixed_20,
              runRound(expected_fixed_20.name, fixed_20, 1));
  verifyRound(&expected_fixed_10,
              runRound(expected_fixed_10.name, fixed_10, 1));
  verifyRound(&expected_variable,
              runRound(expected_variable.name, variable,
                       sizeof(variable) / sizeof(variable[0])));
  verifyFastFinishScheduling();
  verifyInactivePlayerReset();
  verifyQueuedEventReset();
  verifyScriptedHumanTurns();
  verifyWallCollisionTiming();
  verifyBoosterUseAndRecovery();

  freeGameState();
  return EXIT_SUCCESS;
}
