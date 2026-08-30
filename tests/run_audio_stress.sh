#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_build_dir=$(mktemp -d "${TMPDIR:-/tmp}/gltron-audio-stress-XXXXXX")
test_cxx=${CXX:-c++}

cleanup() {
  trap - EXIT HUP INT TERM
  rm -f "$test_build_dir/audio_source_list_stress-plain"
  rm -f "$test_build_dir/audio_source_list_stress-asan"
  rm -f "$test_build_dir/audio_source_list_stress-tsan"
  rmdir "$test_build_dir"
}
trap cleanup EXIT HUP INT TERM

compile_test() {
  mode=$1
  shift
  "$test_cxx" -std=c++11 "$@" \
    -I"$repo_dir/nebu/include" \
    $(sdl-config --cflags) \
    "$repo_dir/tests/audio_source_list_stress.cpp" \
    "$repo_dir/nebu/audio/SoundSystem.cpp" \
    "$repo_dir/nebu/audio/Source.cpp" \
    "$repo_dir/nebu/audio/SourceCopy.cpp" \
    "$repo_dir/nebu/audio/SourceMusic.cpp" \
    "$repo_dir/nebu/audio/SourceSample.cpp" \
    $(sdl-config --libs) -lSDL_sound -pthread \
    -o "$test_build_dir/audio_source_list_stress-$mode"
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

run_plain() {
  compile_test plain -O2 -g -Wall -Wextra -Wpedantic
  run_bounded env SDL_AUDIODRIVER=dummy \
    "$test_build_dir/audio_source_list_stress-plain" \
    "${GLTRON_STRESS_SOURCES:-100000}" \
    "${GLTRON_STRESS_TRACK:-$repo_dir/music/song_revenge_of_cats.it}" \
    "${GLTRON_STRESS_ONESHOT:-$repo_dir/data/game_crash.wav}"
}

run_asan() {
  compile_test asan -O1 -g3 -fno-omit-frame-pointer \
    -fno-optimize-sibling-calls -fsanitize=address,undefined \
    -fno-sanitize-recover=all
  run_bounded env \
    ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    SDL_AUDIODRIVER=dummy \
    "$test_build_dir/audio_source_list_stress-asan" \
    "${GLTRON_STRESS_SOURCES:-100000}" \
    "${GLTRON_STRESS_TRACK:-$repo_dir/music/song_revenge_of_cats.it}" \
    "${GLTRON_STRESS_ONESHOT:-$repo_dir/data/game_crash.wav}"
}

run_tsan() {
  compile_test tsan -O1 -g3 -fno-omit-frame-pointer -fsanitize=thread
  run_bounded env TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1 \
    SDL_AUDIODRIVER=dummy \
    "$test_build_dir/audio_source_list_stress-tsan" \
    "${GLTRON_STRESS_SOURCES:-100000}" \
    "${GLTRON_STRESS_TRACK:-$repo_dir/music/song_revenge_of_cats.it}" \
    "${GLTRON_STRESS_ONESHOT:-$repo_dir/data/game_crash.wav}"
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
