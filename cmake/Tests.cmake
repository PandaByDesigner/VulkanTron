function(gltron_add_regression name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE gltron_options)
  gltron_enable_test_assertions(${name})
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/tests")
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 60
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_VIDEO_DRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endfunction()

set(GLTRON_GAMEPLAY_TEST_SOURCES
  src/game/engine.c src/game/event.c src/game/computer.c
  src/game/computer_utilities.c src/game/globals.c src/video/player_visual.c
  nebu/base/vector.c nebu/base/random.c nebu/base/util.c)
gltron_add_regression(classic-gameplay-regression
  tests/classic_gameplay_regression.c ${GLTRON_GAMEPLAY_TEST_SOURCES})
gltron_add_regression(local-multiplayer-regression
  tests/local_multiplayer_regression.c ${GLTRON_GAMEPLAY_TEST_SOURCES}
  src/video/video.c src/video/display_layout.c)
gltron_add_regression(camera-regression
  tests/camera_regression.c src/game/camera.c src/game/globals.c)
gltron_add_regression(display-layout-test
  tests/display_layout_test.c src/game/globals.c src/video/display_layout.c)
gltron_add_regression(hud-layout-test
  tests/hud_layout_test.c src/game/globals.c src/video/display_layout.c src/video/hud_layout.c)
gltron_add_regression(input-binding-regression tests/input_binding_regression.c src/input/input.c)
gltron_add_regression(input-translation-test
  tests/input_translation_test.c nebu/input/input_system.c nebu/input/system_keynames.c)

# These harnesses intentionally link selected production functions; discard
# unrelated entry points that normally depend on the rest of the running game.
foreach(test local-multiplayer-regression input-binding-regression)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${test} PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
      target_link_options(${test} PRIVATE -Wl,-dead_strip)
    else()
      target_link_options(${test} PRIVATE -Wl,--gc-sections)
    endif()
  endif()
endforeach()

if(UNIX)
  add_executable(settings-persistence-regression
    tests/settings_persistence_regression.c src/configuration/settings.c nebu/scripting/scripting.c)
  target_link_libraries(settings-persistence-regression PRIVATE gltron_options gltron_lua)
  gltron_enable_test_assertions(settings-persistence-regression)
  file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/test-state/settings")
  add_test(NAME settings-persistence-regression COMMAND
    settings-persistence-regression "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/test-state/settings")
  set_tests_properties(settings-persistence-regression PROPERTIES TIMEOUT 60)

  add_executable(resource-loading-regression tests/resource_loading_test.c
    src/video/fonts.c src/video/fonttex.c src/video/load_texture.c
    nebu/video/png_texture.c nebu/filesystem/file_io.c)
  target_link_libraries(resource-loading-regression PRIVATE gltron_options PNG::PNG)
  gltron_enable_test_assertions(resource-loading-regression)
  file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/test-state/resources")
  add_test(NAME resource-loading-regression COMMAND resource-loading-regression
    "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/test-state/resources")
  set_tests_properties(resource-loading-regression PROPERTIES TIMEOUT 60)

  add_executable(artpack-enumeration-regression tests/artpack_enumeration_test.c
    src/video/artpack.c nebu/scripting/scripting.c)
  target_link_libraries(artpack-enumeration-regression PRIVATE gltron_options gltron_lua)
  gltron_enable_test_assertions(artpack-enumeration-regression)
  add_test(NAME artpack-enumeration-regression COMMAND artpack-enumeration-regression
    "${PROJECT_SOURCE_DIR}")
  set_tests_properties(artpack-enumeration-regression PROPERTIES TIMEOUT 60)

  add_executable(music-enumeration-regression tests/music_enumeration_test.c
    src/audio/sound.c nebu/scripting/scripting.c)
  target_link_libraries(music-enumeration-regression PRIVATE gltron_options gltron_lua)
  gltron_enable_test_assertions(music-enumeration-regression)
  target_compile_options(music-enumeration-regression PRIVATE -ffunction-sections -fdata-sections)
  if(APPLE)
    target_link_options(music-enumeration-regression PRIVATE -Wl,-dead_strip)
  else()
    target_link_options(music-enumeration-regression PRIVATE -Wl,--gc-sections)
  endif()
  add_test(NAME music-enumeration-regression COMMAND music-enumeration-regression
    "${PROJECT_SOURCE_DIR}")
  set_tests_properties(music-enumeration-regression PROPERTIES TIMEOUT 60)

  gltron_add_regression(trail-geometry-regression tests/trail_geometry_regression.c
    src/video/trail.c src/video/trail_geometry.c src/video/trail_render.c nebu/base/vector.c)
