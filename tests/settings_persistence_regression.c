#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "configuration/configuration.h"
#include "filesystem/path.h"
#include "Nebu_scripting.h"

#define TEST_PATH_CAPACITY 4096

extern lua_State *L;

SettingsCache gSettingsCache;

static char repository_path[TEST_PATH_CAPACITY];
static char preferences_path[TEST_PATH_CAPACITY];
static char preferences_directory[TEST_PATH_CAPACITY];
static int scripting_active;

static const int expected_bindings[4][5] = {
  { 97, 100, 113, 101, 119 },
  { 106, 107, 117, 105, 108 },
  { 276, 275, 127, 279, 274 },
  { 260, 262, 263, 265, 261 }
};

static const char *binding_names[5] = {
  "left", "right", "glance_left", "glance_right", "boost"
};

static void fail(const char *format, ...) {
  va_list arguments;

  fprintf(stderr, "FAIL: ");
  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  va_end(arguments);
  fputc('\n', stderr);
  exit(EXIT_FAILURE);
}

static char *duplicateString(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);

  if(copy == NULL)
    fail("out of memory copying a path");
  memcpy(copy, text, length);
  return copy;
}

static char *joinedPath(const char *directory, const char *filename) {
  size_t directory_length = strlen(directory);
  size_t filename_length = strlen(filename);
  char *path = malloc(directory_length + filename_length + 2);

  if(path == NULL)
    fail("out of memory joining a path");
  memcpy(path, directory, directory_length);
  path[directory_length] = '/';
  memcpy(path + directory_length + 1, filename, filename_length + 1);
  return path;
}

const char *getDirectory(int location) {
  if(location == PATH_PREFERENCES)
    return preferences_directory;
  if(location == PATH_SCRIPTS)
    return repository_path;
  return NULL;
}

char *getPath(int location, const char *filename) {
  char *scripts;
  char *path;

  if(location != PATH_SCRIPTS || filename == NULL)
    return NULL;
  scripts = joinedPath(repository_path, "scripts");
  path = joinedPath(scripts, filename);
  free(scripts);
  return path;
}

char *getPossiblePath(int location, const char *filename) {
  (void)filename;
  if(location != PATH_PREFERENCES)
    return NULL;
  return duplicateString(preferences_path);
}

static void checkedRun(const char *command) {
  if(scripting_RunChecked(command) != 0)
    fail("Lua command failed: %s", command);
}

static void checkedRunFile(const char *path) {
  if(scripting_RunFileChecked(path) != 0)
    fail("Lua file failed: %s", path);
}

static void resetLua(void) {
  char *configuration;

  if(scripting_active)
    scripting_Quit();
  scripting_Init();
  scripting_active = 1;
  configuration = getPath(PATH_SCRIPTS, "config.lua");
  checkedRunFile(configuration);
  free(configuration);
}

static void installFixture(void) {
  resetLua();
  checkedRun(
    "settings.version = 0.70\n"
    "settings.keys[1].left = 97\n"
    "settings.keys[1].right = 100\n"
    "settings.keys[1].glance_left = 113\n"
    "settings.keys[1].glance_right = 101\n"
    "settings.keys[1].boost = 119\n"
    "settings.keys[2].left = 106\n"
    "settings.keys[2].right = 107\n"
    "settings.keys[2].glance_left = 117\n"
    "settings.keys[2].glance_right = 105\n"
    "settings.keys[2].boost = 108\n"
    "settings.keys[3].left = 276\n"
    "settings.keys[3].right = 275\n"
    "settings.keys[3].glance_left = 127\n"
    "settings.keys[3].glance_right = 279\n"
    "settings.keys[3].boost = 274\n"
    "settings.keys[4].left = 260\n"
    "settings.keys[4].right = 262\n"
    "settings.keys[4].glance_left = 263\n"
    "settings.keys[4].glance_right = 265\n"
    "settings.keys[4].boost = 261\n"
    "settings.current_artpack = \"quoted \\\"name\\\" \\\\ path\\nnext\"\n"
    "settings[\"_private_transient\"] = \"do not save\"\n"
    "settings.unknown_number = 123456789\n"
    "settings.unknown_binding_boundaries = { 512, 535, 544, 567 }\n"
    "settings.unknown_table = { }\n"
    "settings.unknown_table[\"punct.key[]\"] = \"tab\\tvalue\"\n"
    "settings.unknown_table.__visited__ = \"user-owned\"\n"
    "settings.unknown_shared = { }\n"
    "settings.unknown_shared.value = 77\n"
    "settings.unknown_alias = settings.unknown_shared\n"
    "settings.unknown_shared.self = settings.unknown_shared\n"
    "settings.unknown_shared.root = settings\n");
}

