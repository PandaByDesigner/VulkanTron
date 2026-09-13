#include "filesystem/path.h"
#include "filesystem/dirsetup.h"

#include "Nebu_filesystem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #include <unistd.h>
#ifdef GLTRON_DIRECT_VULKAN
#include "faithful_platform.h"
#endif
#include <limits.h>
#include <unistd.h>

#ifndef PATH_MAX
// #warning PATH_MAX "is not defined in limits.h!"
#define PATH_MAX 255
#endif

static char preferences_dir[PATH_MAX];
static char snapshots_dir[PATH_MAX];
static char data_dir[PATH_MAX];
static char art_dir[PATH_MAX];
static char music_dir[PATH_MAX];
static char scripts_dir[PATH_MAX];

static char executable_path[PATH_MAX];

static void copyPath(char *destination, const char *source) {
  if(source == NULL || snprintf(destination, PATH_MAX, "%s", source) >= PATH_MAX) {
    fprintf(stderr, "[filesystem] path is missing or too long\n");
    exit(EXIT_FAILURE);
  }
}

static void joinPath(char *destination, const char *parent, const char *name) {
  if(snprintf(destination, PATH_MAX, "%s%c%s", parent, SEPARATOR, name) >= PATH_MAX) {
    fprintf(stderr, "[filesystem] asset path is too long\n");
    exit(EXIT_FAILURE);
  }
}

void setExecutablePath(const char *path) {
  copyPath(executable_path, path);
}

static int isAssetRoot(const char *path) {
  char probe[PATH_MAX];
  if(path == NULL || path[0] == '\0') return 0;
  joinPath(probe, path, "scripts/main.lua");
  if(!fileExists(probe)) return 0;
  joinPath(probe, path, "data/fonts.txt");
  return fileExists(probe);
}

static int executableDirectory(char *directory) {
  char resolved[PATH_MAX];
  char *separator;
#if defined(__linux__)
  ssize_t length = readlink("/proc/self/exe", resolved, sizeof(resolved) - 1);
  if(length >= 0) resolved[length] = '\0';
  else
#endif
  if(realpath(executable_path, resolved) == NULL) return 0;
  separator = strrchr(resolved, '/');
  if(separator == NULL) return 0;
  *separator = '\0';
  copyPath(directory, resolved);
  return 1;
}