endif()

if(GLTRON_ENABLE_AUDIO)
  set(GLTRON_AUDIO_TEST_SOURCES
    nebu/audio/SoundSystem.cpp nebu/audio/Source.cpp nebu/audio/SourceCopy.cpp
    nebu/audio/SourceMusic.cpp nebu/audio/SourceSample.cpp
    nebu/audio/Source3D.cpp nebu/audio/SourceEngine.cpp)
  add_executable(audio-production-parity tests/audio_production_parity.cpp ${GLTRON_AUDIO_TEST_SOURCES})
  target_link_libraries(audio-production-parity PRIVATE gltron_options ${GLTRON_AUDIO_LIBRARIES})
  gltron_enable_test_assertions(audio-production-parity)
  add_test(NAME audio-production-parity COMMAND "${CMAKE_COMMAND}"
    "-DPROBE=$<TARGET_FILE:audio-production-parity>" "-DASSET_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/VerifyAudio.cmake")
  add_executable(audio-source-list-stress tests/audio_source_list_stress.cpp ${GLTRON_AUDIO_TEST_SOURCES})
  target_link_libraries(audio-source-list-stress PRIVATE gltron_options ${GLTRON_AUDIO_LIBRARIES})
  gltron_enable_test_assertions(audio-source-list-stress)
  set(GLTRON_STRESS_ASSETS "${PROJECT_SOURCE_DIR}/music/song_revenge_of_cats.it")
  if(GLTRON_SDL_BACKEND STREQUAL "SDL1")
    list(APPEND GLTRON_STRESS_ASSETS "${PROJECT_SOURCE_DIR}/data/game_crash.wav")
  endif()
  add_test(NAME audio-source-list-stress COMMAND audio-source-list-stress 100000
    ${GLTRON_STRESS_ASSETS})
  set_tests_properties(audio-production-parity audio-source-list-stress PROPERTIES
    TIMEOUT 120 ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
  if(UNIX AND NOT GLTRON_SDL_BACKEND STREQUAL "SDL1")
    add_executable(audio-wav-music-regression tests/audio_wav_music_test.cpp
      ${GLTRON_AUDIO_TEST_SOURCES})
    target_link_libraries(audio-wav-music-regression PRIVATE gltron_options ${GLTRON_AUDIO_LIBRARIES})
    gltron_enable_test_assertions(audio-wav-music-regression)
    add_test(NAME audio-wav-music-regression COMMAND audio-wav-music-regression)
    set_tests_properties(audio-wav-music-regression PROPERTIES TIMEOUT 60
      ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
    add_executable(audio-presentation-regression tests/audio_presentation_test.cpp
      ${GLTRON_AUDIO_TEST_SOURCES})
    target_link_libraries(audio-presentation-regression PRIVATE gltron_options ${GLTRON_AUDIO_LIBRARIES})
    gltron_enable_test_assertions(audio-presentation-regression)
    add_test(NAME audio-presentation-regression COMMAND audio-presentation-regression
      "${PROJECT_SOURCE_DIR}")
    set_tests_properties(audio-presentation-regression PROPERTIES TIMEOUT 60
      ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
  endif()
endif()

if(GLTRON_ENABLE_REFERENCE_TESTS)
  foreach(script run_sdl_backend_regression run_audio_decoder_parity run_audio_production_parity)
    add_test(NAME reference-${script} COMMAND sh "${PROJECT_SOURCE_DIR}/tests/${script}.sh")
    set_tests_properties(reference-${script} PROPERTIES TIMEOUT 180 LABELS reference)
  endforeach()
endif()
