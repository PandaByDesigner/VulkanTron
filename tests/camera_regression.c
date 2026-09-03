#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/game.h"
#include "input/input.h"
#include "video/video.h"

#define CAMERA_EPSILON 0.0002f

extern void playerCamera(PlayerVisual *visual, Player *player);

static Player test_players[PLAYERS];
static Data test_data[PLAYERS];
static AI test_ai[PLAYERS];
static Camera test_cameras[PLAYERS];
static PlayerVisual test_visuals[PLAYERS];
static segment2 test_trails[PLAYERS];

static int setting_camera_type;
static int setting_debug_output;
static int setting_write_count;
static unsigned int fake_elapsed_time;
static int elapsed_time_calls;
static vec2 recognizer_position;
static vec2 recognizer_velocity;
static int recognizer_calls;
static int console_message_count;
static outloc_e console_message_where;
static char console_message[128];

static void fail(const char *message) {
  fprintf(stderr, "FAIL: %s\n", message);
  exit(EXIT_FAILURE);
}

static void expectInt(const char *name, int actual, int expected) {
  if(actual != expected) {
    fprintf(stderr, "FAIL: %s: got %d, expected %d\n",
            name, actual, expected);
    exit(EXIT_FAILURE);
  }
}

static void expectFloat(const char *name, float actual, float expected) {
  if(fabsf(actual - expected) > CAMERA_EPSILON) {
    fprintf(stderr, "FAIL: %s: got %.9f, expected %.9f\n",
            name, actual, expected);
    exit(EXIT_FAILURE);
  }
}

static void expectVec3(const char *name, const float actual[3],
                       float x, float y, float z) {
  char component[96];

  snprintf(component, sizeof(component), "%s x", name);
  expectFloat(component, actual[0], x);
  snprintf(component, sizeof(component), "%s y", name);
  expectFloat(component, actual[1], y);
  snprintf(component, sizeof(component), "%s z", name);
  expectFloat(component, actual[2], z);
}

static void testClassicGlobalTables(void) {
  static const float expected_defaults[CAM_COUNT][3] = {
    { 17.0f, 1.047197580f, 0.0f },
    { 18.0f, 0.785398185f, 0.043633234f },
    { 4.0f, 0.392699093f, 0.0f },
    { 17.0f, 1.047197580f, 0.0f }
  };
  static const int expected_dirs_x[4] = { 0, -1, 0, 1 };
  static const int expected_dirs_y[4] = { -1, 0, 1, 0 };
  static const float expected_angles[5] = {
    1.570796371f, 0.0f, 4.712388992f, 3.141592741f, 6.283185482f
  };
  int type;
  int component;

  for(type = 0; type < CAM_COUNT; type++) {
    for(component = 0; component < 3; component++)
      expectFloat("classic camera default",
                  cam_defaults[type][component],
                  expected_defaults[type][component]);
  }
  for(component = 0; component < 4; component++) {
    expectInt("classic x direction", dirsX[component],
              expected_dirs_x[component]);
    expectInt("classic y direction", dirsY[component],
              expected_dirs_y[component]);
  }
  for(component = 0; component < 5; component++)
    expectFloat("classic camera angle", camAngles[component],
                expected_angles[component]);
}

static void resetCameraDefaults(void) {
  cam_defaults[CAM_CIRCLE][CAM_R] = 17.0f;
  cam_defaults[CAM_CIRCLE][CAM_CHI] = 1.047197580f;
  cam_defaults[CAM_CIRCLE][CAM_PHI] = 0.0f;

  cam_defaults[CAM_FOLLOW][CAM_R] = 18.0f;
  cam_defaults[CAM_FOLLOW][CAM_CHI] = 0.785398185f;
  cam_defaults[CAM_FOLLOW][CAM_PHI] = 0.043633234f;

  cam_defaults[CAM_COCKPIT][CAM_R] = 4.0f;
  cam_defaults[CAM_COCKPIT][CAM_CHI] = 0.392699093f;
  cam_defaults[CAM_COCKPIT][CAM_PHI] = 0.0f;

  cam_defaults[CAM_FREE][CAM_R] = 17.0f;
  cam_defaults[CAM_FREE][CAM_CHI] = 1.047197580f;
  cam_defaults[CAM_FREE][CAM_PHI] = 0.0f;
}

