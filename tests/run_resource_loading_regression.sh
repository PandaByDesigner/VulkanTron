#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/gltron-resource-regression.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
case "${1:-plain}" in
  plain) flags='-O2 -g' ;;
  asan) flags='-O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all' ;;
  *) printf 'usage: %s [plain|asan]\n' "$0" >&2; exit 2 ;;
esac
"${CC:-cc}" -std=gnu99 $flags -Wall -Wextra -Werror -DGLTRON_USE_SDL3 \
  -I"$repo_dir/nebu/include" -I"$repo_dir/src/include" -I"$repo_dir/lua/include" \
  $(pkg-config --cflags sdl3 libpng) \
  "$repo_dir/tests/resource_loading_test.c" \
  "$repo_dir/src/video/fonts.c" "$repo_dir/src/video/fonttex.c" \
  "$repo_dir/src/video/load_texture.c" "$repo_dir/nebu/video/png_texture.c" \
  "$repo_dir/nebu/filesystem/file_io.c" \
  $(pkg-config --libs libpng) -lz -o "$build_dir/resource-loading-test"
if ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=${GLTRON_TEST_LEAKS:-0} \
   UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
   timeout --signal=TERM --kill-after=5s 45s \
     "$build_dir/resource-loading-test" "$repo_dir" "$build_dir" \
     2>"$build_dir/expected-errors.log"; then
  :
else
  status=$?
  cat "$build_dir/expected-errors.log" >&2
  exit "$status"
fi
