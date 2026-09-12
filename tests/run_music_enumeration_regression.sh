#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/gltron-music-regression.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
case "${1:-plain}" in
  plain) flags='-O2 -g' ;;
  asan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all' ;;
  *) printf 'usage: %s [plain|asan]\n' "$0" >&2; exit 2 ;;
esac
for backend in sdl1 sdl2 sdl3; do
  case "$backend" in
    sdl1) backend_flags=$(sdl-config --cflags) ;;
    sdl2) backend_flags="-DGLTRON_USE_SDL2 -DGLTRON_SDL2_AUDIO $(sdl2-config --cflags)" ;;
    sdl3) backend_flags="-DGLTRON_USE_SDL3 -DGLTRON_SDL3_AUDIO $(pkg-config --cflags sdl3)" ;;
  esac
  "${CC:-cc}" -std=gnu99 $flags -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-implicit-fallthrough \
    $backend_flags -DHAVE_MKSTEMP=1 -D_POSIX_C_SOURCE=200809L \
    -ffunction-sections -fdata-sections \
    -I"$repo_dir/nebu/include" -I"$repo_dir/nebu/include/scripting" \
    -I"$repo_dir/src/include" -I"$repo_dir/lua/include" -I"$repo_dir/lua/src" \
    "$repo_dir/tests/music_enumeration_test.c" "$repo_dir/src/audio/sound.c" \
    "$repo_dir/nebu/scripting/scripting.c" \
    "$repo_dir"/lua/src/*.c "$repo_dir"/lua/src/lib/*.c -Wl,--gc-sections -lm \
    -o "$build_dir/music-enumeration-$backend"
  if ASAN_OPTIONS=detect_leaks=${GLTRON_TEST_LEAKS:-0}:halt_on_error=1 \
     UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
       timeout --signal=TERM --kill-after=5s 30s \
       "$build_dir/music-enumeration-$backend" "$repo_dir" >"$build_dir/$backend.log" 2>&1; then
    printf '%s: ' "$backend"
    tail -n 1 "$build_dir/$backend.log"
  else
    status=$?
    cat "$build_dir/$backend.log" >&2
    exit "$status"
  fi
done