static void setPlayerPosition(int player, float x, float y) {
  test_trails[player].vStart.v[0] = x - 2.0f;
  test_trails[player].vStart.v[1] = y - 3.0f;
  test_trails[player].vDirection.v[0] = 2.0f;
  test_trails[player].vDirection.v[1] = 3.0f;
  test_data[player].trailOffset = 0;
  test_data[player].trails = &test_trails[player];
}

static void resetFixture(void) {
  int i;

  memset(&main_game, 0, sizeof(main_game));
  memset(&main_game2, 0, sizeof(main_game2));
  memset(test_players, 0, sizeof(test_players));
  memset(test_data, 0, sizeof(test_data));
  memset(test_ai, 0, sizeof(test_ai));
  memset(test_cameras, 0, sizeof(test_cameras));
  memset(test_visuals, 0, sizeof(test_visuals));
  memset(test_trails, 0, sizeof(test_trails));
  memset(&gInput, 0, sizeof(gInput));
  memset(&gSettingsCache, 0, sizeof(gSettingsCache));

  game = &main_game;
  game2 = &main_game2;
  game->players = PLAYERS;
  game->player = test_players;
  gPlayerVisuals = test_visuals;

  for(i = 0; i < PLAYERS; i++) {
    test_players[i].data = &test_data[i];
    test_players[i].ai = &test_ai[i];
    test_players[i].camera = &test_cameras[i];
    test_data[i].dir = i & 3;
    test_data[i].last_dir = test_data[i].dir;
    test_data[i].speed = 12.0f;
    test_ai[i].active = AI_HUMAN;
    setPlayerPosition(i, 100.0f + 25.0f * i, 50.0f + 10.0f * i);
  }

  game2->time.current = 1000U;
  game2->time.dt = 20U;
  setting_camera_type = CAM_TYPE_CIRCLING;
  setting_debug_output = 0;
  setting_write_count = 0;
  fake_elapsed_time = 0U;
  elapsed_time_calls = 0;
  recognizer_position.v[0] = 321.25f;
  recognizer_position.v[1] = 123.5f;
  recognizer_velocity.v[0] = -2.5f;
  recognizer_velocity.v[1] = 4.0f;
  recognizer_calls = 0;
  console_message_count = 0;
  console_message_where = (outloc_e)0;
  console_message[0] = '\0';
  resetCameraDefaults();
}

int getSettingi(const char *name) {
  if(strcmp(name, "camType") == 0)
    return setting_camera_type;
  if(strcmp(name, "debug_output") == 0)
    return setting_debug_output;

  fprintf(stderr, "FAIL: unexpected integer setting read: %s\n", name);
  exit(EXIT_FAILURE);
}

void setSettingi(const char *name, int value) {
  if(strcmp(name, "camType") != 0) {
    fprintf(stderr, "FAIL: unexpected integer setting write: %s\n", name);
    exit(EXIT_FAILURE);
  }

  setting_camera_type = value;
  setting_write_count++;
}

unsigned int SystemGetElapsedTime(void) {
  elapsed_time_calls++;
  return fake_elapsed_time;
}

void displayMessage(outloc_e where, const char *format, ...) {
  va_list args;

  console_message_where = where;
  va_start(args, format);
  vsnprintf(console_message, sizeof(console_message), format, args);
  va_end(args);
  console_message_count++;
}

void getPositionFromData(float *x, float *y, Data *data) {
  const segment2 *trail = &data->trails[data->trailOffset];

  *x = trail->vStart.v[0] + trail->vDirection.v[0];
  *y = trail->vStart.v[1] + trail->vDirection.v[1];
}