static char *readFile(const char *path, size_t *length) {
  FILE *input = fopen(path, "rb");
  long end = 0;
  char *contents;

  if(input == NULL)
    fail("cannot read %s: %s", path, strerror(errno));
  if(fseek(input, 0, SEEK_END) != 0 || (end = ftell(input)) < 0 ||
     fseek(input, 0, SEEK_SET) != 0)
    fail("cannot measure %s", path);
  contents = malloc((size_t)end + 1);
  if(contents == NULL)
    fail("out of memory reading %s", path);
  if(fread(contents, 1, (size_t)end, input) != (size_t)end)
    fail("short read from %s", path);
  if(fclose(input) != 0)
    fail("cannot close %s after reading", path);
  contents[end] = '\0';
  *length = (size_t)end;
  return contents;
}

static void expectFileEquals(const char *expected, size_t expected_length,
                             const char *context) {
  size_t actual_length;
  char *actual = readFile(preferences_path, &actual_length);

  if(actual_length != expected_length ||
     memcmp(actual, expected, expected_length) != 0)
    fail("preferences changed after %s", context);
  free(actual);
}

static int integerResult(const char *command) {
  int value = -999;

  checkedRun(command);
  if(scripting_GetIntegerResult(&value) != 0)
    fail("expected integer result from: %s", command);
  return value;
}

static int strictIntegerResult(const char *command) {
  int value = -999;

  checkedRun(command);
  if(scripting_GetStrictIntegerResult(&value) != 0)
    fail("expected strict integer result from: %s", command);
  return value;
}

static void expectString(const char *command, const char *expected) {
  char *actual = NULL;

  checkedRun(command);
  if(scripting_GetStringResult(&actual) != 0)
    fail("expected string result from: %s", command);
  if(strcmp(actual, expected) != 0)
    fail("string setting changed while reloading: %s", command);
  free(actual);
}

static void validateReload(const char *serialized) {
  int player;
  int binding;

  resetLua();
  checkedRun(serialized);

  if(integerResult("return save_completed") != 1)
    fail("serialized file did not carry its completion marker");
  for(player = 0; player < 4; player++) {
    for(binding = 0; binding < 5; binding++) {
      char command[128];
      int size = snprintf(command, sizeof(command),
                          "return settings.keys[%d].%s",
                          player + 1, binding_names[binding]);
      if(size < 0 || (size_t)size >= sizeof(command))
        fail("binding query overflowed");
      if(strictIntegerResult(command) != expected_bindings[player][binding])
        fail("player %d binding %s changed", player + 1,
             binding_names[binding]);
    }
  }

  expectString("return settings.current_artpack",
               "quoted \"name\" \\ path\nnext");
  expectString("return settings.unknown_table[\"punct.key[]\"]",
               "tab\tvalue");
  expectString("return settings.unknown_table.__visited__", "user-owned");
  if(integerResult("return settings.unknown_number") != 123456789 ||
     integerResult("return settings.unknown_shared.value") != 77)
    fail("unknown numeric settings changed");
  if(strictIntegerResult("return settings.unknown_binding_boundaries[1]") != 512 ||
     strictIntegerResult("return settings.unknown_binding_boundaries[2]") != 535 ||
     strictIntegerResult("return settings.unknown_binding_boundaries[3]") != 544 ||
     strictIntegerResult("return settings.unknown_binding_boundaries[4]") != 567)
    fail("boundary numeric values changed");
  if(integerResult("return settings.unknown_alias == settings.unknown_shared") != 1 ||
     integerResult("return settings.unknown_shared.self == settings.unknown_shared") != 1 ||
     integerResult("return settings.unknown_shared.root == settings") != 1)
    fail("shared or cyclic settings-table references changed");
  if(integerResult("return settings[\"_private_transient\"] == nil") != 1)
    fail("top-level underscore-prefixed setting was unexpectedly persisted");
}

