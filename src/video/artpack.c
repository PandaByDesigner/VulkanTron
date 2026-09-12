#include "video/video.h"
#include "filesystem/path.h"
#include "base/util.h"

#include "Nebu_scripting.h"
#include "Nebu_filesystem.h"
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>

/* An art directory can also contain build files and incomplete downloads. */
static int isArtpack(const char *directory, const char *name) {
  static const char suffix[] = "/artpack.lua";
  size_t directory_length = strlen(directory);
  size_t name_length = strlen(name);
  size_t size;
  char *path;
  struct stat info;
  int valid = 0;
  if(name_length > SIZE_MAX - sizeof(suffix) - 1 ||
     directory_length > SIZE_MAX - name_length - sizeof(suffix) - 1)
    return 0;
  size = directory_length + name_length + sizeof(suffix) + 1;
  path = malloc(size);
  if(path == NULL) return 0;
  snprintf(path, size, "%s/%s", directory, name);
  if(stat(path, &info) == 0 && S_ISDIR(info.st_mode)) {
    strcat(path, suffix);
    valid = stat(path, &info) == 0 && S_ISREG(info.st_mode);
  }
  free(path);
  return valid;
}

static int registerArtpack(int index, const char *name) {
  size_t length = strlen(name), i, used = 0;
  char *quoted;
  int result;
  if(length > (SIZE_MAX - 1) / 4) return -1;
  quoted = malloc(length * 4 + 1);
  if(quoted == NULL) return -1;
  /* Lua4 decimal escapes preserve filename bytes without interpreting quotes,
   * backslashes, newlines, or any other filename content as program text. */
  for(i = 0; i < length; i++) {
    unsigned char byte = (unsigned char)name[i];
    if(byte >= 32 && byte < 127 && byte != '\\' && byte != '"')
      quoted[used++] = (char)byte;
    else {
      snprintf(quoted + used, 5, "\\%03u", (unsigned)byte);
      used += 4;
    }
  }
  quoted[used] = '\0';
  result = scripting_RunFormatChecked("artpacks[%d] = \"%s\"", index, quoted);
  free(quoted);
  return result;
}

void initArtpacks(void) {
  const char *art_path = getDirectory(PATH_ART);
  DIR *directory = art_path != NULL ? opendir(art_path) : NULL;
  struct dirent *entry;
  int count = 0, failed = 0;
  if(directory == NULL) {
    fprintf(stderr, "[fatal] cannot open art directory: %s\n",
            art_path != NULL ? art_path : "(missing)");
    exit(EXIT_FAILURE);
  }
  if(scripting_RunChecked("artpacks = {}") != 0) failed = 1;
  /* Retain the previous readdir order and setupArtpacks selection behavior. */
  while(!failed && (entry = readdir(directory)) != NULL) {
    if(entry->d_name[0] != '.' && isArtpack(art_path, entry->d_name)) {
      if(count == INT_MAX || registerArtpack(count + 1, entry->d_name) != 0)
        failed = 1;
      else
        count++;
    }
  }
  closedir(directory);
  if(failed || count == 0) {
    fprintf(stderr, "[fatal] no usable artpacks found in '%s'\n", art_path);
    exit(EXIT_FAILURE);
  }
  scripting_Run("setupArtpacks()");
}

void loadArt(void) {
  char *path;
  char *artpack;

	runScript(PATH_SCRIPTS, "artpack.lua"); // load default art settings

	scripting_GetGlobal("settings", "current_artpack", NULL);
  scripting_GetStringResult(&artpack);
  fprintf(stderr, "[status] loading artpack '%s'\n", artpack);
	
  path = getArtPath(artpack, "artpack.lua");
  free(artpack);

  if(path != NULL) {
    scripting_RunFile(path);
    free(path);
  }

  initTexture(gScreen);
  initFonts();
}

void reloadArt(void) {
  printf("[status] reloading art\n");
  deleteTextures(gScreen);
  loadArt();
}
    