void getRecognizerPositionVelocity(vec2 *position, vec2 *velocity) {
  *position = recognizer_position;
  *velocity = recognizer_velocity;
  recognizer_calls++;
}

static void expectCameraType(const Camera *camera, int type) {
  static const int expected_interpolated_cam[CAM_COUNT] = { 0, 1, 0, 0 };
  static const int expected_interpolated_target[CAM_COUNT] = { 0, 0, 1, 0 };
  static const int expected_coupled[CAM_COUNT] = { 0, 1, 1, 0 };
  static const int expected_freedom[CAM_COUNT][3] = {
    { 1, 0, 1 },
    { 1, 1, 1 },
    { 0, 1, 0 },
    { 1, 1, 1 }
  };
  int freedom;

  expectInt("camera type", camera->type.type, type);
  expectInt("interpolated camera", camera->type.interpolated_cam,
            expected_interpolated_cam[type]);
  expectInt("interpolated target", camera->type.interpolated_target,
            expected_interpolated_target[type]);
  expectInt("coupled camera", camera->type.coupled,
            expected_coupled[type]);
  for(freedom = 0; freedom < 3; freedom++)
    expectInt("camera freedom", camera->type.freedom[freedom],
              expected_freedom[type][freedom]);
}

static void expectInitializedMovement(const Camera *camera, int type) {
  static const float expected_movement[CAM_COUNT][4] = {
    { 17.0f, 1.047197580f, 0.0f, 0.0f },
    { 18.0f, 0.785398185f, 0.043633234f, 0.0f },
    { 4.0f, 0.392699093f, 3.141592741f, 0.0f },
    { 17.0f, 1.047197580f, 0.0f, 0.0f }
  };
  int movement;

  for(movement = 0; movement < 4; movement++)
    expectFloat("initialized camera movement", camera->movement[movement],
                expected_movement[type][movement]);
}

static void testInitialization(void) {
  int type;

  resetFixture();
  for(type = 0; type < CAM_COUNT; type++) {
    Camera *camera = &test_cameras[0];

    memset(camera, 0xa5, sizeof(*camera));
    initCamera(camera, &test_data[0], type);
    expectCameraType(camera, type);
    expectInitializedMovement(camera, type);
    expectVec3("initialized camera position", camera->cam,
               117.0f, 50.0f, 8.0f);
    expectVec3("initialized target", camera->target,
               100.0f, 50.0f, 0.0f);
  }
}

static void prepareMovement(int type) {
  resetFixture();
  test_data[0].dir = 0;
  test_data[0].last_dir = 0;
  test_data[0].turn_time = 0U;
  initCamera(&test_cameras[0], &test_data[0], type);
}

static void testClassicMovementGoldens(void) {
  prepareMovement(CAM_TYPE_CIRCLING);
  playerCamera(&test_visuals[0], &test_players[0]);
  expectVec3("circling camera", test_cameras[0].cam,
             114.722435f, 50.0f, 8.5f);
  expectVec3("circling target", test_cameras[0].target,
             100.0f, 50.0f, 0.0f);
  expectFloat("circling azimuth advance",
              test_cameras[0].movement[CAM_PHI], 0.006980f);

  prepareMovement(CAM_TYPE_FOLLOW);
  playerCamera(&test_visuals[0], &test_players[0]);
  expectVec3("follow camera", test_cameras[0].cam,
             99.444817f, 62.715809f, 12.727921f);
  expectVec3("follow target", test_cameras[0].target,
             100.0f, 50.0f, 0.0f);
  expectFloat("follow azimuth remains stable",
              test_cameras[0].movement[CAM_PHI], 0.043633234f);

  prepareMovement(CAM_TYPE_COCKPIT);
  playerCamera(&test_visuals[0], &test_players[0]);
  expectVec3("cockpit camera", test_cameras[0].cam,
             100.0f, 45.9f, 4.1f);
  expectVec3("cockpit target", test_cameras[0].target,
             100.0f, 44.0f, 4.0f);

  prepareMovement(CAM_TYPE_MOUSE);
  playerCamera(&test_visuals[0], &test_players[0]);
  expectVec3("mouse camera", test_cameras[0].cam,
             114.722435f, 50.0f, 8.5f);
  expectVec3("mouse target", test_cameras[0].target,
             100.0f, 50.0f, 0.0f);
  expectFloat("mouse azimuth remains stable",
              test_cameras[0].movement[CAM_PHI], 0.0f);
}

