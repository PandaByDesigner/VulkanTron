option(VULKANTRON_BUILD_DIRECT_RENDERER "Build VulkanTron and its direct Vulkan development tools" ON)
if(NOT VULKANTRON_BUILD_DIRECT_RENDERER)
  return()
endif()
if(NOT GLTRON_SDL_BACKEND STREQUAL "SDL3")
  message(STATUS "VulkanTron requires SDL3; only the OpenGL reference is built with this preset")
  return()
endif()

set(VULKANTRON_VERSION "0.3.0")
find_package(Vulkan 1.3 REQUIRED)
find_program(VULKANTRON_GLSLC glslc REQUIRED)
find_program(VULKANTRON_SPIRV_VAL spirv-val REQUIRED)
set(VULKANTRON_SHADER_DIR "${PROJECT_BINARY_DIR}/bin/vulkantron-shaders")
set(VULKANTRON_SHADER_OUTPUTS)
foreach(shader_name scene.vert scene.frag faithful.vert faithful.frag bloom.vert bloom.frag)
  set(shader "${PROJECT_SOURCE_DIR}/vulkantron/shaders/${shader_name}")
  set(output "${VULKANTRON_SHADER_DIR}/${shader_name}.spv")
  add_custom_command(OUTPUT "${output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VULKANTRON_SHADER_DIR}"
    COMMAND "${VULKANTRON_GLSLC}" --target-env=vulkan1.3 "${shader}" -o "${output}"
    COMMAND "${VULKANTRON_SPIRV_VAL}" --target-env vulkan1.3 "${output}"
    DEPENDS "${shader}" VERBATIM)
  list(APPEND VULKANTRON_SHADER_OUTPUTS "${output}")
endforeach()
add_custom_target(vulkantron-shaders DEPENDS ${VULKANTRON_SHADER_OUTPUTS})

# The Vulkan program consumes production simulation and camera code, but does
# not link gltron_options: that interface intentionally links the OpenGL reference.
add_library(vulkantron-classic STATIC
  vulkantron/classic_bridge.c
  src/game/engine.c src/game/event.c src/game/computer.c
  src/game/computer_utilities.c src/game/globals.c src/game/camera.c
  src/video/player_visual.c nebu/base/vector.c nebu/base/random.c nebu/base/util.c)
target_include_directories(vulkantron-classic PUBLIC
  "${PROJECT_SOURCE_DIR}/vulkantron"
  PRIVATE "${PROJECT_SOURCE_DIR}/src/include" "${PROJECT_SOURCE_DIR}/nebu/include"
  "${PROJECT_SOURCE_DIR}/nebu/include/scripting" "${PROJECT_SOURCE_DIR}/lua/include"
  "${GLTRON_SDL3_INCLUDE_DIR}")
target_compile_definitions(vulkantron-classic PRIVATE GLTRON_USE_SDL3=1)
target_link_libraries(vulkantron-classic PRIVATE PkgConfig::GLTRON_SDL gltron_instrumentation)
if(UNIX)
  target_link_libraries(vulkantron-classic PUBLIC m)
endif()

