#include "video/video.h"
#include "filesystem/path.h"
#include "Nebu_filesystem.h"

void initFonts(void) {
  char *path = getPath(PATH_DATA, "fonts.txt");
  file_handle file = NULL;
  char buf[256];
  char gamefont[100] = "";
  char guifont[100] = "";
  FontTex *newgame = NULL, *newgui = NULL;
  int valid = 1;

  if(path != NULL) {
    file = file_open(path, "r");
    free(path);
  }
  if(file == NULL) goto fail;
  while(file_gets(file, buf, sizeof(buf)) != NULL) {
    char value[100], extra;
    size_t length = strlen(buf);
    if(length == sizeof(buf) - 1 && buf[length - 1] != '\n') {
      valid = 0;
      break;
    }
    if(strncmp(buf, "game:", 5) == 0) {
      if(sscanf(buf, "game: %99s %c", value, &extra) != 1) {
        valid = 0;
        break;
      }
      strcpy(gamefont, value);
    } else if(strncmp(buf, "menu:", 5) == 0) {
      if(sscanf(buf, "menu: %99s %c", value, &extra) != 1) {
        valid = 0;
        break;
      }
      strcpy(guifont, value);
    }
  }
  file_close(file);
  if(!valid || gamefont[0] == '\0' || guifont[0] == '\0') goto fail;
  newgame = ftxLoadFont(gamefont);
  if(newgame == NULL) goto fail;
  newgui = ftxLoadFont(guifont);
  if(newgui == NULL) goto fail;

  /* Keep the previous pair alive until both replacements are ready. */
  deleteFonts();
  gameFtx = newgame;
  guiFtx = newgui;
  return;

fail:
  ftxUnloadFont(newgame);
  ftxUnloadFont(newgui);
  fprintf(stderr, "can't load complete font definitions from fonts.txt\n");
  if(gameFtx == NULL || guiFtx == NULL)
    exit(1); /* Critical: no previous working font pair is available. */
}

void deleteFonts(void) {
  ftxUnloadFont(gameFtx);
  gameFtx = NULL;
  ftxUnloadFont(guiFtx);
  guiFtx = NULL;
}
