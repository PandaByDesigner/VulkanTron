#include "game/gltron.h"
#include "filesystem/path.h"
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#define NUM_GAME_FX 3

static char *game_fx_names[] = {

#if 1
  "game_engine.wav",
  "game_crash.wav",
  "game_recognizer.wav"
#else
  "game_engine.ogg",
  "game_crash.ogg",
  "game_recognizer.ogg"
#endif
};

void Sound_loadFX(void) {
  int i;
  char *path;
  const int obsidian = getVideoSettingi("obsidian_arena");


  for(i = 0; i < NUM_GAME_FX; i++) {
    path = NULL;
    if(obsidian) {
      char name[96];
      snprintf(name, sizeof(name), "obsidian/%s", game_fx_names[i]);
      path = getPath(PATH_DATA, name);
    }
    if(path == NULL)
      path = getPath(PATH_DATA, game_fx_names[i]);
    if(path) {
      Audio_LoadSample(path, i);
      free(path);
    } else {
      fprintf(stderr, "[error] can't load sound fx file %s\n",
	     game_fx_names[i]);
      exit(1); // FIXME: handle missing fx somewhere else
    }
  }
  if(obsidian) {
    path = getPath(PATH_DATA, "obsidian/game_boost.wav");
    if(path != NULL) {
      Audio_LoadSample(path, NUM_GAME_FX);
      free(path);
    }
  }
}

void Sound_reloadTrack(void) {
#ifdef GLTRON_NO_SOUND
	return;
#else
  char *song;
  char *path;
	scripting_GetGlobal("settings", "current_track", NULL);
  scripting_GetStringResult(&song);
  fprintf(stderr, "[sound] loading song %s\n", song);
  path = getPath( PATH_MUSIC, song );
  free(song);
  if(path == NULL) {
    fprintf(stderr, "[sound] can't find song...exiting\n");
    exit(1); // FIXME: handle missing song somewhere else
  }
  Sound_load(path);
  Sound_play();

  free(path);
#endif
}

void Sound_shutdown(void) {
  Audio_Quit();
}
  

void Sound_load(char *name) {
  Audio_LoadMusic(name);
}

void Sound_play(void) {
  Audio_SetMusicVolume(getSettingf("musicVolume"));
  Audio_PlayMusic();
  return;
}

void Sound_stop(void) {
  Audio_StopMusic();
}

void Sound_idle(void) {
  Audio_Idle();
}

void Sound_setMusicVolume(float volume) {
  if(volume > 1) volume = 1;
  if(volume < 0) volume = 0;
  Audio_SetMusicVolume(volume);
}

void Sound_setFxVolume(float volume) {
  if(volume > 1) volume = 1;
  if(volume < 0) volume = 0;
  Audio_SetFxVolume(volume);
}

static int musicExtensionSupported(const char *name) {
  const char *dot = strrchr(name, '.');
  char extension[8];
  size_t length, i;
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
  static const char *const supported[] = { "it", "wav" };
#else
  /* Keep SDL_sound's legacy decoder families selectable. Native SDL2/3 uses
     the shipped Impulse Tracker decoder plus recorded PCM16 WAV music. */
  static const char *const supported[] = {
    "it", "mod", "xm", "s3m", "669", "amf", "dsm", "far", "gdm", "imf",
    "m15", "med", "mtm", "okt", "stm", "stx", "ult", "uni",
    "wav", "aiff", "aif", "au", "ogg", "voc", "raw", "shn", "flac", "fla",
    "mp3", "spx"
  };
#endif
  if(dot == NULL || dot == name) return 0;
  length = strlen(++dot);
  if(length == 0 || length >= sizeof(extension)) return 0;
  for(i = 0; i < length; ++i)
    extension[i] = (char)tolower((unsigned char)dot[i]);
  extension[length] = '\0';
  for(i = 0; i < sizeof(supported)/sizeof(supported[0]); ++i)
    if(strcmp(extension, supported[i]) == 0) return 1;
  return 0;
}

static int registerTrack(int index, const char *name) {
  size_t length = strlen(name), i, used = 0;
  char *quoted;
  int result;
  if(length > (SIZE_MAX - 1) / 4) return -1;
  quoted = malloc(length * 4 + 1);
  if(quoted == NULL) return -1;
  /* Match artpack registration: Lua4 decimal escapes preserve filename bytes
     without interpreting quotes, backslashes or control bytes as source. */
  for(i = 0; i < length; ++i) {
    unsigned char byte = (unsigned char)name[i];
    if(byte >= 32 && byte < 127 && byte != '\\' && byte != '"')
      quoted[used++] = (char)byte;
    else {
      snprintf(quoted + used, 5, "\\%03u", (unsigned)byte);
      used += 4;
    }
  }
  quoted[used] = '\0';
  result = scripting_RunFormatChecked("tracks[%d] = \"%s\"", index, quoted);
  free(quoted);
  return result;
}

void Sound_initTracks(void) {
  const char *music_path = getDirectory(PATH_MUSIC);
  DIR *directory = music_path != NULL ? opendir(music_path) : NULL;
  struct dirent *entry;
  int count = 0, failed = 0;
  if(directory == NULL) {
    fprintf(stderr, "[sound] cannot open music directory: %s\n",
            music_path != NULL ? music_path : "(missing)");
    exit(EXIT_FAILURE);
  }
  if(scripting_RunChecked("tracks = {}") != 0) failed = 1;
  /* Retain original readdir order and setupSoundTrack's saved selection. */
  while(!failed && (entry = readdir(directory)) != NULL) {
    char *path;
    struct stat info;
    if(entry->d_name[0] == '.' || !musicExtensionSupported(entry->d_name))
      continue;
    path = getPossiblePath(PATH_MUSIC, entry->d_name);
    if(path != NULL && stat(path, &info) == 0 && S_ISREG(info.st_mode) &&
       access(path, R_OK) == 0) {
      if(count == INT_MAX || registerTrack(count + 1, entry->d_name) != 0)
        failed = 1;
      else
        ++count;
    }
    free(path);
  }
  closedir(directory);
  if(failed || count == 0) {
    fprintf(stderr, "[sound] no supported music files found in '%s'\n", music_path);
    exit(EXIT_FAILURE);
  }
  scripting_Run("setupSoundTrack()");
}

void Sound_setup(void) {
  printf("[sound] initializing sound\n");

  Audio_Init();
  Sound_loadFX();
  Audio_LoadPlayers();
  Sound_setFxVolume(getSettingf("fxVolume"));
  Sound_reloadTrack();
  Sound_setMusicVolume(getSettingf("musicVolume"));
  Audio_Start();
}
