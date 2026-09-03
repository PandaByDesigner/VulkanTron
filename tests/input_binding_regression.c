#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/gltron.h"

enum {
  BIND_LEFT = 0,
  BIND_RIGHT,
  BIND_GLANCE_LEFT,
  BIND_GLANCE_RIGHT,
  BIND_BOOST,
  BIND_COUNT
};

typedef struct TestBinding {
  int value;
  int valid;
} TestBinding;

static const char *binding_names[BIND_COUNT] = {
  "left", "right", "glance_left", "glance_right", "boost"
};

Game main_game;
Game *game = &main_game;
Game2 main_game2;
Game2 *game2 = &main_game2;
Visual *gScreen;

static Player players[PLAYERS];
static Data player_data[PLAYERS];
static AI player_ai[PLAYERS];
static Camera player_cameras[PLAYERS];
static Visual screen;
static TestBinding bindings[PLAYERS][BIND_COUNT];

static int pending_player;
static int pending_action;
static int result_pending;
static int query_players[64];
static int query_actions[64];
static int query_count;
static int malformed_count;
static int event_count;
static int event_player;
static event_type_e event_type;
static int message_count;

static void fail(const char *message) {
  fprintf(stderr, "FAIL: %s\n", message);
  exit(EXIT_FAILURE);
}

static int actionIndex(const char *name) {
  int action;

  for(action = 0; action < BIND_COUNT; action++) {
    if(strcmp(name, binding_names[action]) == 0)
      return action;
  }
  return -1;
}

int scripting_RunFormatChecked(const char *format, ...) {
  char command[128];
  char action[32];
  va_list args;
  int player;

  if(result_pending)
    fail("a prior malformed binding result was not removed from the stack");

  va_start(args, format);
  vsnprintf(command, sizeof(command), format, args);
  va_end(args);
  if(sscanf(command, "return settings.keys[%d].%31s", &player, action) != 2)
    fail("binding reader issued an unexpected Lua query");

  pending_player = player - 1;
  pending_action = actionIndex(action);
  if(pending_player < 0 || pending_player >= PLAYERS || pending_action < 0)
    fail("binding reader queried an invalid player or action");
  if(query_count >= (int)(sizeof(query_players) / sizeof(query_players[0])))
    fail("binding query log overflowed");
  query_players[query_count] = pending_player;
  query_actions[query_count] = pending_action;
  query_count++;
  result_pending = 1;
  return 0;
}

int scripting_GetStrictIntegerResult(int *value) {
  const TestBinding *binding;

  if(!result_pending)
    fail("binding getter ran without a pending Lua result");
  if(*value != 0)
    fail("binding destination was not initialized before extraction");

  binding = &bindings[pending_player][pending_action];
  result_pending = 0;
  if(!binding->valid) {
    *value = 0;
    malformed_count++;
    return 1;
  }

  *value = binding->value;
  return 0;
}

void createEvent(int player, event_type_e type) {
  event_count++;
  event_player = player;
  event_type = type;
}

float getSettingf(const char *name) {
  if(strcmp(name, "booster_min") == 0)
    return 1.0f;
  fail("gameplay binding path queried an unexpected float setting");
  return 0.0f;
}

void displayMessage(outloc_e where, const char *format, ...) {
  (void) format;
  if(where != TO_STDERR)
    fail("unbound key message used an unexpected destination");
  message_count++;
}

char *SystemGetKeyName(int key) {
  (void) key;
  return "test key";
}

void SystemExitLoop(int code) { (void) code; }
void changeDisplay(int view) { (void) view; }
void saveSettings(void) { }
void nextCameraType(void) { }
void doBmpScreenShot(Visual *display) { (void) display; }
void doPngScreenShot(Visual *display) { (void) display; }
void consoleScrollBackward(int range) { (void) range; }
void consoleScrollForward(int range) { (void) range; }

static void setBinding(int player, int action, int value) {
  bindings[player][action].value = value;
  bindings[player][action].valid = 1;
}

static void resetObservation(void) {
  pending_player = -1;
  pending_action = -1;
  result_pending = 0;
  query_count = 0;
  malformed_count = 0;
  event_count = 0;
  event_player = -1;
  event_type = (event_type_e)0;
  message_count = 0;
}

