set(GLTRON_LUA_SOURCES
  lua/src/lapi.c lua/src/lcode.c lua/src/ldebug.c lua/src/ldo.c
  lua/src/lfunc.c lua/src/lgc.c lua/src/llex.c lua/src/lmem.c
  lua/src/lobject.c lua/src/lparser.c lua/src/lstate.c lua/src/lstring.c
  lua/src/ltable.c lua/src/ltests.c lua/src/ltm.c lua/src/lundump.c
  lua/src/lvm.c lua/src/lzio.c
  lua/src/lib/lauxlib.c lua/src/lib/lbaselib.c lua/src/lib/ldblib.c
  lua/src/lib/liolib.c lua/src/lib/lmathlib.c lua/src/lib/lstrlib.c)

set(GLTRON_GAME_SOURCES
  src/base/util.c
  src/configuration/settings.c
  src/filesystem/path.c src/filesystem/dirsetup.c
  src/game/camera.c src/game/computer.c src/game/computer_utilities.c
  src/game/credits.c src/game/engine.c src/game/event.c src/game/game.c
  src/game/globals.c src/game/gui.c src/game/init.c src/game/init_sdl.c
  src/game/menu.c src/game/pause.c src/game/timedemo.c
  src/game/switchCallbacks.c src/game/scripting_interface.c
  src/input/input.c
  src/video/artpack.c src/video/display_layout.c src/video/explosion.c
  src/video/fonts.c src/video/fonttex.c src/video/gamegraphics.c
  src/video/graphics_fx.c src/video/graphics_hud.c src/video/graphics_lights.c
  src/video/hud_layout.c src/video/graphics_utility.c src/video/graphics_world.c
  src/video/load_texture.c src/video/material.c src/video/model.c
  src/video/player_visual.c src/video/recognizer.c src/video/screenshot.c
  src/video/skybox.c src/video/texture.c src/video/trail.c
  src/video/trail_geometry.c src/video/trail_render.c src/video/video.c
  src/video/visuals_2d.c
  nebu/base/geom.c nebu/base/vector.c nebu/base/matrix.c
  nebu/base/random.c nebu/base/util.c nebu/base/system.c
  nebu/filesystem/filesystem.c nebu/filesystem/file_io.c
  nebu/filesystem/directory.c nebu/filesystem/findpath.c
  nebu/input/system_keynames.c nebu/input/input_system.c
  nebu/scripting/scripting.c
  nebu/video/console.c nebu/video/pixels.c
  nebu/video/png_texture.c nebu/video/video_system.c)

set(GLTRON_AUDIO_SOURCES
  src/audio/sound.c src/audio/sound_glue.cpp
  nebu/audio/SoundSystem.cpp nebu/audio/Source.cpp nebu/audio/Source3D.cpp
  nebu/audio/SourceCopy.cpp nebu/audio/SourceEngine.cpp
  nebu/audio/SourceMusic.cpp nebu/audio/SourceSample.cpp)
