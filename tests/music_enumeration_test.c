#include "game/gltron.h"
#include "filesystem/path.h"
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern lua_State *L;
static const char *music_directory;
static int obsidian_enabled, available_effects, effect_load_count;
static const char *effect_names[] = {
  "game_engine.wav", "game_crash.wav", "game_recognizer.wav", "game_boost.wav"
};
int getVideoSettingi(const char *name) {
  assert(strcmp(name, "obsidian_arena") == 0);
  return obsidian_enabled;
}
char *getPath(int location, const char *name) {
  int i;
  assert(location == PATH_DATA);
  if(strncmp(name, "obsidian/", 9) == 0) {
    for(i = 0; i < 4; ++i)
      if(strcmp(name + 9, effect_names[i]) == 0)
        return (available_effects & (1 << i)) ? strdup(name) : NULL;
    assert(0);
  }
  return strdup(name);
}
void Audio_LoadSample(char *name, int number) {
  const int custom = obsidian_enabled && (available_effects & (1 << number));
  assert(number == effect_load_count && number < 4);
  assert(strcmp(name + (custom ? 9 : 0), effect_names[number]) == 0);
  if(custom) assert(strncmp(name, "obsidian/", 9) == 0);
  ++effect_load_count;
}
static void checkEffects(void) {
  for(obsidian_enabled = 0; obsidian_enabled <= 1; ++obsidian_enabled) {
    for(available_effects = 0; available_effects < 16; ++available_effects) {
      effect_load_count = 0;
      Sound_loadFX();
      assert(effect_load_count == 3 + (obsidian_enabled && (available_effects & 8) != 0));
    }
  }
}
const char *getDirectory(int location) {
  assert(location == PATH_MUSIC);
  return music_directory;
}
static char *join(const char *parent, const char *name) {
  size_t size = strlen(parent) + strlen(name) + 2;
  char *result = malloc(size);
  assert(result != NULL);
  snprintf(result, size, "%s/%s", parent, name);
  return result;
}
char *getPossiblePath(int location, const char *name) {
  assert(location == PATH_MUSIC);
  return join(music_directory, name);
}
static void makeFile(const char *name) {
  char *path = join(music_directory, name);
  FILE *file = fopen(path, "wbx");
  assert(file != NULL);
  assert(fputs("fixture for file enumeration only\n", file) >= 0);
  assert(fclose(file) == 0);
  free(path);
}
static int countTracks(void) {
  int count;
  assert(scripting_RunChecked("return getn(tracks)") == 0);
  assert(scripting_GetIntegerResult(&count) == 0);
  return count;
}
static void checkSelection(const char *expected) {
  char *actual;
  assert(scripting_GetGlobal("settings", "current_track", NULL) == 0);
  assert(scripting_GetStringResult(&actual) == 0);
  assert(actual != NULL && strcmp(actual, expected) == 0);
  free(actual);
}
static void badDirectory(const char *name) {
  pid_t child = fork();
  int status;
  assert(child >= 0);
  if(child == 0) {
    music_directory = name;
    Sound_initTracks();
    _exit(0);
  }
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE);
}
int main(int argc, char **argv) {
  static const char unusual[] = "quote\" backslash\\ newline\n\303\251.it";
  static const char injection[] = "quote\"; injected = 1; --.it";
  const char *valid[] = {
    "song.it", "UPPER.IT", unusual, injection, "legacy.wav", "RECORDED.WAV",
#if !defined(GLTRON_SDL2_AUDIO) && !defined(GLTRON_SDL3_AUDIO)
    "legacy.mod", "legacy.ogg", "legacy.mp3"
#endif
  };
  char template_path[] = "/tmp/gltron-music-enumeration.XXXXXX";
  char *path, *script, *expected[sizeof(valid)/sizeof(valid[0])];
  int expected_count = 0, i, iteration;
  DIR *directory;
  struct dirent *entry;
  assert(argc == 2);
  checkEffects();
  music_directory = mkdtemp(template_path);
  assert(music_directory != NULL);
  makeFile("song.it"); makeFile("UPPER.IT"); makeFile(unusual); makeFile(injection);
  makeFile("legacy.mod"); makeFile("legacy.wav"); makeFile("legacy.ogg"); makeFile("legacy.mp3");
  makeFile("RECORDED.WAV"); makeFile(".hidden.wav"); makeFile("track.wav.bak");
  makeFile("Makefile.am"); makeFile("Makefile.in"); makeFile("README.txt");
  makeFile("not-a-track"); makeFile("song.it.bak"); makeFile(".hidden.it");
  path = join(music_directory, "directory.it"); assert(mkdir(path, 0700) == 0); free(path);
  path = join(music_directory, "fifo.it"); assert(mkfifo(path, 0600) == 0); free(path);
  path = join(music_directory, "dangling.it"); assert(symlink("missing-target", path) == 0); free(path);
  directory = opendir(music_directory); assert(directory != NULL);
  while((entry = readdir(directory)) != NULL) {
    for(i = 0; i < (int)(sizeof(valid)/sizeof(valid[0])); ++i) {
      if(strcmp(valid[i], entry->d_name) == 0) {
        assert(expected_count < (int)(sizeof(expected)/sizeof(expected[0])));
        expected[expected_count++] = strdup(entry->d_name);
      }
    }
  }
  closedir(directory);
  assert(expected_count == (int)(sizeof(valid)/sizeof(valid[0])));
  scripting_Init();
  assert(scripting_RunChecked("settings={current_track='song.it'}; tracks={'stale1','stale2','stale3','stale4','stale5','stale6','stale7','stale8','stale9'}; injected=0") == 0);
  script = join(argv[1], "scripts/audio.lua");
  assert(scripting_RunFileChecked(script) == 0); free(script);
  for(iteration = 0; iteration < 32; ++iteration) {
    int top = lua_gettop(L), injected;
    Sound_initTracks();
    assert(lua_gettop(L) == top && countTracks() == expected_count);
    checkSelection("song.it");
    for(i = 0; i < expected_count; ++i) {
      char *name;
      assert(scripting_RunFormatChecked("return tracks[%d]", i + 1) == 0);
      assert(scripting_GetStringResult(&name) == 0);
      assert(strcmp(name, expected[i]) == 0); free(name);
    }
    assert(scripting_RunChecked("return injected") == 0);
    assert(scripting_GetIntegerResult(&injected) == 0 && injected == 0);
  }
  lua_getglobal(L, "settings"); lua_pushstring(L, "current_track");
  lua_pushstring(L, unusual); lua_settable(L, -3); lua_pop(L, 1);
  Sound_initTracks(); checkSelection(unusual);
  path = join(music_directory, "UPPER.IT"); assert(unlink(path) == 0); free(path);
  Sound_initTracks(); assert(countTracks() == expected_count - 1); checkSelection(unusual);
  assert(scripting_RunChecked("settings.current_track='missing.it'") == 0);
  Sound_initTracks();
  checkSelection(strcmp(expected[0], "UPPER.IT") == 0 ? expected[1] : expected[0]);
  path = join(music_directory, "empty"); assert(mkdir(path, 0700) == 0);
  badDirectory(path); assert(rmdir(path) == 0); free(path);
  path = join(music_directory, "missing"); badDirectory(path); free(path);
  badDirectory(NULL);
  scripting_Quit();
  for(i = 0; i < expected_count; ++i) free(expected[i]);
  directory = opendir(music_directory); assert(directory != NULL);
  while((entry = readdir(directory)) != NULL) {
    struct stat info;
    if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    path = join(music_directory, entry->d_name); assert(lstat(path, &info) == 0);
    if(S_ISDIR(info.st_mode)) assert(rmdir(path) == 0);
    else assert(unlink(path) == 0);
    free(path);
  }
  closedir(directory); assert(rmdir(music_directory) == 0);
  puts("PASS: supported music formats, regular-file filtering, literal filename bytes, selection/order, rescans, directory failures and effect-pack fallback");
  return 0;
}
