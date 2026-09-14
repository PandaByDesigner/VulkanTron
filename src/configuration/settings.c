#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef HAVE_MKSTEMP
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "base/util.h"
#include "game/gltron.h"
#include "filesystem/path.h"

#define BUFSIZE 100
#define MAX_VAR_NAME_LEN 64

void checkSettings(void) {
  /* sanity check: speed, grid_size */
  if(getSettingf("speed") <= 0) {
    fprintf(stderr, "[gltron] sanity check failed: speed = %.2ff\n",
	    getSettingf("speed"));
    setSettingf("speed", 6.0);
    fprintf(stderr, "[gltron] reset speed: speed = %.2f\n",
	    getSettingf("speed"));
  }
  if(getSettingi("grid_size") % 8) {
    fprintf(stderr, "[gltron] sanity check failed: grid_size %% 8 != 0: "
	    "grid_size = %d\n", getSettingi("grid_size"));
    setSettingi("grid_size", 240);
    fprintf(stderr, "[gltron] reset grid_size: grid_size = %d\n",
	    getSettingi("grid_size"));
  }
}

static int writeSettingsAtomically(const char *path, const char *contents,
                                   size_t length) {
#ifdef HAVE_MKSTEMP
  static const char suffix[] = ".tmp.XXXXXX";
  struct stat target_status;
  FILE *output = NULL;
  char *temporary = NULL;
  size_t path_length;
  size_t written = 0;
  int descriptor = -1;
  int have_target = 0;
  int saved_errno = 0;

  if(path == NULL || path[0] == '\0' || (contents == NULL && length != 0)) {
    errno = EINVAL;
    return 1;
  }

  if(stat(path, &target_status) == 0) {
    have_target = 1;
  } else if(errno != ENOENT) {
    saved_errno = errno;
    goto failed;
  }

  path_length = strlen(path);
  if(path_length > (size_t)-1 - sizeof(suffix)) {
    saved_errno = ENAMETOOLONG;
    goto failed;
  }
  temporary = malloc(path_length + sizeof(suffix));
  if(temporary == NULL) {
    saved_errno = ENOMEM;
    goto failed;
  }
  if(snprintf(temporary, path_length + sizeof(suffix), "%s%s",
              path, suffix) != (int)(path_length + sizeof(suffix) - 1)) {
    saved_errno = ENAMETOOLONG;
    goto failed;
  }

  descriptor = mkstemp(temporary);
  if(descriptor < 0) {
    saved_errno = errno;
    goto failed;
  }
  if(have_target &&
     fchmod(descriptor, target_status.st_mode & (mode_t)07777) != 0) {
    saved_errno = errno;
    goto failed;
  }

  output = fdopen(descriptor, "wb");
  if(output == NULL) {
    saved_errno = errno;
    goto failed;
  }
  descriptor = -1;

  while(written < length) {
    size_t count = fwrite(contents + written, 1, length - written, output);
    written += count;
    if(count == 0 || ferror(output)) {
      saved_errno = errno != 0 ? errno : EIO;
      goto failed;
    }
  }
  if(fflush(output) != 0) {
    saved_errno = errno;
    goto failed;
  }
  while(fsync(fileno(output)) != 0) {
    if(errno != EINTR) {
      saved_errno = errno;
      goto failed;
    }
  }
  if(fclose(output) != 0) {
    output = NULL;
    saved_errno = errno;
    goto failed;
  }
  output = NULL;

  if(rename(temporary, path) != 0) {
    saved_errno = errno;
    goto failed;
  }

  free(temporary);
  return 0;

failed:
  if(output != NULL)
    fclose(output);
  else if(descriptor >= 0)
    close(descriptor);
  if(temporary != NULL) {
    unlink(temporary);
    free(temporary);
  }
  errno = saved_errno != 0 ? saved_errno : EIO;
  fprintf(stderr, "[gltron] cannot save settings to %s: %s\n",
          path != NULL ? path : "(null)", strerror(errno));
  return 1;
#else
  (void)contents;
  (void)length;
  fprintf(stderr,
          "[gltron] cannot save settings to %s: atomic files unavailable\n",
          path != NULL ? path : "(null)");
  return 1;
#endif
}

