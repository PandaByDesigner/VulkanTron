if(NOT GLTRON_SDL_BACKEND STREQUAL "SDL1")
  add_executable(native-game-smoke tests/native_game_smoke.c)
  target_link_libraries(native-game-smoke PRIVATE gltron_game gltron_lua
    gltron_options PNG::PNG ${GLTRON_AUDIO_LIBRARIES})
  gltron_enable_test_assertions(native-game-smoke)
  add_executable(video-platform-regression tests/video_platform_regression.c
    nebu/video/video_system.c src/video/screenshot.c)
  target_link_libraries(video-platform-regression PRIVATE gltron_options PNG::PNG)
  gltron_enable_test_assertions(video-platform-regression)
  if(BUILD_TESTING AND GLTRON_ENABLE_GRAPHICS_TESTS)
    add_test(NAME video-platform-regression COMMAND video-platform-regression)
    set_tests_properties(video-platform-regression PROPERTIES TIMEOUT 90 LABELS graphics RUN_SERIAL TRUE)
    set(GLTRON_TEST_ARTPACKS default)
    if(EXISTS "${PROJECT_SOURCE_DIR}/art/faithful/artpack.lua")
      list(APPEND GLTRON_TEST_ARTPACKS faithful)
    endif()
    foreach(artpack IN LISTS GLTRON_TEST_ARTPACKS)
      set(state_dir "${PROJECT_BINARY_DIR}/test-state/native/${artpack}")
      add_test(NAME native-game-smoke-${artpack} COMMAND "${CMAKE_COMMAND}"
        "-DSMOKE=$<TARGET_FILE:native-game-smoke>" "-DSTATE_ROOT=${state_dir}"
        "-DASSET_ROOT=${PROJECT_SOURCE_DIR}" "-DARTPACK=${artpack}"
        -P "${PROJECT_SOURCE_DIR}/cmake/RunNativeSmoke.cmake")
      set_tests_properties(native-game-smoke-${artpack} PROPERTIES
        TIMEOUT 120 LABELS graphics RUN_SERIAL TRUE)
    endforeach()
  endif()
endif()