static void testCoupledTurnInterpolation(void) {
  prepareMovement(CAM_TYPE_FOLLOW);
  game2->time.current = 1100U;
  test_data[0].turn_time = 1000U;
  test_data[0].last_dir = 2;
  test_data[0].dir = 1;

  playerCamera(&test_visuals[0], &test_players[0]);
  expectVec3("wrapped follow turn camera", test_cameras[0].cam,
             109.384010f, 41.401142f, 12.727921f);
  expectVec3("wrapped follow turn target", test_cameras[0].target,
             100.0f, 50.0f, 0.0f);

  prepareMovement(CAM_TYPE_FOLLOW);
  game2->time.current = 1050U;
  test_data[0].turn_time = 1000U;
  test_data[0].last_dir = 1;
  test_data[0].dir = 2;

  playerCamera(&test_visuals[0], &test_players[0]);
  expectVec3("reverse-wrapped follow turn camera", test_cameras[0].cam,
             111.960335f, 45.646793f, 12.727921f);
  expectVec3("reverse-wrapped follow turn target", test_cameras[0].target,
             100.0f, 50.0f, 0.0f);
}

static void testGlanceOffset(void) {
  prepareMovement(CAM_TYPE_MOUSE);
  test_cameras[0].movement[CAM_PHI_OFFSET] = PI / 2.0f;
  playerCamera(&test_visuals[0], &test_players[0]);
  expectVec3("positive glance offset camera", test_cameras[0].cam,
             100.0f, 64.722435f, 8.5f);

  prepareMovement(CAM_TYPE_MOUSE);
  test_cameras[0].movement[CAM_PHI_OFFSET] = -PI / 2.0f;
  playerCamera(&test_visuals[0], &test_players[0]);
  expectVec3("negative glance offset camera", test_cameras[0].cam,
             100.0f, 35.277565f, 8.5f);

  test_cameras[0].movement[CAM_PHI_OFFSET] = PI / 2.0f;
  initCamera(&test_cameras[0], &test_data[0], CAM_TYPE_MOUSE);
  expectFloat("camera reinitialization clears glance offset",
              test_cameras[0].movement[CAM_PHI_OFFSET], 0.0f);
}

