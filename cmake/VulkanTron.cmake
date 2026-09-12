option(VULKANTRON_BUILD_DIRECT_RENDERER "Build the direct Vulkan development slice" ON)
if(NOT VULKANTRON_BUILD_DIRECT_RENDERER)
  return()
endif()
if(NOT GLTRON_SDL_BACKEND STREQUAL "SDL3")
  message(STATUS "VulkanTron requires SDL3; only the OpenGL reference is built with this preset")
  return()
endif()

find_package(Vulkan 1.3 REQUIRED)
find_program(VULKANTRON_GLSLC glslc REQUIRED)
find_program(VULKANTRON_SPIRV_VAL spirv-val REQUIRED)
set(VULKANTRON_SHADER_DIR "${PROJECT_BINARY_DIR}/bin/vulkantron-shaders")
set(VULKANTRON_SHADER_OUTPUTS)
foreach(stage vert frag)
  set(shader "${PROJECT_SOURCE_DIR}/vulkantron/shaders/scene.${stage}")
  set(output "${VULKANTRON_SHADER_DIR}/scene.${stage}.spv")
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

add_executable(vulkantron vulkantron/main.cpp vulkantron/renderer.cpp vulkantron/scene.cpp)
set_target_properties(vulkantron PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
target_compile_definitions(vulkantron PRIVATE
  VT_SOURCE_ROOT="${PROJECT_SOURCE_DIR}" VT_VERSION="0.1.0-dev")
target_link_libraries(vulkantron PRIVATE vulkantron-classic Vulkan::Vulkan
  PkgConfig::GLTRON_SDL PNG::PNG gltron_instrumentation)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vulkantron PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_dependencies(vulkantron vulkantron-shaders)

if(BUILD_TESTING)
  add_executable(vulkantron-scene-test vulkantron/scene_test.cpp vulkantron/scene.cpp)
  set_target_properties(vulkantron-scene-test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
  target_link_libraries(vulkantron-scene-test PRIVATE gltron_instrumentation)
  add_test(NAME vulkantron-scene COMMAND vulkantron-scene-test "${PROJECT_SOURCE_DIR}")
  add_executable(vulkantron-classic-test vulkantron/classic_bridge_test.c)
  target_link_libraries(vulkantron-classic-test PRIVATE vulkantron-classic gltron_instrumentation)
  gltron_enable_test_assertions(vulkantron-classic-test)
  add_test(NAME vulkantron-classic-bridge COMMAND vulkantron-classic-test)
  add_test(NAME vulkantron-headless-check COMMAND vulkantron --self-test)
  set_tests_properties(vulkantron-classic-bridge vulkantron-headless-check vulkantron-scene
    PROPERTIES TIMEOUT 30 LABELS vulkantron)
endif()

install(TARGETS vulkantron RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES ${VULKANTRON_SHADER_OUTPUTS}
  DESTINATION "${CMAKE_INSTALL_DATADIR}/vulkantron/shaders")