void initDirectories(void) {
  const char *override = getenv("GLTRON_DATA_DIR");
  const char *config = getenv("GLTRON_CONFIG_DIR");
  const char *screenshots = getenv("GLTRON_SCREENSHOT_DIR");
  char root[PATH_MAX] = "";
  char executable_dir[PATH_MAX];
#ifdef GLTRON_INSTALL_DATA_SUBDIR
  char candidate[PATH_MAX];
#endif

#ifdef GLTRON_DIRECT_VULKAN
  const char *vulkan_data = getenv("VULKANTRON_DATA_DIR");
  const char *vulkan_config = getenv("VULKANTRON_CONFIG_DIR");
  const char *vulkan_screenshots = getenv("VULKANTRON_SCREENSHOT_DIR");
  if(vulkan_data != NULL && vulkan_data[0]) override = vulkan_data;
  /* A renderer comparison may have classic overrides in its parent shell.
   * VulkanTron profiles are selected only through its own application names. */
  config = vulkan_config;
  screenshots = vulkan_screenshots;
  if(config == NULL || !config[0]) config = VT_FaithfulDefaultDirectory(0);
  if(screenshots == NULL || !screenshots[0]) screenshots = VT_FaithfulDefaultDirectory(1);
  if(config == NULL || screenshots == NULL) exit(EXIT_FAILURE);
#endif
  if(config != NULL && config[0] != '\0') copyPath(preferences_dir, config);
  else if(PREF_DIR[0] != '~') copyPath(preferences_dir, PREF_DIR);
  else {
    if(snprintf(preferences_dir, sizeof(preferences_dir), "%s%s", getHome(),
                 PREF_DIR + 1) >= (int)sizeof(preferences_dir)) exit(EXIT_FAILURE);
  }
  if(screenshots != NULL && screenshots[0] != '\0') copyPath(snapshots_dir, screenshots);
  else if(SNAP_DIR[0] != '~') copyPath(snapshots_dir, SNAP_DIR);
  else {
    if(snprintf(snapshots_dir, sizeof(snapshots_dir), "%s%s", getHome(),
                 SNAP_DIR + 1) >= (int)sizeof(snapshots_dir)) exit(EXIT_FAILURE);
  }

  if(override != NULL && override[0] != '\0') {
    if(!isAssetRoot(override)) {
      fprintf(stderr, "[filesystem] GLTRON_DATA_DIR does not contain GLTron assets: %s\n", override);
      exit(EXIT_FAILURE);
    }
    copyPath(root, override);
  }
  if(root[0] == '\0' && executableDirectory(executable_dir)) {
#ifdef GLTRON_INSTALL_DATA_SUBDIR
    joinPath(candidate, executable_dir, "../" GLTRON_INSTALL_DATA_SUBDIR);
    if(isAssetRoot(candidate)) copyPath(root, candidate);
#endif
    if(root[0] == '\0' && isAssetRoot(executable_dir)) copyPath(root, executable_dir);
  }
#ifdef GLTRON_SOURCE_DATA_DIR
  /* Development builds must use their matching scripts, even when an older
   * GLTron is installed. Relocatable installed assets above still win. */
  if(root[0] == '\0' && isAssetRoot(GLTRON_SOURCE_DATA_DIR))
    copyPath(root, GLTRON_SOURCE_DATA_DIR);
#endif
#ifdef LOCAL_DATA
  if(root[0] == '\0' && isAssetRoot(".")) copyPath(root, ".");
#else
  if(root[0] == '\0' && isAssetRoot(DATA_DIR)) copyPath(root, DATA_DIR);
#endif
  if(root[0] == '\0') {
    fprintf(stderr, "[filesystem] Cannot find GLTron assets. Install share/gltron beside bin,\n"
                    "or set GLTRON_DATA_DIR to the directory containing scripts, data and art.\n");
    exit(EXIT_FAILURE);
  }
  joinPath(data_dir, root, "data");
  joinPath(art_dir, root, "art");
  joinPath(scripts_dir, root, "scripts");
  joinPath(music_dir, root, "music");
  fprintf(stderr, "[filesystem] using assets from %s\n", root);
  makeDirectory(preferences_dir);
  makeDirectory(snapshots_dir);
}

char* getPath( int eLocation, const char *filename) {
  char *path = getPossiblePath( eLocation, filename );
  if( fileExists(path) )
    return path;


  fprintf(stderr, "*** failed to locate file '%s' at '%s' (type %d)\n",
	  filename, path, eLocation);
  free(path);
  return NULL;
}

char* getPossiblePath(int eLocation, const char *filename) {
  const char *directory = getDirectory(eLocation);
  size_t length;
  char *path;
  if(directory == NULL || filename == NULL) return NULL;
  length = strlen(directory) + strlen(filename) + 2;
  path = malloc(length);
  if(path != NULL) snprintf(path, length, "%s%c%s", directory, SEPARATOR, filename);
  return path;
}

const char* getDirectory( int eLocation ) {
  switch( eLocation ) {
  case PATH_PREFERENCES: return preferences_dir; break;
  case PATH_SNAPSHOTS: return snapshots_dir; break;
  case PATH_DATA: return data_dir; break;
  case PATH_SCRIPTS: return scripts_dir; break;
  case PATH_MUSIC: return music_dir; break;
  case PATH_ART: return art_dir; break;
  default:
    fprintf(stderr, "invalid path type\n");
    assert(0);
  }
  return NULL;
}
char *getArtPath(const char *artpack, const char *filename) {
  const char *packs[2] = { artpack, "default" };
  int i;
  if(filename == NULL) return NULL;
  for(i = 0; i < 2; i++) {
    size_t length;
    char *path;
    if(packs[i] == NULL) continue;
    length = strlen(art_dir) + strlen(packs[i]) + strlen(filename) + 3;
    path = malloc(length);
    if(path == NULL) return NULL;
    snprintf(path, length, "%s%c%s%c%s", art_dir, SEPARATOR, packs[i],
             SEPARATOR, filename);
    if(fileExists(path)) return path;
    free(path);
  }
  fprintf(stderr, "*** failed to locate art file '%s'\n", filename);
  return NULL;
}
