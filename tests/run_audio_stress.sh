#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_build_dir=$(mktemp -d "${TMPDIR:-/tmp}/gltron-audio-stress-XXXXXX")
test_cxx=${CXX:-c++}

cleanup() {
  trap - EXIT HUP INT TERM
  rm -f "$test_build_dir/audio_source_list_stress-sdl1-plain"
  rm -f "$test_build_dir/audio_source_list_stress-sdl1-asan"
  rm -f "$test_build_dir/audio_source_list_stress-sdl1-tsan"
  rm -f "$test_build_dir/audio_source_list_stress-sdl2-plain"
  rm -f "$test_build_dir/audio_source_list_stress-sdl2-asan"
  rm -f "$test_build_dir/audio_source_list_stress-sdl2-tsan"
  rm -f "$test_build_dir/audio_source_list_stress-sdl3-plain"
  rm -f "$test_build_dir/audio_source_list_stress-sdl3-asan"
  rm -f "$test_build_dir/audio_source_list_stress-sdl3-tsan"
  rmdir "$test_build_dir"
}
trap cleanup EXIT HUP INT TERM

compile_test() {
  backend=$1
  mode=$2
  shift 2

  case "$backend" in
    sdl1)
      sdl_cflags=$(sdl-config --cflags)
      sdl_libs=$(sdl-config --libs)
      backend_cflags=
      backend_libs=-lSDL_sound
      ;;
    sdl2)
      sdl_cflags=$(sdl2-config --cflags)
      sdl_libs=$(sdl2-config --libs)
      backend_cflags=-DGLTRON_SDL2_AUDIO
      backend_libs=-lmikmod
      ;;
    sdl3)
      sdl_cflags=$(pkg-config --cflags sdl3)
      sdl_libs=$(pkg-config --libs sdl3)
      backend_cflags=-DGLTRON_SDL3_AUDIO
      backend_libs=-lmikmod
      ;;
    *)
      printf 'unknown audio backend: %s\n' "$backend" >&2
      return 2
      ;;
  esac

  "$test_cxx" -std=c++11 "$@" $backend_cflags \
    -I"$repo_dir/nebu/include" \
    $sdl_cflags \
    "$repo_dir/tests/audio_source_list_stress.cpp" \
    "$repo_dir/nebu/audio/SoundSystem.cpp" \
    "$repo_dir/nebu/audio/Source.cpp" \
    "$repo_dir/nebu/audio/SourceCopy.cpp" \
    "$repo_dir/nebu/audio/SourceMusic.cpp" \
    "$repo_dir/nebu/audio/SourceSample.cpp" \
    $sdl_libs $backend_libs -pthread \
    -o "$test_build_dir/audio_source_list_stress-$backend-$mode"
}

run_bounded() {
  if timeout --signal=TERM --kill-after=5s "${GLTRON_STRESS_TIMEOUT:-60s}" "$@"; then
    return 0
  else
    status=$?
  fi

  if [ "$status" -eq 124 ] || [ "$status" -eq 137 ]; then
    printf 'audio stress test exceeded %s (status %s)\n' \
      "${GLTRON_STRESS_TIMEOUT:-60s}" "$status" >&2
  fi
  return "$status"
}

run_test() {
  backend=$1
  mode=$2
  shift 2
  binary="$test_build_dir/audio_source_list_stress-$backend-$mode"

  printf 'Running %s %s audio stress test\n' "$backend" "$mode"
  if [ "$backend" = sdl1 ]; then
    run_bounded env "$@" SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy SDL_AUDIO_DEVICE_SAMPLE_FRAMES=64 \
      "$binary" \
      "${GLTRON_STRESS_SOURCES:-100000}" \
      "${GLTRON_STRESS_TRACK:-$repo_dir/music/song_revenge_of_cats.it}" \
      "${GLTRON_STRESS_ONESHOT:-$repo_dir/data/game_crash.wav}"
  else
    run_bounded env "$@" SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy SDL_AUDIO_DEVICE_SAMPLE_FRAMES=64 \
      "$binary" \
      "${GLTRON_STRESS_SOURCES:-100000}" \
      "${GLTRON_STRESS_TRACK:-$repo_dir/music/song_revenge_of_cats.it}"
  fi
}

run_plain() {
  for backend in ${GLTRON_AUDIO_BACKENDS:-sdl1 sdl2 sdl3}; do
    compile_test "$backend" plain -O2 -g -Wall -Wextra -Wpedantic
    run_test "$backend" plain
  done
}

run_asan() {
  for backend in ${GLTRON_AUDIO_BACKENDS:-sdl1 sdl2 sdl3}; do
    compile_test "$backend" asan -O1 -g3 -fno-omit-frame-pointer \
      -fno-optimize-sibling-calls -fsanitize=address,undefined \
      -fno-sanitize-recover=all
    run_test "$backend" asan \
      ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=0 \
      UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
  done
}

run_tsan() {
  for backend in ${GLTRON_AUDIO_BACKENDS:-sdl1 sdl2 sdl3}; do
    compile_test "$backend" tsan -O1 -g3 -fno-omit-frame-pointer \
      -fsanitize=thread
    run_test "$backend" tsan \
      TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1
  done
}

case "${1:-plain}" in
  plain)
    run_plain
    ;;
  asan)
    run_asan
    ;;
  tsan)
    run_tsan
    ;;
  all)
    run_plain
    run_asan
    run_tsan
    ;;
  *)
    printf 'usage: %s [plain|asan|tsan|all]\n' "$0" >&2
    exit 2
    ;;
esac