static void testInputFreedomAndClamping(void) {
  prepareMovement(CAM_TYPE_MOUSE);
  game2->time.dt = 12000U;
  gInput.mouse1 = 1;
  gInput.mousex = 400;
  gInput.mousey = 400;
  playerCamera(&test_visuals[0], &test_players[0]);
  expectFloat("maximum camera radius clamp",
              test_cameras[0].movement[CAM_R], 45.0f);
  expectFloat("maximum camera elevation clamp",
              test_cameras[0].movement[CAM_CHI], 1.178097248f);
  expectFloat("mouse camera azimuth input",
              test_cameras[0].movement[CAM_PHI], -1.2f);
  expectVec3("maximum-clamped mouse camera", test_cameras[0].cam,
             115.064873f, 11.250870f, 17.220755f);

  prepareMovement(CAM_TYPE_MOUSE);
  game2->time.dt = 12000U;
  gInput.mouse2 = 1;
  gInput.mousex = -400;
  gInput.mousey = -400;
  playerCamera(&test_visuals[0], &test_players[0]);
  expectFloat("minimum camera radius clamp",
              test_cameras[0].movement[CAM_R], 6.0f);
  expectFloat("minimum camera elevation clamp",
              test_cameras[0].movement[CAM_CHI], 0.392699093f);
  expectFloat("reverse mouse camera azimuth input",
              test_cameras[0].movement[CAM_PHI], 1.2f);
  expectVec3("minimum-clamped mouse camera", test_cameras[0].cam,
             100.832008f, 52.140057f, 5.543277f);

  prepareMovement(CAM_TYPE_COCKPIT);
  game2->time.dt = 1000U;
  gInput.mouse1 = 1;
  gInput.mousex = 100;
  gInput.mousey = 100;
  playerCamera(&test_visuals[0], &test_players[0]);
  expectFloat("cockpit radius is fixed",
              test_cameras[0].movement[CAM_R], 4.0f);
  expectFloat("cockpit elevation is fixed",
              test_cameras[0].movement[CAM_CHI], 0.392699093f);
  expectFloat("cockpit azimuth remains free",
              test_cameras[0].movement[CAM_PHI], 2.841592789f);

  prepareMovement(CAM_TYPE_CIRCLING);
  game2->time.dt = 1000U;
  gInput.mousex = 100;
  playerCamera(&test_visuals[0], &test_players[0]);
  expectFloat("circling azimuth ignores mouse input",
              test_cameras[0].movement[CAM_PHI], 0.349f);
}

static void testMovementRouting(void) {
  int i;

  resetFixture();
  test_ai[0].active = AI_HUMAN;
  test_ai[1].active = AI_COMPUTER;
  test_ai[2].active = AI_HUMAN;
  test_ai[3].active = AI_NONE;
  test_data[0].speed = 12.0f;
  test_data[1].speed = 12.0f;
  test_data[2].speed = SPEED_CRASHED;
  test_data[3].speed = SPEED_GONE;
  initCamera(&test_cameras[0], &test_data[0], CAM_TYPE_FOLLOW);
  initCamera(&test_cameras[1], &test_data[1], CAM_TYPE_CIRCLING);
  initCamera(&test_cameras[2], &test_data[2], CAM_TYPE_COCKPIT);
  initCamera(&test_cameras[3], &test_data[3], CAM_TYPE_MOUSE);

  doCameraMovement();
  expectVec3("live human uses player camera", test_cameras[0].target,
             100.0f, 50.0f, 0.0f);
  expectVec3("live computer uses player camera", test_cameras[1].target,
             125.0f, 60.0f, 0.0f);
  expectVec3("crashed player retains player camera", test_cameras[2].target,
             150.0f, 76.0f, 4.0f);
  expectVec3("gone player uses recognizer camera", test_cameras[3].cam,
             321.25f, 123.5f, 50.0f);
  expectVec3("gone player targets recognizer velocity", test_cameras[3].target,
             318.75f, 127.5f, 48.0f);
  expectInt("recognizer route count", recognizer_calls, 1);

  for(i = 0; i < PLAYERS; i++)
    test_data[i].speed = SPEED_GONE;
  gInput.mouse1 = 1;
  gInput.mouse2 = 1;
  gInput.mousex = 13;
  gInput.mousey = -21;
  doCameraMovement();
  expectInt("mouse x delta consumed", gInput.mousex, 0);
  expectInt("mouse y delta consumed", gInput.mousey, 0);
  expectInt("mouse button one retained", gInput.mouse1, 1);
  expectInt("mouse button two retained", gInput.mouse2, 1);
  expectInt("all observer routes called", recognizer_calls, 5);
}

