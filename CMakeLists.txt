cmake_minimum_required(VERSION 3.21)
project(GLTron VERSION 0.70.1 DESCRIPTION "GLTron Faithful Remaster" LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build: cmake -S . -B build/release")
endif()

include(GNUInstallDirs)
include(CTest)
include(CheckSymbolExists)

set(GLTRON_RELEASE_SUFFIX "preview" CACHE STRING "Release label; empty only for a verified final release")
if(GLTRON_RELEASE_SUFFIX AND NOT GLTRON_RELEASE_SUFFIX MATCHES "^[A-Za-z0-9][A-Za-z0-9.-]*$")
  message(FATAL_ERROR "GLTRON_RELEASE_SUFFIX may contain letters, digits, dots and hyphens")
endif()
set(GLTRON_DISPLAY_VERSION "${PROJECT_VERSION}")
if(GLTRON_RELEASE_SUFFIX)
  string(APPEND GLTRON_DISPLAY_VERSION "-${GLTRON_RELEASE_SUFFIX}")
endif()

set(GLTRON_SDL_BACKEND "SDL3" CACHE STRING "Platform backend: SDL3, SDL2, or SDL1")
set_property(CACHE GLTRON_SDL_BACKEND PROPERTY STRINGS SDL3 SDL2 SDL1)
string(TOUPPER "${GLTRON_SDL_BACKEND}" GLTRON_SDL_BACKEND)
option(GLTRON_ENABLE_AUDIO "Build the original effects and tracker music" ON)
option(GLTRON_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(GLTRON_WARNINGS_AS_ERRORS "Treat compiler warnings in game and Nebu code as errors" OFF)
option(GLTRON_ENABLE_REFERENCE_TESTS "Run cross-backend shell parity tests (requires all three SDL development backends and decoders)" OFF)
option(GLTRON_ENABLE_GRAPHICS_TESTS "Run window/screenshot tests (requires a real OpenGL display)" OFF)
option(GLTRON_BUILD_NATIVE_TESTS "Build production game/window smoke tools for a graphical session" OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
find_package(PNG REQUIRED)

if(GLTRON_SDL_BACKEND STREQUAL "SDL3")
  pkg_check_modules(GLTRON_SDL REQUIRED IMPORTED_TARGET sdl3>=3.2)
  # The retained platform files use <SDL.h>; SDL3 normally expects <SDL3/SDL.h>.
  find_path(GLTRON_SDL3_INCLUDE_DIR SDL.h HINTS ${GLTRON_SDL_INCLUDEDIR}
    PATH_SUFFIXES SDL3 REQUIRED)
elseif(GLTRON_SDL_BACKEND STREQUAL "SDL2")
  pkg_check_modules(GLTRON_SDL REQUIRED IMPORTED_TARGET sdl2>=2.0.18)
elseif(GLTRON_SDL_BACKEND STREQUAL "SDL1")
  pkg_check_modules(GLTRON_SDL REQUIRED IMPORTED_TARGET sdl>=1.2)
else()
  message(FATAL_ERROR "GLTRON_SDL_BACKEND must be SDL3, SDL2, or SDL1")
endif()

check_symbol_exists(mkstemp "stdlib.h" GLTRON_HAVE_MKSTEMP)
add_library(gltron_options INTERFACE)
target_include_directories(gltron_options INTERFACE
  "${PROJECT_SOURCE_DIR}/src/include"
  "${PROJECT_SOURCE_DIR}/nebu/include"
  "${PROJECT_SOURCE_DIR}/nebu/include/scripting"
  "${PROJECT_SOURCE_DIR}/lua/include"
  "${PROJECT_BINARY_DIR}/generated")
if(GLTRON_SDL_BACKEND STREQUAL "SDL3")
  target_include_directories(gltron_options SYSTEM INTERFACE "${GLTRON_SDL3_INCLUDE_DIR}")
  target_compile_definitions(gltron_options INTERFACE GLTRON_USE_SDL3=1)
endif()
target_link_libraries(gltron_options INTERFACE PkgConfig::GLTRON_SDL OpenGL::GL)
if(UNIX)
  target_link_libraries(gltron_options INTERFACE m)
endif()
target_compile_definitions(gltron_options INTERFACE
  GLTRON_CMAKE_BUILD=1 PACKAGE="gltron" VERSION="${GLTRON_DISPLAY_VERSION}" "SEPARATOR='/'"
  PREF_DIR="~" SNAP_DIR="~"
  DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/gltron"
  GLTRON_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}"
  GLTRON_INSTALL_DATA_SUBDIR="${CMAKE_INSTALL_DATADIR}/gltron"
  $<$<BOOL:${GLTRON_HAVE_MKSTEMP}>:HAVE_MKSTEMP=1>)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(gltron_options INTERFACE -Wall -Wextra
    $<$<BOOL:${GLTRON_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

add_library(gltron_instrumentation INTERFACE)
if(GLTRON_ENABLE_SANITIZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR
     NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "GLTRON_ENABLE_SANITIZERS requires GCC or Clang")
  endif()
  target_compile_options(gltron_instrumentation INTERFACE
    -fsanitize=address,undefined -fno-sanitize-recover=all
    -fno-omit-frame-pointer -fno-optimize-sibling-calls)
  target_link_options(gltron_instrumentation INTERFACE -fsanitize=address,undefined)
endif()
target_link_libraries(gltron_options INTERFACE gltron_instrumentation)

function(gltron_enable_test_assertions target)
  # Release adds NDEBUG globally, but assertions in a regression may evaluate
  # the operation being checked. Keep those checks active in every build type.
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endfunction()

include(cmake/Sources.cmake)
# Objects avoid the cyclic archive order in the original link, including the
# reference from Nebu's initFilesystem() back to the game's dirSetup().
add_library(gltron_lua OBJECT ${GLTRON_LUA_SOURCES})
target_include_directories(gltron_lua PRIVATE lua/include lua/src)
target_link_libraries(gltron_lua PRIVATE gltron_instrumentation)

set(GLTRON_AUDIO_DESCRIPTION "disabled")
set(GLTRON_AUDIO_LIBRARIES)
if(GLTRON_ENABLE_AUDIO)
  find_package(Threads REQUIRED)
  list(APPEND GLTRON_GAME_SOURCES ${GLTRON_AUDIO_SOURCES})
  list(APPEND GLTRON_AUDIO_LIBRARIES Threads::Threads)
  if(GLTRON_SDL_BACKEND STREQUAL "SDL1")
    find_path(GLTRON_SDL_SOUND_INCLUDE_DIR SDL_sound.h
      HINTS ${GLTRON_SDL_INCLUDE_DIRS} PATH_SUFFIXES SDL REQUIRED)
    find_library(GLTRON_SDL_SOUND_LIBRARY NAMES SDL_sound REQUIRED)
    target_include_directories(gltron_options SYSTEM INTERFACE "${GLTRON_SDL_SOUND_INCLUDE_DIR}")
    list(APPEND GLTRON_AUDIO_LIBRARIES "${GLTRON_SDL_SOUND_LIBRARY}")
    set(GLTRON_AUDIO_DESCRIPTION "SDL_sound classic decoder")
  else()
    pkg_check_modules(GLTRON_MIKMOD REQUIRED IMPORTED_TARGET libmikmod)
    target_compile_definitions(gltron_options INTERFACE GLTRON_${GLTRON_SDL_BACKEND}_AUDIO=1)
    list(APPEND GLTRON_AUDIO_LIBRARIES PkgConfig::GLTRON_MIKMOD)
    set(GLTRON_AUDIO_DESCRIPTION "native ${GLTRON_SDL_BACKEND} with libmikmod ${GLTRON_MIKMOD_VERSION}")
  endif()
else()
  target_compile_definitions(gltron_options INTERFACE GLTRON_NO_SOUND=1)
  list(APPEND GLTRON_GAME_SOURCES src/audio/sound.c src/audio/sound_glue.cpp)
endif()

add_library(gltron_game OBJECT ${GLTRON_GAME_SOURCES})
target_link_libraries(gltron_game PRIVATE gltron_options PNG::PNG ${GLTRON_AUDIO_LIBRARIES})
add_executable(gltron src/gltron.c)
target_link_libraries(gltron PRIVATE gltron_game gltron_lua gltron_options PNG::PNG ${GLTRON_AUDIO_LIBRARIES})
set_target_properties(gltron PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

include(cmake/BuildInfo.cmake)
if(BUILD_TESTING)
  include(cmake/Tests.cmake)
endif()
if(GLTRON_BUILD_NATIVE_TESTS OR GLTRON_ENABLE_GRAPHICS_TESTS)
  include(cmake/NativeTests.cmake)
endif()
include(cmake/Packaging.cmake)

message(STATUS "GLTron Faithful Remaster ${GLTRON_DISPLAY_VERSION}: ${GLTRON_SDL_BACKEND} ${GLTRON_SDL_VERSION}, OpenGL, audio ${GLTRON_AUDIO_DESCRIPTION}")
message(STATUS "Install assets: ${CMAKE_INSTALL_FULL_DATADIR}/gltron")
