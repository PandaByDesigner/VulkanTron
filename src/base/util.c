#include "filesystem/path.h"
#include "Nebu_scripting.h"

#include <stdlib.h>
#include <stdio.h>

void runScript(int ePath, const char *name) {
        char *s;
        s = getPath(ePath, name);
        if(s == NULL || scripting_RunFileChecked(s) != 0) {
          fprintf(stderr, "[fatal] cannot run required script %s\n", name);
          free(s);
          exit(EXIT_FAILURE);
        }
        free(s);
}
