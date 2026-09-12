#include "filesystem/nebu_filesystem.h"

#include <stdio.h>

void initFilesystem(int argc, const char *argv[]) {
	dirSetup(argv[0]);
}

int fileExists(const char *path) {
  FILE *f;
  if(path == NULL) return 0;
  if((f = fopen(path, "r"))) {
    fclose(f);
    return 1;
  }
  return 0;
}