static void testCameraTypeCycling(void) {
  static const int expected_types[CAM_COUNT] = {
    CAM_TYPE_FOLLOW,
    CAM_TYPE_COCKPIT,
    CAM_TYPE_MOUSE,
    CAM_TYPE_CIRCLING
  };
  static const char *expected_messages[CAM_COUNT] = {
    "[camera] Behind Camera",
    "[camera] Cockpit Camera",
    "[camera] Mouse Camera",
    "[camera] Circling Camera"
  };
  Camera computer_before;
  Camera absent_before;
  int transition;

  resetFixture();
  test_ai[0].active = AI_HUMAN;
  test_ai[1].active = AI_COMPUTER;
  test_ai[2].active = AI_NONE;
  test_ai[3].active = AI_HUMAN;
  memset(&test_cameras[0], 0x11, sizeof(test_cameras[0]));
  memset(&test_cameras[1], 0x22, sizeof(test_cameras[1]));
  memset(&test_cameras[2], 0x33, sizeof(test_cameras[2]));
  memset(&test_cameras[3], 0x44, sizeof(test_cameras[3]));
  computer_before = test_cameras[1];
  absent_before = test_cameras[2];
  setting_camera_type = CAM_TYPE_CIRCLING;
  setting_debug_output = 1;

  for(transition = 0; transition < CAM_COUNT; transition++) {
    nextCameraType();
    expectInt("cycled setting", setting_camera_type,
              expected_types[transition]);
    expectInt("cycled settings cache", gSettingsCache.camType,
              expected_types[transition]);
    expectInt("camera setting write count", setting_write_count,
              transition + 1);
    expectInt("camera console message count", console_message_count,
              transition + 1);
    expectInt("camera console destination", console_message_where,
              TO_CONSOLE);
    if(strcmp(console_message, expected_messages[transition]) != 0)
      fail("camera cycle message changed");

    expectCameraType(&test_cameras[0], expected_types[transition]);
    expectInitializedMovement(&test_cameras[0], expected_types[transition]);
    expectVec3("cycled first human camera", test_cameras[0].cam,
               117.0f, 50.0f, 8.0f);
    expectVec3("cycled first human target", test_cameras[0].target,
               100.0f, 50.0f, 0.0f);
    expectCameraType(&test_cameras[3], expected_types[transition]);
    expectInitializedMovement(&test_cameras[3], expected_types[transition]);
    expectVec3("cycled second human camera", test_cameras[3].cam,
               192.0f, 80.0f, 8.0f);
    expectVec3("cycled second human target", test_cameras[3].target,
               175.0f, 80.0f, 0.0f);
    if(memcmp(&test_cameras[1], &computer_before,
              sizeof(test_cameras[1])) != 0)
      fail("computer camera changed during human camera cycling");
    if(memcmp(&test_cameras[2], &absent_before,
              sizeof(test_cameras[2])) != 0)
      fail("inactive camera changed during human camera cycling");
  }
}

static void testPausedWallClockMovement(void) {
  prepareMovement(CAM_TYPE_MOUSE);
  game2->time.dt = 0U;
  fake_elapsed_time = 250U;
  gInput.mouse1 = 1;

  playerCamera(&test_visuals[0], &test_players[0]);
  expectInt("elapsed time sampled twice", elapsed_time_calls, 2);
  expectFloat("paused wall-clock zoom",
              test_cameras[0].movement[CAM_R], 27.0f);
  expectVec3("paused wall-clock camera", test_cameras[0].cam,
             123.382687f, 50.0f, 13.5f);
}

int main(void) {
  testClassicGlobalTables();
  testInitialization();
  testClassicMovementGoldens();
  testCoupledTurnInterpolation();
  testGlanceOffset();
  testInputFreedomAndClamping();
  testMovementRouting();
  testCameraTypeCycling();
  testPausedWallClockMovement();

  puts("PASS: production camera initialization and four-mode movement goldens");
  puts("PASS: coupled turn seams, glance offsets, input freedoms, and clamps");
  puts("PASS: live, crashed, and recognizer camera routing");
  puts("PASS: human-only camera cycling and classic console labels");
  return EXIT_SUCCESS;
}