void saveSettings(void) {
  char *contents = NULL;
  char *path = NULL;
  char *script = getPath(PATH_SCRIPTS, "save.lua");

  if(script == NULL) {
    fprintf(stderr, "[gltron] cannot locate settings serializer\n");
    return;
  }
  if(scripting_RunFileChecked(script) != 0) {
    fprintf(stderr, "[gltron] cannot load settings serializer %s\n", script);
    free(script);
    return;
  }
  free(script);

  if(scripting_RunChecked("return save()") != 0 ||
     scripting_GetStringResult(&contents) != 0) {
    fprintf(stderr, "[gltron] cannot serialize settings\n");
    free(contents);
    return;
  }

#ifdef WIN32
  path = malloc(sizeof(RC_NAME));
  if(path != NULL)
    memcpy(path, RC_NAME, sizeof(RC_NAME));
#else
  path = getPossiblePath(PATH_PREFERENCES, RC_NAME);
#endif
  if(path == NULL) {
    fprintf(stderr, "[gltron] cannot locate preferences file\n");
    free(contents);
    return;
  }

  (void)writeSettingsAtomically(path, contents, strlen(contents));
  free(path);
  free(contents);
}

int getSettingi(const char *name) {
	return (int) getSettingf(name);
}

int getVideoSettingi(const char *name) {
	return (int) getVideoSettingf(name);
}

float getSettingf(const char *name) {
  float value;
  if( scripting_GetGlobal("settings", name, NULL) ) {
    /* does not exit, return default */
    fprintf(stderr, "error accessing setting '%s'!\n", name);
    assert(0);
    return 0;
  }
	if( scripting_GetFloatResult(&value) ) {
		fprintf(stderr, "error reading setting '%s'!\n", name);
		assert(0);
		return 0;
	}
	return value;
}

float getVideoSettingf(const char *name) {
  float value;
  if( scripting_GetGlobal("video", "settings", name, NULL) ) {
    /* does not exit, return default */
    fprintf(stderr, "error accessing setting '%s'!\n", name);
    assert(0);
    return 0;
  }
	if( scripting_GetFloatResult(&value) ) {
		fprintf(stderr, "error reading setting '%s'!\n", name);
		assert(0);
		return 0;
	}
	return value;
}

int isSetting(const char *name) {
	scripting_GetGlobal("settings", name, NULL);
	return ! scripting_IsNilResult();
}

void setSettingf(const char *name, float f) {
  scripting_SetFloat( f, name, "settings", NULL );
}

void setSettingi(const char *name, int i) {
	setSettingf(name, (float)i);
}

void updateSettingsCache(void) {
  gSettingsCache.obsidian_arena = getVideoSettingi("obsidian_arena");
  /* cache lua settings that don't change during play */
  gSettingsCache.use_stencil = getSettingi("use_stencil");
  gSettingsCache.show_scores = getSettingi("show_scores");
  gSettingsCache.show_ai_status = getSettingi("show_ai_status");
  gSettingsCache.ai_level = getSettingi("ai_level");
  gSettingsCache.show_fps = getSettingi("show_fps");
  gSettingsCache.show_console = getSettingi("show_console");
  gSettingsCache.softwareRendering = getSettingi("softwareRendering");
  gSettingsCache.line_spacing = getSettingi("line_spacing");
  gSettingsCache.alpha_trails = getSettingi("alpha_trails");
  gSettingsCache.antialias_lines = getSettingi("antialias_lines");
  gSettingsCache.turn_cycle = getSettingi("turn_cycle"); 
  gSettingsCache.light_cycles = getSettingi("light_cycles"); 
  gSettingsCache.lod = getSettingi("lod"); 
  gSettingsCache.fov = getSettingi("fov"); 

  gSettingsCache.show_floor_texture = getVideoSettingi("show_floor_texture");
  gSettingsCache.show_skybox = getVideoSettingi("show_skybox"); 
  gSettingsCache.show_wall = getVideoSettingi("show_wall");
  gSettingsCache.stretch_textures = getVideoSettingi("stretch_textures"); 
  gSettingsCache.show_decals = getVideoSettingi("show_decals");

  gSettingsCache.show_impact = getSettingi("show_impact");
  gSettingsCache.show_glow = getSettingi("show_glow"); 
  gSettingsCache.show_recognizer = getSettingi("show_recognizer");

  gSettingsCache.fast_finish = getSettingi("fast_finish");
  gSettingsCache.fov = getSettingf("fov");
  gSettingsCache.znear = getSettingf("znear");
  gSettingsCache.camType = getSettingi("camType");
  gSettingsCache.invert_mouse_y = getSettingi("invert_mouse_y");
  gSettingsCache.playEffects = getSettingi("playEffects");
  gSettingsCache.playMusic = getSettingi("playMusic");
	gSettingsCache.map_ratio_w = getSettingf("map_ratio_w");
	gSettingsCache.map_ratio_h = getSettingf("map_ratio_h");

	scripting_GetGlobal("clear_color", NULL);
  scripting_GetFloatArrayResult(gSettingsCache.clear_color, 4);
}
