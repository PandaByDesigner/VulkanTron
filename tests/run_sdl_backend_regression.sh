#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/gltron-sdl-backend.XXXXXX")
cc=${CC:-cc}
mode=${1:-plain}

cleanup() {
  trap - EXIT HUP INT TERM
  rm -f "$build_dir/input-sdl1" "$build_dir/input-sdl2"
  rmdir "$build_dir"
}
trap cleanup EXIT HUP INT TERM

case "$mode" in
  plain)
    sanitizer_flags=
    ;;
  asan)
    sanitizer_flags='-O1 -fno-omit-frame-pointer -fsanitize=address,undefined'
    ;;
  *)
    echo "usage: $0 [plain|asan]" >&2
    exit 2
    ;;
esac

for backend in sdl1 sdl2; do
  if [ "$backend" = sdl1 ]; then
    config_tool=sdl-config
  else
    config_tool=sdl2-config
  fi

  if ! command -v "$config_tool" >/dev/null 2>&1; then
    echo "missing required backend tool: $config_tool" >&2
    exit 1
  fi

  sdl_cflags=$($config_tool --cflags)
  sdl_libs=$($config_tool --libs)
  "$cc" -std=gnu99 -O2 -g -Wall -Wextra -Werror \
    $sanitizer_flags \
    $sdl_cflags \
    -I"$repo_dir/nebu/include" \
    -I"$repo_dir/src/include" \
    -I"$repo_dir/lua/include" \
    "$repo_dir/tests/input_translation_test.c" \
    "$repo_dir/nebu/input/input_system.c" \
    "$repo_dir/nebu/input/system_keynames.c" \
    $sdl_libs -lm -o "$build_dir/input-$backend"

  if [ "$mode" = asan ]; then
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$build_dir/input-$backend"
  else
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$build_dir/input-$backend"
  fi
done