static void resetFixture(void) {
  int player;

  memset(&main_game, 0, sizeof(main_game));
  memset(&main_game2, 0, sizeof(main_game2));
  memset(players, 0, sizeof(players));
  memset(player_data, 0, sizeof(player_data));
  memset(player_ai, 0, sizeof(player_ai));
  memset(player_cameras, 0, sizeof(player_cameras));
  memset(bindings, 0, sizeof(bindings));

  game->players = PLAYERS;
  game->player = players;
  gScreen = &screen;
  for(player = 0; player < PLAYERS; player++) {
    players[player].data = &player_data[player];
    players[player].ai = &player_ai[player];
    players[player].camera = &player_cameras[player];
    player_data[player].speed = player < 2 ? 12.0f : SPEED_GONE;
    player_data[player].booster = 6.5f;
    player_ai[player].active = player < 2 ? AI_HUMAN : AI_NONE;
  }
  resetObservation();
}

static void expectQuery(int index, int player, int action) {
  if(index >= query_count || query_players[index] != player ||
     query_actions[index] != action)
    fail("gameplay binding query order changed");
}

static void testValidBindingOrder(void) {
  resetFixture();
  setBinding(0, BIND_LEFT, 567);
  setBinding(0, BIND_RIGHT, 567);
  setBinding(1, BIND_LEFT, 567);

  keyGame(SYSTEM_KEYSTATE_DOWN, 567, 0, 0);
  if(event_count != 1 || event_player != 0 || event_type != EVENT_TURN_LEFT ||
     query_count != 1 || result_pending)
    fail("first player/action no longer wins duplicate numeric bindings");
  expectQuery(0, 0, BIND_LEFT);

  resetObservation();
  setBinding(0, BIND_LEFT, 566);
  keyGame(SYSTEM_KEYSTATE_DOWN, 567, 0, 0);
  if(event_count != 1 || event_player != 0 || event_type != EVENT_TURN_RIGHT ||
     query_count != 2 || result_pending)
    fail("right-turn binding dispatch changed");
  expectQuery(0, 0, BIND_LEFT);
  expectQuery(1, 0, BIND_RIGHT);

  printf("PASS: valid numeric bindings retain player and action precedence\n");
}

static void testMalformedBindingsAreSkipped(void) {
  resetFixture();
  setBinding(0, BIND_RIGHT, 544);

  keyGame(SYSTEM_KEYSTATE_DOWN, 544, 0, 0);
  if(event_count != 1 || event_player != 0 || event_type != EVENT_TURN_RIGHT ||
     malformed_count != 1 || query_count != 2 || result_pending)
    fail("malformed left binding did not safely fall through to right");

  resetFixture();
  setBinding(1, BIND_LEFT, 548);
  keyGame(SYSTEM_KEYSTATE_DOWN, 548, 0, 0);
  if(event_count != 1 || event_player != 1 || event_type != EVENT_TURN_LEFT ||
     malformed_count != BIND_COUNT || query_count != BIND_COUNT + 1 ||
     result_pending)
    fail("malformed first-player bindings blocked the next human player");
  expectQuery(0, 0, BIND_LEFT);
  expectQuery(4, 0, BIND_BOOST);
  expectQuery(5, 1, BIND_LEFT);

  resetFixture();
  player_data[1].speed = SPEED_GONE;
  keyGame(SYSTEM_KEYSTATE_DOWN, 700, 0, 0);
  if(event_count != 0 || message_count != 1 ||
     malformed_count != BIND_COUNT || query_count != BIND_COUNT ||
     result_pending)
    fail("all-malformed bindings did not end as one unbound key event");

  printf("PASS: malformed bindings are skipped with balanced result handling\n");
}

static void testHeldActionTransitions(void) {
  resetFixture();
  player_data[1].speed = SPEED_GONE;
  setBinding(0, BIND_GLANCE_LEFT, 512);
  setBinding(0, BIND_BOOST, 535);

  keyGame(SYSTEM_KEYSTATE_DOWN, 512, 0, 0);
  if(fabsf(player_cameras[0].movement[CAM_PHI_OFFSET] - PI / 2.0f) >
       0.0001f)
    fail("valid glance press no longer sets its classic camera offset");
  keyGame(SYSTEM_KEYSTATE_UP, 512, 0, 0);
  if(fabsf(player_cameras[0].movement[CAM_PHI_OFFSET]) > 0.0001f)
    fail("valid glance release no longer clears its camera offset");

  keyGame(SYSTEM_KEYSTATE_DOWN, 535, 0, 0);
  if(player_data[0].boost_enabled != 1)
    fail("valid boost press no longer enables boost");
  keyGame(SYSTEM_KEYSTATE_UP, 535, 0, 0);
  if(player_data[0].boost_enabled != 0 || result_pending)
    fail("valid boost release no longer disables boost");

  printf("PASS: valid glance and boost pressed/released semantics are unchanged\n");
}

int main(void) {
  testValidBindingOrder();
  testMalformedBindingsAreSkipped();
  testHeldActionTransitions();
  return EXIT_SUCCESS;
}