add_executable(vulkantron-lab vulkantron/main.cpp vulkantron/renderer.cpp vulkantron/scene.cpp)
set_target_properties(vulkantron-lab PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
target_compile_definitions(vulkantron-lab PRIVATE
  VT_SOURCE_ROOT="${PROJECT_SOURCE_DIR}" VT_VERSION="0.1.0-dev")
target_link_libraries(vulkantron-lab PRIVATE vulkantron-classic Vulkan::Vulkan
  PkgConfig::GLTRON_SDL PNG::PNG gltron_instrumentation)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vulkantron-lab PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_dependencies(vulkantron-lab vulkantron-shaders)

if(BUILD_TESTING)
  add_executable(vulkantron-scene-test vulkantron/scene_test.cpp vulkantron/scene.cpp)
  set_target_properties(vulkantron-scene-test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
  target_link_libraries(vulkantron-scene-test PRIVATE gltron_instrumentation)
  add_test(NAME vulkantron-scene COMMAND vulkantron-scene-test "${PROJECT_SOURCE_DIR}")
  add_executable(vulkantron-classic-test vulkantron/classic_bridge_test.c)
  target_link_libraries(vulkantron-classic-test PRIVATE vulkantron-classic gltron_instrumentation)
  gltron_enable_test_assertions(vulkantron-classic-test)
  add_test(NAME vulkantron-classic-bridge COMMAND vulkantron-classic-test)
  add_test(NAME vulkantron-headless-check COMMAND vulkantron-lab --self-test)
  set_tests_properties(vulkantron-classic-bridge vulkantron-headless-check vulkantron-scene
    PROPERTIES TIMEOUT 30 LABELS vulkantron)
endif()

install(FILES ${VULKANTRON_SHADER_OUTPUTS}
  DESTINATION "${CMAKE_INSTALL_DATADIR}/vulkantron/shaders")

# The complete game keeps its production simulation, menus, asset loaders,
# input and audio. Only graphics calls/window context creation change.
add_library(vulkantron_options INTERFACE)
get_target_property(VT_GAME_INCLUDES gltron_options INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(VT_GAME_DEFINITIONS gltron_options INTERFACE_COMPILE_DEFINITIONS)
get_target_property(VT_GAME_WARNINGS gltron_options INTERFACE_COMPILE_OPTIONS)
list(FILTER VT_GAME_DEFINITIONS EXCLUDE REGEX "^(PACKAGE|VERSION)=")
target_include_directories(vulkantron_options INTERFACE ${VT_GAME_INCLUDES}
  "${PROJECT_SOURCE_DIR}/vulkantron")
target_compile_definitions(vulkantron_options INTERFACE ${VT_GAME_DEFINITIONS}
  GLTRON_DIRECT_VULKAN=1 PACKAGE="vulkantron" VERSION="${VULKANTRON_VERSION}" VT_VERSION="${VULKANTRON_VERSION}")
if(VT_GAME_WARNINGS)
  target_compile_options(vulkantron_options INTERFACE ${VT_GAME_WARNINGS})
endif()
target_link_libraries(vulkantron_options INTERFACE PkgConfig::GLTRON_SDL gltron_instrumentation)
if(UNIX)
  target_link_libraries(vulkantron_options INTERFACE m)
endif()
add_library(vulkantron_game OBJECT ${GLTRON_GAME_SOURCES})
target_link_libraries(vulkantron_game PRIVATE vulkantron_options PNG::PNG ${GLTRON_AUDIO_LIBRARIES})
add_library(vulkantron_faithful_renderer STATIC
  vulkantron/renderer.cpp vulkantron/fixed_function.cpp vulkantron/faithful_platform.cpp)
set_target_properties(vulkantron_faithful_renderer PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(vulkantron_faithful_renderer PRIVATE vulkantron_options Vulkan::Vulkan PNG::PNG)
add_dependencies(vulkantron_faithful_renderer vulkantron-shaders)
if(BUILD_TESTING)
  add_executable(vulkantron-fixed-function-test
    vulkantron/fixed_function_test.cpp vulkantron/fixed_function.cpp)
  set_target_properties(vulkantron-fixed-function-test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
  target_link_libraries(vulkantron-fixed-function-test PRIVATE PkgConfig::GLTRON_SDL gltron_instrumentation)
  gltron_enable_test_assertions(vulkantron-fixed-function-test)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vulkantron-fixed-function-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vulkantron-fixed-function COMMAND vulkantron-fixed-function-test)
  set_tests_properties(vulkantron-fixed-function PROPERTIES TIMEOUT 30 LABELS vulkantron)
  add_executable(vulkantron-platform-test vulkantron/faithful_platform_test.cpp
    vulkantron/faithful_platform.cpp vulkantron/fixed_function.cpp)
  set_target_properties(vulkantron-platform-test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
  target_include_directories(vulkantron-platform-test PRIVATE "${PROJECT_SOURCE_DIR}/vulkantron")
  target_link_libraries(vulkantron-platform-test PRIVATE PkgConfig::GLTRON_SDL gltron_instrumentation)
  gltron_enable_test_assertions(vulkantron-platform-test)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vulkantron-platform-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vulkantron-platform COMMAND vulkantron-platform-test)
  set_tests_properties(vulkantron-platform PROPERTIES TIMEOUT 30 LABELS vulkantron)
endif()
add_executable(vulkantron vulkantron/faithful_main.c)
target_link_libraries(vulkantron PRIVATE vulkantron_game gltron_lua
  vulkantron_faithful_renderer vulkantron_options PNG::PNG ${GLTRON_AUDIO_LIBRARIES})
set_target_properties(vulkantron PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

if(GLTRON_BUILD_NATIVE_TESTS OR GLTRON_ENABLE_GRAPHICS_TESTS)
  add_executable(vulkantron-bloom-native-test tests/vulkantron_bloom_native.cpp)
  set_target_properties(vulkantron-bloom-native-test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
  target_link_libraries(vulkantron-bloom-native-test PRIVATE
    vulkantron_faithful_renderer vulkantron_options PNG::PNG)
  add_executable(vulkantron-faithful-smoke tests/vulkantron_faithful_smoke.c)
  target_link_libraries(vulkantron-faithful-smoke PRIVATE vulkantron_game gltron_lua
    vulkantron_faithful_renderer vulkantron_options PNG::PNG ${GLTRON_AUDIO_LIBRARIES})
  target_link_options(vulkantron-faithful-smoke PRIVATE -Wl,--wrap=SystemGetElapsedTime)
  gltron_enable_test_assertions(vulkantron-faithful-smoke)
  add_executable(vulkantron-reference-smoke tests/vulkantron_faithful_smoke.c)
  target_link_libraries(vulkantron-reference-smoke PRIVATE gltron_game gltron_lua
    gltron_options PNG::PNG ${GLTRON_AUDIO_LIBRARIES})
  target_link_options(vulkantron-reference-smoke PRIVATE -Wl,--wrap=SystemGetElapsedTime)
  gltron_enable_test_assertions(vulkantron-reference-smoke)
endif()

install(TARGETS vulkantron RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
