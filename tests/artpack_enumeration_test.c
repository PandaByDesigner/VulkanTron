#include "video/video.h"
#include "filesystem/path.h"
#include "Nebu_scripting.h"
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern lua_State *L;
static const char *art_directory;
static int art_visual_flag, loaded_visual_flag = -1;
Visual *gScreen;
const char *getDirectory(int location) { assert(location == PATH_ART); return art_directory; }
char *getArtPath(const char *pack, const char *filename) { (void)pack; (void)filename; return NULL; }
void runScript(int location, const char *name) { (void)location; (void)name; }
void initTexture(Visual *display) { (void)display; }
void deleteTextures(Visual *display) { (void)display; }
void initFonts(void) {}
int getVideoSettingi(const char *name) {
  assert(strcmp(name, "obsidian_arena") == 0);
  return art_visual_flag;
}
void reloadLightcycleModels(int obsidian) { loaded_visual_flag = obsidian; }

static char *join(const char *parent, const char *name) {
  size_t size = strlen(parent) + strlen(name) + 2;
  char *result = malloc(size);
  assert(result != NULL);
  snprintf(result, size, "%s/%s", parent, name);
  return result;
}
static void makeFile(const char *path) {
  FILE *file = fopen(path, "wbx");
  assert(file != NULL);
  assert(fputs("-- valid artpack marker\n", file) >= 0);
  assert(fclose(file) == 0);
}
static void makePack(const char *name, int marker_kind) {
  char *directory = join(art_directory, name);
  char *marker = join(directory, "artpack.lua");
  assert(mkdir(directory, 0700) == 0);
  if(marker_kind == 1) makeFile(marker);
  else if(marker_kind == 2) assert(mkdir(marker, 0700) == 0);
  free(marker); free(directory);
}
static int artpackCount(void) {
  int result;
  assert(scripting_RunChecked("return getn(artpacks)") == 0);
  assert(scripting_GetIntegerResult(&result) == 0);
  return result;
}
static void checkSelection(const char *expected) {
  char *value;
  assert(scripting_GetGlobal("settings", "current_artpack", NULL) == 0);
  assert(scripting_GetStringResult(&value) == 0);
  if(strcmp(value, expected) != 0)
    fprintf(stderr, "Selection mismatch: actual '%s', expected '%s'\n", value, expected);
  assert(strcmp(value, expected) == 0);
  free(value);
}

static void checkBadDirectory(const char *directory) {
  pid_t child = fork();
  int status;
  assert(child >= 0);
  if(child == 0) {
    art_directory = directory;
    initArtpacks();
    _exit(0);
  }
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE);
}

int main(int argc, char **argv) {
  static const char unusual_name[] = "quote\" backslash\\ newline\n\303\251";
  const char *valid_names[] = { "default", unusual_name, "Makefile-artpack" };
  char template_path[] = "/tmp/gltron-artpack-enumeration.XXXXXX";
  char *path, *script;
  char *expected_order[3];
  int expected_count = 0, iteration, i;
  DIR *directory;
  struct dirent *entry;
  assert(argc == 2);
  art_directory = mkdtemp(template_path);
  assert(art_directory != NULL);
  makePack("default", 1);
  makePack(unusual_name, 1);
  makePack("Makefile-artpack", 1);
  makePack("unfinished", 0);
  makePack("directory-marker", 2);
  makePack(".hidden-pack", 1);
  path = join(art_directory, "README.txt"); makeFile(path); free(path);
  path = join(art_directory, "artpack.lua"); makeFile(path); free(path);

  /* Preserve the original relative enumeration order, independently of the
   * directory order chosen by the machine/filesystem running this test. */
  directory = opendir(art_directory);
  assert(directory != NULL);
  while((entry = readdir(directory)) != NULL) {
    for(i = 0; i < 3; i++) if(strcmp(entry->d_name, valid_names[i]) == 0) {
      assert(expected_count < 3);
      expected_order[expected_count++] = strdup(entry->d_name);
    }
  }
  closedir(directory);
  assert(expected_count == 3);
  scripting_Init();
  assert(scripting_RunChecked("settings = { current_artpack = 'default' }; artpacks = { 'stale', 'stale2', 'stale3', 'stale4' }") == 0);
  script = join(argv[1], "scripts/video.lua");
  assert(scripting_RunFileChecked(script) == 0);
  free(script);
  /* Art loading applies the model choice in both directions, independently
   * of enumeration order or the prior model pack. */
  art_visual_flag = 1;
  loadArt();
  assert(loaded_visual_flag == 1);
  art_visual_flag = 0;
  reloadArt();
  assert(loaded_visual_flag == 0);
  for(iteration = 0; iteration < 32; iteration++) {
    int top = lua_gettop(L);
    initArtpacks();
    assert(lua_gettop(L) == top);
    assert(artpackCount() == 3);
    checkSelection("default");
    for(i = 0; i < 3; i++) {
      char *name;
      assert(scripting_RunFormatChecked("return artpacks[%d]", i + 1) == 0);
      assert(scripting_GetStringResult(&name) == 0);
      assert(strcmp(name, expected_order[i]) == 0);
      free(name);
    }
  }
  /* Select a filename that cannot safely be embedded as literal Lua source. */
  lua_getglobal(L, "settings");
  lua_pushstring(L, "current_artpack");
  lua_pushstring(L, unusual_name);
  lua_settable(L, -3);
  lua_pop(L, 1);
  initArtpacks();
  checkSelection(unusual_name);
  path = join(art_directory, "Makefile-artpack/artpack.lua");
  assert(unlink(path) == 0); free(path);
  initArtpacks();
  assert(artpackCount() == 2);
  checkSelection(unusual_name);
  /* A missing saved pack falls back to the same first valid entry as before. */
  assert(scripting_RunChecked("settings.current_artpack = 'missing'") == 0);
  initArtpacks();
  checkSelection(strcmp(expected_order[0], "Makefile-artpack") == 0 ?
                   expected_order[1] : expected_order[0]);
  path = join(art_directory, "empty");
  assert(mkdir(path, 0700) == 0);
  checkBadDirectory(path);
  assert(rmdir(path) == 0); free(path);
  path = join(art_directory, "missing-directory");
  checkBadDirectory(path); free(path);
  checkBadDirectory(NULL);
  scripting_Quit();

  for(i = 0; i < 3; i++) free(expected_order[i]);
  directory = opendir(art_directory);
  assert(directory != NULL);
  while((entry = readdir(directory)) != NULL) {
    struct stat info;
    char *marker;
    if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    path = join(art_directory, entry->d_name);
    assert(stat(path, &info) == 0);
    if(S_ISDIR(info.st_mode)) {
      marker = join(path, "artpack.lua");
      if(stat(marker, &info) == 0) {
        if(S_ISDIR(info.st_mode)) assert(rmdir(marker) == 0);
        else assert(unlink(marker) == 0);
      }
      free(marker);
      assert(rmdir(path) == 0);
    } else assert(unlink(path) == 0);
    free(path);
  }
  closedir(directory);
  assert(rmdir(art_directory) == 0);
  puts("PASS: artpack directory/marker filtering, literal filename bytes, selection/order, rescans, and missing-directory handling");
  return 0;
}