static void testResultHelpers(void) {
  char truncated[4];
  char oversized[5000];
  float float_value = 91.0f;
  int value = 91;

  resetLua();
  checkedRun("return \"not a number\"");
  if(scripting_GetIntegerResult(&value) == 0 || value != 0 ||
     lua_gettop(L) != 0)
    fail("invalid integer result was not initialized and popped");

  value = 91;
  checkedRun("return 3.5");
  if(scripting_GetIntegerResult(&value) != 0 || value != 3 ||
     lua_gettop(L) != 0)
    fail("generic integer result lost legacy truncation semantics");

  value = 91;
  checkedRun("return 3.5");
  if(scripting_GetStrictIntegerResult(&value) == 0 || value != 0 ||
     lua_gettop(L) != 0)
    fail("strict integer result accepted a fractional value");

  value = 91;
  checkedRun("return 1e100");
  if(scripting_GetIntegerResult(&value) == 0 || value != 0 ||
     lua_gettop(L) != 0)
    fail("out-of-range integer result was accepted");

  checkedRun("return { }");
  if(scripting_GetFloatResult(&float_value) == 0 || float_value != 0.0f ||
     lua_gettop(L) != 0)
    fail("invalid float result was not initialized and popped");

  checkedRun("return \"abcdef\"");
  if(scripting_CopyStringResult(truncated, sizeof(truncated)) != 2 ||
     strcmp(truncated, "abc") != 0 || lua_gettop(L) != 0)
    fail("bounded string-result copy did not truncate safely");

  memset(oversized, 'x', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = '\0';
  if(scripting_RunFormatChecked("settings.too_long = \"%s\"", oversized) == 0)
    fail("oversized formatted Lua command was accepted");
  if(integerResult("return settings.too_long == nil") != 1)
    fail("truncated Lua command was executed");

  printf("PASS: checked scripting helpers reject malformed results and commands\n");
}

static void testRepeatedSaveAndReload(void) {
  struct stat status;
  char *first;
  char *second;
  size_t first_length;
  size_t second_length;

  installFixture();
  saveSettings();
  first = readFile(preferences_path, &first_length);
  if(strstr(first, "settings[\"keys\"] = keys") != NULL)
    fail("first save emitted an unqualified table reference");
  if(integerResult("return settings.keys.__visited__ == nil") != 1)
    fail("first save mutated settings.keys");
  if(integerResult("return _save_output == nil") != 1 ||
     integerResult("return _save_tables == nil") != 1)
    fail("serializer retained per-call state");

  if(chmod(preferences_path, 0640) != 0)
    fail("cannot set test preference mode: %s", strerror(errno));
  saveSettings();
  second = readFile(preferences_path, &second_length);
  if(first_length != second_length || memcmp(first, second, first_length) != 0)
    fail("two saves from unchanged settings produced different content");
  if(stat(preferences_path, &status) != 0 ||
     (status.st_mode & 0777) != 0640)
    fail("atomic replacement did not preserve the target mode");

  validateReload(first);
  validateReload(second);
  printf("PASS: two saves reload with strings, unknowns, cycles, and all bindings intact\n");

  free(first);
  free(second);
}

static int refreshMouseYSettings(lua_State *state) {
  (void)state;
  updateSettingsCache();
  return 0;
}

static void loadMouseYMenu(void) {
  static const char *scripts[] = {
    "artpack.lua", "menu.lua", "menu_functions.lua"
  };
  unsigned i;

  scripting_Register("c_update_settings_cache", refreshMouseYSettings);
  for(i = 0; i < sizeof(scripts) / sizeof(scripts[0]); i++) {
    char *path = getPath(PATH_SCRIPTS, scripts[i]);
    checkedRunFile(path);
    free(path);
  }
}

static void testMouseYPreference(void) {
  int expected;

  resetLua();
  if(getSettingi("invert_mouse_y") != 0)
    fail("fresh configuration did not default to normal mouse Y");
  /* A valid older profile lacks this field and overlays the current defaults. */
  checkedRun("settings.version = 0.70; settings.camType = 3; "
             "settings.keys[1].left = 100; save_completed = 1");
  loadMouseYMenu();
  updateSettingsCache();
  if(getSettingi("invert_mouse_y") != 0 || gSettingsCache.invert_mouse_y != 0)
    fail("older profile did not inherit normal mouse Y");
  expectString("return Menu.InvertMouseY.parent", "GameSettingsMenu");
  expectString("return Menu.GameSettingsMenu.items[2]", "CameraMode");
  expectString("return Menu.GameSettingsMenu.items[3]", "InvertMouseY");
  expectString("return Menu.InvertMouseY.caption", "Invert Mouse Y");
  expectString("return GetMenuValueString('InvertMouseY')", "off");

  for(expected = 1; expected >= 0; expected--) {
    char *serialized;
    size_t length;

    checkedRun("MenuAction[MenuC.type.list]('InvertMouseY')");
    if(getSettingi("invert_mouse_y") != expected ||
       gSettingsCache.invert_mouse_y != expected)
      fail("mouse Y menu toggle did not update settings and cache immediately");
    expectString("return GetMenuValueString('InvertMouseY')", expected ? "on" : "off");
    saveSettings();
    serialized = readFile(preferences_path, &length);
    resetLua();
    checkedRun(serialized);
    free(serialized);
    loadMouseYMenu();
    updateSettingsCache();
    if(getSettingi("invert_mouse_y") != expected ||
       gSettingsCache.invert_mouse_y != expected)
      fail("mouse Y preference changed across save and reload");
    if(getSettingi("camType") != 3 ||
       strictIntegerResult("return settings.keys[1].left") != 100)
      fail("mouse Y preference changed an existing camera mode or binding");
    if(integerResult("return save_completed") != 1)
      fail("mouse Y preferences save did not complete");
  }
  printf("PASS: normal mouse Y default, immediate menu toggles, and both saved states\n");
}

static void testOldTargetSurvivesFailures(void) {
  struct rlimit original_limit;
  struct rlimit blocked_limit;
  void (*original_handler)(int);
  char *protected_contents;
  size_t protected_length;

  installFixture();
  saveSettings();
  protected_contents = readFile(preferences_path, &protected_length);
  if(strstr(protected_contents, "987654321") != NULL)
    fail("write-failure sentinel was already present in the old target");

  checkedRun("settings.unknown_number = 987654321");
  checkedRun("function foreach(table, callback) error(\"forced save failure\") end");
  saveSettings();
  expectFileEquals(protected_contents, protected_length,
                   "a serialization failure");

  installFixture();
  checkedRun("settings.unknown_number = 987654321");
  if(getrlimit(RLIMIT_FSIZE, &original_limit) != 0)
    fail("cannot read test file-size limit: %s", strerror(errno));
  blocked_limit = original_limit;
  blocked_limit.rlim_cur = 0;
  original_handler = signal(SIGXFSZ, SIG_IGN);
  if(original_handler == SIG_ERR)
    fail("cannot ignore SIGXFSZ for the write-failure test");
  if(setrlimit(RLIMIT_FSIZE, &blocked_limit) != 0)
    fail("cannot install test file-size limit: %s", strerror(errno));
  saveSettings();
  if(setrlimit(RLIMIT_FSIZE, &original_limit) != 0)
    fail("cannot restore test file-size limit: %s", strerror(errno));
  if(signal(SIGXFSZ, original_handler) == SIG_ERR)
    fail("cannot restore SIGXFSZ handling");
  expectFileEquals(protected_contents, protected_length,
                   "an atomic write/flush failure");

  {
    DIR *directory = opendir(preferences_directory);
    struct dirent *entry;

    if(directory == NULL)
      fail("cannot inspect temporary preferences directory");
    while((entry = readdir(directory)) != NULL) {
      if(strstr(entry->d_name, ".tmp.") != NULL)
        fail("failed atomic save left a temporary file behind");
    }
    if(closedir(directory) != 0)
      fail("cannot close temporary preferences directory");
  }

  free(protected_contents);
  printf("PASS: serialization and write/flush failures retain the old preferences file\n");
}

int main(int argc, char **argv) {
  int size;

  if(argc != 3)
    fail("usage: %s REPOSITORY TEMP_PREFERENCES_DIRECTORY", argv[0]);
  size = snprintf(repository_path, sizeof(repository_path), "%s", argv[1]);
  if(size < 0 || (size_t)size >= sizeof(repository_path))
    fail("repository path is too long");
  size = snprintf(preferences_directory, sizeof(preferences_directory),
                  "%s", argv[2]);
  if(size < 0 || (size_t)size >= sizeof(preferences_directory))
    fail("temporary preferences path is too long");
  size = snprintf(preferences_path, sizeof(preferences_path), "%s/.gltronrc",
                  preferences_directory);
  if(size < 0 || (size_t)size >= sizeof(preferences_path))
    fail("temporary preference filename is too long");

  testResultHelpers();
  testRepeatedSaveAndReload();
  testMouseYPreference();
  testOldTargetSurvivesFailures();

  if(scripting_active)
    scripting_Quit();
  printf("All settings persistence regressions passed.\n");
  return EXIT_SUCCESS;
}
