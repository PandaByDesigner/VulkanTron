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

  freeGameState();
  return EXIT_SUCCESS;
}
